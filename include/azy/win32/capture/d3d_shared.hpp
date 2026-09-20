// Azy Skin — Win32 layer: the D3D11 device shared by the capture and the overlay.
//
// Capture and presentation must live on the *same* D3D11 device: a resource from
// one device cannot be copied to a resource on another, and GPU sharing across
// devices would mean extra sync and an extra copy. One device, one immediate
// context, guarded by one mutex, is the cheapest correct arrangement:
//
//   * the capture worker copies each arrived frame into a texture Azy owns,
//   * the render thread copies that texture into the DirectComposition swap chain.
//
// Both directions are sub-millisecond for a window-sized surface and neither runs
// while the other is blocked on a frame.
#pragma once

#include <mutex>
#include <string>

#include <d3d11.h>

namespace azy {
namespace win {

struct D3dShared {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    // Guards `context` only. The device itself is free-threaded.
    std::mutex context_mutex;

    bool ready() const { return device != nullptr && context != nullptr; }

    // Hardware device with BGRA support (the desktop composition format), feature
    // level 11.1 down to 11.0. No WARP fallback on purpose: a software rasterizer
    // would burn CPU to show a skin, which is exactly what this feature must not
    // do. Failure here means the callers fall back to the static treatment.
    bool create(std::string* error);
    void destroy();
};

}  // namespace win
}  // namespace azy
