#include "azy/win32/capture/wgc_abi.hpp"

#include <mutex>

#include "azy/core/log.hpp"

namespace azy {
namespace win {
namespace wgc {
namespace {

const GUID kCaptureItemInterop = {0x3628E81B, 0x3CAC, 0x4C60, {0xB7, 0xF4, 0x23, 0xCE, 0x0E, 0x0C, 0x33, 0x56}};
const GUID kCaptureItem = {0x79C3F95B, 0x31F7, 0x4EC2, {0xA4, 0x64, 0x63, 0x2E, 0xF5, 0xD3, 0x07, 0x60}};
const GUID kFramePoolStatics2 = {0x589B103F, 0x6BBC, 0x5DF5, {0xA9, 0x91, 0x02, 0xE2, 0x8B, 0x3B, 0x66, 0xD5}};
const GUID kSession = {0x814E42A9, 0xF70F, 0x4AD7, {0x93, 0x9B, 0xFD, 0xDC, 0xC6, 0xEB, 0x88, 0x0D}};
const GUID kSession2 = {0x2C39AE40, 0x7D2E, 0x5044, {0x80, 0x4E, 0x8B, 0x67, 0x99, 0xD4, 0xCF, 0x9E}};
const GUID kSession3 = {0xF2CDD966, 0x22AE, 0x5EA1, {0x95, 0x96, 0x3A, 0x28, 0x93, 0x44, 0xC3, 0xBE}};
const GUID kDxgiInterfaceAccess = {0xA9B3D012, 0x3DF2, 0x4EE3, {0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1}};

Activation g_activation;
std::once_flag g_once;

// Combines the loader error with the API name so the log says which entry point
// was missing rather than "something failed".
std::string loader_error(const char* library, const char* function) {
    const DWORD code = GetLastError();
    std::string text = "GetProcAddress(";
    text += library;
    text += ", ";
    text += function;
    text += ") failed (";
    text += std::to_string(static_cast<unsigned long>(code));
    text += ")";
    return text;
}

template <typename T>
bool resolve(HMODULE module, const char* name, T& target, std::string& error) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    if (target == nullptr && error.empty()) error = loader_error("?", name);
    return target != nullptr;
}

void resolve_once() {
    HMODULE combase = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (combase == nullptr) combase = LoadLibraryW(L"combase.dll");
    if (combase == nullptr) {
        g_activation.error = "combase.dll could not be loaded";
        return;
    }

    HMODULE d3d11 = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (d3d11 == nullptr) d3d11 = LoadLibraryW(L"d3d11.dll");
    if (d3d11 == nullptr) {
        g_activation.error = "d3d11.dll could not be loaded";
        return;
    }

    // A missing symbol keeps the pointer null; every caller checks `available`
    // first, so a partial resolve is safe.
    resolve(combase, "RoInitialize", g_activation.ro_initialize, g_activation.error);
    resolve(combase, "RoUninitialize", g_activation.ro_uninitialize, g_activation.error);
    resolve(combase, "RoGetActivationFactory", g_activation.get_activation_factory, g_activation.error);
    resolve(combase, "WindowsCreateString", g_activation.create_hstring, g_activation.error);
    resolve(combase, "WindowsDeleteString", g_activation.delete_hstring, g_activation.error);
    resolve(d3d11, "CreateDirect3D11DeviceFromDXGIDevice", g_activation.create_direct3d_device, g_activation.error);

    g_activation.available = g_activation.ro_initialize != nullptr && g_activation.ro_uninitialize != nullptr &&
                             g_activation.get_activation_factory != nullptr &&
                             g_activation.create_hstring != nullptr && g_activation.delete_hstring != nullptr &&
                             g_activation.create_direct3d_device != nullptr;
    if (!g_activation.available && g_activation.error.empty()) {
        g_activation.error = "capture entry points are not present on this system";
    }
}

}  // namespace

const GUID& Guids::capture_item_interop() { return kCaptureItemInterop; }
const GUID& Guids::capture_item() { return kCaptureItem; }
const GUID& Guids::frame_pool_statics2() { return kFramePoolStatics2; }
const GUID& Guids::session() { return kSession; }
const GUID& Guids::session2() { return kSession2; }
const GUID& Guids::session3() { return kSession3; }
const GUID& Guids::dxgi_interface_access() { return kDxgiInterfaceAccess; }

const Activation& activation() {
    std::call_once(g_once, resolve_once);
    return g_activation;
}

bool capture_available() {
    const Activation& api = activation();
    return api.available && api.ro_initialize != nullptr && api.ro_uninitialize != nullptr;
}

const wchar_t* const kGraphicsCaptureItemClass = L"Windows.Graphics.Capture.GraphicsCaptureItem";
const wchar_t* const kDirect3D11CaptureFramePoolClass = L"Windows.Graphics.Capture.Direct3D11CaptureFramePool";

}  // namespace wgc
}  // namespace win
}  // namespace azy
