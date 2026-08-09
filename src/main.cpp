#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <shobjidl.h>
#include <gdiplus.h>

#include "app/app_config.hpp"
#include "app/application.hpp"
#include "platform/startup.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetProcessDPIAware();
    SetCurrentProcessExplicitAppUserModelID(L"LookAway");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    const bool autostart_requested =
        lookaway::startup::command_line_requests_autostart();
    if (autostart_requested) {
        show_command = SW_HIDE;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, lookaway::app::kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!autostart_requested) {
            if (HWND existing = FindWindowW(lookaway::app::kMainClass, nullptr)) {
                PostMessageW(existing, lookaway::app::kShowExisting, 0, 0);
            }
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
    result = lookaway::runtime::run_application(instance, show_command);
    if (gdiplus_token) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }
    if (mutex) {
        CloseHandle(mutex);
    }
    return result;
}
