#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <combaseapi.h>
#include <objidl.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "resource.h"
#include "usage_stats.hpp"
#include "work_timer.hpp"

namespace {

using namespace std::chrono_literals;

constexpr wchar_t kMainClass[] = L"LookAwayMainWindow";
constexpr wchar_t kReminderClass[] = L"LookAwayReminderWindow";
constexpr wchar_t kSettingsClass[] = L"LookAwaySettingsWindow";
constexpr wchar_t kStatisticsClass[] = L"LookAwayStatisticsWindow";
constexpr wchar_t kMutexName[] = L"Local\\LookAway.SingleInstance.1";
constexpr wchar_t kRegistryPath[] = L"Software\\LookAway";
constexpr wchar_t kUsageRegistryValue[] = L"UsageHistory";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowExisting = WM_APP + 2;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kAnimationTimer = 2;
constexpr UINT kMenuOpen = 1001;
constexpr UINT kMenuPause = 1002;
constexpr UINT kMenuReset = 1003;
constexpr UINT kMenuStatistics = 1004;
constexpr UINT kMenuSettings = 1005;
constexpr UINT kMenuExit = 1006;
constexpr int kMinWorkMinutes = 5;
constexpr int kMaxWorkMinutes = 60;
constexpr int kWorkMinuteStep = 5;
constexpr int kMinRestMinutes = 1;
constexpr int kMaxRestMinutes = 20;

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
constexpr ULONGLONG kFileTimeTicksPerDay = 864000000000ULL;
constexpr ULONGLONG kFileTimeTicksPerHour = 36000000000ULL;

Gdiplus::Color gdiplus_color(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

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

void add_rounded_rectangle(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& bounds,
                           Gdiplus::REAL radius) {
    const Gdiplus::REAL diameter = radius * 2.0F;
    path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(bounds.GetRight() - diameter, bounds.Y, diameter, diameter,
                270.0F, 90.0F);
    path.AddArc(bounds.GetRight() - diameter, bounds.GetBottom() - diameter,
                diameter, diameter, 0.0F, 90.0F);
    path.AddArc(bounds.X, bounds.GetBottom() - diameter, diameter, diameter,
                90.0F, 90.0F);
    path.CloseFigure();
}

void draw_frosted_rect(HDC dc, const RECT& rect, int radius) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const int shadow_offset = std::max(1, radius / 3);
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath shadow_path;
        add_rounded_rectangle(
            shadow_path,
            Gdiplus::RectF(static_cast<Gdiplus::REAL>(rect.left + shadow_offset),
                           static_cast<Gdiplus::REAL>(rect.top + shadow_offset),
                           static_cast<Gdiplus::REAL>(width),
                           static_cast<Gdiplus::REAL>(height)),
            static_cast<Gdiplus::REAL>(radius));
        Gdiplus::SolidBrush shadow(Gdiplus::Color(42, 54, 67, 61));
        graphics.FillPath(&shadow, &shadow_path);
    }

    const int sample_width = std::max(1, width / 5);
    const int sample_height = std::max(1, height / 5);
    HDC sample_dc = CreateCompatibleDC(dc);
    HBITMAP sample_bitmap = CreateCompatibleBitmap(dc, sample_width, sample_height);
    if (sample_dc && sample_bitmap) {
        HGDIOBJ old_bitmap = SelectObject(sample_dc, sample_bitmap);
        SetStretchBltMode(sample_dc, HALFTONE);
        StretchBlt(sample_dc, 0, 0, sample_width, sample_height,
                   dc, rect.left, rect.top, width, height, SRCCOPY);

        const int saved_dc = SaveDC(dc);
        HRGN clip = CreateRoundRectRgn(rect.left, rect.top, rect.right + 1,
                                      rect.bottom + 1, radius * 2, radius * 2);
        SelectClipRgn(dc, clip);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, rect.left, rect.top, width, height,
                   sample_dc, 0, 0, sample_width, sample_height, SRCCOPY);
        RestoreDC(dc, saved_dc);
        DeleteObject(clip);

        SelectObject(sample_dc, old_bitmap);
    }
    if (sample_bitmap) {
        DeleteObject(sample_bitmap);
    }
    if (sample_dc) {
        DeleteDC(sample_dc);
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    Gdiplus::GraphicsPath glass_path;
    add_rounded_rectangle(
        glass_path,
        Gdiplus::RectF(static_cast<Gdiplus::REAL>(rect.left),
                       static_cast<Gdiplus::REAL>(rect.top),
                       static_cast<Gdiplus::REAL>(width),
                       static_cast<Gdiplus::REAL>(height)),
        static_cast<Gdiplus::REAL>(radius));
    Gdiplus::SolidBrush glass(Gdiplus::Color(218, 244, 248, 246));
    graphics.FillPath(&glass, &glass_path);
    Gdiplus::Pen border(Gdiplus::Color(185, 181, 198, 190), 1.0F);
    graphics.DrawPath(&border, &glass_path);
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

void draw_smooth_text(HDC dc, HWND window, const wchar_t* text, const RECT& rect,
                      int points, int weight, COLORREF color) {
    HFONT native_font = create_font(window, points, weight);
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::Font font(dc, native_font);
        Gdiplus::SolidBrush brush(gdiplus_color(color));
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentNear);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        format.SetTrimming(Gdiplus::StringTrimmingNone);
        const Gdiplus::RectF bounds(
            static_cast<Gdiplus::REAL>(rect.left),
            static_cast<Gdiplus::REAL>(rect.top),
            static_cast<Gdiplus::REAL>(rect.right - rect.left),
            static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
        graphics.DrawString(text, -1, &font, bounds, &format, &brush);
    }
    DeleteObject(native_font);
}

void draw_app_mark(HDC dc, const RECT& bounds, HICON icon) {
    if (!icon) {
        return;
    }
    DrawIconEx(dc, bounds.left, bounds.top, icon,
               bounds.right - bounds.left, bounds.bottom - bounds.top,
               0, nullptr, DI_NORMAL);
}

void draw_statistics_icon(HDC dc, const RECT& bounds, COLORREF color) {
    const int left = bounds.left + (bounds.right - bounds.left) / 4;
    const int right = bounds.right - (bounds.right - bounds.left) / 4;
    const int bottom = bounds.bottom - (bounds.bottom - bounds.top) / 4;
    const int top = bounds.top + (bounds.bottom - bounds.top) / 4;
    const int thickness = std::max(1, static_cast<int>((bounds.right - bounds.left) / 14));
    HPEN pen = CreatePen(PS_SOLID, thickness, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, left, bottom, nullptr);
    LineTo(dc, left, top);
    LineTo(dc, right, top);
    MoveToEx(dc, left + (right - left) / 5, bottom, nullptr);
    LineTo(dc, left + (right - left) / 5, bottom - (bottom - top) / 3);
    MoveToEx(dc, left + (right - left) / 2, bottom, nullptr);
    LineTo(dc, left + (right - left) / 2, bottom - (bottom - top) * 2 / 3);
    MoveToEx(dc, left + (right - left) * 4 / 5, bottom, nullptr);
    LineTo(dc, left + (right - left) * 4 / 5, top + (bottom - top) / 5);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void draw_progress_ring(HDC dc, int center_x, int center_y, int radius, int thickness,
                         double progress, COLORREF color) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    const Gdiplus::RectF bounds{
        static_cast<Gdiplus::REAL>(center_x - radius),
        static_cast<Gdiplus::REAL>(center_y - radius),
        static_cast<Gdiplus::REAL>(radius * 2),
        static_cast<Gdiplus::REAL>(radius * 2)};
    const auto pen_width = static_cast<Gdiplus::REAL>(thickness);
    Gdiplus::Pen track(gdiplus_color(kTrack), pen_width);
    graphics.DrawEllipse(&track, bounds);

    progress = std::clamp(progress, 0.0, 1.0);
    if (progress > 0.001) {
        Gdiplus::Pen active(gdiplus_color(color), pen_width);
        active.SetStartCap(Gdiplus::LineCapRound);
        active.SetEndCap(Gdiplus::LineCapRound);
        if (progress >= 0.999) {
            graphics.DrawEllipse(&active, bounds);
        } else {
            graphics.DrawArc(&active, bounds, -90.0F,
                             static_cast<Gdiplus::REAL>(progress * 360.0));
        }
    }
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

std::int64_t local_day_index() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    local.wHour = 0;
    local.wMinute = 0;
    local.wSecond = 0;
    local.wMilliseconds = 0;

    FILETIME file_time{};
    if (!SystemTimeToFileTime(&local, &file_time)) {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart / kFileTimeTicksPerDay);
}

SYSTEMTIME system_time_for_day(std::int64_t day_index) {
    ULARGE_INTEGER value{};
    value.QuadPart = static_cast<ULONGLONG>(day_index) * kFileTimeTicksPerDay;
    FILETIME file_time{value.LowPart, value.HighPart};
    SYSTEMTIME result{};
    FileTimeToSystemTime(&file_time, &result);
    return result;
}

std::int64_t local_hour_index() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    local.wMinute = 0;
    local.wSecond = 0;
    local.wMilliseconds = 0;

    FILETIME file_time{};
    if (!SystemTimeToFileTime(&local, &file_time)) {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart / kFileTimeTicksPerHour);
}

SYSTEMTIME system_time_for_hour(std::int64_t hour_index) {
    ULARGE_INTEGER value{};
    value.QuadPart = static_cast<ULONGLONG>(hour_index) * kFileTimeTicksPerHour;
    FILETIME file_time{value.LowPart, value.HighPart};
    SYSTEMTIME result{};
    FileTimeToSystemTime(&file_time, &result);
    return result;
}

std::wstring format_stats_duration(lookaway::UsageStats::Duration duration) {
    const auto total_minutes = std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::minutes>(duration).count());
    if (total_minutes == 0 && duration > lookaway::UsageStats::Duration{0}) {
        return L"不足 1 分钟";
    }
    const auto hours = total_minutes / 60;
    const auto minutes = total_minutes % 60;
    if (hours == 0) {
        return std::to_wstring(minutes) + L" 分钟";
    }
    if (minutes == 0) {
        return std::to_wstring(hours) + L" 小时";
    }
    return std::to_wstring(hours) + L" 小时 " + std::to_wstring(minutes) + L" 分钟";
}

std::wstring format_chart_minutes(std::int64_t minutes) {
    const auto total_minutes = std::max<std::int64_t>(0, minutes);
    if (total_minutes >= 60 && total_minutes % 60 == 0) {
        return std::to_wstring(total_minutes / 60) + L"h";
    }
    return std::to_wstring(total_minutes) + L"m";
}

std::wstring format_day_label(std::int64_t day_index, std::int64_t today) {
    if (day_index == today) {
        return L"今天";
    }
    const SYSTEMTIME date = system_time_for_day(day_index);
    wchar_t buffer[16]{};
    std::swprintf(buffer, std::size(buffer), L"%02u/%02u",
                  static_cast<unsigned>(date.wMonth),
                  static_cast<unsigned>(date.wDay));
    return buffer;
}

std::wstring format_hour_label(std::int64_t hour_index, std::int64_t current_hour) {
    if (hour_index == current_hour) {
        return L"现在";
    }
    const SYSTEMTIME time = system_time_for_hour(hour_index);
    wchar_t buffer[16]{};
    std::swprintf(buffer, std::size(buffer), L"%02u:00",
                  static_cast<unsigned>(time.wHour));
    return buffer;
}

std::wstring format_day_hover_label(std::int64_t day_index) {
    const SYSTEMTIME date = system_time_for_day(day_index);
    wchar_t buffer[16]{};
    std::swprintf(buffer, std::size(buffer), L"%02u/%02u",
                  static_cast<unsigned>(date.wMonth),
                  static_cast<unsigned>(date.wDay));
    return buffer;
}

std::wstring format_hour_hover_label(std::int64_t hour_index) {
    const SYSTEMTIME time = system_time_for_hour(hour_index);
    wchar_t buffer[24]{};
    std::swprintf(buffer, std::size(buffer), L"%02u/%02u %02u:00",
                  static_cast<unsigned>(time.wMonth),
                  static_cast<unsigned>(time.wDay),
                  static_cast<unsigned>(time.wHour));
    return buffer;
}

std::wstring format_precise_duration(lookaway::UsageStats::Duration duration) {
    const auto total_seconds = std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(duration).count());
    if (total_seconds == 0) {
        return duration > lookaway::UsageStats::Duration{0} ? L"不足 1 秒" : L"0 秒";
    }

    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds % 3600) / 60;
    const auto seconds = total_seconds % 60;
    std::wstring result;
    if (hours > 0) {
        result += std::to_wstring(hours) + L" 小时 ";
    }
    if (minutes > 0 || hours > 0) {
        result += std::to_wstring(minutes) + L" 分 ";
    }
    result += std::to_wstring(seconds) + L" 秒";
    return result;
}

enum class StatisticsRange {
    SevenDays,
    TwentyFourHours,
};

struct StatisticsChartPoint {
    POINT position{};
    lookaway::UsageStats::Duration active{};
    std::wstring label;
};

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
        load_usage_stats();
        timer_ = lookaway::WorkTimer{
            std::chrono::minutes(work_minutes_), 1min,
            std::chrono::minutes(rest_minutes_), 5min};
        draft_work_minutes_ = work_minutes_;
        draft_rest_minutes_ = rest_minutes_;
    }

    ~Application() {
        if (usage_stats_dirty_) {
            persist_usage_stats();
        }
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
        last_usage_persist_tick_ = last_tick_;
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
    HWND statistics_window_{};
    HWND tooltip_window_{};
    HICON large_icon_{};
    HICON small_icon_{};
    HICON mark_icon_{};
    lookaway::WorkTimer timer_{};
    lookaway::UsageStats usage_stats_{};
    GifAnimation working_animation_;
    GifAnimation waiting_animation_;
    ULONGLONG last_tick_{};
    ULONGLONG last_animation_tick_{};
    ULONGLONG last_usage_persist_tick_{};
    ULONGLONG last_raw_input_tick_{};
    bool raw_input_registered_{false};
    bool system_idle_{false};
    bool long_idle_{false};
    bool show_working_animation_{true};
    bool shutting_down_{false};
    bool tray_hint_shown_{false};
    bool usage_stats_dirty_{false};
    bool statistics_mouse_tracking_{false};
    StatisticsRange statistics_range_{StatisticsRange::SevenDays};
    int statistics_hovered_point_{-1};
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
    RECT statistics_button_{};
    RECT statistics_days_tab_{};
    RECT statistics_hours_tab_{};
    std::vector<StatisticsChartPoint> statistics_chart_points_;

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

    void load_usage_stats() {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE,
                          &key) != ERROR_SUCCESS) {
            return;
        }

        DWORD type = 0;
        DWORD size = 0;
        const LSTATUS query_status = RegQueryValueExW(
            key, kUsageRegistryValue, nullptr, &type, nullptr, &size);
        if (query_status == ERROR_SUCCESS && type == REG_BINARY && size > 0) {
            std::string serialized(size, '\0');
            if (RegQueryValueExW(key, kUsageRegistryValue, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(serialized.data()), &size) ==
                ERROR_SUCCESS) {
                usage_stats_.deserialize(std::string_view(serialized.data(), size));
            }
        }
        RegCloseKey(key);
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

    bool persist_usage_stats() const {
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                            &key, nullptr) != ERROR_SUCCESS) {
            return false;
        }
        const std::string serialized = usage_stats_.serialize();
        const bool saved = RegSetValueExW(
                               key, kUsageRegistryValue, 0, REG_BINARY,
                               reinterpret_cast<const BYTE*>(serialized.data()),
                               static_cast<DWORD>(serialized.size())) == ERROR_SUCCESS;
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

    static LRESULT CALLBACK statistics_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        Application* app = from_window(window);
        return app ? app->handle_statistics(window, message, wparam, lparam)
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

        WNDCLASSEXW statistics_class{};
        statistics_class.cbSize = sizeof(statistics_class);
        statistics_class.style = CS_HREDRAW | CS_VREDRAW;
        statistics_class.lpfnWndProc = statistics_proc;
        statistics_class.hInstance = instance_;
        statistics_class.hIcon = large_icon_;
        statistics_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        statistics_class.hbrBackground = nullptr;
        statistics_class.lpszClassName = kStatisticsClass;
        statistics_class.hIconSm = small_icon_;
        RegisterClassExW(&statistics_class);
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
        statistics_button_ = scaled_rect(main_window_, 198, 25, 230, 57);
        settings_button_ = scaled_rect(main_window_, 238, 25, 270, 57);
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
        tool.rect = statistics_button_;
        tool.lpszText = const_cast<wchar_t*>(L"查看用眼统计");
        SendMessageW(tooltip_window_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        tool.uId = 2;
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

    void record_usage(lookaway::UsageStats::Duration duration, ULONGLONG now) {
        if (duration <= lookaway::UsageStats::Duration{0}) {
            return;
        }
        usage_stats_.add_active(local_day_index(), duration);
        usage_stats_.add_hourly_active(local_hour_index(), duration);
        usage_stats_dirty_ = true;
        if (now - last_usage_persist_tick_ >= 60000) {
            if (persist_usage_stats()) {
                usage_stats_dirty_ = false;
            }
            last_usage_persist_tick_ = now;
        }
    }

    void tick() {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG raw_delta = now - last_tick_;
        last_tick_ = now;
        const auto delta = std::chrono::milliseconds(std::min<ULONGLONG>(raw_delta, 5000));
        const auto idle = system_idle_time();
        system_idle_ = timer_.is_system_idle(idle);
        long_idle_ = timer_.is_long_idle(idle);

        const bool usage_active = timer_.is_usage_active(idle);
        const auto event = timer_.tick(delta, idle);
        if (usage_active) {
            record_usage(delta, now);
        }
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
        update_tray_tooltip();
        InvalidateRect(main_window_, nullptr, FALSE);
        if (statistics_window_) {
            InvalidateRect(statistics_window_, nullptr, FALSE);
        }
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
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = small_icon_;
        const std::wstring tip = tray_tooltip_text();
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &data);
        data.uVersion = 4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    std::wstring tray_tooltip_text() const {
        const bool resting = timer_.state() == lookaway::WorkTimer::State::Resting;
        const bool paused = timer_.state() == lookaway::WorkTimer::State::Paused;
        const bool snoozing = timer_.is_snoozing();
        const wchar_t* status = L"正在计时";
        if (resting) {
            status = L"正在休息";
        } else if (snoozing) {
            status = L"稍后提醒";
        } else if (paused) {
            status = L"计时已暂停";
        } else if (system_idle_) {
            status = long_idle_ ? L"长时间空闲，已重新计时" : L"已空闲，暂停计时";
        }

        const auto shown_time = resting ? timer_.rest_remaining()
                                        : (snoozing ? timer_.snooze_remaining()
                                                     : timer_.remaining());
        return L"LookAway - " + std::wstring(status) +
               L" - 剩余 " + format_time(shown_time);
    }

    void update_tray_tooltip() const {
        if (!main_window_) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = main_window_;
        data.uID = kTrayId;
        data.uFlags = NIF_TIP | NIF_SHOWTIP;
        const std::wstring tip = tray_tooltip_text();
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
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
        data.dwInfoFlags = (flags & ~NIIF_ICON_MASK) | NIIF_USER | NIIF_LARGE_ICON;
        data.hBalloonIcon = large_icon_;
        wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
        wcsncpy_s(data.szInfo, body, _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void hide_to_tray() {
        ShowWindow(main_window_, SW_HIDE);
        if (statistics_window_) {
            ShowWindow(statistics_window_, SW_HIDE);
        }
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
        AppendMenuW(menu, MF_STRING, kMenuStatistics, L"用眼统计");
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
            case kMenuStatistics:
                show_statistics();
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
                if (statistics_window_) {
                    DestroyWindow(statistics_window_);
                }
                DestroyWindow(main_window_);
                break;
            default:
                break;
        }
    }

    void ensure_statistics_window() {
        if (statistics_window_) {
            return;
        }
        statistics_window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME, kStatisticsClass, L"LookAway 用眼统计",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, scale_for(main_window_, 560),
            scale_for(main_window_, 560), main_window_, nullptr, instance_, this);
        if (!statistics_window_) {
            return;
        }

        RECT client{};
        RECT window{};
        GetClientRect(statistics_window_, &client);
        GetWindowRect(statistics_window_, &window);
        const int target_width = scale_for(statistics_window_, 560);
        const int target_height = scale_for(statistics_window_, 520);
        const int frame_width = (window.right - window.left) - (client.right - client.left);
        const int frame_height = (window.bottom - window.top) - (client.bottom - client.top);
        SetWindowPos(statistics_window_, nullptr, 0, 0,
                     target_width + frame_width, target_height + frame_height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void position_statistics_window() const {
        RECT owner{};
        RECT statistics{};
        GetWindowRect(main_window_, &owner);
        GetWindowRect(statistics_window_, &statistics);
        const int width = statistics.right - statistics.left;
        const int height = statistics.bottom - statistics.top;
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
        SetWindowPos(statistics_window_, HWND_TOP, x, y, width, height,
                     SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }

    void show_statistics() {
        show_main();
        ensure_statistics_window();
        if (!statistics_window_) {
            return;
        }
        position_statistics_window();
        ShowWindow(statistics_window_, SW_SHOW);
        SetForegroundWindow(statistics_window_);
        InvalidateRect(statistics_window_, nullptr, FALSE);
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
        draw_text(dc, window, L"LookAway", scaled_rect(window, 72, 20, 190, 48),
                  17, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"护眼计时", scaled_rect(window, 72, 43, 190, 64),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        statistics_button_ = scaled_rect(window, 198, 25, 230, 57);
        round_rect(dc, statistics_button_, scale_for(window, 6), kSurface, kLine);
        draw_statistics_icon(dc, scaled_rect(window, 206, 31, 222, 51), kMuted);

        settings_button_ = scaled_rect(window, 238, 25, 270, 57);
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
            status = L"稍后提醒";
            status_color = kAmber;
            status_fill = kAmberSoft;
        } else if (paused) {
            status = L"计时已暂停";
            status_color = kMuted;
            status_fill = RGB(234, 236, 234);
        } else if (system_idle_) {
            status = long_idle_ ? L"空闲重置" : L"空闲暂停";
            status_color = kAmber;
            status_fill = kAmberSoft;
        }
        RECT status_rect = scaled_rect(window, 280, 27, 408, 54);
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

    void draw_statistics_tooltip(HDC dc, HWND window, const RECT& chart) const {
        if (statistics_hovered_point_ < 0 ||
            statistics_hovered_point_ >=
                static_cast<int>(statistics_chart_points_.size())) {
            return;
        }

        const StatisticsChartPoint& point =
            statistics_chart_points_[statistics_hovered_point_];
        const int width = scale_for(window, 180);
        const int height = scale_for(window, 56);
        const int margin = scale_for(window, 10);
        const int gap = scale_for(window, 12);

        int left = point.position.x - width / 2;
        left = std::clamp(left, static_cast<int>(chart.left) + margin,
                          static_cast<int>(chart.right) - margin - width);
        int top = point.position.y - gap - height;
        if (top < chart.top + margin) {
            top = point.position.y + gap;
        }
        top = std::clamp(top, static_cast<int>(chart.top) + margin,
                         static_cast<int>(chart.bottom) - margin - height);

        const RECT tooltip{left, top, left + width, top + height};
        draw_frosted_rect(dc, tooltip, scale_for(window, 7));

        RECT label_rect = tooltip;
        label_rect.left += scale_for(window, 12);
        label_rect.right -= scale_for(window, 12);
        label_rect.top += scale_for(window, 7);
        label_rect.bottom = label_rect.top + scale_for(window, 18);
        draw_smooth_text(dc, window, point.label.c_str(), label_rect, 8, FW_MEDIUM,
                         kGreenDark);

        const std::wstring duration = L"用眼 " + format_precise_duration(point.active);
        RECT duration_rect = tooltip;
        duration_rect.left += scale_for(window, 12);
        duration_rect.right -= scale_for(window, 12);
        duration_rect.top += scale_for(window, 25);
        duration_rect.bottom -= scale_for(window, 7);
        draw_smooth_text(dc, window, duration.c_str(), duration_rect, 9, FW_MEDIUM,
                         kInk);
    }

    void paint_statistics(HWND window) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        fill_rect(dc, client, kBackground);

        const bool hourly_view = statistics_range_ == StatisticsRange::TwentyFourHours;
        const std::int64_t today = local_day_index();
        const std::int64_t current_hour = local_hour_index();
        std::vector<lookaway::UsageStats::Duration> values;
        std::vector<std::wstring> labels;
        std::vector<std::wstring> hover_labels;
        lookaway::UsageStats::Duration total{};
        lookaway::UsageStats::Duration average{};
        lookaway::UsageStats::Duration current_usage{};
        std::size_t active_periods = 0;

        if (hourly_view) {
            const auto hours = usage_stats_.recent_hours(current_hour, 24);
            total = usage_stats_.total_for_hours(current_hour, 24);
            average = lookaway::UsageStats::Duration{total.count() / 24};
            active_periods = usage_stats_.active_hours(current_hour, 24);
            values.reserve(hours.size());
            labels.reserve(hours.size());
            hover_labels.reserve(hours.size());
            for (std::size_t index = 0; index < hours.size(); ++index) {
                values.push_back(hours[index].active);
                const bool show_label = index % 6 == 0 || index + 1 == hours.size();
                labels.push_back(show_label
                                     ? format_hour_label(hours[index].hour_index, current_hour)
                                     : L"");
                hover_labels.push_back(format_hour_hover_label(hours[index].hour_index));
            }
            if (!hours.empty()) {
                current_usage = hours.back().active;
            }
        } else {
            const auto days = usage_stats_.recent(today, 7);
            total = usage_stats_.total_for_period(today, 7);
            average = lookaway::UsageStats::Duration{total.count() / 7};
            active_periods = usage_stats_.active_days(today, 7);
            values.reserve(days.size());
            labels.reserve(days.size());
            hover_labels.reserve(days.size());
            for (const lookaway::UsageDay& day : days) {
                values.push_back(day.active);
                labels.push_back(format_day_label(day.day_index, today));
                hover_labels.push_back(format_day_hover_label(day.day_index));
            }
            if (!days.empty()) {
                current_usage = days.back().active;
            }
        }

        const wchar_t* subtitle = hourly_view
                                      ? L"最近 24 小时 · 分时用眼趋势"
                                      : L"最近 7 天 · 用眼时长趋势";
        const wchar_t* total_label = hourly_view ? L"24 小时总计" : L"7 天总计";
        const wchar_t* average_label = hourly_view ? L"小时平均" : L"日均";
        const wchar_t* active_label = hourly_view ? L"活跃小时" : L"活跃天数";
        const wchar_t* chart_title = hourly_view ? L"每小时趋势" : L"每日趋势";
        const wchar_t* current_label = hourly_view ? L"当前小时" : L"今天";

        draw_app_mark(dc, scaled_rect(window, 24, 20, 58, 54), mark_icon_);
        draw_text(dc, window, L"用眼统计", scaled_rect(window, 72, 18, 300, 48),
                  17, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, subtitle,
                  scaled_rect(window, 72, 45, 360, 67),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT range_control = scaled_rect(window, 386, 22, 528, 58);
        const int range_middle = (range_control.left + range_control.right) / 2;
        statistics_days_tab_ = RECT{range_control.left, range_control.top,
                                    range_middle, range_control.bottom};
        statistics_hours_tab_ = RECT{range_middle, range_control.top,
                                     range_control.right, range_control.bottom};
        round_rect(dc, range_control, scale_for(window, 6), kSurface, kLine);

        const RECT selected_range = hourly_view ? statistics_hours_tab_
                                                 : statistics_days_tab_;
        const int saved_dc = SaveDC(dc);
        HRGN range_clip = CreateRoundRectRgn(
            range_control.left, range_control.top, range_control.right + 1,
            range_control.bottom + 1, scale_for(window, 6), scale_for(window, 6));
        SelectClipRgn(dc, range_clip);
        fill_rect(dc, selected_range, kGreenSoft);
        RestoreDC(dc, saved_dc);
        DeleteObject(range_clip);

        HPEN range_pen = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ old_range_pen = SelectObject(dc, range_pen);
        HGDIOBJ old_range_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, range_control.left, range_control.top,
                  range_control.right, range_control.bottom,
                  scale_for(window, 6), scale_for(window, 6));
        MoveToEx(dc, range_middle, range_control.top, nullptr);
        LineTo(dc, range_middle, range_control.bottom);
        SelectObject(dc, old_range_brush);
        SelectObject(dc, old_range_pen);
        DeleteObject(range_pen);

        draw_text(dc, window, L"7 天", statistics_days_tab_, 8,
                  hourly_view ? FW_NORMAL : FW_SEMIBOLD,
                  hourly_view ? kMuted : kGreenDark,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"24 小时", statistics_hours_tab_, 8,
                  hourly_view ? FW_SEMIBOLD : FW_NORMAL,
                  hourly_view ? kGreenDark : kMuted,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        HPEN divider = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ old_pen = SelectObject(dc, divider);
        MoveToEx(dc, scale_for(window, 24), scale_for(window, 75), nullptr);
        LineTo(dc, scale_for(window, 536), scale_for(window, 75));
        SelectObject(dc, old_pen);
        DeleteObject(divider);

        draw_text(dc, window, total_label, scaled_rect(window, 32, 88, 180, 108),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring total_text = format_stats_duration(total);
        draw_text(dc, window, total_text.c_str(), scaled_rect(window, 32, 108, 180, 136),
                  15, FW_SEMIBOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        draw_text(dc, window, average_label, scaled_rect(window, 208, 88, 340, 108),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring average_text = format_stats_duration(average);
        draw_text(dc, window, average_text.c_str(), scaled_rect(window, 208, 108, 340, 136),
                  15, FW_SEMIBOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        draw_text(dc, window, active_label, scaled_rect(window, 392, 88, 520, 108),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring active_periods_text =
            std::to_wstring(active_periods) + (hourly_view ? L" 小时" : L" 天");
        draw_text(dc, window, active_periods_text.c_str(),
                  scaled_rect(window, 392, 108, 520, 136),
                  15, FW_SEMIBOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT chart = scaled_rect(window, 32, 158, 528, 390);
        round_rect(dc, chart, scale_for(window, 8), kSurface, kLine);
        draw_text(dc, window, chart_title, scaled_rect(window, 52, 174, 230, 198),
                  11, FW_BOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"用眼时长", scaled_rect(window, 390, 174, 508, 198),
                  8, FW_NORMAL, kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        const RECT plot = scaled_rect(window, 72, 212, 496, 332);
        const auto max_value = std::max_element(values.begin(), values.end());
        const auto max_minutes = max_value == values.end()
                                     ? 0LL
                                     : std::max<std::int64_t>(
                                           0, std::chrono::duration_cast<std::chrono::minutes>(
                                                  *max_value).count());
        const auto upper_minutes = std::max<std::int64_t>(
            60, ((max_minutes + 59) / 60) * 60);

        HPEN grid_pen = CreatePen(PS_DOT, 1, kLine);
        HGDIOBJ old_grid_pen = SelectObject(dc, grid_pen);
        for (int row = 0; row <= 4; ++row) {
            const int y = plot.bottom -
                          MulDiv(plot.bottom - plot.top, row, 4);
            MoveToEx(dc, plot.left, y, nullptr);
            LineTo(dc, plot.right, y);
            const auto minutes = upper_minutes * row / 4;
            const std::wstring axis_label = format_chart_minutes(minutes);
            draw_text(dc, window, axis_label.c_str(),
                      RECT{scale_for(window, 36), y - scale_for(window, 8),
                           scale_for(window, 66), y + scale_for(window, 8)},
                      8, FW_NORMAL, kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, old_grid_pen);
        DeleteObject(grid_pen);

        std::vector<Gdiplus::PointF> points;
        points.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const double fraction = values.size() <= 1
                                        ? 0.5
                                        : static_cast<double>(index) /
                                              static_cast<double>(values.size() - 1);
            const double active_minutes =
                std::chrono::duration<double, std::ratio<60>>(values[index]).count();
            const double height_fraction = std::clamp(
                active_minutes / static_cast<double>(upper_minutes), 0.0, 1.0);
            points.emplace_back(
                static_cast<Gdiplus::REAL>(
                    plot.left + fraction * static_cast<double>(plot.right - plot.left)),
                static_cast<Gdiplus::REAL>(
                    plot.bottom - height_fraction * static_cast<double>(plot.bottom - plot.top)));
        }

        const bool has_chart_data = max_value != values.end() &&
                                    *max_value > lookaway::UsageStats::Duration{0};
        statistics_chart_points_.clear();
        if (has_chart_data) {
            statistics_chart_points_.reserve(points.size());
            for (std::size_t index = 0; index < points.size(); ++index) {
                statistics_chart_points_.push_back(StatisticsChartPoint{
                    POINT{static_cast<LONG>(std::lround(points[index].X)),
                          static_cast<LONG>(std::lround(points[index].Y))},
                    values[index], hover_labels[index]});
            }
        }
        if (statistics_hovered_point_ < 0 ||
            statistics_hovered_point_ >=
                static_cast<int>(statistics_chart_points_.size())) {
            statistics_hovered_point_ = -1;
        }

        if (has_chart_data) {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

            std::vector<Gdiplus::PointF> area = points;
            area.emplace_back(points.back().X, static_cast<Gdiplus::REAL>(plot.bottom));
            area.emplace_back(points.front().X, static_cast<Gdiplus::REAL>(plot.bottom));
            Gdiplus::SolidBrush area_brush(gdiplus_color(kGreenSoft));
            graphics.FillPolygon(&area_brush, area.data(), static_cast<INT>(area.size()));

            Gdiplus::Pen trend_pen(
                gdiplus_color(kGreen), static_cast<Gdiplus::REAL>(scale_for(window, 2)));
            trend_pen.SetStartCap(Gdiplus::LineCapRound);
            trend_pen.SetEndCap(Gdiplus::LineCapRound);
            trend_pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawLines(&trend_pen, points.data(), static_cast<INT>(points.size()));

            Gdiplus::SolidBrush point_brush(gdiplus_color(kGreen));
            const auto point_radius = static_cast<Gdiplus::REAL>(
                scale_for(window, hourly_view ? 3 : 4));
            for (const Gdiplus::PointF& point : points) {
                graphics.FillEllipse(&point_brush, point.X - point_radius,
                                     point.Y - point_radius, point_radius * 2,
                                     point_radius * 2);
            }

            if (statistics_hovered_point_ >= 0) {
                const Gdiplus::PointF& hovered = points[statistics_hovered_point_];
                const auto halo_radius = point_radius +
                                         static_cast<Gdiplus::REAL>(scale_for(window, 3));
                Gdiplus::SolidBrush halo_brush(gdiplus_color(kSurface));
                graphics.FillEllipse(&halo_brush, hovered.X - halo_radius,
                                     hovered.Y - halo_radius, halo_radius * 2,
                                     halo_radius * 2);
                graphics.FillEllipse(&point_brush, hovered.X - point_radius,
                                     hovered.Y - point_radius, point_radius * 2,
                                     point_radius * 2);
            }
        } else {
            draw_text(dc, window, L"还没有可展示的记录", plot, 10, FW_NORMAL, kMuted,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        for (std::size_t index = 0; index < labels.size(); ++index) {
            if (labels[index].empty()) {
                continue;
            }
            const double fraction = labels.size() <= 1
                                        ? 0.5
                                        : static_cast<double>(index) /
                                              static_cast<double>(labels.size() - 1);
            const int x = plot.left +
                          static_cast<int>(fraction * (plot.right - plot.left));
            draw_text(dc, window, labels[index].c_str(),
                      RECT{x - scale_for(window, 32), plot.bottom + scale_for(window, 8),
                           x + scale_for(window, 32), plot.bottom + scale_for(window, 28)},
                      8, FW_NORMAL, index + 1 == labels.size() ? kGreenDark : kMuted,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        draw_statistics_tooltip(dc, window, chart);

        HPEN footer_pen = CreatePen(PS_SOLID, 1, kLine);
        HGDIOBJ old_footer_pen = SelectObject(dc, footer_pen);
        MoveToEx(dc, scale_for(window, 32), scale_for(window, 421), nullptr);
        LineTo(dc, scale_for(window, 528), scale_for(window, 421));
        SelectObject(dc, old_footer_pen);
        DeleteObject(footer_pen);

        draw_text(dc, window, current_label, scaled_rect(window, 32, 438, 150, 462),
                  9, FW_NORMAL, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const std::wstring current_text = format_stats_duration(current_usage);
        draw_text(dc, window, current_text.c_str(), scaled_rect(window, 32, 460, 260, 488),
                  15, FW_SEMIBOLD, kInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, window, L"用眼时长", scaled_rect(window, 420, 450, 520, 474),
                  9, FW_NORMAL, kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
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
                } else if (PtInRect(&statistics_button_, point)) {
                    show_statistics();
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
                    PtInRect(&statistics_button_, point) || PtInRect(&settings_button_, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
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
                if (statistics_window_) {
                    DestroyWindow(statistics_window_);
                }
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

    LRESULT handle_statistics(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_PAINT:
                paint_statistics(window);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_MOUSEMOVE: {
                if (!statistics_mouse_tracking_) {
                    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
                    statistics_mouse_tracking_ = TrackMouseEvent(&tracking) != FALSE;
                }

                const POINT mouse{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const auto hit_radius = static_cast<long long>(scale_for(window, 11));
                const auto hit_radius_squared = hit_radius * hit_radius;
                long long nearest_distance = hit_radius_squared + 1;
                int hovered = -1;
                for (std::size_t index = 0;
                     index < statistics_chart_points_.size(); ++index) {
                    const auto dx = static_cast<long long>(mouse.x) -
                                    statistics_chart_points_[index].position.x;
                    const auto dy = static_cast<long long>(mouse.y) -
                                    statistics_chart_points_[index].position.y;
                    const auto distance = dx * dx + dy * dy;
                    if (distance <= hit_radius_squared && distance < nearest_distance) {
                        nearest_distance = distance;
                        hovered = static_cast<int>(index);
                    }
                }
                if (hovered != statistics_hovered_point_) {
                    statistics_hovered_point_ = hovered;
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                statistics_mouse_tracking_ = false;
                if (statistics_hovered_point_ >= 0) {
                    statistics_hovered_point_ = -1;
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONUP: {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (PtInRect(&statistics_days_tab_, point)) {
                    statistics_range_ = StatisticsRange::SevenDays;
                    statistics_hovered_point_ = -1;
                    InvalidateRect(window, nullptr, FALSE);
                } else if (PtInRect(&statistics_hours_tab_, point)) {
                    statistics_range_ = StatisticsRange::TwentyFourHours;
                    statistics_hovered_point_ = -1;
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            case WM_SETCURSOR: {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (PtInRect(&statistics_days_tab_, point) ||
                    PtInRect(&statistics_hours_tab_, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
            case WM_KEYDOWN:
                if (wparam == VK_ESCAPE) {
                    ShowWindow(window, SW_HIDE);
                } else if (wparam == VK_LEFT) {
                    statistics_range_ = StatisticsRange::SevenDays;
                    statistics_hovered_point_ = -1;
                    InvalidateRect(window, nullptr, FALSE);
                } else if (wparam == VK_RIGHT) {
                    statistics_range_ = StatisticsRange::TwentyFourHours;
                    statistics_hovered_point_ = -1;
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            case WM_CLOSE:
                statistics_hovered_point_ = -1;
                ShowWindow(window, SW_HIDE);
                return 0;
            case WM_DESTROY:
                statistics_mouse_tracking_ = false;
                statistics_hovered_point_ = -1;
                statistics_chart_points_.clear();
                statistics_window_ = nullptr;
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
    SetCurrentProcessExplicitAppUserModelID(L"LookAway");
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
