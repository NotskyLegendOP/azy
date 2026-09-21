#include "azy/win32/capture/d3d_shared.hpp"

#include <vector>

#include "azy/core/log.hpp"

namespace azy {
namespace win {

bool D3dShared::create(std::string* error) {
    if (ready()) return true;

    const D3D_FEATURE_LEVEL wanted[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, wanted,
                                   static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION, &device, &got, &context);
    if (FAILED(hr)) {
        // 11.1 is rejected with E_INVALIDARG on Windows 7-era runtimes; retrying
        // without it is the documented workaround, not a second attempt at
        // something that already failed.
        const D3D_FEATURE_LEVEL fallback[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                             D3D_FEATURE_LEVEL_10_0};
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fallback,
                               static_cast<UINT>(std::size(fallback)), D3D11_SDK_VERSION, &device, &got, &context);
    }
    if (FAILED(hr) || device == nullptr || context == nullptr) {
        destroy();
        if (error != nullptr) {
            *error = "D3D11CreateDevice failed (0x" + std::to_string(static_cast<unsigned long>(hr)) + ")";
        }
        return false;
    }

    log_info("overlay: D3D11 device created (feature level 0x%x)", static_cast<unsigned>(got));
    return true;
}

void D3dShared::destroy() {
    std::lock_guard<std::mutex> lock(context_mutex);
    if (context != nullptr) {
        context->ClearState();
        context->Flush();
        context->Release();
        context = nullptr;
    }
    if (device != nullptr) {
        device->Release();
        device = nullptr;
    }
}

}  // namespace win
}  // namespace azy
