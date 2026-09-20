// Azy Skin — entry point.
//
// Startup order matters: DPI awareness first (so every coordinate Azy ever
// handles is a physical pixel), then logging (so failures are recorded), then
// the single-instance guard, then the controller.
//
// Azy is a plain Win32 application: no service, no driver, no injected code, and
// nothing that Premiere depends on. If it is not running, Premiere is completely
// unaffected.

#include "azy/win32/os/win_compat.hpp"  // windows.h first: everything else needs it
#include <string>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "azy/app/app_controller.hpp"
#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_api.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/os/win_version.hpp"
#include "azy/win32/skin/gdiplus_renderer.hpp"
#include "azy/win32/skin/input_guard.hpp"
#include "azy/win32/ui/app_icon.hpp"

namespace {

// Session-local (per-user) instance mutex: Azy is a per-user utility with
// per-user settings, so a second Windows session may run its own copy. The name
// is also declared as AppMutex in packaging/AzySkin.iss, which is how Setup knows
// to ask a running instance to close before replacing the executable.
constexpr const wchar_t* kSingleInstanceMutex = L"AzySkin.SingleInstance.7f2a1c94";
constexpr const char* kProductVersion = "1.0.0";

struct SingleInstance {
    HANDLE mutex = nullptr;

    ~SingleInstance() {
        if (mutex != nullptr) CloseHandle(mutex);
    }
};

// Makes every window Azy creates per-monitor-DPI aware. The application manifest
// already does this; the runtime call is the belt to the manifest's braces (it
// also covers the case of a build without an embedded manifest).
void ensure_dpi_awareness() {
    const azy::win::Api& api = azy::win::api();
    if (api.set_process_dpi_awareness_context) {
        api.set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    // Fall back for Windows 8.1/: shcore's SetProcessDpiAwareness(2).
    if (api.shcore_set_process_dpi_awareness) {
        api.shcore_set_process_dpi_awareness(2 /* PROCESS_PER_MONITOR_DPI_AWARE */);
    }
}

azy::app::CommandLine parse_command_line() {
    azy::app::CommandLine command_line;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return command_line;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = azy::to_lower(azy::win::to_utf8(argv[i]));
        if (arg == "--tray" || arg == "-tray" || arg == "/tray") command_line.tray_only = true;
        else if (arg == "--settings" || arg == "-settings" || arg == "/settings") command_line.open_settings = true;
        else if (arg == "--reset" || arg == "-reset" || arg == "/reset") command_line.reset_settings = true;
        else if (arg == "--debug" || arg == "-debug" || arg == "/debug") command_line.verbose_logging = true;
        else if (arg == "--no-tray" || arg == "/no-tray") command_line.no_tray = true;
        else if (arg == "--help" || arg == "-h" || arg == "/?") {
            MessageBoxW(nullptr,
                        L"Azy Skin - a lightweight visual skin for Adobe Premiere Pro.\n\n"
                        L"Options:\n"
                        L"  --tray      start minimised to the system tray (used at login)\n"
                        L"  --settings  open the settings window at startup\n"
                        L"  --reset     reset the configuration to defaults\n"
                        L"  --debug     verbose logging\n"
                        L"  --no-tray   do not create a tray icon (diagnostics)\n\n"
                        L"Azy Skin only reads information Windows publishes about Premiere Pro; it never modifies\n"
                        L"Adobe files, never injects code and never changes how Premiere works.",
                        L"Azy Skin", MB_OK | MB_ICONINFORMATION);
            ExitProcess(0);
        }
    }
    LocalFree(argv);
    return command_line;
}

void log_startup_banner(const azy::app::CommandLine& command_line) {
    using namespace azy;
    log_info("Azy Skin %s starting (pid %lu)", kProductVersion, GetCurrentProcessId());
    log_info("host: %s", win::host_info().host_summary.c_str());
    log_info("DPI awareness: %s", win::process_is_per_monitor_aware() ? "per-monitor v2" : "system (fallback)");
    log_info("%s", win::input_guard::contract_description());
    log_info("settings: %s", win::to_utf8(win::settings_path().wstring()).c_str());
    log_info("log: %s", win::to_utf8(win::log_path().wstring()).c_str());
    if (command_line.tray_only) log_info("launched in tray mode (start with Windows or --tray)");
    if (command_line.verbose_logging) log_info("verbose logging enabled");
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace azy;

    // 1. DPI awareness before any window exists.
    ensure_dpi_awareness();

    // 2. Command line + logging, so even an early failure is recorded.
    const app::CommandLine command_line = parse_command_line();
    Logger& logger = Logger::instance();
    logger.set_min_level(command_line.verbose_logging ? LogLevel::Debug : LogLevel::Info);
    if (!logger.open(win::log_path())) {
        logger.set_min_level(LogLevel::Debug);  // debugger-only mode
    }

    // 3. Single instance: a second launch just shows the settings window.
    SingleInstance single;
    single.mutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (single.mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        // A second launch is treated as "show me Azy": bring up Settings so the
        // user can see the state and change it, rather than starting a rival
        // instance that would fight over the same window.
        if (HWND existing = FindWindowW(L"AzySkin.MessageWindow", nullptr)) {
            PostMessageW(existing, app::AppController::open_settings_message_id(), 0, 0);
        }
        log_info("another Azy Skin instance is already running; exiting");
        return 0;
    }

    // 4. COM (used for the optional WMI process notifications) and the common
    //    controls used by the settings window.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);

    // 5. GDI+: Azy's only renderer, started once and shut down at exit.
    std::string gdi_error;
    if (!win::GdiPlusSession::start(&gdi_error)) {
        log_error("could not start GDI+: %s", gdi_error.c_str());
        MessageBoxW(nullptr,
                    L"Azy Skin could not start the Windows graphics library (GDI+).\n"
                    L"Premiere Pro is not affected; Azy Skin will now exit.",
                    L"Azy Skin", MB_OK | MB_ICONERROR);
        return 1;
    }

    log_startup_banner(command_line);

    int exit_code = 0;
    {
        app::AppController controller;
        std::string error;
        if (!controller.initialize(instance, command_line, &error)) {
            log_error("startup failed: %s", error.c_str());
            MessageBoxW(nullptr,
                        (L"Azy Skin could not start:\n\n" + win::to_wide(error) +
                         L"\n\nPremiere Pro is not affected. See the log for details.")
                            .c_str(),
                        L"Azy Skin", MB_OK | MB_ICONERROR);
            exit_code = 1;
        } else {
            exit_code = controller.run();
        }
    }

    win::GdiPlusSession::stop();
    win::destroy_app_icon();
    CoUninitialize();
    Logger::instance().close();
    return exit_code;
}
