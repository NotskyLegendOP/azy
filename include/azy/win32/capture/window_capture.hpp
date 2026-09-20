// Azy Skin — Win32 layer: a GPU capture of one window.
//
// This is the "look at Premiere" half of the duplicate-window architecture. It
// hands back frames as textures on the caller's D3D11 device; it never touches the
// CPU's copy of the pixels, never writes a file, and never asks the desktop for a
// screenshot.
//
// The capture runs on its own worker thread with the *free-threaded* frame pool
// (no DispatcherQueue, no message pump required) and is paced deliberately: frames
// arrive at the display's rate, which is far more often than a skin needs, so the
// worker takes at most one frame per paced interval and drops the rest. A drop
// costs nothing because the desktop is already composing the real Premiere window
// underneath - the mirror only has to look right, not be the source of truth.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <d3d11.h>

#include "azy/win32/capture/d3d_shared.hpp"

namespace azy {
namespace win {

enum class CaptureStart {
    Started,
    Unsupported,     // no WinRT/Windows.Graphics.Capture on this system
    Disabled,        // switched off by the user, or by the crash marker
    AlreadyRunning,
    WrongWindow,     // the handle is not a valid window owned by the expected process
    Minimized,       // start was asked for too early; the caller retries on restore
    DeviceFailed,
    ItemFailed,      // the OS refused to create a capture item for this window
    PoolFailed,
    SessionFailed,
};

const char* capture_start_text(CaptureStart value);

struct CaptureStatus {
    bool running = false;
    bool item_closed = false;   // the OS ended the capture (window closed/hidden)
    int content_width = 0;
    int content_height = 0;
    unsigned paced_fps = 0;
    unsigned long long frames = 0;
    unsigned long long copies = 0;      // frames actually handed to the renderer
    unsigned long long empty_polls = 0;
    unsigned long long failures = 0;
    unsigned long long pool_resizes = 0;   // window resizes the frame pool followed
    double seconds_since_frame = -1.0;
    bool cursor_disabled = false;       // false means a second cursor may appear
    int border_state = 0;               // 0 unknown, 1 hidden, 2 unsupported
    std::string detail;
};

// Owns the capture of one window. Not copyable, not movable: its worker thread
// points at it.
class WindowCapture {
public:
    WindowCapture() = default;
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    // Starts capturing `target` (which must belong to `expected_pid`). `shared`
    // must already hold a device and must outlive this object. `paced_fps` of 0
    // means "as fast as frames arrive", which is only used for measurements.
    CaptureStart start(HWND target, unsigned long expected_pid, D3dShared* shared, unsigned paced_fps,
                       std::string* detail);

    // Stops the worker and releases every GPU resource it owns. Idempotent, and
    // safe to call from the thread that owns the overlay window.
    void stop();

    bool running() const { return running_.load(std::memory_order_acquire); }
    bool item_closed() const { return item_closed_.load(std::memory_order_acquire); }
    HWND target() const { return target_; }

    // Size of the frame that would be handed out next, 0 until one has arrived.
    void source_size(int* width, int* height) const;

    // Copies the newest captured frame into `destination` (same device as
    // `shared`). Returns false when nothing new has arrived since the last call,
    // and also when `destination` is too small to hold the whole frame - a partial
    // copy would silently break the caller's coordinate mapping, so it is refused
    // and left for the next call.
    // `context` must be the shared immediate context; the caller holds no lock.
    bool read_into(ID3D11DeviceContext* context, ID3D11Texture2D* destination);

    // Pacing control: the overlay raises it while Premiere is playing or a menu is
    // open, and lowers it when nothing is happening.
    void set_paced_fps(unsigned fps);
    void request_burst(double seconds);

    CaptureStatus status() const;

private:
    void thread_main();
    void release_all();
    void set_detail(const std::string& text);
    std::string detail() const;

    D3dShared* shared_ = nullptr;
    HWND target_ = nullptr;
    unsigned long pid_ = 0;
    unsigned paced_fps_ = 30;

    std::thread worker_;
    HANDLE stop_event_ = nullptr;

    // GPU objects, owned by the worker thread but created and destroyed on the
    // caller's thread; they are only touched while the worker is not running.
    ID3D11Texture2D* pooled_ = nullptr;
    int pooled_width_ = 0;
    int pooled_height_ = 0;

    // Handed from the worker to the render thread. Mutable so that const
    // status() can read the last content size safely.
    mutable std::mutex frame_mutex_;
    bool fresh_ = false;
    int content_width_ = 0;
    int content_height_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> item_closed_{false};
    std::atomic<unsigned> paced_fps_live_{30};
    std::atomic<long long> burst_until_ms_{0};
    std::atomic<unsigned long long> frames_{0};
    std::atomic<unsigned long long> copies_{0};
    std::atomic<unsigned long long> empty_polls_{0};
    std::atomic<unsigned long long> failures_{0};
    std::atomic<unsigned long long> pool_resizes_{0};
    std::atomic<long long> last_frame_ms_{0};
    std::atomic<bool> cursor_disabled_{false};
    std::atomic<int> border_state_{0};
    mutable std::mutex detail_mutex_;
    std::string detail_;
};

}  // namespace win
}  // namespace azy
