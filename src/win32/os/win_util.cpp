#include "azy/win32/os/win_util.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <shlobj.h>

#include <cstdio>

#include "azy/core/strings.hpp"
#include "azy/win32/os/win_api.hpp"

namespace azy {
namespace win {
namespace {

std::wstring query_module_path(HMODULE module) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD written = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return std::wstring();
        if (written < buffer.size() - 1) return std::wstring(buffer.data(), written);
        if (buffer.size() > 32768) return std::wstring();
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace

std::filesystem::path executable_path() { return std::filesystem::path(query_module_path(nullptr)); }

std::filesystem::path executable_dir() {
    const std::filesystem::path exe = executable_path();
    return exe.has_parent_path() ? exe.parent_path() : std::filesystem::current_path();
}

std::filesystem::path local_app_data_dir() {
    PWSTR raw = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        dir = std::filesystem::path(raw) / L"Azy Skin";
    }
    if (raw) CoTaskMemFree(raw);
    if (dir.empty()) {
        // Extremely unlikely fallback; keeps logging and settings working in a
        // locked-down profile rather than failing silently.
        dir = executable_dir() / L"AzySkinData";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path settings_path() { return local_app_data_dir() / L"settings.ini"; }
std::filesystem::path log_path() { return local_app_data_dir() / L"azy.log"; }

bool read_text_file(const std::filesystem::path& path, std::string& out) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    std::FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    out.clear();
    char buffer[8192];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        out.append(buffer, read);
        if (out.size() > 4u * 1024u * 1024u) break;  // sanity cap
    }
    std::fclose(f);
    return true;
}

bool write_text_file_atomic(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temp = path.wstring() + L".tmp";
    std::FILE* f = _wfopen(temp.c_str(), L"wb");
    if (!f) return false;
    const size_t written = contents.empty() ? 0 : std::fwrite(contents.data(), 1, contents.size(), f);
    std::fclose(f);
    if (written != contents.size()) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    // MoveFileEx gives an atomic replace, so a crash can never leave a partial
    // settings file behind.
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::string to_utf8(const std::wstring& s) { return utf16_to_utf8(s); }
std::wstring to_wide(const std::string& s) { return utf8_to_utf16(s); }

std::wstring last_error_text(DWORD error) {
    if (error == 0) error = GetLastError();
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text;
    if (length && buffer) {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
    }
    if (buffer) LocalFree(buffer);
    if (text.empty()) text = L"error " + std::to_wstring(error);
    return text;
}

std::wstring hresult_text(HRESULT hr) {
    wchar_t buffer[128];
    std::swprintf(buffer, 128, L"0x%08lX", static_cast<unsigned long>(hr));
    return std::wstring(buffer);
}

bool iequals_wide(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring window_class_name(HWND hwnd) {
    wchar_t buffer[256] = {0};
    const int length = GetClassNameW(hwnd, buffer, 256);
    return length > 0 ? std::wstring(buffer, static_cast<size_t>(length)) : std::wstring();
}

std::wstring window_text(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return std::wstring();
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
    const int copied = GetWindowTextW(hwnd, buffer.data(), length + 1);
    return copied > 0 ? std::wstring(buffer.data(), static_cast<size_t>(copied)) : std::wstring();
}

bool visible_frame_rect(HWND hwnd, RECT& out) {
    RECT bounds{};
    const HRESULT hr = DwmGetWindowAttribute(hwnd, dwm_attr::kExtendedFrameBounds, &bounds, sizeof(bounds));
    if (SUCCEEDED(hr) && bounds.right > bounds.left && bounds.bottom > bounds.top) {
        out = bounds;
        return true;
    }
    return GetWindowRect(hwnd, &out) != FALSE;
}

Rect to_rect(const RECT& r) { return Rect{r.left, r.top, r.right, r.bottom}; }
RECT to_native(const Rect& r) {
    RECT out{};
    out.left = r.left;
    out.top = r.top;
    out.right = r.right;
    out.bottom = r.bottom;
    return out;
}

bool is_window_minimized(HWND hwnd) { return IsIconic(hwnd) != FALSE; }
bool is_window_maximized(HWND hwnd) { return IsZoomed(hwnd) != FALSE; }

bool is_window_cloaked(HWND hwnd) {
    // DWM cloaking hides windows that exist but are not on screen (virtual
    // desktop switching, suspended UWP hosts). Treat them as invisible.
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != FALSE;
    }
    return false;
}

bool system_uses_dark_apps() {
    DWORD value = 1;  // AppsUseLightTheme: 1 = light
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
    }
    return value == 0;
}

double monotonic_seconds() {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start).count();
}

double wall_seconds() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

bool cursor_in_rect(const RECT& rect) {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    return cursor.x >= rect.left && cursor.x < rect.right && cursor.y >= rect.top && cursor.y < rect.bottom;
}

MachineClass detect_machine_class() {
    MachineClass info;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        info.physical_memory_bytes = memory.ullTotalPhys;
    }
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    info.logical_processors = system_info.dwNumberOfProcessors;

    // "Lower-end" is deliberately generous: an 8 GB / 4-thread machine runs
    // Premiere, but the difference between the two treatments is a slightly
    // softer edge, and being conservative there is never wrong.
    const bool small_ram = info.physical_memory_bytes > 0 && info.physical_memory_bytes < (8ull << 30);
    const bool few_cores = info.logical_processors > 0 && info.logical_processors <= 4;
    info.low_end = small_ram || few_cores;
    return info;
}

}  // namespace win
}  // namespace azy
