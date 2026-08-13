#include "platform/fullscreen.hpp"

#include <windows.h>
#include <dwmapi.h>

namespace lookaway::platform {

bool is_foreground_window_fullscreen() {
    HWND window = GetForegroundWindow();
    if (!window) {
        return false;
    }

    window = GetAncestor(window, GA_ROOT);
    if (!window || !IsWindowVisible(window) ||
        window == GetShellWindow() || window == GetDesktopWindow()) {
        return false;
    }

    RECT window_bounds{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &window_bounds, sizeof(window_bounds))) &&
        !GetWindowRect(window, &window_bounds)) {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (!monitor) {
        return false;
    }

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info)) {
        return false;
    }

    // Allow for the one-pixel rounding differences used by some borderless games.
    constexpr LONG tolerance = 2;
    const RECT& screen = monitor_info.rcMonitor;
    return window_bounds.left <= screen.left + tolerance &&
           window_bounds.top <= screen.top + tolerance &&
           window_bounds.right >= screen.right - tolerance &&
           window_bounds.bottom >= screen.bottom - tolerance;
}

}  // namespace lookaway::platform
