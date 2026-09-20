#include "azy/win32/capture/window_capture.hpp"

#include <inspectable.h>

#include <algorithm>

#include "azy/core/log.hpp"
#include "azy/win32/capture/wgc_abi.hpp"

namespace azy {
namespace win {
namespace {

constexpr unsigned kMaxPacedFps = 60;
constexpr unsigned long long kMaxConsecutiveFailures = 240;

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

template <typename T>
void release(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

}  // namespace

const char* capture_start_text(CaptureStart value) {
    switch (value) {
        case CaptureStart::Started: return "started";
        case CaptureStart::Unsupported: return "unsupported";
        case CaptureStart::Disabled: return "disabled";
        case CaptureStart::AlreadyRunning: return "already running";
        case CaptureStart::WrongWindow: return "window is not owned by the expected process";
        case CaptureStart::Minimized: return "window is minimized";
        case CaptureStart::DeviceFailed: return "D3D11 device unavailable";
        case CaptureStart::ItemFailed: return "capture item could not be created";
        case CaptureStart::PoolFailed: return "frame pool could not be created";
        case CaptureStart::SessionFailed: return "capture session could not be created";
    }
    return "unknown";
}

WindowCapture::~WindowCapture() { stop(); }

void WindowCapture::set_detail(const std::string& text) {
    std::lock_guard<std::mutex> lock(detail_mutex_);
    detail_ = text;
}

std::string WindowCapture::detail() const {
    std::lock_guard<std::mutex> lock(detail_mutex_);
    return detail_;
}

CaptureStart WindowCapture::start(HWND target, unsigned long expected_pid, D3dShared* shared, unsigned paced_fps,
                                  std::string* detail) {
    const auto fail = [&](CaptureStart value, const std::string& text) {
        set_detail(text);
        if (detail != nullptr) *detail = text;
        return value;
    };

    if (running_.load(std::memory_order_acquire)) return CaptureStart::AlreadyRunning;
    if (target == nullptr || !IsWindow(target)) return fail(CaptureStart::WrongWindow, "no window handle");
    if (shared == nullptr || !shared->ready()) return fail(CaptureStart::DeviceFailed, "no D3D11 device");

    unsigned long window_pid = 0;
    GetWindowThreadProcessId(target, &window_pid);
    if (window_pid == 0 || (expected_pid != 0 && window_pid != expected_pid)) {
        // A recycled handle would otherwise let Azy mirror an unrelated
        // application's window, which is both wrong and a privacy problem.
        return fail(CaptureStart::WrongWindow, "window belongs to pid " + std::to_string(window_pid) + ", expected " +
                                                   std::to_string(expected_pid));
    }
    if (IsIconic(target)) return fail(CaptureStart::Minimized, "window is minimized");
    // The recursion guard, in code rather than in a comment. Windows Graphics
    // Capture is a *window* capture, so the mirror can only ever contain the
    // target window's own pixels - but if Azy were ever asked to mirror one of its
    // own windows (the duplicate itself, say), the copy would nest and grow. Azy
    // never mirrors itself, and this is where that is enforced.
    if (window_pid == GetCurrentProcessId()) {
        return fail(CaptureStart::WrongWindow, "refusing to mirror Azy's own window");
    }

    const wgc::Activation& activation = wgc::activation();
    if (!activation.available) return fail(CaptureStart::Unsupported, activation.error);

    shared_ = shared;
    target_ = target;
    pid_ = window_pid;
    paced_fps_ = paced_fps == 0 ? 30 : paced_fps;
    paced_fps_live_.store(paced_fps_, std::memory_order_relaxed);
    burst_until_ms_.store(0, std::memory_order_relaxed);
    frames_.store(0, std::memory_order_relaxed);
    copies_.store(0, std::memory_order_relaxed);
    empty_polls_.store(0, std::memory_order_relaxed);
    failures_.store(0, std::memory_order_relaxed);
    last_frame_ms_.store(now_ms(), std::memory_order_relaxed);
    cursor_disabled_.store(false, std::memory_order_relaxed);
    border_state_.store(0, std::memory_order_relaxed);
    item_closed_.store(false, std::memory_order_release);
    set_detail("starting");

    if (stop_event_ == nullptr) stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) return fail(CaptureStart::DeviceFailed, "event could not be created");
    ResetEvent(stop_event_);

    running_.store(true, std::memory_order_release);
    try {
        worker_ = std::thread(&WindowCapture::thread_main, this);
    } catch (const std::exception& error) {
        running_.store(false, std::memory_order_release);
        return fail(CaptureStart::DeviceFailed, std::string("worker thread could not start: ") + error.what());
    }

    if (detail != nullptr) *detail = "started";
    return CaptureStart::Started;
}

void WindowCapture::stop() {
    if (stop_event_ != nullptr) SetEvent(stop_event_);
    if (worker_.joinable()) worker_.join();
    running_.store(false, std::memory_order_release);
    release_all();
    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
    shared_ = nullptr;
    target_ = nullptr;
}

void WindowCapture::release_all() {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (pooled_ != nullptr) {
        pooled_->Release();
        pooled_ = nullptr;
    }
    pooled_width_ = 0;
    pooled_height_ = 0;
    fresh_ = false;
}

void WindowCapture::set_paced_fps(unsigned fps) {
    paced_fps_live_.store(fps == 0 ? 30 : std::min(fps, kMaxPacedFps), std::memory_order_relaxed);
}

void WindowCapture::request_burst(double seconds) {
    if (seconds <= 0.0) return;
    burst_until_ms_.store(now_ms() + static_cast<long long>(seconds * 1000.0), std::memory_order_relaxed);
}

void WindowCapture::source_size(int* width, int* height) const {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (width != nullptr) *width = content_width_;
    if (height != nullptr) *height = content_height_;
}

bool WindowCapture::read_into(ID3D11DeviceContext* context, ID3D11Texture2D* destination) {
    if (context == nullptr || destination == nullptr) return false;

    std::lock_guard<std::mutex> frame_lock(frame_mutex_);
    if (!fresh_ || pooled_ == nullptr || shared_ == nullptr) return false;

    D3D11_TEXTURE2D_DESC dest_desc{};
    destination->GetDesc(&dest_desc);
    if (static_cast<int>(dest_desc.Width) < content_width_ || static_cast<int>(dest_desc.Height) < content_height_) {
        // The caller's staging texture has not caught up with the capture yet.
        // Copying a cropped region would break the caller's UV mapping silently.
        return false;
    }

    std::lock_guard<std::mutex> context_lock(shared_->context_mutex);
    context->CopyResource(destination, pooled_);
    fresh_ = false;
    copies_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

CaptureStatus WindowCapture::status() const {
    CaptureStatus out;
    // How old the newest frame is *now*: the age that matters when the user looks at
    // the debug screen.
    const unsigned long long newest_ms = last_frame_ms_.load(std::memory_order_relaxed);
    const unsigned long long now = static_cast<unsigned long long>(now_ms());
    out.frame_age_ms = newest_ms == 0 ? 0 : (now > newest_ms ? now - newest_ms : 0);
    out.running = running_.load(std::memory_order_acquire);
    out.item_closed = item_closed_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        out.content_width = content_width_;
        out.content_height = content_height_;
    }
    out.paced_fps = paced_fps_live_.load(std::memory_order_relaxed);
    out.frames = frames_.load(std::memory_order_relaxed);
    out.copies = copies_.load(std::memory_order_relaxed);
    out.empty_polls = empty_polls_.load(std::memory_order_relaxed);
    out.failures = failures_.load(std::memory_order_relaxed);
    out.pool_resizes = pool_resizes_.load(std::memory_order_relaxed);
    const long long last = last_frame_ms_.load(std::memory_order_relaxed);
    out.seconds_since_frame = last == 0 ? -1.0 : static_cast<double>(now_ms() - last) / 1000.0;
    out.cursor_disabled = cursor_disabled_.load(std::memory_order_relaxed);
    out.border_state = border_state_.load(std::memory_order_relaxed);
    out.detail = detail();
    return out;
}

void WindowCapture::thread_main() {
    using namespace wgc;

    const wgc::Activation& api = wgc::activation();
    if (!wgc::capture_available()) {
        set_detail(api.error);
        item_closed_.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return;
    }

    // WinRT on this thread only. The free-threaded frame pool removes the need for
    // a DispatcherQueue, but activation still needs the apartment initialised.
    const bool ro_ready = SUCCEEDED(api.ro_initialize(wgc::RoInitType::MultiThreaded));
    if (!ro_ready) log_warn("overlay capture: WinRT could not be initialised on the capture thread");

    IDXGIDevice* dxgi_device = nullptr;
    IInspectable* winrt_device = nullptr;
    HSTRING item_class = nullptr;
    HSTRING pool_class = nullptr;
    IGraphicsCaptureItemInterop* interop = nullptr;
    IGraphicsCaptureItem* item = nullptr;
    IDirect3D11CaptureFramePoolStatics2* pool_statics = nullptr;
    IDirect3D11CaptureFramePool* pool = nullptr;
    IGraphicsCaptureSession* session = nullptr;
    CaptureStart failure = CaptureStart::Started;
    std::string problem;

    do {
        if (FAILED(shared_->device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgi_device)))) {
            failure = CaptureStart::DeviceFailed;
            problem = "device is not a DXGI device";
            break;
        }
        if (FAILED(api.create_direct3d_device(dxgi_device, &winrt_device)) || winrt_device == nullptr) {
            failure = CaptureStart::DeviceFailed;
            problem = "device could not be wrapped for WinRT";
            break;
        }
        if (FAILED(api.create_hstring(kGraphicsCaptureItemClass, 0, &item_class))) {
            failure = CaptureStart::ItemFailed;
            problem = "class name could not be created";
            break;
        }
        if (FAILED(api.get_activation_factory(item_class, Guids::capture_item_interop(),
                                                     reinterpret_cast<void**>(&interop))) ||
            interop == nullptr) {
            failure = CaptureStart::ItemFailed;
            problem = "capture item factory is unavailable";
            break;
        }
        if (FAILED(interop->CreateForWindow(target_, Guids::capture_item(), reinterpret_cast<void**>(&item))) ||
            item == nullptr) {
            failure = CaptureStart::ItemFailed;
            problem = "the OS refused to capture this window";
            break;
        }

        SizeInt32 size{};
        if (FAILED(item->get_Size(&size)) || size.width <= 0 || size.height <= 0) {
            failure = CaptureStart::ItemFailed;
            problem = "capture size is not usable";
            break;
        }

        if (FAILED(api.create_hstring(kDirect3D11CaptureFramePoolClass, 0, &pool_class))) {
            failure = CaptureStart::PoolFailed;
            problem = "pool class name could not be created";
            break;
        }
        if (FAILED(api.get_activation_factory(pool_class, Guids::frame_pool_statics2(),
                                                     reinterpret_cast<void**>(&pool_statics))) ||
            pool_statics == nullptr) {
            failure = CaptureStart::PoolFailed;
            problem = "the OS has no free-threaded frame pool (Windows 10 1809 or newer is required)";
            break;
        }
        if (FAILED(pool_statics->CreateFreeThreaded(winrt_device, kPixelFormatB8G8R8A8UIntNormalized, 2, size,
                                                    &pool)) ||
            pool == nullptr) {
            failure = CaptureStart::PoolFailed;
            problem = "frame pool could not be created";
            break;
        }
        if (FAILED(pool->CreateCaptureSession(item, &session)) || session == nullptr) {
            failure = CaptureStart::SessionFailed;
            problem = "capture session could not be created";
            break;
        }

        // The cursor is drawn by the compositor, not by the captured window, so
        // capture would add a second cursor on top of the real one.
        IGraphicsCaptureSession2* session2 = nullptr;
        if (SUCCEEDED(session->QueryInterface(Guids::session2(), reinterpret_cast<void**>(&session2))) &&
            session2 != nullptr) {
            if (SUCCEEDED(session2->put_IsCursorCaptureEnabled(0))) {
                cursor_disabled_.store(true, std::memory_order_relaxed);
            }
            session2->Release();
        }

        // The yellow capture border is off where the OS allows it. Windows 10
        // without the newer interface returns E_NOINTERFACE here; that is expected
        // and never fatal - the capture simply carries the border.
        IGraphicsCaptureSession3* session3 = nullptr;
        if (SUCCEEDED(session->QueryInterface(Guids::session3(), reinterpret_cast<void**>(&session3))) &&
            session3 != nullptr) {
            const HRESULT hr = session3->put_IsBorderRequired(0);
            border_state_.store(SUCCEEDED(hr) ? 1 : 2, std::memory_order_relaxed);
            session3->Release();
        } else {
            border_state_.store(2, std::memory_order_relaxed);
        }

        if (FAILED(session->StartCapture())) {
            failure = CaptureStart::SessionFailed;
            problem = "capture could not be started";
            break;
        }

        set_detail("capturing " + std::to_string(size.width) + "x" + std::to_string(size.height));
        log_info("overlay capture: started %dx%d, paced at %u fps%s", size.width, size.height, paced_fps_,
                 cursor_disabled_.load(std::memory_order_relaxed) ? ", cursor capture disabled" : "");

        SizeInt32 pool_size = size;
        unsigned long long consecutive_failures = 0;
        while (running_.load(std::memory_order_acquire) && !item_closed_.load(std::memory_order_acquire)) {
            unsigned fps = paced_fps_live_.load(std::memory_order_relaxed);
            if (now_ms() < burst_until_ms_.load(std::memory_order_relaxed)) fps = kMaxPacedFps;
            // Never a zero wait: an unpaced poll loop would be a busy loop, and a
            // busy loop is the one thing this design must not contain. The floor is
            // the burst rate.
            const DWORD wait_ms = std::max<DWORD>(1, 1000 / (fps == 0 ? kMaxPacedFps : fps));
            if (WaitForSingleObject(stop_event_, wait_ms) != WAIT_TIMEOUT) break;

            IDirect3D11CaptureFrame* frame = nullptr;
            const HRESULT hr = pool->TryGetNextFrame(&frame);
            if (FAILED(hr)) {
                ++consecutive_failures;
                failures_.fetch_add(1, std::memory_order_relaxed);
                if (consecutive_failures >= kMaxConsecutiveFailures) {
                    set_detail("capture stopped after repeated failures");
                    item_closed_.store(true, std::memory_order_release);
                    break;
                }
                continue;
            }
            if (frame == nullptr) {
                empty_polls_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            IInspectable* surface = nullptr;
            IDirect3DDxgiInterfaceAccess* access = nullptr;
            ID3D11Texture2D* texture = nullptr;
            SizeInt32 content_size{};
            bool content_known = false;
            bool pool_needs_recreate = false;
            bool copied = false;
            do {
                if (FAILED(frame->get_Surface(&surface)) || surface == nullptr) break;
                if (FAILED(surface->QueryInterface(Guids::dxgi_interface_access(),
                                                   reinterpret_cast<void**>(&access))) ||
                    access == nullptr) {
                    break;
                }
                if (FAILED(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) ||
                    texture == nullptr) {
                    break;
                }

                SizeInt32 content{};
                if (FAILED(frame->get_ContentSize(&content)) || content.width <= 0 || content.height <= 0) break;
                content_size = content;
                content_known = true;

                // The frame pool is sized once, at the size the window had when
                // the capture started. After a resize it has to be told, or it
                // keeps handing out frames at the old size (which reads as a
                // stretched or cropped mirror). The resize happens *after* this
                // frame has been copied and released, so a pool rebuild can never
                // invalidate the texture being read.
                pool_needs_recreate = content.width != pool_size.width || content.height != pool_size.height;

                D3D11_TEXTURE2D_DESC source_desc{};
                texture->GetDesc(&source_desc);
                if (source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
                    // The pool was created for this format, so a different one
                    // means the OS changed the pipeline under us: stop rather
                    // than copy mismatched bytes.
                    problem = "capture format is not BGRA8";
                    failure = CaptureStart::ItemFailed;
                    break;
                }

                std::lock_guard<std::mutex> frame_lock(frame_mutex_);
                if (pooled_ == nullptr || pooled_width_ != static_cast<int>(source_desc.Width) ||
                    pooled_height_ != static_cast<int>(source_desc.Height)) {
                    if (pooled_ != nullptr) {
                        pooled_->Release();
                        pooled_ = nullptr;
                    }
                    D3D11_TEXTURE2D_DESC desc = source_desc;
                    desc.Usage = D3D11_USAGE_DEFAULT;
                    desc.SampleDesc.Count = 1;
                    desc.SampleDesc.Quality = 0;
                    desc.BindFlags = 0;
                    desc.CPUAccessFlags = 0;
                    desc.MiscFlags = 0;
                    if (SUCCEEDED(shared_->device->CreateTexture2D(&desc, nullptr, &pooled_)) && pooled_ != nullptr) {
                        pooled_width_ = static_cast<int>(desc.Width);
                        pooled_height_ = static_cast<int>(desc.Height);
                    }
                }
                if (pooled_ == nullptr) break;

                {
                    std::lock_guard<std::mutex> context_lock(shared_->context_mutex);
                    const D3D11_BOX box{0, 0, 0, static_cast<UINT>(content.width), static_cast<UINT>(content.height), 1};
                    if (source_desc.SampleDesc.Count > 1) {
                        // Multisampled capture surfaces cannot be copied with a
                        // box; resolving is the correct path for them.
                        shared_->context->ResolveSubresource(pooled_, 0, texture, 0, source_desc.Format);
                    } else {
                        shared_->context->CopySubresourceRegion(pooled_, 0, 0, 0, 0, texture, 0, &box);
                    }
                }
                content_width_ = static_cast<int>(content.width);
                content_height_ = static_cast<int>(content.height);
                fresh_ = true;
                copied = true;
            } while (false);

            release(surface);
            release(access);
            release(texture);
            release(frame);

            // Only now that this frame is released is it safe to rebuild the pool:
            // a pool resize while one of its textures is still being read is the
            // one ordering that could hand out a half-updated surface.
            if (pool_needs_recreate && content_known) {
                if (SUCCEEDED(pool->Recreate(winrt_device, kPixelFormatB8G8R8A8UIntNormalized, 2, content_size))) {
                    pool_size = content_size;
                    pool_resizes_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (copied) {
                consecutive_failures = 0;
                frames_.fetch_add(1, std::memory_order_relaxed);
                last_frame_ms_.store(now_ms(), std::memory_order_relaxed);
            } else {
                ++consecutive_failures;
                failures_.fetch_add(1, std::memory_order_relaxed);
                if (consecutive_failures >= kMaxConsecutiveFailures) {
                    if (problem.empty()) {
                        problem = "frames could not be read back";
                        failure = CaptureStart::ItemFailed;
                    }
                    break;
                }
            }
        }
    } while (false);

    release(session);
    release(pool);
    release(pool_statics);
    release(item);
    release(interop);
    if (item_class != nullptr) api.delete_hstring(item_class);
    if (pool_class != nullptr) api.delete_hstring(pool_class);
    release(winrt_device);
    release(dxgi_device);
    if (ro_ready) api.ro_uninitialize();

    if (failure != CaptureStart::Started) {
        set_detail(std::string(capture_start_text(failure)) + ": " + problem);
        log_warn("overlay capture: %s - %s", capture_start_text(failure), problem.c_str());
    } else if (!problem.empty()) {
        set_detail(problem);
    } else if (running_.load(std::memory_order_acquire)) {
        set_detail("stopped");
    }
    running_.store(false, std::memory_order_release);
}

}  // namespace win
}  // namespace azy
