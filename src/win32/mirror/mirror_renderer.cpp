#include "azy/win32/mirror/mirror_renderer.hpp"

#include <cstdio>

#include "azy/win32/os/win_util.hpp"

#include <d3dcompiler.h>

#include <cstring>

#include "azy/core/log.hpp"
#include "azy_mirror_shader.hpp"

namespace azy {
namespace win {
namespace {

constexpr wchar_t kMirrorClass[] = L"AzySkin.MirrorWindow";
constexpr UINT_PTR kRenderTimerId = 1;

// Pacing. "Active" means frames keep arriving - Premiere is playing, scrubbing or
// being used - and the mirror follows at the display's own rate. "Idle" means the
// window is sitting still, where each tick costs one flag read and nothing else.
//
// These are deliberately generous: this milestone is about the mirror being live and
// correct, not about the smallest possible number (spec §41).
constexpr UINT kActiveFps = 60;
constexpr UINT kActiveFpsPerformance = 30;
constexpr UINT kIdleFps = 15;
constexpr UINT kIdleFpsPerformance = 8;
// How long one fresh frame keeps the active pacing: long enough to cover the gap
// between two edits, short enough that a paused window drops back quickly.
constexpr long long kActiveHoldMs = 2000;

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

template <typename T>
void release(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

Rect from_mirror_rect(const MirrorRect& rect) {
    return Rect{rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

// The DXGI factory is reached through the device's adapter, exactly as the capture
// does it: no factory creation flags, no debug layer, nothing that could differ
// between the two halves of the pipeline.
bool get_dxgi_factory(ID3D11Device* device, IDXGIFactory2** out, std::string* error) {
    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device))) ||
        dxgi_device == nullptr) {
        if (error != nullptr) *error = "device is not a DXGI device";
        return false;
    }
    IDXGIAdapter* adapter = nullptr;
    const HRESULT hr = dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();
    if (FAILED(hr) || adapter == nullptr) {
        if (error != nullptr) *error = "adapter could not be resolved";
        return false;
    }
    const HRESULT factory_hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(out));
    adapter->Release();
    if (FAILED(factory_hr) || *out == nullptr) {
        if (error != nullptr) *error = "DXGI 1.2 factory is unavailable";
        return false;
    }
    return true;
}

// d3dcompiler_47.dll is loaded dynamically: nothing new is linked, and a machine
// without it reports "the shader could not be compiled" instead of failing to start.
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

struct ShaderCompiler {
    HMODULE module = nullptr;
    D3DCompileFn compile = nullptr;

    bool load(std::string* error) {
        if (compile != nullptr) return true;
        module = LoadLibraryW(L"d3dcompiler_47.dll");
        if (module == nullptr) {
            if (error != nullptr) *error = "d3dcompiler_47.dll is not available";
            return false;
        }
        compile = reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile"));
        if (compile == nullptr) {
            if (error != nullptr) *error = "D3DCompile is missing from d3dcompiler_47.dll";
            return false;
        }
        return true;
    }
};

ShaderCompiler& shader_compiler() {
    static ShaderCompiler compiler;
    return compiler;
}

std::string format_hresult(HRESULT hr) {
    char buffer[32] = {0};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(buffer);
}

}  // namespace

MirrorRenderer::~MirrorRenderer() { destroy(); }

// ------------------------------------------------------------------ the window

LRESULT CALLBACK MirrorRenderer::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<MirrorRenderer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->handle_message(hwnd, message, wparam, lparam);
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT MirrorRenderer::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    switch (message) {
        case WM_NCHITTEST:
            // The second half of the click-through guarantee: the layered bit makes
            // Windows skip this window across processes, and this answers the
            // same-process case.
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;  // the compositor owns every pixel; there is nothing to erase
        case WM_PAINT: {
            // A composition window still receives paint messages; validating them
            // without drawing keeps DWM from resending them forever.
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wparam == kRenderTimerId) {
                pump();
                return 0;
            }
            break;
        case WM_SETFOCUS:
            // Never take the keyboard: if the system ever hands this window focus,
            // it goes straight back to the window being mirrored.
            if (frame_.anchor != nullptr && IsWindow(frame_.anchor)) SetFocus(frame_.anchor);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kRenderTimerId);
            timer_ms_ = 0;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool MirrorRenderer::create(HINSTANCE instance, std::string* error) {
    if (window_ != nullptr) return true;

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &MirrorRenderer::window_proc;
    cls.hInstance = instance;
    cls.hCursor = nullptr;
    cls.hbrBackground = nullptr;
    cls.lpszClassName = kMirrorClass;
    if (RegisterClassExW(&cls) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error != nullptr) *error = "mirror window class could not be registered";
        return false;
    }

    // Window styles, and why each one is there:
    //   WS_EX_NOREDIRECTIONBITMAP - there is no GDI surface behind this window, so
    //       the compositor's visual is the only content that exists. Nothing can be
    //       painted black behind the mirror, and no window-sized bitmap is allocated.
    //   WS_EX_LAYERED | WS_EX_TRANSPARENT - the click-through mechanism for a window
    //       over *another process* (HTTRANSPARENT alone only forwards within a
    //       thread). Premiere keeps every click.
    //   WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW - never in the focus chain, never in
    //       alt-tab.
    // Deliberately *not* topmost: the mirror sits directly above Premiere and below
    // every other application, so bringing another program forward covers it.
    const DWORD ex_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                           WS_EX_NOREDIRECTIONBITMAP;
    window_ = CreateWindowExW(ex_style, kMirrorClass, L"Azy Skin mirror", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                              instance, this);
    if (window_ == nullptr) {
        if (error != nullptr) *error = "mirror window could not be created";
        return false;
    }

    // A layered window states its constant alpha. 255 changes nothing: the skin's
    // transparency comes from the compositor's premultiplied alpha, and a smaller
    // value would dim the entire mirror.
    if (SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA) == FALSE) {
        log_warn("mirror: the layered attribute was refused (click-through may not hold)");
    }

    std::string problem;
    if (!create_device_resources(&problem) || !compile_shaders(&problem)) {
        if (error != nullptr) *error = problem;
        destroy();
        return false;
    }

    log_info("mirror: window ready (click-through, never activating, not topmost)");
    return true;
}

void MirrorRenderer::destroy() {
    // Note the ownership contract: the *capture* is not owned here (the skin engine
    // owns its lifetime), but the D3D11 device is - so the caller must stop the capture
    // before destroying this object, which every teardown path in the engine does.
    if (window_ != nullptr) {
        KillTimer(window_, kRenderTimerId);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    timer_ms_ = 0;
    visible_ = false;
    device_lost_ = false;

    release(target_view_);
    release(stage_view_);
    release(stage_);
    stage_width_ = 0;
    stage_height_ = 0;
    release(visual_);
    release(composition_target_);
    release(composition_);
    release(swap_chain_);
    release(pixel_shader_);
    release(vertex_shader_);
    release(point_sampler_);
    release(mip_sampler_);
    release(constants_);
    shared_.destroy();

    compiled_ = false;
    stats_ = Stats{};
    has_frame_ = false;
    dirty_ = true;
}

// ------------------------------------------------------------ device and shaders

bool MirrorRenderer::create_device_resources(std::string* error) {
    if (!shared_.create(error)) return false;

    IDXGIFactory2* factory = nullptr;
    if (!get_dxgi_factory(shared_.device, &factory, error)) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 1;
    desc.Height = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // The shader emits premultiplied alpha; the compositor expects exactly that.
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    const HRESULT chain_hr = factory->CreateSwapChainForComposition(shared_.device, &desc, nullptr, &swap_chain_);
    factory->Release();
    if (FAILED(chain_hr) || swap_chain_ == nullptr) {
        if (error != nullptr) *error = "composition swap chain could not be created (" + format_hresult(chain_hr) + ")";
        return false;
    }

    if (FAILED(DCompositionCreateDevice(nullptr, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&composition_))) ||
        composition_ == nullptr) {
        if (error != nullptr) *error = "DirectComposition device could not be created";
        return false;
    }
    if (FAILED(composition_->CreateTargetForHwnd(window_, TRUE, &composition_target_)) ||
        composition_target_ == nullptr) {
        if (error != nullptr) *error = "DirectComposition target could not be created";
        return false;
    }
    if (FAILED(composition_->CreateVisual(&visual_)) || visual_ == nullptr) {
        if (error != nullptr) *error = "DirectComposition visual could not be created";
        return false;
    }
    if (FAILED(visual_->SetContent(swap_chain_))) {
        if (error != nullptr) *error = "the swap chain could not be attached to the visual";
        return false;
    }
    if (FAILED(composition_target_->SetRoot(visual_)) || FAILED(composition_->Commit())) {
        if (error != nullptr) *error = "the composition could not be committed";
        return false;
    }

    // Two samplers over the same texture: point sampling for the pixel-exact copy
    // (the mirror must not blur Premiere's UI) and linear mip sampling for the glass
    // diffusion, which wants exactly that blur.
    D3D11_SAMPLER_DESC point_desc{};
    point_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    point_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    point_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    point_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    point_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    point_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(shared_.device->CreateSamplerState(&point_desc, &point_sampler_))) {
        if (error != nullptr) *error = "the point sampler could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC mip_desc = point_desc;
    mip_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(shared_.device->CreateSamplerState(&mip_desc, &mip_sampler_))) {
        if (error != nullptr) *error = "the mip sampler could not be created";
        return false;
    }

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(MirrorParams);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(shared_.device->CreateBuffer(&buffer_desc, nullptr, &constants_))) {
        if (error != nullptr) *error = "the constant buffer could not be created";
        return false;
    }
    return true;
}

bool MirrorRenderer::compile_shaders(std::string* error) {
    std::string problem;
    if (!shader_compiler().load(&problem)) {
        if (error != nullptr) *error = problem;
        return false;
    }

    // Try Shader Model 5.0 first, then 4.0: the source uses nothing that needs 5.0,
    // and some older Windows 10 builds only ship the 4.0 path.
    const char* pixel_profiles[] = {"ps_5_0", "ps_4_0"};
    const char* vertex_profiles[] = {"vs_5_0", "vs_4_0"};
    ID3DBlob* pixel_code = nullptr;
    ID3DBlob* vertex_code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = E_FAIL;

    for (const char* profile : pixel_profiles) {
        hr = shader_compiler().compile(kMirrorShaderSource, std::strlen(kMirrorShaderSource), "mirror.hlsl", nullptr,
                                       nullptr, "ps_main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixel_code,
                                       &errors);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr) || pixel_code == nullptr) {
        const std::string message = errors != nullptr ? std::string(static_cast<const char*>(errors->GetBufferPointer()))
                                                      : std::string("no compiler message");
        if (error != nullptr) *error = "the mirror shader did not compile: " + message;
        release(errors);
        release(pixel_code);
        return false;
    }
    release(errors);

    for (const char* profile : vertex_profiles) {
        hr = shader_compiler().compile(kMirrorShaderSource, std::strlen(kMirrorShaderSource), "mirror.hlsl", nullptr,
                                       nullptr, "vs_main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertex_code,
                                       &errors);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr) || vertex_code == nullptr) {
        if (error != nullptr) *error = "the mirror vertex shader did not compile";
        release(errors);
        release(vertex_code);
        release(pixel_code);
        return false;
    }
    release(errors);

    const bool ok =
        SUCCEEDED(shared_.device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(),
                                                    nullptr, &pixel_shader_)) &&
        SUCCEEDED(shared_.device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(),
                                                     nullptr, &vertex_shader_));
    release(pixel_code);
    release(vertex_code);
    if (!ok) {
        if (error != nullptr) *error = "the mirror shaders could not be created";
        return false;
    }

    compiled_ = true;
    log_info("mirror: composition shader compiled");
    return true;
}

bool MirrorRenderer::ensure_stage_texture(int width, int height, std::string* error) {
    if (stage_ != nullptr && stage_width_ == width && stage_height_ == height) return true;
    if (shared_.device == nullptr) return false;

    release(stage_view_);
    release(stage_);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    // A full mip chain: the shader's glass diffusion samples a high mip level, which
    // is what makes the frosted backdrop cost one texture read (spec §14).
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(shared_.device->CreateTexture2D(&desc, nullptr, &stage_)) || stage_ == nullptr) {
        if (error != nullptr) *error = "the staging texture could not be created";
        return false;
    }
    if (FAILED(shared_.device->CreateShaderResourceView(stage_, nullptr, &stage_view_)) || stage_view_ == nullptr) {
        release(stage_);
        if (error != nullptr) *error = "the staging view could not be created";
        return false;
    }
    stage_width_ = width;
    stage_height_ = height;
    return true;
}

// -------------------------------------------------------------- frame and layout

bool MirrorRenderer::set_frame(const MirrorFrame& frame) {
    const bool changed = !has_frame_ || !(frame_ == frame);
    frame_ = frame;
    has_frame_ = true;
    refresh_regions();
    dirty_ = true;
    if (!changed) return false;
    if (window_ != nullptr && compiled_) update_window();
    return true;
}

void MirrorRenderer::refresh_regions() {
    if (!has_frame_) return;
    const Rect overlay = from_mirror_rect(frame_.overlay);
    const UvRect uv = map_overlay_to_capture(overlay, frame_.captured);
    params_.uv_min[0] = uv.u0;
    params_.uv_min[1] = uv.v0;
    params_.uv_max[0] = uv.u1;
    params_.uv_max[1] = uv.v1;
    stats_.uv[0] = uv.u0;
    stats_.uv[1] = uv.v0;
    stats_.uv[2] = uv.u1;
    stats_.uv[3] = uv.v1;

    media_regions_ = monitor_pass_through(frame_.panels, overlay, frame_.client_origin, frame_.dpi);
    panel_regions_ =
        panel_hairlines(frame_.panels, overlay, frame_.client_origin, media_regions_, kMirrorPanelSlots);
    stats_.media_regions = static_cast<int>(media_regions_.size());
    stats_.panel_rects = static_cast<int>(panel_regions_.size());
    stats_.capture_size_agrees = frame_.size_agrees;
}

bool MirrorRenderer::update_window() {
    if (window_ == nullptr || !has_frame_ || frame_.overlay.empty()) return false;

    // hWndInsertAfter is the window this one is placed *under*, so passing the window
    // in front of Premiere puts the mirror directly above Premiere. A null hint means
    // "leave the order alone": HWND_TOP would be a silent always-on-top, which the
    // brief forbids.
    const HWND insert_after =
        frame_.insert_after != nullptr ? frame_.insert_after : win::z_order_anchor(frame_.anchor);
    if (!SetWindowPos(window_, insert_after, frame_.overlay.x, frame_.overlay.y, frame_.overlay.width,
                      frame_.overlay.height, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS)) {
        stats_.note = "window placement failed";
        return false;
    }
    stats_.window_x = frame_.overlay.x;
    stats_.window_y = frame_.overlay.y;
    stats_.window_w = frame_.overlay.width;
    stats_.window_h = frame_.overlay.height;
    ++stats_.geometry_updates;
    return resize_swap_chain(frame_.overlay.width, frame_.overlay.height, &stats_.note);
}

bool MirrorRenderer::resize_swap_chain(int width, int height, std::string* error) {
    if (swap_chain_ == nullptr || shared_.device == nullptr) return false;
    if (width <= 0 || height <= 0) return false;

    DXGI_SWAP_CHAIN_DESC1 current{};
    if (SUCCEEDED(swap_chain_->GetDesc1(&current)) && static_cast<int>(current.Width) == width &&
        static_cast<int>(current.Height) == height && target_view_ != nullptr) {
        return true;
    }

    // Every reference to the back buffer has to be gone before ResizeBuffers.
    std::lock_guard<std::mutex> lock(shared_.context_mutex);
    shared_.context->OMSetRenderTargets(0, nullptr, nullptr);
    release(target_view_);
    const HRESULT hr = swap_chain_->ResizeBuffers(2, static_cast<UINT>(width), static_cast<UINT>(height),
                                                 DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        if (error != nullptr) *error = "swap chain resize failed (" + format_hresult(hr) + ")";
        return false;
    }
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer))) ||
        buffer == nullptr) {
        if (error != nullptr) *error = "the swap chain's back buffer is unavailable";
        return false;
    }
    const HRESULT view_hr = shared_.device->CreateRenderTargetView(buffer, nullptr, &target_view_);
    buffer->Release();
    if (FAILED(view_hr) || target_view_ == nullptr) {
        if (error != nullptr) *error = "the render target view could not be created";
        return false;
    }
    stats_.width = width;
    stats_.height = height;
    return true;
}

// ------------------------------------------------------------------ style, fade

void MirrorRenderer::set_style(const MirrorStyle& style) {
    // A colour change is a theme change: re-run the appearance transition so the
    // switch reads as a fade rather than a jump (spec §28, §31).
    const bool theme_changed = style.accent[0] != style_.accent[0] || style.accent[1] != style_.accent[1] ||
                               style.accent[2] != style_.accent[2] || style.background[0] != style_.background[0] ||
                               style.visible != style_.visible;
    style_ = style;
    dirty_ = true;
    if (theme_changed && visible_) restart_fade();
}

void MirrorRenderer::set_animations(bool enabled) {
    if (animations_ == enabled) return;
    animations_ = enabled;
    if (!enabled) {
        fade_ = 1.0f;
        dirty_ = true;
    }
}

void MirrorRenderer::restart_fade() {
    if (!animations_) {
        fade_ = 1.0f;
        return;
    }
    fade_from_ = fade_;
    fade_start_ms_ = static_cast<unsigned long long>(now_ms());
    fade_ = 0.0f;
    dirty_ = true;
}

// ------------------------------------------------------------------- visibility

bool MirrorRenderer::show() {
    if (window_ == nullptr || !compiled_) return false;
    if (!has_frame_ || frame_.overlay.empty()) return false;
    if (!update_window()) return false;
    if (!visible_) {
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        visible_ = true;
        restart_fade();
    }
    stats_.visible = true;
    set_timer_interval(1000 / (paced_fps_ == 0 ? kActiveFps : paced_fps_));
    return true;
}

void MirrorRenderer::hide() {
    if (window_ == nullptr) {
        visible_ = false;
        stats_.visible = false;
        return;
    }
    if (visible_) ShowWindow(window_, SW_HIDE);
    visible_ = false;
    stats_.visible = false;
    // No timer while hidden: this is what makes "Suspend Skin" free.
    KillTimer(window_, kRenderTimerId);
    timer_ms_ = 0;
    dirty_ = true;

    // Nothing is drawn while hidden, so the window-sized allocations go back to the
    // driver: the staging texture and the swap chain's buffers. The device and the
    // compiled shaders stay, because rebuilding those is what would make switching the
    // skin back on slow.
    if (shared_.context != nullptr) {
        std::lock_guard<std::mutex> lock(shared_.context_mutex);
        shared_.context->OMSetRenderTargets(0, nullptr, nullptr);
        release(target_view_);
        release(stage_view_);
        release(stage_);
        stage_width_ = 0;
        stage_height_ = 0;
        if (swap_chain_ != nullptr) swap_chain_->ResizeBuffers(2, 1, 1, DXGI_FORMAT_UNKNOWN, 0);
    }
}

void MirrorRenderer::request_burst(double seconds) {
    if (seconds <= 0.0) return;
    burst_until_ms_ = now_ms() + static_cast<long long>(seconds * 1000.0);
    dirty_ = true;
}

void MirrorRenderer::set_timer_interval(UINT milliseconds) {
    if (window_ == nullptr) return;
    if (milliseconds < 8) milliseconds = 8;
    if (timer_ms_ == milliseconds) return;
    timer_ms_ = milliseconds;
    // One timer, re-armed with a different period. While the mirror is hidden there
    // is no timer at all, so a stopped Azy costs nothing here.
    SetTimer(window_, kRenderTimerId, milliseconds, nullptr);
    stats_.paced_fps = milliseconds == 0 ? 0 : 1000 / milliseconds;
}

// ------------------------------------------------------------------- the frame

void MirrorRenderer::fill_params() {
    std::memset(&params_, 0, sizeof(params_));
    const float fade = fade_;
    params_.size[0] = static_cast<float>(stats_.width);
    params_.size[1] = static_cast<float>(stats_.height);
    params_.uv_min[0] = stats_.uv[0];
    params_.uv_min[1] = stats_.uv[1];
    params_.uv_max[0] = stats_.uv[2];
    params_.uv_max[1] = stats_.uv[3];

    // The fade scales the *strengths*, not the colours: at 0 the shader returns the
    // captured pixels untouched, so the skin appears by growing into the picture
    // instead of the window fading in. When animations are off, fade is always 1.
    const auto scaled = [fade](float value) { return value * fade; };
    params_.base_dark = scaled(style_.base_dark);
    params_.surface = scaled(style_.surface);
    params_.transition = style_.transition;
    params_.boundary_soft = style_.boundary_soft;
    params_.mid_tone = style_.mid_tone;
    params_.mid_boost = scaled(style_.mid_boost);
    params_.density = scaled(style_.density);
    params_.radius = style_.radius;
    params_.shadow = scaled(style_.shadow);
    params_.shadow_soft = style_.shadow_soft;
    params_.clarity = scaled(style_.clarity);
    params_.clarity_offset = style_.clarity_offset;
    params_.highlight = scaled(style_.highlight);
    params_.gloss = scaled(style_.gloss);
    params_.glass = scaled(style_.glass);
    params_.glow = scaled(style_.glow);
    params_.grain = scaled(style_.grain);
    params_.accent_mix = style_.accent_mix;
    params_.highlight_band = style_.highlight_band;
    params_.vig = scaled(style_.vig);
    params_.key_light = scaled(style_.key_light);
    params_.dpi = style_.dpi;
    params_.content_keep = scaled(style_.content_keep);

    for (int i = 0; i < 3; ++i) {
        params_.background[i] = style_.background[i];
        params_.surface_colour[i] = style_.surface_colour[i];
        params_.border_colour[i] = style_.border_colour[i];
        params_.accent[i] = style_.accent[i];
        params_.glow_colour[i] = style_.glow_colour[i];
    }
    params_.background[3] = 1.0f;
    params_.surface_colour[3] = 1.0f;
    params_.border_colour[3] = 1.0f;
    params_.accent[3] = 1.0f;
    params_.glow_colour[3] = 1.0f;

    pack_rects(media_regions_, &params_.pass[0][0], kMirrorPassSlots);
    pack_rects(panel_regions_, &params_.panel[0][0], kMirrorPanelSlots);
    for (int i = 0; i < kMirrorPanelSlots; ++i) {
        params_.active[i / 4][i % 4] = i < static_cast<int>(panel_regions_.size()) ? 1.0f : 0.0f;
    }
    for (int i = 0; i < kMirrorPassSlots; ++i) {
        params_.pass_active[i / 4][i % 4] = i < static_cast<int>(media_regions_.size()) ? 1.0f : 0.0f;
    }
}

bool MirrorRenderer::pump() {
    if (window_ == nullptr || !compiled_ || !visible_) return false;
    const unsigned long long now = static_cast<unsigned long long>(now_ms());

    // ---- the appearance transition (spec §31) --------------------------------
    if (animations_ && fade_ < 1.0f) {
        const unsigned long long elapsed = now > fade_start_ms_ ? now - fade_start_ms_ : 0;
        const float t = fade_ms_ == 0 ? 1.0f : static_cast<float>(elapsed) / static_cast<float>(fade_ms_);
        const float eased = 1.0f - (1.0f - (t > 1.0f ? 1.0f : t)) * (1.0f - (t > 1.0f ? 1.0f : t));
        fade_ = fade_from_ + (1.0f - fade_from_) * eased;
        if (t >= 1.0f) fade_ = 1.0f;
        dirty_ = true;
    }
    stats_.fade = fade_;

    // ---- the freshest captured frame -----------------------------------------
    bool fresh = false;
    if (capture_ != nullptr && capture_->running()) {
        int content_width = 0;
        int content_height = 0;
        capture_->source_size(&content_width, &content_height);
        if (content_width > 0 && content_height > 0 && !ensure_stage_texture(content_width, content_height, nullptr)) {
            stats_.note = "staging texture unavailable";
            return false;
        }
        if (stage_ != nullptr && capture_->read_into(shared_.context, stage_)) {
            // The glass diffusion reads a mip level, so the chain has to be
            // generated for this frame. One GPU pass over a window-sized texture,
            // no CPU involvement.
            {
                std::lock_guard<std::mutex> lock(shared_.context_mutex);
                shared_.context->GenerateMips(stage_view_);
            }
            fresh = true;
            ++stats_.capture_copies;
            last_fresh_ms_ = now;
        }
    }

    // ---- pacing --------------------------------------------------------------
    // Frames arriving means Premiere is doing something (playback, a menu, a scrub),
    // so the mirror follows at the active rate; when they stop, the timer drops to
    // the idle rate where each tick costs one flag read.
    const bool active = (now - last_fresh_ms_) < static_cast<unsigned long long>(kActiveHoldMs) ||
                        now < static_cast<unsigned long long>(burst_until_ms_) || fade_ < 1.0f;
    const unsigned want_fps = active ? (performance_mode_ ? kActiveFpsPerformance : kActiveFps)
                                     : (performance_mode_ ? kIdleFpsPerformance : kIdleFps);
    if (want_fps != paced_fps_) {
        paced_fps_ = want_fps;
        if (capture_ != nullptr) capture_->set_paced_fps(want_fps);
        set_timer_interval(1000 / want_fps);
    }
    stats_.active = active;

    if (!fresh && !dirty_) {
        ++stats_.idle_pumps;
        stats_.note = active ? "waiting for frames" : "idle";
        return false;
    }

    if (target_view_ == nullptr || stage_view_ == nullptr || pixel_shader_ == nullptr || vertex_shader_ == nullptr) {
        stats_.note = "nothing to draw yet";
        return false;
    }

    fill_params();

    {
        std::lock_guard<std::mutex> lock(shared_.context_mutex);
        ID3D11DeviceContext* context = shared_.context;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            stats_.note = "constants could not be mapped";
            return false;
        }
        std::memcpy(mapped.pData, &params_, sizeof(params_));
        context->Unmap(constants_, 0);

        ID3D11RenderTargetView* target = target_view_;
        context->OMSetRenderTargets(1, &target, nullptr);
        const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(stats_.width), static_cast<float>(stats_.height),
                                      0.0f, 1.0f};
        context->RSSetViewports(1, &viewport);

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex_shader_, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constants_);
        context->PSSetShader(pixel_shader_, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &constants_);
        // The same texture on two slots: t0 sampled point-wise for the exact copy,
        // t1 sampled from the mip chain for the glass diffusion.
        ID3D11ShaderResourceView* views[2] = {stage_view_, stage_view_};
        context->PSSetShaderResources(0, 2, views);
        ID3D11SamplerState* samplers[2] = {point_sampler_, mip_sampler_};
        context->PSSetSamplers(0, 2, samplers);

        context->Draw(3, 0);

        // Unbind before presenting: holding a reference to the back buffer across a
        // Present is what makes a later ResizeBuffers fail.
        ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
        context->PSSetShaderResources(0, 2, none);
        context->OMSetRenderTargets(0, nullptr, nullptr);
    }

    const HRESULT present_hr = swap_chain_->Present(1, 0);
    if (present_hr == DXGI_ERROR_DEVICE_REMOVED || present_hr == DXGI_ERROR_DEVICE_RESET) {
        // The device is gone: everything built on it is unusable and must be
        // recreated from scratch by the owner.
        stats_.note = "the GPU device was lost";
        device_lost_ = true;
        hide();
        return false;
    }
    if (FAILED(present_hr)) {
        stats_.note = "present failed (" + format_hresult(present_hr) + ")";
        return false;
    }

    dirty_ = false;
    ++stats_.presents;
    stats_.last_present_ms = static_cast<double>(GetTickCount64());
    stats_.note = fresh ? "presented captured frame" : "presented (style or geometry change)";
    return true;
}

}  // namespace win
}  // namespace azy
