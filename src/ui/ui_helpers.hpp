#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <string>

#include "core/usage_stats.hpp"
#include "core/work_timer.hpp"

namespace lookaway::ui {

Gdiplus::Color gdiplus_color(COLORREF color, BYTE alpha = 255);

UINT dpi_for(HWND window);
int scale_for(HWND window, int value);
RECT scaled_rect(HWND window, int left, int top, int right, int bottom);
HFONT create_font(HWND window, int points, int weight = FW_NORMAL);

void fill_rect(HDC dc, const RECT& rect, COLORREF color);
void round_rect(HDC dc, const RECT& rect, int radius, COLORREF fill,
                COLORREF border = CLR_INVALID);
void draw_frosted_rect(HDC dc, const RECT& rect, int radius);
void draw_text(HDC dc, HWND window, const wchar_t* text, const RECT& rect,
               int points, int weight, COLORREF color, UINT format);
void draw_smooth_text(HDC dc, HWND window, const wchar_t* text, const RECT& rect,
                      int points, int weight, COLORREF color);
void draw_app_mark(HDC dc, const RECT& bounds, HICON icon);
void draw_statistics_icon(HDC dc, const RECT& bounds, COLORREF color);
void draw_collection_icon(HDC dc, const RECT& bounds, COLORREF color);
void draw_progress_ring(HDC dc, int center_x, int center_y, int radius,
                        int thickness, double progress, COLORREF color);

std::wstring format_time(lookaway::WorkTimer::Duration duration);
std::int64_t local_day_index();
std::int64_t local_hour_index();
std::wstring format_stats_duration(lookaway::UsageStats::Duration duration);
std::wstring format_chart_minutes(std::int64_t minutes);
std::wstring format_day_label(std::int64_t day_index, std::int64_t today);
std::wstring format_hour_label(std::int64_t hour_index, std::int64_t current_hour);
std::wstring format_day_hover_label(std::int64_t day_index);
std::wstring format_hour_hover_label(std::int64_t hour_index);
std::wstring format_precise_duration(lookaway::UsageStats::Duration duration);

enum class StatisticsRange {
    SevenDays,
    TwentyFourHours,
};

struct StatisticsChartPoint {
    POINT position{};
    lookaway::UsageStats::Duration active{};
    std::wstring label;
};

}  // namespace lookaway::ui
