#include "azy/win32/gloss/gloss_overlay.hpp"

#include "azy/win32/os/win_util.hpp"

#include <d3dcompiler.h>

#include <cstring>

#include "azy/core/log.hpp"
#include "azy_gloss_shader.hpp"

namespace azy {
namespace win {
namespace {

constexpr wchar_t kOverlayClass[] = L"AzySkin.GlossOverlay";
constexpr UINT_PTR kRenderTimerId = 1;
// Pacing. Active means "frames keep arriving" (playback, scrubbing, a menu);
// idle means the window is sitting still, where the only cost left is the poll
// that notices the next change.
constexpr UINT kActiveFps = 30;
constexpr UINT kActiveFpsPerformance = 24;
constexpr UINT kIdleFps = 10;
constexpr UINT kIdleFpsPerformance = 5;
// How long a single fresh frame keeps the active pacing: long enough to cover the
// gap between two edits, short enough that a paused timeline drops back quickly.
constexpr long long kActiveHoldMs = 1500;

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

// Mirrors `cbuffer AzyParams` in resources/shaders/gloss.hlsl byte for byte. The
// HLSL side deliberately uses float4/float2/scalars only, so no member can be
// displaced by a packing rule, and tools/check-overlay-shader.py checks that the
// two lists still line up.
struct AzyParams {
    float size[2];              // 0
    float uv_min[2];            // 8
    float uv_max[2];            // 16
    float unused[2];            // 24 (keeps the scalars on their 16-byte boundary)
    float darkness;             // 32
    float veil;                 // 36
    float gloss;                // 40
    float border;               // 44
    float depth;                // 48
    float grain;                // 52
    float radius;               // 56
    float bezel;                // 60
    float dpi;                  // 64
    float lift;                 // 68
    float pad[2];               // 72 (aligns the next float4 to a register)
    float charcoal[4];          // 80
    float pass[kOverlayPassSlots][4];        // 96
    float panel[kOverlayPanelSlots][4];      // 160
    float active[kOverlayPanelSlots];        // 224
    float pass_active[kOverlayPassSlots];    // 240
    float accent[4];            // 256
};

static_assert(sizeof(AzyParams) == 272, "the constant buffer must match the HLSL layout");

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID source, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO* defines,
                                      ID3DInclude* include, LPCSTR entry, LPCSTR profile, UINT flags1, UINT flags2,
                                      ID3DBlob** code, ID3DBlob** errors);

// The software rasterizer path is refused on purpose: a skin that costs CPU would
// defeat the point of the whole feature. The capture path uses the same rule.
bool get_dxgi_factory(ID3D11Device* device, IDXGIFactory2** out, std::string* error) {
    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgi_device))) ||
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
    const HRESULT factory_hr = adapter->GetParent(IID_IDXGIFactory2, reinterpret_cast<void**>(out));
    adapter->Release();
    if (FAILED(factory_hr) || *out == nullptr) {
        if (error != nullptr) *error = "DXGI 1.2 factory is unavailable";
        return false;
    }
    return true;
}

template <typename T>
void release(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

}  // namespace

GlossOverlay::~GlossOverlay() { destroy(); }

// --------------------------------------------------------------------- window

LRESULT CALLBACK GlossOverlay::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<GlossOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) return self->handle_message(hwnd, message, wparam, lparam);
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT GlossOverlay::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_NCHITTEST:
            // The click-through guarantee at window level: every point of this
            // window belongs to what is underneath it. Nothing here ever asks the
            // system for a hit, a capture or a focus change.
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;  // DWM owns every pixel; there is nothing to erase
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
            // give it straight back to Premiere.
            // WS_EX_NOACTIVATE already keeps this window out of the focus chain;
            // if the system hands it the keyboard anyway, it goes straight back to
            // the application that owns the window being mirrored.
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

bool GlossOverlay::create(HINSTANCE instance, std::string* error) {
    if (created()) return true;
    instance_ = instance;

    std::string problem;
    if (!shared_.create(&problem)) {
        if (error != nullptr) *error = problem;
        return false;
    }

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = 0;
    cls.lpfnWndProc = &GlossOverlay::window_proc;
    cls.hInstance = instance;
    cls.hCursor = nullptr;
    cls.hbrBackground = nullptr;
    cls.lpszClassName = kOverlayClass;
    if (RegisterClassExW(&cls) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error != nullptr) *error = "overlay window class could not be registered";
        shared_.destroy();
        return false;
    }

    // Window styles, and why each one is there:
    //   WS_EX_NOREDIRECTIONBITMAP - there is no GDI surface behind this window, so
    //       the compositor's visual is the only content that exists. Nothing can
    //       ever be painted black behind the mirror, and no window-sized bitmap is
    //       allocated.
    //   WS_EX_LAYERED | WS_EX_TRANSPARENT - the click-through mechanism for a
    //       window over *another process*. `WM_NCHITTEST` answering HTTRANSPARENT
    //       only forwards a hit to windows of the same thread (documented
    //       behaviour), so it cannot be what carries a click from here into
    //       Premiere; the layered bit is what makes the system skip this window
    //       during hit testing, and the window procedure keeps answering
    //       HTTRANSPARENT as well, for the same-process case.
    //   WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW - never in the focus chain, never in
    //       alt-tab.
    // Deliberately *not* topmost: the duplicate sits directly above Premiere and
    // below every other application, so bringing another program forward covers it.
    const DWORD ex_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                           WS_EX_NOREDIRECTIONBITMAP;
    window_ = CreateWindowExW(ex_style, kOverlayClass, L"Azy Skin overlay", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                              instance, this);
    if (window_ == nullptr) {
        if (error != nullptr) *error = "overlay window could not be created";
        shared_.destroy();
        return false;
    }

    // A layered window needs its constant alpha stated. 255 is the value that
    // changes nothing: the compositor's own premultiplied alpha is what the skin is
    // made of, and a smaller constant would dim the whole mirror.
    if (SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA) == FALSE) {
        log_warn("overlay: the layered attribute was refused (click-through may not hold)");
    }

    if (!create_device_resources(&problem) || !compile_shader(&problem)) {
        if (error != nullptr) *error = problem;
        destroy();
        return false;
    }

    apply_style_constants();
    log_info("overlay: duplicate window ready (click-through, not topmost)");
    return true;
}

bool GlossOverlay::create_device_resources(std::string* error) {
    if (!get_dxgi_factory(shared_.device, &factory_, error)) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 16;
    desc.Height = 16;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // the format DWM composes in
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;  // the shader emits premultiplied
    desc.Flags = 0;

    HRESULT hr = factory_->CreateSwapChainForComposition(shared_.device, &desc, nullptr, &swap_chain_);
    if (FAILED(hr) || swap_chain_ == nullptr) {
        if (error != nullptr) {
            *error = "composition swap chain could not be created (0x" +
                     std::to_string(static_cast<unsigned long>(hr)) + ")";
        }
        return false;
    }

    ID3D11Texture2D* buffer = nullptr;
    hr = swap_chain_->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&buffer));
    if (FAILED(hr) || buffer == nullptr) {
        if (error != nullptr) *error = "swap chain buffer is unavailable";
        return false;
    }
    hr = shared_.device->CreateRenderTargetView(buffer, nullptr, &target_view_);
    buffer->Release();
    if (FAILED(hr) || target_view_ == nullptr) {
        if (error != nullptr) *error = "render target view could not be created";
        return false;
    }

    IDXGIDevice* dxgi_device = nullptr;
    if (FAILED(shared_.device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgi_device))) ||
        dxgi_device == nullptr) {
        if (error != nullptr) *error = "device is not a DXGI device";
        return false;
    }
    hr = DCompositionCreateDevice(dxgi_device, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&composition_));
    dxgi_device->Release();
    if (FAILED(hr) || composition_ == nullptr) {
        if (error != nullptr) *error = "DirectComposition is unavailable on this host";
        return false;
    }
    if (FAILED(composition_->CreateTargetForHwnd(window_, TRUE, &composition_target_)) || composition_target_ == nullptr) {
        if (error != nullptr) *error = "composition target could not be created";
        return false;
    }
    if (FAILED(composition_->CreateVisual(&visual_)) || visual_ == nullptr) {
        if (error != nullptr) *error = "composition visual could not be created";
        return false;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;  // never wrap into the far side
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(shared_.device->CreateSamplerState(&sampler_desc, &sampler_)) || sampler_ == nullptr) {
        if (error != nullptr) *error = "sampler could not be created";
        return false;
    }

    D3D11_BUFFER_DESC buffer_desc{};
    buffer_desc.ByteWidth = sizeof(AzyParams);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(shared_.device->CreateBuffer(&buffer_desc, nullptr, &constants_)) || constants_ == nullptr) {
        if (error != nullptr) *error = "constant buffer could not be created";
        return false;
    }

    // Full-screen triangle: no vertex buffer, no input layout, one draw call.
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;  // the triangle's winding is not worth a bug
    raster.DepthClipEnable = TRUE;
    ID3D11RasterizerState* state = nullptr;
    if (SUCCEEDED(shared_.device->CreateRasterizerState(&raster, &state)) && state != nullptr) {
        // Kept for the lifetime of the object: stored on the context for every
        // draw, so a state leak is not possible.
        shared_.context->RSSetState(state);
        state->Release();
    }

    if (FAILED(visual_->SetContent(swap_chain_)) || FAILED(composition_target_->SetRoot(visual_))) {
        if (error != nullptr) *error = "composition content could not be attached";
        return false;
    }
    visual_->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR);
    composition_->Commit();
    return true;
}

bool GlossOverlay::compile_shader(std::string* error) {
    const wchar_t* candidates[] = {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll"};
    D3DCompileFn compile = nullptr;
    for (const wchar_t* name : candidates) {
        compiler_ = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (compiler_ == nullptr) compiler_ = LoadLibraryW(name);
        if (compiler_ == nullptr) continue;
        compile = reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler_, "D3DCompile"));
        if (compile != nullptr) break;
        FreeLibrary(compiler_);
        compiler_ = nullptr;
    }
    if (compile == nullptr) {
        if (error != nullptr) {
            *error = "no shader compiler is available (d3dcompiler_47.dll) - the duplicate window cannot be drawn";
        }
        return false;
    }

    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    // 5.0 first, 4.0 as the compatibility profile: the shader is written to
    // compile under both, and the older compiler DLLs only offer ps_4_0.
    struct Pair {
        const char* vs;
        const char* ps;
    };
    const Pair profiles[] = {{"vs_5_0", "ps_5_0"}, {"vs_4_0", "ps_4_0"}};

    ID3DBlob* code = nullptr;
    ID3DBlob* messages = nullptr;
    HRESULT hr = E_FAIL;
    for (const Pair& profile : profiles) {
        code = nullptr;
        messages = nullptr;
        hr = compile(kGlossShaderSource, std::strlen(kGlossShaderSource), "azy_gloss.hlsl", nullptr, nullptr,
                     "vs_main", profile.vs, flags, 0, &code, &messages);
        if (SUCCEEDED(hr) && code != nullptr) {
            hr = shared_.device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                   &vertex_shader_);
        }
        release(code);
        release(messages);
        if (FAILED(hr) || vertex_shader_ == nullptr) {
            release(vertex_shader_);
            continue;
        }

        code = nullptr;
        messages = nullptr;
        hr = compile(kGlossShaderSource, std::strlen(kGlossShaderSource), "azy_gloss.hlsl", nullptr, nullptr,
                     "ps_main", profile.ps, flags, 0, &code, &messages);
        if (SUCCEEDED(hr) && code != nullptr) {
            hr = shared_.device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                  &pixel_shader_);
        }
        release(code);
        release(messages);
        if (SUCCEEDED(hr) && pixel_shader_ != nullptr) {
            log_info("overlay: composition shader compiled for %s", profile.ps);
            return true;
        }
        release(pixel_shader_);
    }

    if (error != nullptr) *error = "the composition shader could not be compiled";
    return false;
}

// Note the ownership contract: the *capture* is not owned here (the engine owns its
// lifetime), but the D3D11 device the capture was handed is - so a caller must stop
// the capture before destroying this object, which is exactly what the engine's
// teardown path does.
void GlossOverlay::destroy() {
    if (window_ != nullptr) {
        KillTimer(window_, kRenderTimerId);
        timer_ms_ = 0;
        DestroyWindow(window_);
        window_ = nullptr;
    }
    visible_ = false;
    device_lost_ = false;
    release(stage_view_);
    release(stage_);
    stage_width_ = 0;
    stage_height_ = 0;
    release(target_view_);
    release(swap_chain_);
    release(factory_);
    release(visual_);
    release(composition_target_);
    release(composition_);
    release(pixel_shader_);
    release(vertex_shader_);
    release(constants_);
    release(sampler_);
    if (compiler_ != nullptr) {
        FreeLibrary(compiler_);
        compiler_ = nullptr;
    }
    shared_.destroy();
    has_frame_ = false;
    dirty_ = true;
    stats_ = Stats{};
}

// ------------------------------------------------------------------- geometry

bool GlossFrame::operator==(const GlossFrame& other) const {
    if (overlay != other.overlay || captured != other.captured || client_origin != other.client_origin) return false;
    if (dpi != other.dpi || insert_after != other.insert_after || anchor != other.anchor ||
        size_agrees != other.size_agrees) {
        return false;
    }
    if (panels.size() != other.panels.size()) return false;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        if (panels[i].id != other.panels[i].id || panels[i].usable != other.panels[i].usable) return false;
        if (panels[i].rect != other.panels[i].rect) return false;
    }
    return true;
}

bool GlossOverlay::set_frame(const GlossFrame& frame) {
    if (has_frame_ && frame_ == frame) return uv_.valid;

    frame_ = frame;
    has_frame_ = true;
    ++stats_.geometry_updates;

    stats_.width = frame.overlay.width();
    stats_.height = frame.overlay.height();
    uv_ = map_overlay_to_capture(frame.overlay, frame.captured);
    stats_.uv[0] = uv_.u0;
    stats_.uv[1] = uv_.v0;
    stats_.uv[2] = uv_.u1;
    stats_.uv[3] = uv_.v1;
    stats_.capture_size_agrees = frame.size_agrees;

    if (!uv_.valid) {
        // Nothing sensible to show: the overlay and the capture do not describe
        // the same region any more. Better no image than a stretched one.
        stats_.note = "capture and window geometry disagree";
        panel_lines_.clear();
        pass_regions_.clear();
        return false;
    }

    pass_regions_ = monitor_pass_through(frame.panels, frame.overlay, frame.client_origin);
    panel_lines_ = panel_hairlines(frame.panels, frame.overlay, frame.client_origin, pass_regions_,
                                   kOverlayPanelSlots);
    stats_.pass_regions = static_cast<int>(pass_regions_.size());
    stats_.panel_lines = static_cast<int>(panel_lines_.size());

    dirty_ = true;
    // A move or resize is the moment the mirror must not lag: burst for a moment.
    request_burst(1.2);
    return update_window();
}

void GlossOverlay::clear_frame() {
    has_frame_ = false;
    dirty_ = false;
    uv_ = UvRect{};
    pass_regions_.clear();
    panel_lines_.clear();
}

bool GlossOverlay::update_window() {
    if (window_ == nullptr || !has_frame_) return false;

    if (frame_.overlay.empty()) return false;

    // hWndInsertAfter is the window the overlay goes *under*, so the caller passes
    // the window that is currently in front of Premiere; passing Premiere itself
    // puts the duplicate directly above it, which is where it belongs.
    // hWndInsertAfter is the window this one is placed *under*, so passing the
    // window in front of Premiere puts the duplicate directly above Premiere. A
    // null hint means "leave the order alone": HWND_TOP would be a silent
    // always-on-top, which the brief forbids.
    const HWND insert_after =
        frame_.insert_after != nullptr ? frame_.insert_after : win::z_order_anchor(frame_.anchor);
    if (!SetWindowPos(window_, insert_after, frame_.overlay.left, frame_.overlay.top, frame_.overlay.width(),
                      frame_.overlay.height(),
                      SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS)) {
        stats_.note = "window placement failed";
        return false;
    }
    stats_.window_x = frame_.overlay.left;
    stats_.window_y = frame_.overlay.top;
    stats_.window_w = frame_.overlay.width();
    stats_.window_h = frame_.overlay.height();
    if (!resize_swap_chain(frame_.overlay.width(), frame_.overlay.height(), &stats_.note)) return false;
    return true;
}

bool GlossOverlay::resize_swap_chain(int width, int height, std::string* error) {
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
        if (error != nullptr) *error = "swap chain resize failed";
        return false;
    }
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(swap_chain_->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&buffer))) || buffer == nullptr) {
        if (error != nullptr) *error = "swap chain buffer is unavailable after resize";
        return false;
    }
    const HRESULT view_hr = shared_.device->CreateRenderTargetView(buffer, nullptr, &target_view_);
    buffer->Release();
    if (FAILED(view_hr) || target_view_ == nullptr) {
        if (error != nullptr) *error = "render target view could not be recreated";
        return false;
    }
    if (composition_ != nullptr) composition_->Commit();
    return true;
}

bool GlossOverlay::ensure_stage_texture(int width, int height, std::string* error) {
    if (stage_ != nullptr && stage_width_ == width && stage_height_ == height) return true;
    if (shared_.device == nullptr) return false;

    release(stage_view_);
    release(stage_);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(shared_.device->CreateTexture2D(&desc, nullptr, &stage_)) || stage_ == nullptr) {
        if (error != nullptr) *error = "staging texture could not be created";
        return false;
    }
    if (FAILED(shared_.device->CreateShaderResourceView(stage_, nullptr, &stage_view_)) || stage_view_ == nullptr) {
        release(stage_);
        if (error != nullptr) *error = "staging view could not be created";
        return false;
    }
    stage_width_ = width;
    stage_height_ = height;
    return true;
}

// -------------------------------------------------------------------- drawing

void GlossOverlay::set_style(const OverlayStyle& style) {
    if (style.visible == style_.visible && style.darkening == style_.darkening && style.veil == style_.veil &&
        style.gloss == style_.gloss && style.border == style_.border && style.depth == style_.depth &&
        style.grain == style_.grain && style.radius_dip == style_.radius_dip && style.bezel == style_.bezel &&
        style.lift == style_.lift && style.accent_strength == style_.accent_strength &&
        style.accent[0] == style_.accent[0] && style.accent[1] == style_.accent[1] &&
        style.accent[2] == style_.accent[2] && style.charcoal[0] == style_.charcoal[0] &&
        style.charcoal[1] == style_.charcoal[1] && style.charcoal[2] == style_.charcoal[2]) {
        return;
    }
    style_ = style;
    dirty_ = true;
    apply_style_constants();
}

void GlossOverlay::apply_style_constants() {
    if (constants_ == nullptr || shared_.context == nullptr) return;

    // One upload per settings change; the per-frame upload fills the geometry in.
    // The context is shared with the capture thread, so it is taken under the
    // same lock every other user takes.
    std::lock_guard<std::mutex> lock(shared_.context_mutex);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(shared_.context->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    auto* params = static_cast<AzyParams*>(mapped.pData);
    std::memset(params, 0, sizeof(AzyParams));
    params->darkness = style_.darkening;
    params->veil = style_.veil;
    params->gloss = style_.gloss;
    params->border = style_.border;
    params->depth = style_.depth;
    params->grain = style_.grain;
    params->bezel = style_.bezel;
    params->lift = style_.lift;
    params->charcoal[0] = style_.charcoal[0];
    params->charcoal[1] = style_.charcoal[1];
    params->charcoal[2] = style_.charcoal[2];
    params->accent[0] = style_.accent[0];
    params->accent[1] = style_.accent[1];
    params->accent[2] = style_.accent[2];
    params->accent[3] = style_.accent_strength;
    shared_.context->Unmap(constants_, 0);
}

bool GlossOverlay::pump() {
    if (!visible_ || !has_frame_ || window_ == nullptr) return false;
    // A window with nothing to show (fully covered, or the geometry is not
    // trustworthy yet) is not worth a capture round trip.
    stats_.paced_fps = paced_fps_;
    if (!uv_.valid) {
        stats_.note = "no valid mapping";
        return false;
    }

    const long long now = now_ms();
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
            fresh = true;
            ++stats_.capture_copies;
            last_fresh_ms_ = now;
        }
    }

    // Pacing: frames arriving means Premiere is doing something (playback, a
    // menu, a scrub), so the mirror follows at full rate for a moment; when they
    // stop, the timer drops to the slow rate where each tick costs one flag read.
    const bool active = now - last_fresh_ms_ < kActiveHoldMs || now < burst_until_ms_;
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
        stats_.note = "no capture to draw yet";
        return false;
    }

    // Constants: geometry changes every time, style only when it changed.
    {
        std::lock_guard<std::mutex> lock(shared_.context_mutex);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(shared_.context->Map(constants_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            stats_.note = "constants could not be mapped";
            return false;
        }
        auto* params = static_cast<AzyParams*>(mapped.pData);
        std::memset(params, 0, sizeof(AzyParams));
        params->size[0] = static_cast<float>(stats_.width);
        params->size[1] = static_cast<float>(stats_.height);
        params->uv_min[0] = uv_.u0;
        params->uv_min[1] = uv_.v0;
        params->uv_max[0] = uv_.u1;
        params->uv_max[1] = uv_.v1;
        params->darkness = style_.darkening;
        params->veil = style_.veil;
        params->gloss = style_.gloss;
        params->border = style_.border;
        params->depth = style_.depth;
        params->grain = style_.grain;
        params->bezel = style_.bezel;
        params->lift = style_.lift;
        params->dpi = static_cast<float>(frame_.dpi == 0 ? 96 : frame_.dpi) / 96.0f;
        params->radius = dip_to_px(style_.radius_dip, static_cast<int>(frame_.dpi));
        params->charcoal[0] = style_.charcoal[0];
        params->charcoal[1] = style_.charcoal[1];
        params->charcoal[2] = style_.charcoal[2];
        params->accent[0] = style_.accent[0];
        params->accent[1] = style_.accent[1];
        params->accent[2] = style_.accent[2];
        params->accent[3] = style_.accent_strength;
        pack_rects(pass_regions_, &params->pass[0][0], kOverlayPassSlots);
        pack_rects(panel_lines_, &params->panel[0][0], kOverlayPanelSlots);
        for (int i = 0; i < kOverlayPanelSlots; ++i) params->active[i] = i < static_cast<int>(panel_lines_.size()) ? 1.0f : 0.0f;
        for (int i = 0; i < kOverlayPassSlots; ++i) params->pass_active[i] = i < static_cast<int>(pass_regions_.size()) ? 1.0f : 0.0f;
        shared_.context->Unmap(constants_, 0);

        ID3D11RenderTargetView* target = target_view_;
        shared_.context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(stats_.width), static_cast<float>(stats_.height), 0.0f, 1.0f};
        shared_.context->RSSetViewports(1, &viewport);
        shared_.context->IASetInputLayout(nullptr);
        shared_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        shared_.context->VSSetShader(vertex_shader_, nullptr, 0);
        shared_.context->PSSetShader(pixel_shader_, nullptr, 0);
        shared_.context->PSSetShaderResources(0, 1, &stage_view_);
        shared_.context->PSSetSamplers(0, 1, &sampler_);
        shared_.context->PSSetConstantBuffers(0, 1, &constants_);
        shared_.context->Draw(3, 0);
        // Unbind the source: the capture thread writes into that texture.
        ID3D11ShaderResourceView* none = nullptr;
        shared_.context->PSSetShaderResources(0, 1, &none);
    }

    const HRESULT present_hr = swap_chain_->Present(0, 0);
    if (FAILED(present_hr)) {
        if (present_hr == DXGI_ERROR_DEVICE_REMOVED || present_hr == DXGI_ERROR_DEVICE_RESET) {
            // The device is gone: everything built on it (swap chain, shaders,
            // textures) is unusable and must be recreated from scratch.
            stats_.note = "the GPU device was lost";
            device_lost_ = true;
            hide();
            return false;
        }
        stats_.note = "present failed";
        return false;
    }

    dirty_ = false;
    ++stats_.presents;
    stats_.last_present_ms = static_cast<double>(GetTickCount64());
    stats_.note = fresh ? "presented captured frame" : "presented (geometry change)";
    return true;
}

void GlossOverlay::set_timer_interval(UINT milliseconds) {
    if (window_ == nullptr) return;
    if (milliseconds < 8) milliseconds = 8;
    if (timer_ms_ == milliseconds) return;
    timer_ms_ = milliseconds;
    // One timer, re-armed with a different period. While the overlay is hidden
    // there is no timer at all, so a stopped Azy costs nothing here.
    SetTimer(window_, kRenderTimerId, milliseconds, nullptr);
    stats_.paced_fps = milliseconds == 0 ? 0 : 1000 / milliseconds;
}

void GlossOverlay::request_burst(double seconds) {
    if (seconds <= 0.0) return;
    burst_until_ms_ = now_ms() + static_cast<long long>(seconds * 1000.0);
    if (capture_ != nullptr) capture_->request_burst(seconds);
    if (capture_ != nullptr) capture_->set_paced_fps(performance_mode_ ? kActiveFpsPerformance : kActiveFps);
    paced_fps_ = performance_mode_ ? kActiveFpsPerformance : kActiveFps;
    set_timer_interval(1000 / paced_fps_);
}

void GlossOverlay::set_performance_mode(bool performance_mode) {
    if (performance_mode_ == performance_mode) return;
    performance_mode_ = performance_mode;
    last_fresh_ms_ = 0;
    burst_until_ms_ = 0;
    paced_fps_ = 0;  // forces the next pump to re-arm the timer
}

bool GlossOverlay::show() {
    if (window_ == nullptr) return false;
    if (!update_window()) return false;
    if (!visible_) {
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        visible_ = true;
    }
    stats_.visible = true;
    stats_.paced_fps = paced_fps_;
    set_timer_interval(1000 / (paced_fps_ == 0 ? kIdleFps : paced_fps_));
    return true;
}

void GlossOverlay::hide() {
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
}

}  // namespace win
}  // namespace azy
