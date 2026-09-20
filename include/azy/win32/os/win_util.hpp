// Azy Skin — Win32 layer: small utilities (paths, files, strings, handles).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "azy/core/geometry.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// --- RAII handles ---------------------------------------------------------

class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h) : handle_(h) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HANDLE get() const { return handle_; }
    HANDLE* addr() { return &handle_; }
    explicit operator bool() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    void reset(HANDLE h = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = h;
    }
    HANDLE release() {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    HANDLE handle_ = nullptr;
};

// --- Paths ----------------------------------------------------------------

std::filesystem::path executable_path();
std::filesystem::path executable_dir();
std::filesystem::path local_app_data_dir();   // %LOCALAPPDATA%\Azy Skin (created on demand)
std::filesystem::path settings_path();        // ...\settings.ini
std::filesystem::path log_path();             // ...\azy.log

// --- Files ----------------------------------------------------------------

bool read_text_file(const std::filesystem::path& path, std::string& out);
bool write_text_file_atomic(const std::filesystem::path& path, const std::string& contents);

// --- Strings --------------------------------------------------------------

std::string to_utf8(const std::wstring& s);
std::wstring to_wide(const std::string& s);
std::wstring last_error_text(DWORD error = 0);
std::wstring hresult_text(HRESULT hr);
bool iequals_wide(const std::wstring& a, const std::wstring& b);

std::wstring window_class_name(HWND hwnd);
std::wstring window_text(HWND hwnd);

// --- Windows --------------------------------------------------------------

// The visible frame rectangle (DWM extended frame bounds when available,
// GetWindowRect otherwise). This is the rectangle the user actually sees, and
// therefore the one Azy's composition surface must match.
bool visible_frame_rect(HWND hwnd, RECT& out);
Rect to_rect(const RECT& r);
RECT to_native(const Rect& r);

bool is_window_minimized(HWND hwnd);
bool is_window_maximized(HWND hwnd);
bool is_window_cloaked(HWND hwnd);  // DWM cloak check: hides "ghost" UWP-style windows

// Current Windows "app theme" (Settings > Personalization > Colors > dark mode).
bool system_uses_dark_apps();

// Monotonic and wall-clock seconds.
double monotonic_seconds();
double wall_seconds();

// --- Cursor ---------------------------------------------------------------

// True when the cursor is inside the rectangle (used to suppress skins while
// the user is dragging/scrubbing exactly on the frame edge).
bool cursor_in_rect(const RECT& rect);

// Rough machine class, read once with two cheap system calls (no WMI, no
// registry walk). Used to pick sensible first-run defaults, never to change a
// choice the user already made.
struct MachineClass {
    unsigned long long physical_memory_bytes = 0;
    unsigned logical_processors = 0;
    bool low_end = false;  // ~small RAM and/or few cores
};

MachineClass detect_machine_class();

}  // namespace win
}  // namespace azy
