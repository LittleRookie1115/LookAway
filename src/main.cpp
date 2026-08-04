#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <combaseapi.h>
#include <objidl.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "resource.h"
#include "work_timer.hpp"

namespace {

using namespace std::chrono_literals;

constexpr wchar_t kMainClass[] = L"LookAwayMainWindow";
constexpr wchar_t kReminderClass[] = L"LookAwayReminderWindow";
constexpr wchar_t kSettingsClass[] = L"LookAwaySettingsWindow";
constexpr wchar_t kMutexName[] = L"Local\\LookAway.SingleInstance.1";
constexpr wchar_t kRegistryPath[] = L"Software\\LookAway";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowExisting = WM_APP + 2;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kAnimationTimer = 2;
constexpr UINT kMenuOpen = 1001;
constexpr UINT kMenuPause = 1002;
constexpr UINT kMenuReset = 1003;
constexpr UINT kMenuSettings = 1004;
constexpr UINT kMenuExit = 1005;
constexpr int kMinWorkMinutes = 5;
constexpr int kMaxWorkMinutes = 60;
constexpr int kWorkMinuteStep = 5;
constexpr int kMinRestMinutes = 1;
constexpr int kMaxRestMinutes = 20;
constexpr double kPi = 3.14159265358979323846;

constexpr COLORREF kBackground = RGB(246, 247, 244);
constexpr COLORREF kSurface = RGB(255, 255, 255);
constexpr COLORREF kInk = RGB(31, 38, 35);
constexpr COLORREF kMuted = RGB(100, 108, 103);
constexpr COLORREF kLine = RGB(222, 226, 221);
constexpr COLORREF kTrack = RGB(224, 229, 225);
constexpr COLORREF kGreen = RGB(38, 132, 91);
constexpr COLORREF kGreenDark = RGB(24, 91, 64);
constexpr COLORREF kGreenSoft = RGB(225, 241, 233);
constexpr COLORREF kAmber = RGB(177, 111, 25);
constexpr COLORREF kAmberSoft = RGB(250, 237, 216);
constexpr COLORREF kRestBlue = RGB(55, 104, 154);
constexpr COLORREF kRestSoft = RGB(226, 237, 248);

UINT dpi_for(HWND window) {
    HDC dc = GetDC(window);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) {
        ReleaseDC(window, dc);
    }
    return dpi;
}

int scale_for(HWND window, int value) {
    const UINT dpi = dpi_for(window);
    return MulDiv(value, static_cast<int>(dpi), 96);
}

RECT scaled_rect(HWND window, int left, int top, int right, int bottom) {
    return RECT{scale_for(window, left), scale_for(window, top),
                scale_for(window, right), scale_for(window, bottom)};
}

HFONT create_font(HWND window, int points, int weight = FW_NORMAL) {
    const UINT dpi = dpi_for(window);
    return CreateFontW(-MulDiv(points, static_cast<int>(dpi), 72), 0, 0, 0, weight,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

void fill_rect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void round_rect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border = CLR_INVALID) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, border == CLR_INVALID ? 0 : 1,
                         border == CLR_INVALID ? fill : border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_text(HDC dc, HWND window, const wchar_t* text, const RECT& rect, int points,
               int weight, COLORREF color, UINT format) {
    HFONT font = create_font(window, points, weight);
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT copy = rect;
    DrawTextW(dc, text, -1, &copy, format);
    SelectObject(dc, old);
    DeleteObject(font);
}

void draw_app_mark(HDC dc, const RECT& bounds, HICON icon) {
    if (!icon) {
        return;
    }
    DrawIconEx(dc, bounds.left, bounds.top, icon,
               bounds.right - bounds.left, bounds.bottom - bounds.top,
               0, nullptr, DI_NORMAL);
}

void draw_progress_ring(HDC dc, int center_x, int center_y, int radius, int thickness,
                        double progress, COLORREF color) {
    HPEN track = CreatePen(PS_SOLID, thickness, kTrack);
    HGDIOBJ old_pen = SelectObject(dc, track);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, center_x - radius, center_y - radius,
            center_x + radius, center_y + radius);
    SelectObject(dc, old_pen);
    DeleteObject(track);

    progress = std::clamp(progress, 0.0, 1.0);
    if (progress > 0.001) {
        const int segments = std::max(2, static_cast<int>(progress * 160.0));
        std::vector<POINT> points;
        points.reserve(static_cast<std::size_t>(segments + 1));
        for (int i = 0; i <= segments; ++i) {
            const double fraction = progress * static_cast<double>(i) / segments;
            const double angle = -kPi / 2.0 + fraction * 2.0 * kPi;
            points.push_back(POINT{
                center_x + static_cast<LONG>(std::cos(angle) * radius),
                center_y + static_cast<LONG>(std::sin(angle) * radius)});
        }
        HPEN active = CreatePen(PS_SOLID, thickness, color);
        SelectObject(dc, active);
        Polyline(dc, points.data(), static_cast<int>(points.size()));
        SelectObject(dc, old_pen);
        DeleteObject(active);

        HBRUSH cap = CreateSolidBrush(color);
        HGDIOBJ cap_old = SelectObject(dc, cap);
        HGDIOBJ cap_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        const int cap_radius = thickness / 2;
        const POINT& start = points.front();
        const POINT& end = points.back();
        Ellipse(dc, start.x - cap_radius, start.y - cap_radius,
                start.x + cap_radius, start.y + cap_radius);
        Ellipse(dc, end.x - cap_radius, end.y - cap_radius,
                end.x + cap_radius, end.y + cap_radius);
        SelectObject(dc, cap_pen);
        SelectObject(dc, cap_old);
        DeleteObject(cap);
    }
    SelectObject(dc, old_brush);
}

std::wstring format_time(lookaway::WorkTimer::Duration duration) {
    const auto total_seconds = std::max<std::int64_t>(0, duration.count() / 1000);
    const auto minutes = total_seconds / 60;
    const auto seconds = total_seconds % 60;
    wchar_t buffer[32]{};
    std::swprintf(buffer, std::size(buffer), L"%02lld:%02lld",
                  static_cast<long long>(minutes), static_cast<long long>(seconds));
    return buffer;
}

class GifAnimation {
public:
    GifAnimation() = default;
    GifAnimation(const GifAnimation&) = delete;
    GifAnimation& operator=(const GifAnimation&) = delete;

    ~GifAnimation() {
        clear();
    }

    bool load(HINSTANCE instance, int resource_id) {
        clear();
        HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
        if (!resource) {
            return false;
        }
        HGLOBAL loaded_resource = LoadResource(instance, resource);
        const DWORD resource_size = SizeofResource(instance, resource);
        const void* resource_data = LockResource(loaded_resource);
        if (!resource_data || resource_size == 0) {
            return false;
        }

        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, resource_size);
        if (!memory) {
            return false;
        }
        void* destination = GlobalLock(memory);
        if (!destination) {
            GlobalFree(memory);
            return false;
        }
        std::memcpy(destination, resource_data, resource_size);
        GlobalUnlock(memory);

        if (CreateStreamOnHGlobal(memory, TRUE, &stream_) != S_OK) {
            GlobalFree(memory);
            return false;
        }
        image_.reset(Gdiplus::Image::FromStream(stream_, FALSE));
        if (!image_ || image_->GetLastStatus() != Gdiplus::Ok) {
            clear();
            return false;
        }

        const UINT dimension_count = image_->GetFrameDimensionsCount();
        if (dimension_count == 0) {
            clear();
            return false;
        }
        std::vector<GUID> dimensions(dimension_count);
        if (image_->GetFrameDimensionsList(dimensions.data(), dimension_count) != Gdiplus::Ok) {
            clear();
            return false;
        }
        frame_dimension_ = dimensions.front();
        frame_count_ = image_->GetFrameCount(&frame_dimension_);
        if (frame_count_ == 0) {
            clear();
            return false;
        }

        frame_delays_.assign(frame_count_, 100);
        constexpr PROPID frame_delay_property = 0x5100;
        const UINT property_size = image_->GetPropertyItemSize(frame_delay_property);
        if (property_size >= sizeof(Gdiplus::PropertyItem)) {
            std::vector<BYTE> property_buffer(property_size);
            auto* property = reinterpret_cast<Gdiplus::PropertyItem*>(property_buffer.data());
            if (image_->GetPropertyItem(frame_delay_property, property_size, property) == Gdiplus::Ok &&
                property->value && property->length >= frame_count_ * sizeof(UINT)) {
                const auto* delays = static_cast<const UINT*>(property->value);
                for (UINT index = 0; index < frame_count_; ++index) {
                    frame_delays_[index] = std::max<UINT>(20, delays[index] * 10);
                }
            }
        }
        restart();
        return true;
    }

    void restart() {
        frame_index_ = 0;
        elapsed_in_frame_ = 0;
        if (image_ && frame_count_ > 0) {
            image_->SelectActiveFrame(&frame_dimension_, 0);
        }
    }

    bool advance(ULONGLONG elapsed_ms) {
        if (!image_ || frame_count_ <= 1) {
            return false;
        }
        elapsed_in_frame_ += std::min<ULONGLONG>(elapsed_ms, 1000);
        bool changed = false;
        while (elapsed_in_frame_ >= frame_delays_[frame_index_]) {
            elapsed_in_frame_ -= frame_delays_[frame_index_];
            frame_index_ = (frame_index_ + 1) % frame_count_;
            image_->SelectActiveFrame(&frame_dimension_, frame_index_);
            changed = true;
        }
        return changed;
    }

    void draw(HDC dc, const RECT& bounds) const {
        if (!image_) {
            return;
        }
        const UINT source_width = image_->GetWidth();
        const UINT source_height = image_->GetHeight();
        if (source_width == 0 || source_height == 0) {
            return;
        }
        const int available_width = bounds.right - bounds.left;
        const int available_height = bounds.bottom - bounds.top;
        const double scale = std::min(
            static_cast<double>(available_width) / source_width,
            static_cast<double>(available_height) / source_height);
        const int width = std::max(1, static_cast<int>(source_width * scale));
        const int height = std::max(1, static_cast<int>(source_height * scale));
        const int left = bounds.left + (available_width - width) / 2;
        const int top = bounds.top + (available_height - height) / 2;

        Gdiplus::Graphics graphics(dc);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.DrawImage(image_.get(), left, top, width, height);
    }

private:
    void clear() {
        image_.reset();
        if (stream_) {
            stream_->Release();
            stream_ = nullptr;
        }
        frame_delays_.clear();
        frame_count_ = 0;
        frame_index_ = 0;
        elapsed_in_frame_ = 0;
    }

    IStream* stream_{};
    std::unique_ptr<Gdiplus::Image> image_;
    GUID frame_dimension_{};
    std::vector<UINT> frame_delays_;
    UINT frame_count_{};
    UINT frame_index_{};
    ULONGLONG elapsed_in_frame_{};
};

class Application {
public:
    explicit Application(HINSTANCE instance) : instance_(instance) {
        load_settings();
        timer_ = lookaway::WorkTimer{
            std::chrono::minutes(work_minutes_), 1min,
            std::chrono::minutes(rest_minutes_), 5min};
        draft_work_minutes_ = work_minutes_;
        draft_rest_minutes_ = rest_minutes_;
    }

    ~Application() {
        remove_tray_icon();
        if (large_icon_) {
            DestroyIcon(large_icon_);
        }
        if (small_icon_) {
            DestroyIcon(small_icon_);
        }
        if (mark_icon_) {
            DestroyIcon(mark_icon_);
        }
    }

    int run(int show_command) {
        large_icon_ = load_icon(GetSystemMetrics(SM_CXICON));
        small_icon_ = load_icon(GetSystemMetrics(SM_CXSMICON));
        mark_icon_ = load_icon(128);
        working_animation_.load(instance_, IDR_WORKING_GIF);
        waiting_animation_.load(instance_, IDR_WAITING_GIF);
        register_classes();

        main_window_ = CreateWindowExW(
            0, kMainClass, L"LookAway", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, scale_for(nullptr, 432), scale_for(nullptr, 590),
            nullptr, nullptr, instance_, this);
        if (!main_window_) {
            return 1;
        }

        resize_main_client();
        center_main_window();
        create_settings_tooltip();
        initialize_idle_detection();
        add_tray_icon();
        last_tick_ = GetTickCount64();
        last_animation_tick_ = last_tick_;
        SetTimer(main_window_, kTickTimer, 1000, nullptr);
        SetTimer(main_window_, kAnimationTimer, 50, nullptr);
        ShowWindow(main_window_, show_command);
        UpdateWindow(main_window_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    HINSTANCE instance_{};
    HWND main_window_{};
    HWND reminder_window_{};
    HWND settings_window_{};
    HWND tooltip_window_{};
    HICON large_icon_{};
    HICON small_icon_{};
    HICON mark_icon_{};
    lookaway::WorkTimer timer_{};
    GifAnimation working_animation_;
    GifAnimation waiting_animation_;
    ULONGLONG last_tick_{};
    ULONGLONG last_animation_tick_{};
    ULONGLONG last_raw_input_tick_{};
    bool raw_input_registered_{false};
    bool system_idle_{false};
    bool long_idle_{false};
    bool show_working_animation_{true};
    bool shutting_down_{false};
    bool tray_hint_shown_{false};
    int work_minutes_{45};
    int rest_minutes_{5};
    int draft_work_minutes_{45};
    int draft_rest_minutes_{5};
    RECT settings_button_{};
    RECT main_primary_{};
    RECT main_secondary_{};
    RECT reminder_primary_{};
    RECT reminder_secondary_{};
    RECT work_minus_{};
    RECT work_plus_{};
    RECT rest_minus_{};
    RECT rest_plus_{};
    RECT settings_save_{};
    RECT settings_cancel_{};

    static Application* from_window(HWND window) {
        return reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    HICON load_icon(int size) const {
        HICON icon = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            size, size, LR_DEFAULTCOLOR));
        return icon ? icon : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }

    static int read_registry_minutes(const wchar_t* name, int fallback,
                                     int minimum, int maximum) {
        DWORD value{};
        DWORD size = sizeof(value);
        const LSTATUS status = RegGetValueW(
            HKEY_CURRENT_USER, kRegistryPath, name, RRF_RT_REG_DWORD,
            nullptr, &value, &size);
        if (status != ERROR_SUCCESS || value < static_cast<DWORD>(minimum) ||
            value > static_cast<DWORD>(maximum)) {
            return fallback;
        }
        return static_cast<int>(value);
    }

    void load_settings() {
        work_minutes_ = read_registry_minutes(
            L"WorkMinutes", 45, kMinWorkMinutes, kMaxWorkMinutes);
        rest_minutes_ = read_registry_minutes(
            L"RestMinutes", 5, kMinRestMinutes, kMaxRestMinutes);
    }

    bool persist_settings() const {
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                            &key, nullptr) != ERROR_SUCCESS) {
            return false;
        }
        const DWORD work = static_cast<DWORD>(work_minutes_);
        const DWORD rest = static_cast<DWORD>(rest_minutes_);
        const bool saved =
            RegSetValueExW(key, L"WorkMinutes", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&work), sizeof(work)) == ERROR_SUCCESS &&
            RegSetValueExW(key, L"RestMinutes", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&rest), sizeof(rest)) == ERROR_SUCCESS;
        RegCloseKey(key);
        return saved;
    }

    static LRESULT CALLBACK main_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        Application* app = from_window(window);
        return app ? app->handle_main(window, message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
    }

    static LRESULT CALLBACK reminder_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        Application* app = from_window(window);
        return app ? app->handle_reminder(window, message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
    }

    static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        Application* app = from_window(window);
        return app ? app->handle_settings(window, message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
    }

    void register_classes() {
        WNDCLASSEXW main_class{};
        main_class.cbSize = sizeof(main_class);
        main_class.style = CS_HREDRAW | CS_VREDRAW;
        main_class.lpfnWndProc = main_proc;
        main_class.hInstance = instance_;
        main_class.hIcon = large_icon_;
        main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        main_class.hbrBackground = nullptr;
        main_class.lpszClassName = kMainClass;
        main_class.hIconSm = small_icon_;
        RegisterClassExW(&main_class);

        WNDCLASSEXW reminder_class{};
        reminder_class.cbSize = sizeof(reminder_class);
        reminder_class.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        reminder_class.lpfnWndProc = reminder_proc;
        reminder_class.hInstance = instance_;
        reminder_class.hIcon = large_icon_;
        reminder_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        reminder_class.hbrBackground = nullptr;
        reminder_class.lpszClassName = kReminderClass;
        reminder_class.hIconSm = small_icon_;
        RegisterClassExW(&reminder_class);

        WNDCLASSEXW settings_class{};
        settings_class.cbSize = sizeof(settings_class);
        settings_class.style = CS_HREDRAW | CS_VREDRAW;
        settings_class.lpfnWndProc = settings_proc;
        settings_class.hInstance = instance_;
        settings_class.hIcon = large_icon_;
        settings_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        settings_class.hbrBackground = nullptr;
        settings_class.lpszClassName = kSettingsClass;
        settings_class.hIconSm = small_icon_;
        RegisterClassExW(&settings_class);
    }

    void resize_main_client() const {
        RECT client{};
        RECT window{};
        GetClientRect(main_window_, &client);
        GetWindowRect(main_window_, &window);
        const int target_width = scale_for(main_window_, 432);
        const int target_height = scale_for(main_window_, 558);
        const int frame_width = (window.right - window.left) - (client.right - client.left);
        const int frame_height = (window.bottom - window.top) - (client.bottom - client.top);
        SetWindowPos(main_window_, nullptr, 0, 0, target_width + frame_width,
                     target_height + frame_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void center_main_window() const {
        RECT window{};
        GetWindowRect(main_window_, &window);
        HMONITOR monitor = MonitorFromWindow(main_window_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        const int width = window.right - window.left;
        const int height = window.bottom - window.top;
        const int available_width = static_cast<int>(info.rcWork.right - info.rcWork.left);
        const int available_height = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
        const int x = info.rcWork.left + (available_width - width) / 2;
        const int y = info.rcWork.top + std::max(0, (available_height - height) / 2);
        SetWindowPos(main_window_, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void create_settings_tooltip() {
        settings_button_ = scaled_rect(main_window_, 204, 25, 238, 57);
        tooltip_window_ = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            main_window_, nullptr, instance_, nullptr);
        if (!tooltip_window_) {
            return;
        }
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = main_window_;
        tool.uId = 1;
        tool.rect = settings_button_;
        tool.lpszText = const_cast<wchar_t*>(L"设置计时时长");
        SendMessageW(tooltip_window_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    lookaway::WorkTimer::Duration legacy_system_idle_time() const {
        LASTINPUTINFO input{};
        input.cbSize = sizeof(input);
        if (!GetLastInputInfo(&input)) {
            return 0ms;
        }
        const DWORD idle = GetTickCount() - input.dwTime;
        return std::chrono::milliseconds(idle);
    }

    void initialize_idle_detection() {
        const ULONGLONG now = GetTickCount64();
        const auto legacy_idle = legacy_system_idle_time();
        const ULONGLONG initial_idle =
            static_cast<ULONGLONG>(std::max<std::int64_t>(legacy_idle.count(), 0));
        last_raw_input_tick_ = initial_idle < now ? now - initial_idle : 0;

        RAWINPUTDEVICE devices[] = {
            {0x01, 0x02, RIDEV_INPUTSINK, main_window_},
            {0x01, 0x06, RIDEV_INPUTSINK, main_window_},
        };
        raw_input_registered_ =
            RegisterRawInputDevices(devices, static_cast<UINT>(std::size(devices)),
                                    sizeof(RAWINPUTDEVICE)) != FALSE;
    }

    void record_raw_input(LPARAM raw_input_handle) {
        if (!raw_input_registered_) {
            return;
        }

        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT copied = GetRawInputData(
            reinterpret_cast<HRAWINPUT>(raw_input_handle), RID_INPUT,
            &input, &size, sizeof(RAWINPUTHEADER));
        if (copied == static_cast<UINT>(-1)) {
            return;
        }

        // Precision touchpads can emit zero-motion packets without user activity.
        const bool meaningful =
            input.header.dwType == RIM_TYPEKEYBOARD ||
            (input.header.dwType == RIM_TYPEMOUSE &&
             (input.data.mouse.lLastX != 0 || input.data.mouse.lLastY != 0 ||
              input.data.mouse.usButtonFlags != 0));
        if (meaningful) {
            last_raw_input_tick_ = GetTickCount64();
        }
    }

    lookaway::WorkTimer::Duration system_idle_time() const {
        if (!raw_input_registered_) {
            return legacy_system_idle_time();
        }
        return std::chrono::milliseconds(GetTickCount64() - last_raw_input_tick_);
    }

    void tick() {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG raw_delta = now - last_tick_;
        last_tick_ = now;
        const auto delta = std::chrono::milliseconds(std::min<ULONGLONG>(raw_delta, 5000));
        const auto idle = system_idle_time();
        system_idle_ = timer_.is_system_idle(idle);
        long_idle_ = timer_.is_long_idle(idle);

        const auto event = timer_.tick(delta, idle);
        if (event == lookaway::WorkTimer::Event::ReminderDue) {
            show_reminder();
        } else if (event == lookaway::WorkTimer::Event::RestFinished) {
            const std::wstring body = L"新的 " + std::to_wstring(work_minutes_) +
                                      L" 分钟用眼周期已经开始。";
            show_balloon(L"休息完成", body.c_str(), NIIF_INFO);
            MessageBeep(MB_OK);
        } else if (event == lookaway::WorkTimer::Event::IdleReset) {
            hide_reminder();
        }
        sync_animation_mode();
        InvalidateRect(main_window_, nullptr, FALSE);
    }

    bool should_show_working_animation() const {
        return timer_.state() == lookaway::WorkTimer::State::Working &&
               !system_idle_ && !timer_.is_snoozing() &&
               timer_.remaining() > lookaway::WorkTimer::Duration{0};
    }

    void sync_animation_mode() {
        const bool show_working = should_show_working_animation();
        if (show_working == show_working_animation_) {
            return;
        }
        show_working_animation_ = show_working;
        if (show_working_animation_) {
            working_animation_.restart();
        } else {
            waiting_animation_.restart();
        }
        last_animation_tick_ = GetTickCount64();
        InvalidateRect(main_window_, nullptr, FALSE);
    }

    void animate() {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed = now - last_animation_tick_;
        last_animation_tick_ = now;
        GifAnimation& animation = show_working_animation_
                                      ? working_animation_ : waiting_animation_;
        if (animation.advance(elapsed)) {
            const RECT animation_bounds = scaled_rect(main_window_, 156, 153, 276, 241);
            InvalidateRect(main_window_, &animation_bounds, FALSE);
        }
    }

    void add_tray_icon() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = main_window_;
        data.uID = kTrayId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = small_icon_;
        wcscpy_s(data.szTip, L"LookAway - 护眼计时中");
        Shell_NotifyIconW(NIM_ADD, &data);
        data.uVersion = 4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    void remove_tray_icon() const {
        if (!main_window_) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = main_window_;
        data.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    void show_balloon(const wchar_t* title, const wchar_t* body, DWORD flags) const {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = main_window_;
        data.uID = kTrayId;
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = flags;
        wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
        wcsncpy_s(data.szInfo, body, _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void hide_to_tray() {
        ShowWindow(main_window_, SW_HIDE);
        if (!tray_hint_shown_) {
            tray_hint_shown_ = true;
            show_balloon(L"LookAway 仍在运行", L"计时会在系统托盘中继续。双击托盘图标可重新打开。",
                         NIIF_INFO);
        }
    }

    void show_main() {
        ShowWindow(main_window_, SW_RESTORE);
        ShowWindow(main_window_, SW_SHOW);
        SetForegroundWindow(main_window_);
    }

    void show_tray_menu() {
        HMENU menu = CreatePopupMenu();
        const bool resting = timer_.state() == lookaway::WorkTimer::State::Resting;
        AppendMenuW(menu, MF_STRING, kMenuOpen, L"打开 LookAway");
        AppendMenuW(menu, MF_STRING | (resting ? MF_GRAYED : 0), kMenuPause,
                    resting ? L"正在休息"
                            : (timer_.state() == lookaway::WorkTimer::State::Paused
                                   ? L"继续计时" : L"暂停计时"));
        AppendMenuW(menu, MF_STRING, kMenuReset, L"重新计时");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuSettings, L"计时设置");
        AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");

        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(main_window_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                       point.x, point.y, 0, main_window_, nullptr);
        DestroyMenu(menu);
    }

    void execute_command(UINT command) {
        switch (command) {
            case kMenuOpen:
                show_main();
                break;
            case kMenuPause:
                timer_.toggle_pause();
                sync_animation_mode();
                InvalidateRect(main_window_, nullptr, FALSE);
                break;
            case kMenuReset:
                timer_.reset();
                hide_reminder();
                sync_animation_mode();
                InvalidateRect(main_window_, nullptr, FALSE);
                break;
            case kMenuSettings:
                show_main();
                show_settings();
                break;
            case kMenuExit:
                shutting_down_ = true;
                hide_reminder();
                if (settings_window_) {
                    DestroyWindow(settings_window_);
                }
                DestroyWindow(main_window_);
                break;
            default:
                break;
        }
    }

    void ensure_settings_window() {
        if (settings_window_) {
            return;
        }
        settings_window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME, kSettingsClass, L"LookAway 计时设置",
            WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, scale_for(main_window_, 420),
            scale_for(main_window_, 320), main_window_, nullptr, instance_, this);
        if (!settings_window_) {
            return;
        }

        RECT client{};
        RECT window{};
        GetClientRect(settings_window_, &client);
        GetWindowRect(settings_window_, &window);
        const int target_width = scale_for(settings_window_, 420);
        const int target_height = scale_for(settings_window_, 300);
        const int frame_width = (window.right - window.left) - (client.right - client.left);
        const int frame_height = (window.bottom - window.top) - (client.bottom - client.top);
        SetWindowPos(settings_window_, nullptr, 0, 0,
                     target_width + frame_width, target_height + frame_height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void position_settings_window() const {
        RECT owner{};
        RECT settings{};
        GetWindowRect(main_window_, &owner);
        GetWindowRect(settings_window_, &settings);
        const int width = settings.right - settings.left;
        const int height = settings.bottom - settings.top;
        int x = owner.left + ((owner.right - owner.left) - width) / 2;
        int y = owner.top + ((owner.bottom - owner.top) - height) / 2;

        HMONITOR monitor = MonitorFromWindow(main_window_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        x = std::clamp(x, static_cast<int>(info.rcWork.left),
                       static_cast<int>(info.rcWork.right) - width);
        y = std::clamp(y, static_cast<int>(info.rcWork.top),
                       static_cast<int>(info.rcWork.bottom) - height);
        SetWindowPos(settings_window_, HWND_TOP, x, y, width, height,
                     SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }

    void show_settings() {
        ensure_settings_window();
        if (!settings_window_) {
            return;
        }
        draft_work_minutes_ = work_minutes_;
        draft_rest_minutes_ = rest_minutes_;
        position_settings_window();
        EnableWindow(main_window_, FALSE);
        ShowWindow(settings_window_, SW_SHOW);
        SetForegroundWindow(settings_window_);
        InvalidateRect(settings_window_, nullptr, FALSE);
    }

    void close_settings() {
        if (!settings_window_) {
            return;
        }
        ShowWindow(settings_window_, SW_HIDE);
        EnableWindow(main_window_, TRUE);
        SetForegroundWindow(main_window_);
    }

    void apply_settings() {
        work_minutes_ = draft_work_minutes_;
        rest_minutes_ = draft_rest_minutes_;
        timer_ = lookaway::WorkTimer{
            std::chrono::minutes(work_minutes_), 1min,
            std::chrono::minutes(rest_minutes_), 5min};
        const auto idle = system_idle_time();
        system_idle_ = timer_.is_system_idle(idle);
        long_idle_ = timer_.is_long_idle(idle);
        last_tick_ = GetTickCount64();
        hide_reminder();
        sync_animation_mode();
        const bool saved = persist_settings();
        close_settings();
        InvalidateRect(main_window_, nullptr, FALSE);
        if (!saved) {
            MessageBoxW(main_window_, L"设置已应用，但无法保存到当前 Windows 用户配置。",
                        L"LookAway", MB_OK | MB_ICONWARNING);
        }
    }

    void adjust_setting(RECT button, POINT point, int& value,
                        int amount, int minimum, int maximum) {
        if (!PtInRect(&button, point)) {
            return;
        }
        value = std::clamp(value + amount, minimum, maximum);
        InvalidateRect(settings_window_, nullptr, FALSE);
    }

    void ensure_reminder_window() {
        if (reminder_window_) {
            return;
        }
        reminder_window_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kReminderClass, L"LookAway 护眼提醒", WS_POPUP,
            0, 0, scale_for(main_window_, 480), scale_for(main_window_, 326),
            nullptr, nullptr, instance_, this);
        if (reminder_window_) {
            const int radius = scale_for(reminder_window_, 14);
            SetWindowRgn(reminder_window_, CreateRoundRectRgn(
                0, 0, scale_for(reminder_window_, 480) + 1,
                scale_for(reminder_window_, 326) + 1, radius, radius), TRUE);
        }
    }

    void show_reminder() {
        ensure_reminder_window();
        if (!reminder_window_) {
            const std::wstring body = L"你已工作 " + std::to_wstring(work_minutes_) +
                                      L" 分钟，请离开屏幕休息。";
            show_balloon(L"该让眼睛休息了", body.c_str(), NIIF_WARNING);
            return;
        }

        POINT cursor{};
        GetCursorPos(&cursor);
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        RECT popup{};
        GetWindowRect(reminder_window_, &popup);
        const int width = popup.right - popup.left;
        const int height = popup.bottom - popup.top;
        const int x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
        const int y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
        SetWindowPos(reminder_window_, HWND_TOPMOST, x, y, width, height,
                     SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        SetForegroundWindow(reminder_window_);

        FLASHWINFO flash{sizeof(flash), reminder_window_, FLASHW_ALL, 4, 0};
        FlashWindowEx(&flash);
        MessageBeep(MB_ICONINFORMATION);
    }

    void hide_reminder() {
        if (reminder_window_) {
            ShowWindow(reminder_window_, SW_HIDE);
        }
    }

    void start_rest() {
        timer_.start_rest();
        hide_reminder();
        show_main();
        sync_animation_mode();
        InvalidateRect(main_window_, nullptr, FALSE);
    }

    void snooze() {
        timer_.snooze(5min);
        hide_reminder();
        show_balloon(L"已稍后提醒", L"5 分钟后会再次提醒你休息。", NIIF_INFO);
        sync_animation_mode();
        InvalidateRect(main_window_, nullptr, FALSE);
    }

    void paint_main(HWND window) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const RECT dirty = paint.rcPaint;
        const int dirty_width = dirty.right - dirty.left;
        const int dirty_height = dirty.bottom - dirty.top;
        if (dirty_width <= 0 || dirty_height <= 0) {
            EndPaint(window, &paint);
            return;
        }
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, dirty_width, dirty_height);
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        SetViewportOrgEx(dc, -dirty.left, -dirty.top, nullptr);
        SetBkMode(dc, TRANSPARENT);
        fill_rect(dc, client, kBackground);

        draw_app_mark(dc, scaled_rect(window, 24, 22, 60, 58), mark_icon_);
        draw_text(dc, window, L"LookAway", scaled_rect(window, 72, 20, 240, 48),
                  17, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"护眼计时", scaled_rect(window, 72, 43, 240, 64),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        settings_button_ = scaled_rect(window, 204, 25, 238, 57);
        round_rect(dc, settings_button_, scale_for(window, 6), kSurface, kLine);
        draw_text(dc, window, L"\u2699", settings_button_, 14, FW_NORMAL, kMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const bool resting = timer_.state() == lookaway::WorkTimer::State::Resting;
        const bool paused = timer_.state() == lookaway::WorkTimer::State::Paused;
        const bool snoozing = timer_.is_snoozing();
        const wchar_t* status = L"正在计时";
        COLORREF status_color = kGreenDark;
        COLORREF status_fill = kGreenSoft;
        if (resting) {
            status = L"正在休息";
            status_color = kRestBlue;
            status_fill = kRestSoft;
        } else if (snoozing) {
            status = L"稍后提醒已开启";
            status_color = kAmber;
            status_fill = kAmberSoft;
        } else if (paused) {
            status = L"计时已暂停";
            status_color = kMuted;
            status_fill = RGB(234, 236, 234);
        } else if (system_idle_) {
            status = long_idle_ ? L"长时间空闲，已重新计时" : L"已空闲，暂不计时";
            status_color = kAmber;
            status_fill = kAmberSoft;
        }
        RECT status_rect = scaled_rect(window, 246, 27, 408, 54);
        round_rect(dc, status_rect, scale_for(window, 14), status_fill);
        RECT status_text = status_rect;
        status_text.left += scale_for(window, 10);
        status_text.right -= scale_for(window, 10);
        draw_text(dc, window, status, status_text, 8, FW_MEDIUM, status_color,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const wchar_t* timer_label = resting ? L"本次休息" : (snoozing ? L"距离再次提醒" : L"距离下次提醒");
        draw_text(dc, window, timer_label, scaled_rect(window, 30, 83, 402, 113),
                  11, FW_MEDIUM, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const int center_x = scale_for(window, 216);
        const int center_y = scale_for(window, 232);
        draw_progress_ring(dc, center_x, center_y, scale_for(window, 102),
                           scale_for(window, 9), timer_.progress(), resting ? kRestBlue : kGreen);

        GifAnimation& animation = show_working_animation_
                                      ? working_animation_ : waiting_animation_;
        animation.draw(dc, scaled_rect(window, 156, 153, 276, 241));

        const auto shown_time = resting ? timer_.rest_remaining()
                                        : (snoozing ? timer_.snooze_remaining() : timer_.remaining());
        const std::wstring countdown = format_time(shown_time);
        draw_text(dc, window, countdown.c_str(), scaled_rect(window, 92, 242, 340, 286),
                  29, FW_SEMIBOLD, kInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, resting ? L"放松双眼，暂时离开屏幕"
                                     : (snoozing ? L"稍后提醒倒计时" : L"有效工作时间"),
                  scaled_rect(window, 88, 286, 344, 310), 8, FW_NORMAL, kMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        HPEN divider = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ old_pen = SelectObject(dc, divider);
        MoveToEx(dc, scale_for(window, 24), scale_for(window, 365), nullptr);
        LineTo(dc, scale_for(window, 408), scale_for(window, 365));
        SelectObject(dc, old_pen);
        DeleteObject(divider);

        draw_text(dc, window, resting ? L"让视线停在远处" : L"下一次休息建议",
                  scaled_rect(window, 24, 383, 408, 407), 11, FW_BOLD, kInk,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window,
                  resting ? L"眺望窗外或 6 米以外，慢慢眨眼。"
                          : (L"离开屏幕 " + std::to_wstring(rest_minutes_) +
                             L" 分钟，眺望窗外或 6 米以外。").c_str(),
                  scaled_rect(window, 24, 410, 408, 438), 9, FW_NORMAL, kMuted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        main_primary_ = scaled_rect(window, 24, 462, 244, 508);
        main_secondary_ = scaled_rect(window, 256, 462, 408, 508);
        round_rect(dc, main_primary_, scale_for(window, 6), resting ? kRestBlue : kGreen);
        round_rect(dc, main_secondary_, scale_for(window, 6), kSurface, kLine);
        const wchar_t* primary_text = resting ? L"结束休息" : (paused ? L"继续计时" : L"暂停计时");
        draw_text(dc, window, primary_text, main_primary_, 10, FW_SEMIBOLD, RGB(255, 255, 255),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"重新计时", main_secondary_, 10, FW_SEMIBOLD, kInk,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const std::wstring schedule = std::to_wstring(work_minutes_) +
                                      L" 分钟工作  |  " +
                                      std::to_wstring(rest_minutes_) + L" 分钟休息";
        draw_text(dc, window, schedule.c_str(),
                  scaled_rect(window, 24, 520, 408, 548), 8, FW_NORMAL, kMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetViewportOrgEx(dc, 0, 0, nullptr);
        BitBlt(target, dirty.left, dirty.top, dirty_width, dirty_height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &paint);
    }

    void draw_stepper(HDC dc, HWND window, int top, int value,
                      int minimum, int maximum, RECT& minus, RECT& plus) {
        RECT control = scaled_rect(window, 210, top, 396, top + 44);
        minus = RECT{control.left, control.top, scale_for(window, 252), control.bottom};
        plus = RECT{scale_for(window, 354), control.top, control.right, control.bottom};
        round_rect(dc, control, scale_for(window, 6), kSurface, kLine);

        HPEN divider = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ old_pen = SelectObject(dc, divider);
        MoveToEx(dc, minus.right, control.top, nullptr);
        LineTo(dc, minus.right, control.bottom);
        MoveToEx(dc, plus.left, control.top, nullptr);
        LineTo(dc, plus.left, control.bottom);
        SelectObject(dc, old_pen);
        DeleteObject(divider);

        const COLORREF minus_color = value <= minimum ? RGB(177, 183, 179) : kInk;
        const COLORREF plus_color = value >= maximum ? RGB(177, 183, 179) : kInk;
        draw_text(dc, window, L"\u2212", minus, 14, FW_NORMAL, minus_color,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"+", plus, 14, FW_NORMAL, plus_color,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT value_rect{minus.right, control.top, plus.left, control.bottom};
        const std::wstring value_text = std::to_wstring(value) + L" 分钟";
        draw_text(dc, window, value_text.c_str(), value_rect, 10, FW_SEMIBOLD, kInk,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void paint_settings(HWND window) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        fill_rect(dc, client, kBackground);

        draw_app_mark(dc, scaled_rect(window, 24, 20, 58, 54), mark_icon_);
        draw_text(dc, window, L"计时设置", scaled_rect(window, 72, 18, 300, 56),
                  17, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        HPEN divider = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ old_pen = SelectObject(dc, divider);
        MoveToEx(dc, scale_for(window, 24), scale_for(window, 69), nullptr);
        LineTo(dc, scale_for(window, 396), scale_for(window, 69));
        MoveToEx(dc, scale_for(window, 24), scale_for(window, 147), nullptr);
        LineTo(dc, scale_for(window, 396), scale_for(window, 147));
        SelectObject(dc, old_pen);
        DeleteObject(divider);

        draw_text(dc, window, L"工作时长", scaled_rect(window, 24, 84, 184, 108),
                  11, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"5 - 60 分钟", scaled_rect(window, 24, 108, 184, 130),
                  8, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_stepper(dc, window, 86, draft_work_minutes_,
                     kMinWorkMinutes, kMaxWorkMinutes, work_minus_, work_plus_);

        draw_text(dc, window, L"休息时长", scaled_rect(window, 24, 163, 184, 187),
                  11, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"1 - 20 分钟", scaled_rect(window, 24, 187, 184, 209),
                  8, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_stepper(dc, window, 165, draft_rest_minutes_,
                     kMinRestMinutes, kMaxRestMinutes, rest_minus_, rest_plus_);

        settings_save_ = scaled_rect(window, 24, 232, 270, 278);
        settings_cancel_ = scaled_rect(window, 282, 232, 396, 278);
        round_rect(dc, settings_save_, scale_for(window, 6), kGreen);
        round_rect(dc, settings_cancel_, scale_for(window, 6), kSurface, kLine);
        draw_text(dc, window, L"保存并重新计时", settings_save_, 10, FW_SEMIBOLD,
                  RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"取消", settings_cancel_, 10, FW_SEMIBOLD, kInk,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &paint);
    }

    void paint_reminder(HWND window) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        fill_rect(dc, client, kSurface);

        draw_app_mark(dc, scaled_rect(window, 30, 28, 74, 72), mark_icon_);
        draw_text(dc, window, L"该让眼睛休息了", scaled_rect(window, 94, 24, 446, 58),
                  19, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring completed = L"你已完成 " + std::to_wstring(work_minutes_) +
                                       L" 分钟有效工作";
        draw_text(dc, window, completed.c_str(), scaled_rect(window, 94, 57, 446, 82),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT accent = scaled_rect(window, 30, 108, 36, 185);
        round_rect(dc, accent, scale_for(window, 3), kGreen);
        draw_text(dc, window, L"请暂时离开屏幕", scaled_rect(window, 52, 105, 442, 136),
                  13, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring rest_prompt = L"眺望窗外或 6 米以外，让双眼放松 " +
                                         std::to_wstring(rest_minutes_) + L" 分钟。";
        draw_text(dc, window, rest_prompt.c_str(),
                  scaled_rect(window, 52, 139, 442, 184), 10, FW_NORMAL, kMuted,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);

        reminder_primary_ = scaled_rect(window, 30, 231, 280, 281);
        reminder_secondary_ = scaled_rect(window, 292, 231, 450, 281);
        round_rect(dc, reminder_primary_, scale_for(window, 6), kGreen);
        round_rect(dc, reminder_secondary_, scale_for(window, 6), kSurface, kLine);
        const std::wstring start_rest_text = L"开始休息 " +
                                             std::to_wstring(rest_minutes_) + L" 分钟";
        draw_text(dc, window, start_rest_text.c_str(), reminder_primary_, 10, FW_SEMIBOLD,
                  RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"5 分钟后提醒", reminder_secondary_, 9, FW_SEMIBOLD,
                  kInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &paint);
    }

    LRESULT handle_main(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_PAINT:
                paint_main(window);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_INPUT:
                record_raw_input(lparam);
                return DefWindowProcW(window, message, wparam, lparam);
            case WM_TIMER:
                if (wparam == kTickTimer) {
                    tick();
                } else if (wparam == kAnimationTimer) {
                    animate();
                }
                return 0;
            case WM_LBUTTONUP: {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (PtInRect(&main_primary_, point)) {
                    if (timer_.state() == lookaway::WorkTimer::State::Resting) {
                        timer_.finish_rest();
                    } else {
                        timer_.toggle_pause();
                    }
                    sync_animation_mode();
                    InvalidateRect(window, nullptr, FALSE);
                } else if (PtInRect(&settings_button_, point)) {
                    show_settings();
                } else if (PtInRect(&main_secondary_, point)) {
                    timer_.reset();
                    hide_reminder();
                    sync_animation_mode();
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            case WM_SETCURSOR: {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (PtInRect(&main_primary_, point) || PtInRect(&main_secondary_, point) ||
                    PtInRect(&settings_button_, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
            case WM_SIZE:
                if (wparam == SIZE_MINIMIZED) {
                    hide_to_tray();
                }
                return 0;
            case WM_CLOSE:
                if (shutting_down_) {
                    DestroyWindow(window);
                } else {
                    hide_to_tray();
                }
                return 0;
            case WM_COMMAND:
                execute_command(LOWORD(wparam));
                return 0;
            case WM_QUERYENDSESSION:
                return TRUE;
            case WM_ENDSESSION:
                if (wparam) {
                    shutting_down_ = true;
                    DestroyWindow(window);
                }
                return 0;
            case WM_DESTROY:
                KillTimer(window, kTickTimer);
                KillTimer(window, kAnimationTimer);
                remove_tray_icon();
                main_window_ = nullptr;
                PostQuitMessage(0);
                return 0;
            case kTrayMessage:
                if (LOWORD(lparam) == WM_LBUTTONUP || LOWORD(lparam) == WM_LBUTTONDBLCLK ||
                    LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT) {
                    show_main();
                } else if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
                    show_tray_menu();
                }
                return 0;
            case kShowExisting:
                show_main();
                return 0;
            default:
                break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handle_reminder(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_PAINT:
                paint_reminder(window);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_LBUTTONUP: {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (PtInRect(&reminder_primary_, point)) {
                    start_rest();
                } else if (PtInRect(&reminder_secondary_, point)) {
                    snooze();
                }
                return 0;
            }
            case WM_SETCURSOR: {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (PtInRect(&reminder_primary_, point) || PtInRect(&reminder_secondary_, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
            case WM_KEYDOWN:
                if (wparam == VK_RETURN) {
                    start_rest();
                } else if (wparam == VK_ESCAPE) {
                    snooze();
                }
                return 0;
            case WM_CLOSE:
                snooze();
                return 0;
            case WM_DESTROY:
                reminder_window_ = nullptr;
                return 0;
            default:
                break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handle_settings(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_PAINT:
                paint_settings(window);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_LBUTTONUP: {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (PtInRect(&settings_save_, point)) {
                    apply_settings();
                    return 0;
                }
                if (PtInRect(&settings_cancel_, point)) {
                    close_settings();
                    return 0;
                }
                adjust_setting(work_minus_, point, draft_work_minutes_,
                               -kWorkMinuteStep, kMinWorkMinutes, kMaxWorkMinutes);
                adjust_setting(work_plus_, point, draft_work_minutes_,
                               kWorkMinuteStep, kMinWorkMinutes, kMaxWorkMinutes);
                adjust_setting(rest_minus_, point, draft_rest_minutes_,
                               -1, kMinRestMinutes, kMaxRestMinutes);
                adjust_setting(rest_plus_, point, draft_rest_minutes_,
                               1, kMinRestMinutes, kMaxRestMinutes);
                return 0;
            }
            case WM_SETCURSOR: {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (PtInRect(&work_minus_, point) || PtInRect(&work_plus_, point) ||
                    PtInRect(&rest_minus_, point) || PtInRect(&rest_plus_, point) ||
                    PtInRect(&settings_save_, point) || PtInRect(&settings_cancel_, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
            case WM_KEYDOWN:
                if (wparam == VK_RETURN) {
                    apply_settings();
                } else if (wparam == VK_ESCAPE) {
                    close_settings();
                }
                return 0;
            case WM_CLOSE:
                close_settings();
                return 0;
            case WM_DESTROY:
                settings_window_ = nullptr;
                if (main_window_ && !shutting_down_) {
                    EnableWindow(main_window_, TRUE);
                }
                return 0;
            default:
                break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kMainClass, nullptr)) {
            PostMessageW(existing, kShowExisting, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token{};
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
        gdiplus_token = 0;
    }

    int result = 1;
    {
        Application application(instance);
        result = application.run(show_command);
    }
    if (gdiplus_token) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }
    if (mutex) {
        CloseHandle(mutex);
    }
    return result;
}
