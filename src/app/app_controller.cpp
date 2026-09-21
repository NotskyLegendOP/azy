#include "azy/app/app_controller.hpp"
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <shellapi.h>

#include <algorithm>

#include "azy/core/log.hpp"
#include "azy/core/version_string.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/autostart.hpp"
#include "azy/win32/os/win_api.hpp"
#include "azy/win32/os/win_util.hpp"
#include "azy/win32/os/win_version.hpp"

namespace azy {
namespace app {
namespace {

constexpr wchar_t kMessageClass[] = L"AzySkin.MessageWindow";
constexpr UINT kSyncMessage = WM_APP + 10;
constexpr UINT kSettingsChangedMessage = WM_APP + 11;
constexpr UINT kOpenSettingsMessage = WM_APP + 12;
constexpr UINT_PTR kSyncTimerId = 1;

std::wstring to_wide_path(const std::filesystem::path& path) { return path.wstring(); }

}  // namespace

UINT AppController::sync_message_id() { return kSyncMessage; }
UINT AppController::settings_changed_message_id() { return kSettingsChangedMessage; }
UINT AppController::open_settings_message_id() { return kOpenSettingsMessage; }
UINT AppController::exit_request_message_id() {
    // Registered by *name*, not as a fixed number: a build that has just been
    // installed has to be able to ask a build that is already running - compiled
    // long before it existed - to release the skin and exit.
    static const UINT id = RegisterWindowMessageW(L"AzySkin.ExitRequest");
    return id;
}

UINT AppController::version_query_message_id() {
    static const UINT id = RegisterWindowMessageW(L"AzySkin.VersionQuery");
    return id;
}

unsigned AppController::packed_version() { return pack_version(kAppVersion); }

bool AppController::initialize(HINSTANCE instance, const CommandLine& command_line, std::string* error) {
    instance_ = instance;
    command_line_ = command_line;

    if (!create_message_window(error)) return false;

    // --- settings ---------------------------------------------------------
    SettingsStore::LoadResult load_result = store_.load();
    Settings& settings = store_.settings();
    if (command_line.reset_settings) {
        store_.reset_to_defaults();
        log_info("configuration reset requested on the command line");
    }

    // Failure counters survive restarts: a Premiere build that made Azy fail
    // three times before also gets Safe Mode the next time.
    failures_ = FailureTracker(3, 300.0);
    failures_.set_failures(settings.failure_count);
    failures_.set_window_start(settings.failure_window_start);
    failures_.set_tripped(settings.failure_tripped);
    safe_mode_ = settings.safe_mode;

    // First run only: a lower-end machine starts in performance mode. An explicit
    // choice (settings.ini written by the user or the installer) is never
    // overridden, because then the file already exists.
    if (!load_result.file_existed && !command_line.reset_settings) {
        const win::MachineClass machine = win::detect_machine_class();
        if (machine.low_end) {
            settings.performance_mode = true;
            settings.suspend_when_inactive = true;
            store_.save();
            log_info("first run on a modest machine (%llu MB RAM, %lu logical processors): starting in %s",
                     static_cast<unsigned long long>(machine.physical_memory_bytes >> 20),
                     static_cast<unsigned long>(machine.logical_processors), "performance mode");
        } else {
            log_info("first run on a capable machine (%llu MB RAM, %lu logical processors)",
                     static_cast<unsigned long long>(machine.physical_memory_bytes >> 20),
                     static_cast<unsigned long>(machine.logical_processors));
        }
    }

    apply_installer_defaults();

    // --- observers --------------------------------------------------------
    std::string watch_error;
    if (!watch_.start(L"", &watch_error)) {
        // Not fatal: without the observer Azy falls back to its (slow) timer,
        // which still works, just with visible latency.
        log_warn("window observer unavailable: %s (falling back to timer only)", watch_error.c_str());
    }
    watch_.set_debug_logging(command_line.verbose_logging);

    std::string detector_error;
    detector_.start([this](const win::PremiereDetector::Event& event) { on_premiere_event(event); },
                    &detector_error);

    // Any window event wakes the sync path immediately (coalesced).
    watch_.set_notify([this]() { post_sync(); });

    if (command_line.no_tray == false) {
        win::TrayIcon::Callbacks callbacks;
        callbacks.on_toggle_skin = [this](bool enabled) {
            store_.settings().enabled = enabled;
            manual_apply_ = enabled;
            store_.save();
            log_info("skin %s by the user", enabled ? "enabled" : "disabled");
            sync("toggle");
            update_tray();
            update_settings_window_status();
        };
        callbacks.on_theme = [this](ThemeKey theme) {
            store_.settings().appearance.theme = theme;
            store_.save();
            log_info("theme changed to %s", theme_key_name(theme));
            sync("theme");
            update_tray();
            update_settings_window_status();
        };
        callbacks.on_settings = [this]() {
            settings_window_.show(store_.settings(), win::SettingsWindow::Status{});
            update_settings_window_status();
        };
        callbacks.on_start_with_windows = [this](bool enabled) {
            std::string autostart_error;
            if (!win::set_autostart_enabled(enabled, &autostart_error)) {
                log_warn("could not update the Start with Windows entry: %s", autostart_error.c_str());
            } else {
                store_.settings().start_with_windows = enabled;
                store_.save();
                log_info("Start with Windows %s", enabled ? "enabled" : "disabled");
            }
            update_tray();
            update_settings_window_status();
        };
        callbacks.on_suspend = [this](bool suspended) {
            suspended_manual_ = suspended;
            log_info("skin %s by the user", suspended ? "suspended" : "resumed");
            sync("suspend");
            update_tray();
        };
        callbacks.on_reload_settings = [this]() {
            log_info("configuration reload requested");
            store_.load();
            sync("reload");
            update_tray();
            update_settings_window_status();
        };
        callbacks.on_open_log = [this]() { open_log_file(); };
        callbacks.on_restart_elevated = [this]() { restart_elevated(); };
        callbacks.on_exit = [this]() { request_exit(0); };

        std::string tray_error;
        if (!tray_.create(window_, callbacks, &tray_error)) {
            log_warn("tray icon unavailable: %s", tray_error.c_str());
        }
    }

    // --- settings window --------------------------------------------------
    win::SettingsWindow::Callbacks window_callbacks;
    window_callbacks.on_change = [this](const Settings& incoming) { merge_user_settings(incoming); };
    window_callbacks.on_reset = [this]() {
        store_.reset_to_defaults();
        store_.save();
        sync("reset");
        settings_window_.show(store_.settings(), win::SettingsWindow::Status{});
        update_settings_window_status();
    };
    window_callbacks.on_open_log = [this]() { open_log_file(); };
    window_callbacks.on_check_visibility = [this]() { return check_visibility(); };
    // Live preview (spec §28): while the Appearance page is open, a slider or a theme
    // change is applied to the running mirror immediately, so the user is choosing by
    // looking at Premiere rather than at a description. Nothing is saved until the
    // page's own Save/OK, which is what keeps "preview" from meaning "committed".
    window_callbacks.on_preview = [this](const Settings& incoming) {
        win::SkinRequest request;
        win::SuspendReason reason = win::SuspendReason::NoWindow;
        compute_request(request, reason);
        request.appearance = incoming.appearance;
        request.performance_mode = incoming.performance_mode;
        request.suspend = suspended_manual_ ? win::SuspendReason::SkinDisabled : reason;
        win::SkinState preview_state;
        engine_.apply(request, preview_state);
        // The preview is applied to the live mirror only: the settings object is not
        // saved here, so closing the page without saving cannot leave the skin on a
        // look the user never confirmed.
        (void)preview_state;
    };
    std::string window_error;
    if (!settings_window_.create(instance_, window_callbacks, &window_error)) {
        log_warn("settings window unavailable: %s", window_error.c_str());
    }

    // --- settings.ini watcher --------------------------------------------
    std::string watcher_error;
    if (!settings_watcher_.start(store_.path(), window_, kSettingsChangedMessage, &watcher_error)) {
        log_debug("settings watcher unavailable: %s", watcher_error.c_str());
    }

    // --- engine -----------------------------------------------------------
    std::string engine_error;
    if (!engine_.initialize(&engine_error)) {
        if (error) *error = engine_error;
        return false;
    }
    // Start from a clean slate: nothing applied, no surface window created yet.
    engine_.revert();

    if (command_line.open_settings) {
        settings_window_.show(store_.settings(), win::SettingsWindow::Status{});
    }

    // First sync: picks up a Premiere that is already running.
    detector_.force_rescan(win::monotonic_seconds());
    sync("startup");
    arm_timer(1000);

    if (load_result.file_existed == false) {
        log_info("no settings file yet; defaults created at %s", win::to_utf8(store_.path().wstring()).c_str());
    }
    return true;
}

bool AppController::create_message_window(std::string* error) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AppController::message_window_proc;
    wc.hInstance = instance_;
    wc.lpszClassName = kMessageClass;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error) *error = "RegisterClassEx(message window) failed: " + win::to_utf8(win::last_error_text());
        return false;
    }

    // A real top-level window that is simply never shown. HWND_MESSAGE would be
    // tempting, but message-only windows do not receive broadcast messages, and
    // Azy depends on WM_SETTINGCHANGE / WM_DISPLAYCHANGE / the shell's
    // TaskbarCreated. WS_EX_TOOLWINDOW + WS_EX_NOACTIVATE + zero size + never
    // calling ShowWindow keeps it invisible, unclickable and out of Alt+Tab.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kMessageClass, L"Azy Skin", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance_, this);
    if (window_ == nullptr) {
        if (error) *error = "CreateWindowEx(message window) failed: " + win::to_utf8(win::last_error_text());
        return false;
    }
    return true;
}

LRESULT CALLBACK AppController::message_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    auto* self = reinterpret_cast<AppController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
    return self->handle_message(hwnd, message, wparam, lparam);
}

LRESULT AppController::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == win::TrayIcon::tray_message_id()) {
        tray_.handle_message(wparam, lparam);
        return 0;
    }
    if (message == win::TrayIcon::taskbar_created_message_id()) {
        tray_.recreate_after_shell_restart();
        return 0;
    }
    if (message == exit_request_message_id()) {
        // A newer build is starting and needs this instance to let go of the skin.
        log_info("another build asked this instance to exit; shutting down");
        request_exit(0);
        return 0;
    }
    if (message == version_query_message_id()) {
        return static_cast<LRESULT>(packed_version());
    }
    switch (message) {
        case kSyncMessage:
            sync_pending_ = false;
            sync("event");
            return 0;
        case kSettingsChangedMessage:
            on_settings_changed_on_disk();
            return 0;
        case kOpenSettingsMessage:
            settings_window_.show(store_.settings(), win::SettingsWindow::Status{});
            update_settings_window_status();
            return 0;
        case WM_TIMER:
            if (wparam == kSyncTimerId) {
                sync("timer");
                return 0;
            }
            break;
        case WM_SETTINGCHANGE:
            // Theme, metrics or DPI policy changed: re-resolve what the host can
            // do and re-apply. Cheap, and it is why a DPI change or a Windows
            // theme switch is picked up without any polling.
            log_debug("system settings changed (0x%llX)", static_cast<unsigned long long>(wparam));
            sync("settings change");
            return 0;
        case WM_DISPLAYCHANGE:
            log_info("display configuration changed (%dx%d)", LOWORD(lparam), HIWORD(lparam));
            sync("display change");
            return 0;
        case WM_DEVICECHANGE:
            sync("device change");
            return 0;
        case WM_POWERBROADCAST:
            if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
                log_info("resumed from sleep; re-checking Premiere");
                detector_.force_rescan(win::monotonic_seconds());
                sync("resume");
            }
            return TRUE;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wparam != 0) {
                log_info("Windows is shutting down");
                request_exit(0);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            window_ = nullptr;
            PostQuitMessage(exit_code_);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void AppController::post_sync() {
    if (window_ == nullptr) return;
    if (sync_pending_) return;  // coalesce bursts into a single message
    sync_pending_ = true;
    PostMessageW(window_, kSyncMessage, 0, 0);
}

void AppController::request_exit(int exit_code) {
    exit_code_ = exit_code;
    if (window_ != nullptr) {
        PostMessageW(window_, WM_CLOSE, 0, 0);
    }
}

int AppController::run() {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    shutdown();
    return exit_code_;
}

void AppController::arm_timer(int interval_ms) {
    if (window_ == nullptr) return;
    if (interval_ms == timer_interval_) return;
    timer_interval_ = interval_ms;
    SetTimer(window_, kSyncTimerId, static_cast<UINT>(interval_ms), nullptr);
}

void AppController::on_premiere_event(const win::PremiereDetector::Event& event) {
    switch (event.kind) {
        case win::PremiereDetector::EventKind::Started:
        case win::PremiereDetector::EventKind::Changed: {
            product_ = event.product;
            if (event.pid != 0 && event.pid != tracker_.pid()) {
                // A different build took over (e.g. the user closed Premiere and
                // launched a Beta): drop everything we knew about the old one.
                engine_.revert();
                tracker_.clear();
            }
            if (event.window != nullptr) {
                if (tracker_.hwnd() != event.window) {
                    tracker_.set_window(event.window, event.pid);
                    watch_.set_watched_pid(event.pid);
                    watch_.set_watched_thread(tracker_.thread_id());
                }
            } else if (tracker_.has_window()) {
                engine_.revert();
                tracker_.clear();
            }
            // Windows never lets a window of a lower integrity process be drawn
            // above one of a higher integrity process (UIPI). If Premiere runs
            // elevated and Azy does not, the mirror is composed behind it: every
            // call succeeds and nothing is ever visible. Worth saying out loud.
            if (event.pid != 0) {
                const int premiere_level = win::process_integrity_level(event.pid);
                const int own_level = win::own_integrity_level();
                const bool elevated = premiere_level >= 3 && own_level >= 0 && premiere_level > own_level;
                if (elevated && !premiere_elevated_) {
                    log_warn("Premiere Pro is running elevated (%s) while Azy Skin is not (%s): Windows will not let "
                             "Azy's surface be drawn above it, so the skin cannot be visible. Start Azy Skin as "
                             "administrator as well, or run Premiere Pro normally.",
                             win::integrity_level_name(premiere_level), win::integrity_level_name(own_level));
                }
                premiere_elevated_ = elevated;
            }

            Settings& settings = store_.settings();
            const std::string version = product_.version_string();
            if (version != settings.last_premiere_version && version != "unknown") {
                settings.last_premiere_version = version;
                store_.save();
            }
            break;
        }
        case win::PremiereDetector::EventKind::Stopped: {
            // Premiere is gone (spec §34): stop the capture, release every GPU
            // resource, reset the lifecycle and keep waiting. No handles stay open on
            // a dead process, and nothing keeps running.
            engine_.revert();
            tracker_.clear();
            watch_.set_watched_pid(0);
            watch_.set_watched_thread(0);
            watch_.clear_move_size_loop();
            manual_apply_ = false;
            premiere_elevated_ = false;
            mirror_warning_shown_ = false;
            product_ = ProductInfo{};
            log_info("skin resources released");
            break;
        }
    }
    sync("premiere");
    update_tray();
    update_settings_window_status();
}

std::string AppController::check_visibility() {
    // A manual check is also the documented way back from an overlay that decided
    // this host cannot do it: the answer may have changed (a driver update, a new
    // Premiere window), so the verdict is cleared before the report is built.
    engine_.retry_overlay();
    // Present again even though nothing changed: without this there would be
    // nothing to look at (and nothing to measure).
    engine_.invalidate();
    sync("visibility check");

    std::string report;
    const bool on_screen = engine_.probe_on_screen(&report);

    // One log line, so the report shows up in the file too (newlines would make
    // the log unreadable).
    std::string flat = report;
    for (char& c : flat) {
        if (c == '\n') c = ' ';
    }
    if (on_screen) {
        log_info("visibility check: %s", flat.c_str());
    } else {
        log_warn("visibility check: %s", flat.c_str());
    }

    std::string text = "Azy Skin " + std::string(kAppVersion) + "\n";
    text += "Premiere Pro: " + premiere_summary() + "\n\n";
    text += report + "\n\n";
    text += on_screen ? "The mirror is on screen. If Premiere still looks untouched, the theme is too "
                        "restrained for your display: raise Overall darkness, Border intensity and Glow in "
                        "Appearance."
                      : engine_.last_error().empty()
                            ? std::string("Nothing is on screen yet. The lifecycle line above says which stage "
                                          "it is waiting at; the log file has the same information.")
                            : ("Nothing is on screen: " + engine_.last_error() +
                               "  Press Check visibility again after fixing that, or use Tray > Theme > "
                               "Original to turn the skin off entirely.");
    return text;
}

void AppController::on_settings_changed_on_disk() {
    // Ignore the notification caused by our own save.
    if (!store_.changed_on_disk()) return;
    log_info("settings file changed on disk; reloading");
    store_.load();
    sync("reload");
    update_tray();
    update_settings_window_status();
}

void AppController::compute_request(win::SkinRequest& request, win::SuspendReason& reason) const {
    const Settings& settings = store_.settings();
    request.target = tracker_.target();
    // Safe mode means *stopped*, not "a lesser skin": 2.0.0 has no reduced visual
    // set to fall back to, so the honest reduced state is nothing on screen until the
    // user says otherwise (Settings > Advanced > Re-enable features).
    request.skin_enabled =
        settings.enabled && !safe_mode_ && (settings.apply_automatically || manual_apply_);
    // The theme and the sliders are the whole visual input: every colour the renderer
    // uses comes from the theme engine, and nothing is hardcoded in the renderer.
    request.appearance = settings.appearance;
    // The Advanced page's per-feature overrides ("disabled_features=glass,glow" in
    // settings.ini): each one removes that part of the material rather than hiding a
    // control, so a disabled feature is really absent from the picture.
    if (settings.feature_disabled(feature_key::kGlass)) request.appearance.glass_intensity = 0.0;
    if (settings.feature_disabled(feature_key::kBorder)) request.appearance.border_intensity = 0.0;
    if (settings.feature_disabled(feature_key::kShadow)) request.appearance.shadow_intensity = 0.0;
    if (settings.feature_disabled(feature_key::kGlow)) request.appearance.glow_intensity = 0.0;
    request.performance_mode = settings.performance_mode;
    // Debug mode is deliberately not gated behind `experimental`: it reports facts
    // and changes nothing about the skin itself, and it is the tool a user needs when
    // something *is* wrong.
    request.debug_mode = settings.debug_mode;
    request.workspace = settings.ui_profile;
    reason = win::SuspendReason::NoWindow;
}

void AppController::sync(const char* reason_name) {
    if (shutting_down_) return;
    const double now = win::monotonic_seconds();

    // --- 1. Premiere detection (no work unless something changed) ----------
    // A process scan costs a toolhelp snapshot of every process on the machine, so
    // it is only worth doing while we are still looking for Premiere or for its
    // editor window. Once a target is attached, process changes arrive as WMI
    // events and window changes are handled by the tracker below - so normal
    // interaction in Premiere, and a desktop full of other applications doing
    // whatever they like, produces no process enumeration at all.
    //
    // The detector decides, not the event, because the answer depends on what is
    // already known: with a live process observer and no target, "a window
    // appeared somewhere" is not a reason to enumerate every process on the
    // machine once more.
    if (watch_.consume_dirty() && detector_.wants_window_activity()) {
        detector_.note_window_activity();
    }
    detector_.pump(now);

    // --- 2. Window geometry (only when Windows reported a change) ---------
    const bool dragging = watch_.in_move_size_loop();
    const bool location_dirty = watch_.consume_location_dirty();
    const bool foreground_dirty = watch_.consume_foreground_dirty();
    if (tracker_.has_window() && (location_dirty || foreground_dirty)) {
        // A drag produces one location event per pixel. While a move/size loop is
        // running the surface is hidden anyway, so the tracker samples instead of
        // chasing every event, and the final position is exact because the last
        // event (and MOVESIZE_END) arrives after the movement stops.
        if (!dragging || (now - last_tracker_refresh_) >= kDragRefreshIntervalSeconds) {
            last_tracker_refresh_ = now;
            const win::WindowTracker::RefreshResult result = tracker_.refresh(now);
            if (result.window_gone) {
                log_debug("tracked window disappeared (0x%p)", reinterpret_cast<void*>(tracker_.hwnd()));
                engine_.revert();
                tracker_.clear();
                detector_.note_window_activity();
            }
        }
    }

    // Safety net for a lost EVENT_SYSTEM_MOVESIZEEND (e.g. Premiere was killed
    // mid-drag): a move/size loop with no movement for a second is over.
    if (dragging && tracker_.seconds_since_geometry_change(now) > 1.0) {
        log_debug("move/size loop ended without a notification; resuming");
        watch_.clear_move_size_loop();
    }

    // --- 3. Decide whether anything should be on screen -------------------
    win::PerformanceManager::Inputs inputs;
    inputs.skin_enabled = store_.settings().enabled && !suspended_manual_;
    inputs.original_theme = store_.settings().appearance.theme == ThemeKey::Original;
    inputs.has_window = tracker_.has_window();
    inputs.window_minimized = tracker_.target().minimized;
    inputs.window_cloaked = tracker_.target().cloaked;
    inputs.window_foreground = tracker_.target().foreground;
    inputs.move_size_loop = watch_.in_move_size_loop();
    inputs.seconds_since_move = tracker_.seconds_since_geometry_change(now);
    inputs.suspend_on_minimize = store_.settings().suspend_when_minimized;
    inputs.suspend_on_inactive = store_.settings().suspend_when_inactive;
    inputs.performance_mode = store_.settings().performance_mode;

    const win::PerformanceManager::Decision decision = performance_.evaluate(inputs);

    win::SkinRequest request;
    win::SuspendReason reason = win::SuspendReason::NoWindow;
    compute_request(request, reason);
    if (suspended_manual_) {
        reason = win::SuspendReason::SkinDisabled;
    } else if (!store_.settings().enabled) {
        reason = win::SuspendReason::SkinDisabled;
    } else if (store_.settings().appearance.theme == ThemeKey::Original) {
        reason = win::SuspendReason::OriginalTheme;
    } else if (!tracker_.has_window()) {
        reason = win::SuspendReason::NoWindow;
    } else if (!tracker_.target().visible) {
        // Premiere keeps a handful of hidden top-level windows; decorating one of
        // them would mean drawing a mirror around nothing.
        reason = win::SuspendReason::Hidden;
    } else {
        reason = decision.reason;
    }
    request.suspend = reason;

    // --- 4. Apply ---------------------------------------------------------
    // Kept in sync even when the engine has nothing to do, so the tray tooltip and
    // the settings window always explain the current state.
    engine_state_.suspend = reason;
    const unsigned long long failures_before = engine_state_.failures;
    const bool changed = request.target.hwnd != nullptr || engine_.surface_visible()
                             ? engine_.apply(request, engine_state_)
                             : false;
    if (request.target.valid()) tracker_.mark_applied();

    // One log line per transition, never per tick: this is the line that says whether
    // the mirror is on screen.
    if (engine_state_.mirror_active != mirror_active_ || engine_state_.mirror_capturing != mirror_capturing_) {
        mirror_active_ = engine_state_.mirror_active;
        mirror_capturing_ = engine_state_.mirror_capturing;
        if (mirror_active_) {
            log_info("mirror: on screen (%s)", engine_state_.mirror_note.c_str());
        } else {
            log_info("mirror: off (%s)%s", engine_state_.mirror_note.c_str(),
                     mirror_capturing_ ? " - still capturing" : "");
        }
    }

    if (engine_state_.failures > failures_before) {
        if (failures_.record_failure(now)) {
            enter_safe_mode(str_format("repeated visual failures with %s", product_.display_name().c_str()));
        }
        persist_failure_state();
    } else if (changed) {
        failures_.record_success(now);
        performance_.note_activity(now);
    }

    if (changed) {
        log_debug("skin updated (%s): state=%s | detector: %llu scan(s), %llu process event(s)", reason_name,
                  engine_state_.mirror_state.c_str(), detector_.stats().scans, detector_.stats().events_from_wmi);
    }

    // --- 4c. Keep Azy's surfaces in front of Premiere ----------------------
    // Activating Premiere puts it at the top of the window band, above Azy's strips
    // and overlay - which are ordinary windows - so the skin would silently vanish
    // behind the very window it decorates. A z-order walk and, only when the order is
    // really wrong, one SetWindowPos per surface.
    //
    // When it runs: the z-order of two windows can only change because something was
    // raised, and the case that matters is Premiere coming to the foreground - so
    // this is gated on the foreground changing, or on an apply having just moved
    // Azy's own windows. The settle timer alone (once a second, forever) must not
    // walk the desktop window list for a condition that cannot have changed.
    if (foreground_dirty || changed) {
        engine_.reassert_stacking();
    }

    // --- 4b. Say something when the mirror cannot be seen -----------------
    // A utility whose entire purpose is to be visible must not fail silently. When the
    // skin is meant to be applying and the mirror is not on screen, tell the user once,
    // with the reason, instead of leaving them to wonder (spec §40).
    const bool mirror_expected = request.skin_enabled && request.suspend == win::SuspendReason::None &&
                                 request.target.valid() && engine_.mirror_supported();
    const bool mirror_visible = engine_state_.mirror_active;
    if (mirror_expected && !mirror_visible && !mirror_warning_shown_) {
        mirror_warning_shown_ = true;
        const std::string reason = engine_.last_error().empty() ? engine_state_.mirror_note : engine_.last_error();
        log_warn("the skin is not visible: %s", reason.c_str());
        if (tray_.exists()) {
            tray_.notify(L"Azy Skin - skin not visible",
                         L"Azy Skin is running, but it could not put the skin on screen: " +
                             win::to_wide(reason) + L"  (Settings > Open log file has the details.)",
                         NIIF_WARNING);
        }
    }
    if (mirror_visible) mirror_warning_shown_ = false;

    // --- 5. Housekeeping --------------------------------------------------
    arm_timer(decision.timer_interval_ms);
    const std::string status = status_line();
    if (status != last_status_line_) {
        last_status_line_ = status;
        log_info("%s", status.c_str());
        update_tray();
        update_settings_window_status();
    }
}

void AppController::apply_installer_defaults() {
    // The installer writes %LOCALAPPDATA%\Azy Skin\install-defaults.ini with the
    // choices made in its wizard ("Start with Windows", "Automatically skin
    // Premiere Pro"). Applying it once and deleting it means a re-install cannot
    // silently undo a preference the user changed later.
    const std::filesystem::path defaults_path = win::local_app_data_dir() / L"install-defaults.ini";
    std::string text;
    if (!win::read_text_file(defaults_path, text)) return;

    std::vector<std::string> warnings;
    const Settings installer = Settings::from_ini(text, &warnings);
    Settings& settings = store_.settings();
    settings.enabled = installer.enabled;
    settings.start_with_windows = installer.start_with_windows;
    settings.apply_automatically = installer.apply_automatically;
    settings.clamp();
    store_.save();

    if (settings.start_with_windows) {
        std::string autostart_error;
        if (!win::set_autostart_enabled(true, &autostart_error)) {
            log_warn("installer default: could not create the Start with Windows entry: %s",
                     autostart_error.c_str());
        }
    }

    std::error_code ec;
    std::filesystem::remove(defaults_path, ec);
    log_info("applied installer defaults (skin=%d, autostart=%d, auto-apply=%d) and removed the hand-off file",
             settings.enabled ? 1 : 0, settings.start_with_windows ? 1 : 0,
             settings.apply_automatically ? 1 : 0);
}

void AppController::persist_failure_state() {
    Settings& settings = store_.settings();
    settings.failure_count = failures_.failures();
    settings.failure_window_start = failures_.window_start();
    settings.failure_last = failures_.last_failure();
    settings.failure_tripped = failures_.tripped();
    store_.save();
}

void AppController::enter_safe_mode(const std::string& reason) {
    if (safe_mode_) return;
    safe_mode_ = true;
    Settings& settings = store_.settings();
    settings.safe_mode = true;
    settings.safe_mode_reason = reason;
    settings.failure_count = 0;
    settings.failure_window_start = 0;
    settings.failure_last = 0;
    settings.failure_tripped = false;
    store_.save();

    log_warn("Azy Skin detected an issue with this Premiere version.");
    log_warn("Safe Mode has been enabled: the skin will not be applied until it is re-enabled in Settings. (%s)",
             reason.c_str());
    const std::wstring message =
        L"Azy Skin hit a problem with " + win::to_wide(product_.display_name()) +
        L" and switched to Safe Mode: it has stopped applying the skin. Re-enable it in Settings > Advanced.";
    tray_.notify(L"Azy Skin - Safe Mode", message, NIIF_WARNING);
    watch_.clear_move_size_loop();
    engine_.revert();
    sync("safe mode");
    update_tray();
    update_settings_window_status();
}

void AppController::merge_user_settings(const Settings& incoming) {
    Settings& settings = store_.settings();
    const Settings previous = settings;

    settings.enabled = incoming.enabled;
    settings.start_with_windows = incoming.start_with_windows;
    settings.apply_automatically = incoming.apply_automatically;
    settings.appearance = incoming.appearance;
    settings.performance_mode = incoming.performance_mode;
    settings.suspend_when_minimized = incoming.suspend_when_minimized;
    settings.suspend_when_inactive = incoming.suspend_when_inactive;
    settings.experimental = incoming.experimental;
    settings.safe_mode = incoming.safe_mode;
    settings.safe_mode_reason = incoming.safe_mode_reason;
    settings.clamp();

    safe_mode_ = settings.safe_mode;
    if (!safe_mode_) failures_.reset();

    store_.save();
    apply_settings_side_effects(previous);
    sync("settings");
    update_tray();
    update_settings_window_status();
}

void AppController::apply_settings_side_effects(const Settings& previous) {
    const Settings& settings = store_.settings();
    if (settings.enabled && !previous.enabled) manual_apply_ = true;
    if (settings.start_with_windows != previous.start_with_windows) {
        std::string autostart_error;
        if (!win::set_autostart_enabled(settings.start_with_windows, &autostart_error)) {
            log_warn("could not update the Start with Windows entry: %s", autostart_error.c_str());
        }
    }
}

std::string AppController::premiere_summary() const {
    if (!detector_.has_target()) return "not running";
    if (tracker_.hwnd() == nullptr) return product_.display_name() + " " + product_.version_string() + " (starting)";
    return product_.display_name() + " " + product_.version_string();
}

std::string AppController::treatment_summary() const {
    if (!store_.settings().enabled) return "off";
    if (safe_mode_) return "safe mode";
    if (store_.settings().appearance.theme == ThemeKey::Original) return "original (no skin)";
    std::string summary = "live mirror of Premiere";

    // The lifecycle, when it is the interesting part: naming the mirror for what it
    // actually is beats a generic "active" that hides a stalled capture.
    if (engine_.state() == win::MirrorState::Unsupported) {
        summary = "no mirror on this host (" + engine_.status_note() + ")";
    } else if (!engine_state_.mirror_active) {
        if (engine_.state() == win::MirrorState::Suspended) {
            summary += ", suspended (" + engine_.status_note() + ")";
        } else {
            summary += ", " + engine_.status_note();
        }
    }
    if (store_.settings().performance_mode) summary += ", performance mode";
    return summary;
}

// CPU time of this process between two calls, as a percentage of one core.
//
// Measured with GetProcessTimes, which the scheduler maintains anyway: two calls
// and one subtraction, no timer, no sampling thread. It is a *process* figure (Azy
// is one process) and it is a percentage of a single core, so 100% means one core
// saturated. The first call has no previous sample to compare against and says so
// rather than inventing a number.
std::string AppController::process_cpu_line() const {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return "unavailable";
    }
    const auto to_seconds = [](const FILETIME& value) {
        ULARGE_INTEGER ticks{};
        ticks.LowPart = value.dwLowDateTime;
        ticks.HighPart = value.dwHighDateTime;
        return static_cast<double>(ticks.QuadPart) / 10000000.0;  // 100 ns units
    };
    const double cpu_seconds = to_seconds(kernel) + to_seconds(user);
    const double wall = win::wall_seconds();

    static double previous_cpu = 0.0;
    static double previous_wall = 0.0;
    std::string text;
    if (previous_wall > 0.0 && wall > previous_wall) {
        const double percent = (cpu_seconds - previous_cpu) / (wall - previous_wall) * 100.0;
        text = str_format("%.1f%% of one core (measured between the last two diagnostics reads)", percent);
    } else {
        text = "no previous sample yet (open the diagnostics again)";
    }
    previous_cpu = cpu_seconds;
    previous_wall = wall;
    return text;
}

std::string AppController::measured_rates_line(unsigned long long frames, unsigned long long presents) const {
    const double wall = win::wall_seconds();
    static unsigned long long previous_frames = 0;
    static unsigned long long previous_presents = 0;
    static double previous_wall = 0.0;
    std::string text;
    if (previous_wall > 0.0 && wall > previous_wall + 0.05) {
        const double seconds = wall - previous_wall;
        const double capture_fps = static_cast<double>(frames - previous_frames) / seconds;
        const double draw_fps = static_cast<double>(presents - previous_presents) / seconds;
        text = str_format("capture %.1f fps, draw %.1f fps (measured between the last two diagnostics reads)",
                          capture_fps < 0.0 ? 0.0 : capture_fps, draw_fps < 0.0 ? 0.0 : draw_fps);
    } else {
        text = "no previous sample yet (open the diagnostics again)";
    }
    previous_frames = frames;
    previous_presents = presents;
    previous_wall = wall;
    return text;
}

std::vector<std::string> AppController::diagnostics_lines() const {
    std::vector<std::string> lines;
    const Settings& settings = store_.settings();
    const win::SkinTarget& target = tracker_.target();
    const win::CaptureStatus capture = engine_.capture_status();
    const win::MirrorRenderer::Stats& stats = engine_.mirror_stats();

    // --- the debug screen the brief asks for (spec §38), one line per group -----
    lines.push_back(str_format("Azy Skin %s - lifecycle: %s", kAppVersion, engine_.state_name()));

    // Premiere
    if (target.hwnd != nullptr) {
        lines.push_back(str_format("Premiere: detected YES | pid %lu | hwnd 0x%p | thread %lu | %s %s",
                                   tracker_.pid(), (void*)target.hwnd, target.thread_id,
                                   product_.display_name().c_str(), product_.version_string().c_str()));
    } else if (detector_.has_target()) {
        lines.push_back("Premiere: detected YES (process found, no usable window yet)");
    } else {
        lines.push_back("Premiere: detected NO - waiting for Premiere Pro...");
    }

    // Window
    if (target.hwnd != nullptr) {
        std::string shape = "windowed";
        if (target.minimized) shape = "minimized";
        else if (target.maximized) shape = "maximized";
        else if (target.fullscreen) shape = "fullscreen";
        if (!target.visible) shape += ", hidden";
        lines.push_back(str_format("Window: x=%d y=%d w=%d h=%d | %s | class '%s' | monitor (%d,%d)-(%d,%d)",
                                   target.visible_frame.left, target.visible_frame.top,
                                   target.visible_frame.width(), target.visible_frame.height(), shape.c_str(),
                                   win::to_utf8(target.window_class).c_str(), target.monitor.left,
                                   target.monitor.top, target.monitor.right, target.monitor.bottom));
    } else {
        lines.push_back("Window: none");
    }

    // DPI
    lines.push_back(str_format("DPI: %u%% (%.2f physical pixels per DIP)", target.dpi * 100u / 96u,
                               static_cast<double>(target.dpi) / 96.0));

    // Capture
    if (engine_.mirror_window() != nullptr) {
        lines.push_back(str_format("Capture: %s | source: the Premiere window 0x%p (never the desktop) | "
                                   "content %ux%u | %llu frames | %llu copies | %llu empty polls | %llu failures | "
                                   "%llu pool resizes | newest frame %llu ms old | cursor %s",
                                   capture.running ? (capture.item_closed ? "item closed" : "running") : "stopped",
                                   (void*)target.hwnd, capture.content_width, capture.content_height, capture.frames,
                                   capture.copies, capture.empty_polls, capture.failures, capture.pool_resizes,
                                   capture.frame_age_ms,
                                   capture.cursor_disabled ? "excluded" : "captured (a second pointer may show)"));
    } else {
        lines.push_back("Capture: not created" + (engine_.last_error().empty()
                                                       ? std::string()
                                                       : " - " + engine_.last_error()));
    }

    // Renderer
    lines.push_back(str_format("Renderer: %s | %llu presents | %u fps (%s) | uv %.3f,%.3f-%.3f,%.3f | "
                               "size agrees: %s | fade %.2f",
                               stats.visible ? "drawing" : "idle", stats.presents, stats.paced_fps,
                               stats.active ? "active" : "idle", stats.uv[0], stats.uv[1], stats.uv[2], stats.uv[3],
                               stats.capture_size_agrees ? "yes" : "no", stats.fade));

    // Composition
    lines.push_back(str_format("Composition: mirror window 0x%p (Azy pid %lu) at %d,%d %dx%d | offset from "
                               "Premiere %d,%d | DirectComposition swap chain 2 x %dx%d BGRA8 premultiplied | "
                               "%d media regions untouched | %d panel frames",
                               (void*)engine_.mirror_window(), GetCurrentProcessId(), stats.window_x, stats.window_y,
                               stats.window_w, stats.window_h, stats.window_x - target.visible_frame.left,
                               stats.window_y - target.visible_frame.top, stats.width, stats.height,
                               stats.media_regions, stats.panel_rects));

    // Theme
    lines.push_back(str_format("Theme: %s | darkness %.0f%% | glass %.0f%% | border %.0f%% | shadow %.0f%% | "
                               "corner %d dip | glow %.0f%% | animations %s",
                               theme_key_name(settings.appearance.theme), settings.appearance.darkness * 100.0,
                               settings.appearance.glass_intensity * 100.0,
                               settings.appearance.border_intensity * 100.0,
                               settings.appearance.shadow_intensity * 100.0, settings.appearance.corner_radius_dip,
                               settings.appearance.glow_intensity * 100.0,
                               settings.appearance.animations ? "on" : "off"));
    lines.push_back(str_format("  note: the skin classifies pixels by luminance and by the panel model, and the "
                               "media regions are passed through exactly. It is not per-control segmentation: "
                               "docs/AZY_MIRROR_ARCHITECTURE.md section 10 lists where that can be fooled."));

    // Rates and cost, measured between two reads
    lines.push_back("Rates: " + measured_rates_line(capture.frames, stats.presents));
    lines.push_back("Process cpu: " + process_cpu_line());

    if (premiere_elevated_) {
        lines.push_back("Note: Premiere runs as administrator, Azy Skin does not - Windows blocks placing a window "
                        "above it. Tray menu -> Restart as Administrator fixes that.");
    }
    if (!engine_.last_error().empty()) {
        lines.push_back("Last error: " + engine_.last_error());
    }
    return lines;
}

// One word for "what is on screen right now". The settings window shows this
// verbatim in its headline, so it must never be more optimistic than the engine:
// "active" means the mirror is on screen, not merely that the settings allow it.
std::string AppController::state_summary() const {
    // "active" means the mirror is really on screen - not that the settings allow it.
    if (engine_state_.mirror_active) return "active";
    if (!detector_.has_target()) return "idle";
    if (engine_.state() == win::MirrorState::Unsupported) return "unsupported";
    return win::suspend_reason_name(engine_state_.suspend);
}

std::string AppController::status_line() const {
    const Settings& settings = store_.settings();
    const std::string premiere = premiere_summary();

    if (!settings.enabled) return "Azy Skin: off | Premiere Pro: " + premiere;
    if (suspended_manual_) return "Azy Skin: suspended | Premiere Pro: " + premiere;
    if (settings.appearance.theme == ThemeKey::Original) {
        return "Azy Skin: original (no skin) | Premiere Pro: " + premiere;
    }

    // What is on screen right now, in one word, plus the theme: this is the string the
    // tray tooltip and the log use, so a problem is diagnosable at a glance.
    return str_format("Azy Skin: %s, %s - %s | Premiere Pro: %s", theme_key_name(settings.appearance.theme),
                      treatment_summary().c_str(), state_summary().c_str(), premiere.c_str());
}

void AppController::update_tray() {
    if (!tray_.exists()) return;
    win::TrayIcon::ViewState view;
    view.skin_enabled = store_.settings().enabled;
    view.suspended = suspended_manual_ || performance_.suspended();
    view.start_with_windows = store_.settings().start_with_windows;
    view.theme = store_.settings().appearance.theme;
    view.status_line = status_line();
    view.elevation_mismatch = premiere_elevated_;
    tray_.update(view);
}

void AppController::update_settings_window_status() {
    if (!settings_window_.visible()) return;
    win::SettingsWindow::Status status;
    status.host = win::host_info().os_name;
    status.premiere = premiere_summary();
    // The headline reports the engine's own state, not the configuration: a window
    // that says "active" while nothing is on screen is worse than useless.
    status.state = state_summary();
    status.lines = diagnostics_lines();
    status.treatment = treatment_summary();
    status.safe_mode = safe_mode_;
    status.safe_mode_note = safe_mode_
                                ? "Safe mode is active: " + store_.settings().safe_mode_reason +
                                      ". The skin is not applied until you re-enable it in Settings > Advanced."
                                : std::string();
    settings_window_.refresh(store_.settings(), status, store_.settings().enabled,
                             suspended_manual_ || performance_.suspended());
}

void AppController::restart_elevated() {
    // Premiere running at a higher integrity level than Azy is the one situation Azy
    // cannot work around: Windows does not let a lower-integrity process place a
    // window above a higher-integrity one, so the mirror would sit behind Premiere. Running Azy elevated as well is the fix, and it needs the
    // user's consent - one UAC prompt, then this instance steps aside.
    const std::wstring exe = win::executable_path().wstring();
    const HINSTANCE started = ShellExecuteW(nullptr, L"runas", exe.c_str(), L"--tray", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(started) <= 32) {
        log_warn("restarting with administrator rights was cancelled or refused");
        tray_.notify(L"Azy Skin", L"Restarting as administrator was cancelled. Premiere stays unskinned "
                                  L"while it runs elevated.");
        return;
    }
    log_info("restarting with administrator rights so Azy can draw above an elevated Premiere");
    request_exit(0);
}

void AppController::open_log_file() const {
    const std::wstring path = to_wide_path(Logger::instance().path());
    if (path.empty()) return;
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void AppController::stop_observers() {
    watch_.set_paused(true);
    watch_.set_notify(nullptr);
    watch_.stop();
    detector_.stop();
    settings_watcher_.stop();
}

void AppController::shutdown() {
    if (shutting_down_) return;
    shutting_down_ = true;
    log_info("Azy Skin shutting down - stopping the capture and releasing every GPU resource");

    stop_observers();
    if (window_ != nullptr) KillTimer(window_, kSyncTimerId);

    // Release everything: Premiere must be unaffected by Azy having run, and closing
    // Azy must never require closing Premiere.
    engine_.shutdown();
    tracker_.clear();

    settings_window_.destroy();
    tray_.destroy();

    persist_failure_state();
    store_.save();

    if (window_ != nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    // One line of session statistics: how much work the skin actually did. A high
    // number here without user activity would mean the event filtering is broken.
    log_debug("session totals: %llu skin applies, %llu mirror presentation(s), %llu captured frame(s), "
              "%llu detector scan(s)",
              engine_state_.applies, engine_.mirror_stats().presents, engine_.capture_status().frames,
              detector_.stats().scans);
    log_info("Azy Skin stopped");
    Logger::instance().flush_pending();
}

}  // namespace app
}  // namespace azy
