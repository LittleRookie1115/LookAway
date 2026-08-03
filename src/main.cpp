#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#include "resource.h"
#include "work_timer.hpp"

namespace {

using namespace std::chrono_literals;

constexpr wchar_t kMainClass[] = L"LookAwayMainWindow";
constexpr wchar_t kReminderClass[] = L"LookAwayReminderWindow";
constexpr wchar_t kMutexName[] = L"Local\\LookAway.SingleInstance.1";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowExisting = WM_APP + 2;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTrayId = 1;
constexpr UINT kMenuOpen = 1001;
constexpr UINT kMenuPause = 1002;
constexpr UINT kMenuReset = 1003;
constexpr UINT kMenuExit = 1004;
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

class Application {
public:
    explicit Application(HINSTANCE instance) : instance_(instance) {}

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
        add_tray_icon();
        last_tick_ = GetTickCount64();
        SetTimer(main_window_, kTickTimer, 1000, nullptr);
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
    HICON large_icon_{};
    HICON small_icon_{};
    HICON mark_icon_{};
    lookaway::WorkTimer timer_{};
    ULONGLONG last_tick_{};
    bool system_idle_{false};
    bool long_idle_{false};
    bool shutting_down_{false};
    bool tray_hint_shown_{false};
    RECT main_primary_{};
    RECT main_secondary_{};
    RECT reminder_primary_{};
    RECT reminder_secondary_{};

    static Application* from_window(HWND window) {
        return reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    HICON load_icon(int size) const {
        HICON icon = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
            size, size, LR_DEFAULTCOLOR));
        return icon ? icon : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
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

    lookaway::WorkTimer::Duration system_idle_time() const {
        LASTINPUTINFO input{};
        input.cbSize = sizeof(input);
        if (!GetLastInputInfo(&input)) {
            return 0ms;
        }
        const DWORD idle = GetTickCount() - input.dwTime;
        return std::chrono::milliseconds(idle);
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
            show_balloon(L"休息完成", L"新的 45 分钟用眼周期已经开始。", NIIF_INFO);
            MessageBeep(MB_OK);
        } else if (event == lookaway::WorkTimer::Event::IdleReset) {
            hide_reminder();
        }
        InvalidateRect(main_window_, nullptr, FALSE);
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
                InvalidateRect(main_window_, nullptr, FALSE);
                break;
            case kMenuReset:
                timer_.reset();
                hide_reminder();
                InvalidateRect(main_window_, nullptr, FALSE);
                break;
            case kMenuExit:
                shutting_down_ = true;
                hide_reminder();
                DestroyWindow(main_window_);
                break;
            default:
                break;
        }
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
            show_balloon(L"该让眼睛休息了", L"你已工作 45 分钟，请离开屏幕休息。", NIIF_WARNING);
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
        InvalidateRect(main_window_, nullptr, FALSE);
    }

    void snooze() {
        timer_.snooze(5min);
        hide_reminder();
        show_balloon(L"已稍后提醒", L"5 分钟后会再次提醒你休息。", NIIF_INFO);
        InvalidateRect(main_window_, nullptr, FALSE);
    }

    void paint_main(HWND window) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        SetBkMode(dc, TRANSPARENT);
        fill_rect(dc, client, kBackground);

        draw_app_mark(dc, scaled_rect(window, 24, 22, 60, 58), mark_icon_);
        draw_text(dc, window, L"LookAway", scaled_rect(window, 72, 20, 240, 48),
                  17, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"护眼计时", scaled_rect(window, 72, 43, 240, 64),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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

        const auto shown_time = resting ? timer_.rest_remaining()
                                        : (snoozing ? timer_.snooze_remaining() : timer_.remaining());
        const std::wstring countdown = format_time(shown_time);
        draw_text(dc, window, countdown.c_str(), scaled_rect(window, 92, 194, 340, 258),
                  40, FW_SEMIBOLD, kInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, resting ? L"放松双眼，暂时离开屏幕"
                                     : (snoozing ? L"稍后提醒倒计时" : L"有效工作时间"),
                  scaled_rect(window, 88, 258, 344, 286), 9, FW_NORMAL, kMuted,
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
                          : L"离开屏幕 5 分钟，眺望窗外或 6 米以外。",
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

        draw_text(dc, window, L"45 分钟工作  |  5 分钟休息",
                  scaled_rect(window, 24, 520, 408, 548), 8, FW_NORMAL, kMuted,
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
        draw_text(dc, window, L"你已完成 45 分钟有效工作", scaled_rect(window, 94, 57, 446, 82),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT accent = scaled_rect(window, 30, 108, 36, 185);
        round_rect(dc, accent, scale_for(window, 3), kGreen);
        draw_text(dc, window, L"请暂时离开屏幕", scaled_rect(window, 52, 105, 442, 136),
                  13, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"眺望窗外或 6 米以外，让双眼放松 5 分钟。",
                  scaled_rect(window, 52, 139, 442, 184), 10, FW_NORMAL, kMuted,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);

        reminder_primary_ = scaled_rect(window, 30, 231, 280, 281);
        reminder_secondary_ = scaled_rect(window, 292, 231, 450, 281);
        round_rect(dc, reminder_primary_, scale_for(window, 6), kGreen);
        round_rect(dc, reminder_secondary_, scale_for(window, 6), kSurface, kLine);
        draw_text(dc, window, L"开始休息 5 分钟", reminder_primary_, 10, FW_SEMIBOLD,
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
            case WM_TIMER:
                if (wparam == kTickTimer) {
                    tick();
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
                    InvalidateRect(window, nullptr, FALSE);
                } else if (PtInRect(&main_secondary_, point)) {
                    timer_.reset();
                    hide_reminder();
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            case WM_SETCURSOR: {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (PtInRect(&main_primary_, point) || PtInRect(&main_secondary_, point)) {
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

    Application application(instance);
    const int result = application.run(show_command);
    if (mutex) {
        CloseHandle(mutex);
    }
    return result;
}
