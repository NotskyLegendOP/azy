// Azy Skin — the Windows Graphics Capture ABI, declared by hand.
//
// Why by hand: Windows.Graphics.Capture is a WinRT API, and the toolchain this
// project builds with (zig's bundled MinGW) ships `d3d11.h`, `dxgi1_2.h`,
// `dcomp.h`, `roapi.h` and `hstring.h`, but **no** `windows.graphics.capture.h`
// and no C++/WinRT. So the interfaces are declared here, ABI-compatible with the
// SDK headers.
//
// Every IID and every vtable order below was taken from a published source, not
// from memory:
//
//   * IID_IGraphicsCaptureItemInterop  3628E81B-3CAC-4C60-B7F4-23CE0E0C3356
//   * IID_IGraphicsCaptureItem         79C3F95B-31F7-4EC2-A464-632EF5D30760
//   * IID_IDirect3D11CaptureFramePoolStatics2  589B103F-6BBC-5DF5-A991-02E28B3B66D5
//   * IID_IGraphicsCaptureSession      814E42A9-F70F-4AD7-939B-FDDCC6EB880D
//   * IID_IGraphicsCaptureSession2     2C39AE40-7D2E-5044-804E-8B6799D4CF9E
//         (the SDK header text is quoted in microsoft/Windows.UI.Composition-Win32-Samples
//          issue #97; it also documents that create-for-window cursor capture is on
//          by default, which would draw a second cursor inside the mirror)
//   * IID_IGraphicsCaptureSession3     F2CDD966-22AE-5EA1-9596-3A289344C3BE
//         (IsBorderRequired: absent on Windows 10 - QI fails with E_NOINTERFACE,
//          so it is optional and never fatal)
//   * IID_IDirect3DDxgiInterfaceAccess A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1
//
// Method order follows the IDL declaration order, which is the convention the ABI
// uses: interface methods come after the three IUnknown slots and the three
// IInspectable slots, so the first method of each interface is at slot 6.
//
// The one residual risk is inherent to hand-declared ABI: a wrong slot would not
// fail cleanly, it would call the wrong function. That risk is why the capture
// feature can be switched off in one key, why a crash anywhere in it writes a
// marker that disables it for the next launch (see app/main.cpp), and why the
// working fallback path is never removed.
#pragma once

#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// The WinRT declarations windows.h does not bring in: HSTRING and the
// IInspectable base interface come from plain C headers the toolchain does ship,
// which is why only the *projections* are missing here, not the base types.
// EventRegistrationToken has no header in this toolchain at all, so it is
// declared below; it is a single 64-bit value and cannot vary.
#include <hstring.h>
#include <inspectable.h>

#include <dxgi.h>

namespace azy {
namespace win {
namespace wgc {

// --------------------------------------------------------------------- types

// Windows.Foundation.EventRegistrationToken: a single 64-bit cookie. Declared
// rather than included because this toolchain ships no eventtoken.h.
struct EventRegistrationToken {
    std::int64_t value = 0;
};

struct SizeInt32 {
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// Windows.Graphics.DirectX.DirectXPixelFormat: only the one format this feature
// uses (B8G8R8A8UIntNormalized = 87), which is what the desktop composes in.
constexpr std::int32_t kPixelFormatB8G8R8A8UIntNormalized = 87;

struct Guids {
    // {3628E81B-3CAC-4C60-B7F4-23CE0E0C3356}
    static const GUID& capture_item_interop();
    // {79C3F95B-31F7-4EC2-A464-632EF5D30760}
    static const GUID& capture_item();
    // {589B103F-6BBC-5DF5-A991-02E28B3B66D5}
    static const GUID& frame_pool_statics2();
    // {814E42A9-F70F-4AD7-939B-FDDCC6EB880D}
    static const GUID& session();
    // {2C39AE40-7D2E-5044-804E-8B6799D4CF9E}
    static const GUID& session2();
    // {F2CDD966-22AE-5EA1-9596-3A289344C3BE}
    static const GUID& session3();
    // {A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1}
    static const GUID& dxgi_interface_access();
};

// ----------------------------------------------------------------- interfaces
//
// Declared with the vtable laid out explicitly so the slot order is visible in
// the source rather than implied by a class definition.

struct IGraphicsCaptureItemInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND window, REFIID iid, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID iid, void** result) = 0;
};

// Only the object is needed (it is passed straight to CreateCaptureSession), so
// this is declared as its default interface with the methods we never call.
struct IGraphicsCaptureItem : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_Closed(void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(SizeInt32* value) = 0;
};

struct IDirect3D11CaptureFramePool;
struct IDirect3D11CaptureFrame;
struct IGraphicsCaptureSession;

struct IDirect3D11CaptureFramePoolStatics2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFreeThreaded(IUnknown* device, std::int32_t pixel_format,
                                                         std::int32_t buffer_count, SizeInt32 size,
                                                         IDirect3D11CaptureFramePool** result) = 0;
};

struct IDirect3D11CaptureFramePool : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_FrameArrived(void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameArrived(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE Recreate(IUnknown* device, std::int32_t pixel_format,
                                               std::int32_t buffer_count, SizeInt32 size) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCaptureSession(IGraphicsCaptureItem* item,
                                                           IGraphicsCaptureSession** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetNextFrame(IDirect3D11CaptureFrame** result) = 0;
};

struct IDirect3D11CaptureFrame : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_ContentSize(SizeInt32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Surface(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SystemRelativeTime(void* value) = 0;
};

struct IGraphicsCaptureSession : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE StartCapture() = 0;
};

struct IGraphicsCaptureSession2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsCursorCaptureEnabled(unsigned char* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsCursorCaptureEnabled(unsigned char value) = 0;
};

struct IGraphicsCaptureSession3 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsBorderRequired(unsigned char* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBorderRequired(unsigned char value) = 0;
};

struct IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** result) = 0;
};

// ------------------------------------------------------- activation functions
//
// Loaded from combase.dll / d3d11.dll at runtime rather than linked, so a machine
// without them cannot stop Azy from starting - it only loses this feature.

// WinRT apartment and activation entry points, resolved at run time. RoInitialize
// and RoUninitialize live in combase.dll, which this toolchain has no import
// library for; resolving them the same way as the rest keeps the linker out of it
// and keeps a machine without them from failing to start.
enum class RoInitType : int { SingleThreaded = 0, MultiThreaded = 1 };

struct Activation {
    HRESULT(STDAPICALLTYPE* ro_initialize)(RoInitType type) = nullptr;
    void(STDAPICALLTYPE* ro_uninitialize)() = nullptr;
    HRESULT(STDAPICALLTYPE* get_activation_factory)(HSTRING class_id, REFIID iid, void** factory) = nullptr;
    HRESULT(STDAPICALLTYPE* create_hstring)(const wchar_t* source, UINT32 length, HSTRING* out) = nullptr;
    HRESULT(STDAPICALLTYPE* delete_hstring)(HSTRING value) = nullptr;
    HRESULT(STDAPICALLTYPE* create_direct3d_device)(IDXGIDevice* dxgi_device, IInspectable** out) = nullptr;
    bool available = false;
    std::string error;
};

// Resolves the entry points once. Never throws, never crashes: every failure is
// reported in `error` and leaves `available` false. `available` means "capture can
// be attempted"; the apartment helpers are checked separately by the caller that
// needs them (they are only used on the capture thread).
const Activation& activation();

// True when every entry point needed to start and stop a capture is present.
bool capture_available();

// Runtime class names, spelled once.
extern const wchar_t* const kGraphicsCaptureItemClass;
extern const wchar_t* const kDirect3D11CaptureFramePoolClass;

}  // namespace wgc
}  // namespace win
}  // namespace azy
