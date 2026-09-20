// Azy Skin — Win32 layer: the duplicate window.
//
// This is the window the whole round-8 architecture is about. It sits directly
// above Premiere, has no input of its own (mouse passes through it, the keyboard
// never leaves Premiere), and shows Azy's skinned copy of what the capture sees.
// The real Premiere window underneath stays the application: it keeps the focus,
// the caret, the menus, the timeline, the playback and every plugin.
//
// It is deliberately *not* topmost and *not* a screen-covering sheet: it is an
// ordinary window, sized and positioned to Premiere's visible frame, re-inserted
// above Premiere only when it has to be (see reassert_stacking in the engine).
//
// The drawing itself lives in resources/shaders/gloss.hlsl; this class only owns
// the window, the swap chain, the constants and the pacing.
#pragma once

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>

#include <string>
#include <vector>

#include "azy/core/capture_math.hpp"
#include "azy/core/overlay_style.hpp"
#include "azy/win32/capture/d3d_shared.hpp"
#include "azy/win32/capture/window_capture.hpp"

namespace azy {
namespace win {

// Region slots handed to the shader. These mirror PASS_SLOTS / PANEL_SLOTS in
// resources/shaders/gloss.hlsl (tools/check-overlay.py fails the build if
// the two ever disagree, because the mismatch would show up only as wrong pixels
// on someone else's machine).
constexpr int kOverlayPassSlots = 4;
constexpr int kOverlayPanelSlots = 4;

// Where the duplicate goes and what it is allowed to show. Everything is in
// physical pixels on the virtual desktop.
struct GlossFrame {
    Rect overlay;        // the duplicate window's rectangle (DWM extended frame bounds)
    Rect captured;       // what the capture contains (the tracked window's frame)
    Rect client_origin;  // screen position of the tracked window's client area
    UINT dpi = 96;
    std::vector<PanelRect> panels;  // layout model, client-relative pixels
    HWND insert_after = nullptr;    // the window this one goes *under*, i.e. the one in front of Premiere
    HWND anchor = nullptr;          // the tracked Premiere window (focus returns here)
    // False while the capture's own size disagrees with `captured`: the mirror
    // would be scaled wrongly, which is worth knowing about rather than hiding.
    bool size_agrees = true;

    bool operator==(const GlossFrame& other) const;
    bool operator!=(const GlossFrame& other) const { return !(*this == other); }
};

class GlossOverlay {
public:
    ~GlossOverlay();

    // Creates the D3D11 device, the composition target and the hidden window.
    // Idempotent; on failure everything is released again and `error` explains it.
    bool create(HINSTANCE instance, std::string* error);
    void destroy();
    bool created() const { return window_ != nullptr; }
    HWND window() const { return window_; }

    // The D3D11 device the capture has to use: capture and presentation must be
    // the same device, and this is the one the composition target is attached to.
    D3dShared* shared_device() { return shared_.ready() ? &shared_ : nullptr; }

    void set_style(const OverlayStyle& style);
    const OverlayStyle& style() const { return style_; }
    bool style_visible() const { return style_.visible && !overlay_style_is_passthrough(style_); }

    // Stores the geometry for the next present. Returns false when the frame
    // cannot produce a correct image (overlay and capture do not intersect).
    bool set_frame(const GlossFrame& frame);
    void clear_frame();

    // The capture to mirror. The overlay never starts or stops it; the engine
    // owns its lifetime. Passing nullptr detaches without stopping it.
    void set_capture(WindowCapture* capture) { capture_ = capture; }

    // Draws at most one frame, and only if there is something new to draw or the
    // geometry/style changed. Returns true when a frame was presented.
    bool pump();

    bool show();
    void hide();
    bool visible() const { return visible_; }

    // Temporarily loosens or tightens pacing (a move/resize is about to happen).
    void request_burst(double seconds);
    void set_performance_mode(bool performance_mode);

    struct Stats {
        bool visible = false;
        int width = 0;
        int height = 0;
        float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        int pass_regions = 0;
        int panel_lines = 0;
        unsigned long long presents = 0;
        unsigned long long idle_pumps = 0;
        unsigned long long geometry_updates = 0;
        unsigned long long capture_copies = 0;
        double last_present_ms = 0.0;
        bool capture_size_agrees = true;
        unsigned paced_fps = 0;
        bool active = false;
        std::string note;  // always set: what the last pump did
    };
    const Stats& stats() const { return stats_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    bool create_device_resources(std::string* error);
    bool compile_shader(std::string* error);
    bool ensure_stage_texture(int width, int height, std::string* error);
    bool resize_swap_chain(int width, int height, std::string* error);
    bool update_window();
    void apply_style_constants();
    void set_timer_interval(UINT milliseconds);
    void note_activity();

    D3dShared shared_;
    IDXGIFactory2* factory_ = nullptr;
    IDXGISwapChain1* swap_chain_ = nullptr;
    ID3D11RenderTargetView* target_view_ = nullptr;
    IDCompositionDevice* composition_ = nullptr;
    IDCompositionTarget* composition_target_ = nullptr;
    IDCompositionVisual* visual_ = nullptr;
    ID3D11VertexShader* vertex_shader_ = nullptr;
    ID3D11PixelShader* pixel_shader_ = nullptr;
    ID3D11Buffer* constants_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11Texture2D* stage_ = nullptr;
    ID3D11ShaderResourceView* stage_view_ = nullptr;
    int stage_width_ = 0;
    int stage_height_ = 0;
    HMODULE compiler_ = nullptr;

    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    GlossFrame frame_;
    bool has_frame_ = false;
    UvRect uv_;
    std::vector<LocalRect> pass_regions_;
    std::vector<LocalRect> panel_lines_;
    OverlayStyle style_;
    WindowCapture* capture_ = nullptr;

    bool visible_ = false;
    bool dirty_ = true;
    bool performance_mode_ = false;
    UINT timer_ms_ = 0;
    unsigned paced_fps_ = 10;
    long long last_fresh_ms_ = 0;
    long long burst_until_ms_ = 0;
    Stats stats_;
};

}  // namespace win
}  // namespace azy
