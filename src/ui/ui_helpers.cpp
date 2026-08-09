#include "ui/ui_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "app/app_config.hpp"

namespace lookaway::ui {

using namespace lookaway::app;

Gdiplus::Color gdiplus_color(COLORREF color, BYTE alpha) {
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

HFONT create_font(HWND window, int points, int weight) {
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

void round_rect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border) {
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

void draw_collection_icon(HDC dc, const RECT& bounds, COLORREF color) {
    const int width = bounds.right - bounds.left;
    const int offset = std::max(1, width / 5);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, width / 10), color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, bounds.left + offset, bounds.top,
              bounds.right, bounds.bottom - offset, std::max(2, width / 5),
              std::max(2, width / 5));
    RoundRect(dc, bounds.left, bounds.top + offset,
              bounds.right - offset, bounds.bottom, std::max(2, width / 5),
              std::max(2, width / 5));
    SelectObject(dc, old_brush);
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

}  // namespace lookaway::ui
