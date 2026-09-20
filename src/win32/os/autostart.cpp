#include "azy/win32/os/autostart.hpp"

#include "azy/win32/os/win_compat.hpp"  // windows.h first

#include <shellapi.h>  // CommandLineToArgvW

#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"Azy Skin";

std::wstring quoted_executable() {
    return L"\"" + executable_path().wstring() + L"\" --tray";
}

}  // namespace

bool set_autostart_enabled(bool enabled, std::string* error) {
    HKEY key = nullptr;
    const LSTATUS opened =
        RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        if (error) *error = "cannot open HKCU Run key: " + to_utf8(last_error_text(opened));
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = quoted_executable();
        status = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;  // already absent
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        if (error) *error = "cannot update HKCU Run entry: " + to_utf8(last_error_text(status));
        return false;
    }
    return true;
}

bool autostart_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t buffer[1024] = {0};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status =
        RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool launched_at_startup() {
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        bool tray_flag = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = to_utf8(argv[i]);
            if (iequals(arg, "--tray") || iequals(arg, "-tray") || iequals(arg, "/tray")) tray_flag = true;
        }
        LocalFree(argv);
        if (tray_flag) return true;
    }
    return false;
}

}  // namespace win
}  // namespace azy
