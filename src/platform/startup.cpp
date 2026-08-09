#include "platform/startup.hpp"

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "app/app_config.hpp"

namespace lookaway::startup {

namespace {

using namespace lookaway::app;

std::wstring executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (length != 0 && length >= path.size() - 1) {
        path.resize(path.size() * 2, L'\0');
        length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    if (length == 0) {
        return {};
    }
    path.resize(length);
    return path;
}

bool read_run_at_startup_command(std::wstring& command) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, kRunValueName, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }

    std::wstring value(size / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(
        key, kRunValueName, nullptr, &type,
        reinterpret_cast<LPBYTE>(value.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::size_t terminator = value.find(L'\0');
    if (terminator != std::wstring::npos) {
        value.resize(terminator);
    }
    command = std::move(value);
    return !command.empty();
}

std::wstring run_at_startup_command() {
    const std::wstring path = executable_path();
    return path.empty() ? std::wstring{} : L"\"" + path + L"\" " + kAutostartArg;
}

}  // namespace

bool command_line_requests_autostart() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }

    bool found = false;
    for (int index = 1; index < argc; ++index) {
        if (lstrcmpiW(argv[index], kAutostartArg) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

bool is_run_at_startup_enabled() {
    std::wstring command;
    return read_run_at_startup_command(command);
}

bool set_run_at_startup(bool enabled) {
    HKEY key{};
    if (enabled) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                            &key, nullptr) != ERROR_SUCCESS) {
            return false;
        }
    } else {
        const LSTATUS status = RegOpenKeyExW(
            HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        if (status != ERROR_SUCCESS) {
            return false;
        }
    }

    bool ok = false;
    if (enabled) {
        const std::wstring command = run_at_startup_command();
        if (!command.empty()) {
            ok = RegSetValueExW(
                     key, kRunValueName, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(command.c_str()),
                     static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) ==
                 ERROR_SUCCESS;
        }
    } else {
        const LSTATUS status = RegDeleteValueW(key, kRunValueName);
        ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

void synchronize_run_at_startup_path() {
    std::wstring registered_command;
    const std::wstring current_command = run_at_startup_command();
    if (current_command.empty() || !read_run_at_startup_command(registered_command) ||
        registered_command == current_command) {
        return;
    }
    set_run_at_startup(true);
}

}  // namespace lookaway::startup
