// Azy Skin — Win32 layer: external configuration change watcher.
//
// `settings.ini` is documented as hand-editable, so Azy has to notice when
// somebody edits it. Rather than polling the file, Azy asks Windows for a
// directory change notification and registers a thread-pool wait on it; the
// callback does nothing but post a message to Azy's own window.
#pragma once

#include <filesystem>
#include <string>

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

class DirectoryWatcher {
public:
    ~DirectoryWatcher() { stop(); }

    // Watches the directory containing `file` and posts `message` to
    // `notify_window` when it changes. Azy re-reads (and re-stats) the file only
    // when that message arrives.
    bool start(const std::filesystem::path& file, HWND notify_window, UINT message, std::string* error);
    void stop();
    bool active() const { return handle_ != nullptr; }

private:
    static void CALLBACK wait_callback(void* context, BOOLEAN timed_out);

    void on_signalled();

    HANDLE handle_ = nullptr;   // FindFirstChangeNotification handle
    HANDLE wait_ = nullptr;     // RegisterWaitForSingleObject handle
    HWND window_ = nullptr;
    UINT message_ = 0;
};

}  // namespace win
}  // namespace azy
