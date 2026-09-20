#include "azy/win32/os/file_watcher.hpp"
#include <filesystem>
#include <string>
#include <system_error>

#include "azy/core/log.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {

bool DirectoryWatcher::start(const std::filesystem::path& file, HWND notify_window, UINT message,
                             std::string* error) {
    stop();
    if (notify_window == nullptr) return false;

    std::filesystem::path directory = file.parent_path();
    if (directory.empty()) directory = std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    // FILE_NOTIFY_CHANGE_LAST_WRITE covers an in-place edit; the file-name
    // notification covers an editor that writes a temp file and renames it.
    handle_ = FindFirstChangeNotificationW(directory.c_str(), FALSE,
                                          FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    if (handle_ == INVALID_HANDLE_VALUE || handle_ == nullptr) {
        handle_ = nullptr;
        if (error) *error = "FindFirstChangeNotification failed: " + to_utf8(last_error_text());
        return false;
    }

    window_ = notify_window;
    message_ = message;
    if (!RegisterWaitForSingleObject(&wait_, handle_, &DirectoryWatcher::wait_callback, this, INFINITE,
                                     WT_EXECUTEDEFAULT)) {
        if (error) *error = "RegisterWaitForSingleObject failed: " + to_utf8(last_error_text());
        FindCloseChangeNotification(handle_);
        handle_ = nullptr;
        return false;
    }
    return true;
}

void DirectoryWatcher::stop() {
    if (wait_ != nullptr) {
        // UnregisterWaitEx with INVALID_HANDLE_VALUE waits for the callback to
        // finish, so `this` can never be touched after stop() returns.
        UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
        wait_ = nullptr;
    }
    if (handle_ != nullptr) {
        FindCloseChangeNotification(handle_);
        handle_ = nullptr;
    }
}

void CALLBACK DirectoryWatcher::wait_callback(void* context, BOOLEAN /*timed_out*/) {
    static_cast<DirectoryWatcher*>(context)->on_signalled();
}

void DirectoryWatcher::on_signalled() {
    // Runs on a thread-pool thread: post and return, nothing else.
    if (window_ != nullptr && message_ != 0) {
        PostMessageW(window_, message_, 0, 0);
    }
    if (handle_ != nullptr) {
        // Re-arm for the next change.
        FindNextChangeNotification(handle_);
    }
}

}  // namespace win
}  // namespace azy
