// Azy Skin — Win32 layer: MirrorRenderer.
//
// The mirror window: one click-through, never-activating, non-topmost window placed
// exactly over the Premiere window, into which the live capture is drawn through the
// composition shader (resources/shaders/mirror.hlsl).
//
// This class owns everything that has to be created and destroyed with the visual
// layer: the DirectComposition visual, the flip-model swap chain, the staging
// texture the capture is copied into, the shaders and the pacing timer. It owns no
// policy: when it is shown, what theme it is given and whether the capture should run
// are decided by the composition manager above it (src/win32/skin/skin_engine.cpp).
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// The Direct3D, DXGI and DirectComposition interfaces the renderer holds. Included
// here rather than left to the .cpp: this header is included by the engine, and a
// header that only compiles when something else included <d3d11.h> first is a trap
// (it is also what the include-hygiene check exists to catch).
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>

#include "azy/core/capture_math.hpp"
#include "azy/core/mirror_style.hpp"
#include "azy/win32/capture/d3d_shared.hpp"
#include "azy/win32/capture/window_capture.hpp"
#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// The mirror window's own layout model, in physical pixels.
struct MirrorRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool empty() const { return width <= 0 || height <= 0; }
    bool operator==(const MirrorRect& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

// Everything the renderer needs to know about where it goes and what it draws.
struct MirrorFrame {
    HWND anchor = nullptr;      // the Premiere window being mirrored
    HWND insert_after = nullptr;  // z-order hint: the window in front of the anchor
    MirrorRect overlay;         // where the mirror window goes, in screen pixels
    Rect captured;              // the rectangle the capture contains (the window frame)
    Rect client_origin;         // where the client area starts, for panel coordinates
    UINT dpi = 96;
    std::vector<PanelRect> panels;  // the layout model, for panel frames and media holes
    bool size_agrees = true;    // capture size vs window size (reported, never hidden)

    bool operator==(const MirrorFrame& other) const {
        return anchor == other.anchor && overlay == other.overlay && captured == other.captured &&
               client_origin == other.client_origin && dpi == other.dpi && panels.size() == other.panels.size() &&
               panels == other.panels && size_agrees == other.size_agrees;
    }
};

// The constant buffer, mirrored member for member in resources/shaders/mirror.hlsl.
// Only float2/float4/scalars appear: a float3 would let the compiler insert padding
// the CPU side cannot see. tools/check-mirror.py compares the two lists and the
// packing arithmetic, and fails the build when they drift.
struct MirrorParams {
    float size[2];
    float uv_min[2];
    float uv_max[2];
    float unused[2];
    float base_dark;
    float surface;
    float transition;
    float boundary_soft;
    float mid_tone;
    float mid_boost;
    float density;
    float radius;
    float shadow;
    float shadow_soft;
    float clarity;
    float clarity_offset;
    float highlight;
    float gloss;
    float glass;
    float glow;
    float grain;
    float accent_mix;
    float highlight_band;
    float vig;
    float key_light;
    float dpi;
    float content_keep;
    float pad0;  // Explicit padding. HLSL packs the scalar block up to its next 16-byte
    float pad1;  // boundary; a plain float array in C++ does not, so the pads make the two
    float pad2;  // layouts identical instead of merely compatible. A named member is also
    float pad3;  // one the contract check can compare (tools/check-mirror.py).
    float pad4;
    float background[4];
    float surface_colour[4];
    float border_colour[4];
    float accent[4];
    float glow_colour[4];
    float pass[kMirrorPassSlots][4];
    float panel[kMirrorPanelSlots][4];
    // Slot flags as vectors, not scalars: the shader declares them as arrays of
    // float4, and a vector array is the one shape whose packing is unambiguous.
    float active[kMirrorPanelSlots / 4][4];
    float pass_active[kMirrorPassSlots / 4][4];
};

// 8 floats (four float2) + 28 scalars + 20 (five colour vectors) + 16 (pass) + 32
// (panel) + 8 (active) + 4 (pass_active) = 116 floats = 464 bytes. Every block is
// 16-byte aligned, which is the property that makes this a constant buffer layout
// rather than a guess; tools/check-mirror.py recomputes it from the HLSL declaration.
static_assert(sizeof(MirrorParams) == 464, "MirrorParams must match the HLSL packing rules");

class MirrorRenderer {
public:
    MirrorRenderer() = default;
    ~MirrorRenderer();

    MirrorRenderer(const MirrorRenderer&) = delete;
    MirrorRenderer& operator=(const MirrorRenderer&) = delete;

    struct Stats {
        bool visible = false;
        int width = 0;   // swap chain size == the mirror window's client size
        int height = 0;
        int window_x = 0;  // where the mirror actually is, in physical pixels
        int window_y = 0;
        int window_w = 0;
        int window_h = 0;
        float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        int media_regions = 0;
        int panel_rects = 0;
        unsigned long long presents = 0;
        unsigned long long idle_pumps = 0;
        unsigned long long geometry_updates = 0;
        unsigned long long capture_copies = 0;
        double last_present_ms = 0.0;
        bool capture_size_agrees = true;
        unsigned paced_fps = 0;
        bool active = false;
        float fade = 1.0f;  // the animation envelope currently applied (spec §31)
        std::string note;   // always set: what the last pump did
    };

    // Creates the window and every GPU resource. `error` receives the reason when it
    // fails, so the caller can log something a user can act on.
    bool create(HINSTANCE instance, std::string* error);
    void destroy();
    bool created() const { return window_ != nullptr; }

    // The capture is drawn with this device; the renderer never creates its own, so
    // one device serves the capture, the staging texture and the swap chain.
    D3dShared* shared_device() { return &shared_; }

    // The capture is *not* owned here: the composition manager starts and stops it,
    // and tells the renderer where to find it. A renderer with no capture draws the
    // last frame it copied (or nothing at all).
    void attach_capture(WindowCapture* capture) { capture_ = capture; }

    void set_style(const MirrorStyle& style);
    bool style_visible() const { return style_.visible; }

    // Animations (spec §31): when on, the skin eases in when it appears and
    // cross-fades when the theme changes. Off, everything is instant.
    void set_animations(bool enabled);
    void restart_fade();  // called on show and on a theme change

    bool set_frame(const MirrorFrame& frame);
    bool show();
    void hide();
    bool visible() const { return visible_; }

    // One composition tick: takes the freshest captured frame (if any), updates the
    // constants, draws and presents. Returns true when something was presented.
    bool pump();

    // Temporarily tighter pacing (a move or resize is happening).
    void request_burst(double seconds);

    // True after the GPU device was lost (driver reset, hybrid-GPU switch, a
    // remote-session change). The owner must destroy and recreate the renderer:
    // nothing can be drawn on a removed device.
    bool needs_recreate() const { return device_lost_; }

    const Stats& stats() const { return stats_; }
    HWND window() const { return window_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    bool create_device_resources(std::string* error);
    bool compile_shaders(std::string* error);
    bool ensure_stage_texture(int width, int height, std::string* error);
    bool resize_swap_chain(int width, int height, std::string* error);
    bool update_window();
    void fill_params();
    void refresh_regions();
    void set_timer_interval(UINT milliseconds);

    D3dShared shared_;
    WindowCapture* capture_ = nullptr;  // not owned

    HWND window_ = nullptr;
    bool visible_ = false;
    bool device_lost_ = false;
    bool compiled_ = false;

    IDXGISwapChain1* swap_chain_ = nullptr;
    ID3D11RenderTargetView* target_view_ = nullptr;
    ID3D11Texture2D* stage_ = nullptr;
    ID3D11ShaderResourceView* stage_view_ = nullptr;
    ID3D11VertexShader* vertex_shader_ = nullptr;
    ID3D11PixelShader* pixel_shader_ = nullptr;
    ID3D11SamplerState* point_sampler_ = nullptr;
    ID3D11SamplerState* mip_sampler_ = nullptr;
    ID3D11Buffer* constants_ = nullptr;
    IDCompositionDevice* composition_ = nullptr;
    IDCompositionTarget* composition_target_ = nullptr;
    IDCompositionVisual* visual_ = nullptr;

    int stage_width_ = 0;
    int stage_height_ = 0;

    MirrorStyle style_;
    bool performance_mode_ = false;
    bool animations_ = false;
    MirrorFrame frame_;
    bool has_frame_ = false;
    MirrorParams params_{};

    // Media regions and panel rectangles, in window-local pixels (packed on the way
    // into `params_`, and kept here so the diagnostics can count them).
    std::vector<LocalRect> media_regions_;
    std::vector<LocalRect> panel_regions_;

    // Pacing.
    UINT timer_ms_ = 0;
    unsigned paced_fps_ = 0;
    unsigned long long last_fresh_ms_ = 0;
    long long burst_until_ms_ = 0;
    bool dirty_ = true;

    // Animation (spec §31).
    float fade_ = 1.0f;
    float fade_from_ = 1.0f;
    unsigned long long fade_start_ms_ = 0;
    unsigned long long fade_ms_ = 420;

    Stats stats_;
};

}  // namespace win
}  // namespace azy
