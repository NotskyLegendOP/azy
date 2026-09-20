#include "azy/win32/skin/dwm_composer.hpp"

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

// DWMWA_COLOR_DEFAULT tells DWM to choose the colour itself, which is the
// correct "undo" for a colour we could not read back.
constexpr DWORD kColorDefault = 0xFFFFFFFF;

bool set_int(HWND hwnd, DWORD attribute, int value) {
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value)));
}

bool set_color(HWND hwnd, DWORD attribute, DWORD color) {
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &color, sizeof(color)));
}

bool set_bool(HWND hwnd, DWORD attribute, BOOL value) {
    return SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value)));
}

}  // namespace

bool dwm_query_dark_mode(HWND hwnd, BOOL& out) {
    BOOL value = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, dwm_attr::kUseImmersiveDarkMode, &value, sizeof(value)))) {
        out = value;
        return true;
    }
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, dwm_attr::kUseImmersiveDarkModeLegacy, &value, sizeof(value)))) {
        out = value;
        return true;
    }
    return false;
}

bool dwm_query_corner_preference(HWND hwnd, int& out) {
    int value = dwm_attr::kCornerDefault;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, dwm_attr::kWindowCornerPreference, &value, sizeof(value)))) {
        out = value;
        return true;
    }
    return false;
}

bool dwm_query_color(HWND hwnd, DWORD attribute, DWORD& out) {
    DWORD value = kColorDefault;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, attribute, &value, sizeof(value)))) {
        out = value;
        return true;
    }
    return false;
}

void DwmComposer::reset_state() { applied_ = FrameState{}; }

DwmComposer::ApplyResult DwmComposer::apply(const SkinTarget& target, const ThemePalette& palette,
                                           const FeatureSet& features, bool maximized_or_fullscreen) {
    ApplyResult result;
    if (target.hwnd == nullptr || !IsWindow(target.hwnd)) {
        result.error = "no valid window";
        result.any_failed = true;
        return result;
    }
    // Never write to a handle we cannot prove is still Premiere's: a recycled
    // handle would silently restyle an unrelated application's window.
    if (!window_belongs_to(target.hwnd, target.pid)) {
        result.error = "the window no longer belongs to Premiere";
        result.any_failed = true;
        return result;
    }

    // A different window: undo the previous one first so only one Premiere
    // window is ever modified.
    if (applied_.hwnd != nullptr && applied_.hwnd != target.hwnd) revert();

    if (applied_.hwnd == nullptr) {
        applied_.hwnd = target.hwnd;
        applied_.pid = target.pid;
    }

    // --- Dark frame (Windows 10 1809+) ------------------------------------
    if (features.dark_frame) {
        if (!applied_.dark) {
            applied_.dark_saved = dwm_query_dark_mode(target.hwnd, applied_.previous_dark);
            BOOL value = TRUE;
            bool ok = set_bool(target.hwnd, dwm_attr::kUseImmersiveDarkMode, value);
            if (!ok) {
                // Builds before 20H1 use attribute 19 for the same thing.
                ok = set_bool(target.hwnd, dwm_attr::kUseImmersiveDarkModeLegacy, value);
            }
            if (ok) {
                applied_.dark = true;
                result.dark_frame = true;
            } else {
                result.error = "dark title bar rejected";
                log_debug("dwm: dark title bar attribute not accepted for 0x%p",
                          reinterpret_cast<void*>(target.hwnd));
            }
        } else {
            result.dark_frame = true;
        }
    } else if (applied_.dark) {
        // The feature was switched off (performance mode, Safe Mode, a per-feature
        // override in settings.ini) while the window was styled. Leaving it on would
        // make the window disagree with everything Azy reports about itself, so the
        // original value goes back now rather than at detach time.
        const BOOL value = applied_.dark_saved ? applied_.previous_dark : FALSE;
        if (!set_bool(target.hwnd, dwm_attr::kUseImmersiveDarkMode, value)) {
            set_bool(target.hwnd, dwm_attr::kUseImmersiveDarkModeLegacy, value);
        }
        applied_.dark = false;
        result.dark_frame = false;
        log_debug("dwm: dark title bar removed (feature switched off)");
    }

    // --- Frame colours (Windows 11 22000+) --------------------------------
    if (features.frame_colors && palette.apply_frame_colors) {
        if (!applied_.colors) {
            applied_.caption_saved = dwm_query_color(target.hwnd, dwm_attr::kCaptionColor, applied_.previous_caption);
            applied_.border_saved = dwm_query_color(target.hwnd, dwm_attr::kBorderColor, applied_.previous_border);
            applied_.text_saved = dwm_query_color(target.hwnd, dwm_attr::kTextColor, applied_.previous_text);

            const DWORD caption = dwm_attr::colorref(palette.frame_caption.r, palette.frame_caption.g,
                                                     palette.frame_caption.b);
            const DWORD border = dwm_attr::colorref(palette.frame_border.r, palette.frame_border.g,
                                                    palette.frame_border.b);
            const DWORD text = dwm_attr::colorref(palette.frame_text.r, palette.frame_text.g, palette.frame_text.b);

            const bool caption_ok = set_color(target.hwnd, dwm_attr::kCaptionColor, caption);
            const bool border_ok = set_color(target.hwnd, dwm_attr::kBorderColor, border);
            const bool text_ok = set_color(target.hwnd, dwm_attr::kTextColor, text);
            applied_.colors = caption_ok || border_ok || text_ok;
            result.frame_colors = applied_.colors;
            if (!applied_.colors) {
                result.error += "frame colours rejected;";
                log_debug("dwm: frame colour attributes not accepted for 0x%p",
                          reinterpret_cast<void*>(target.hwnd));
            }
        } else {
            result.frame_colors = true;
        }
    }

    // --- Corner rounding (Windows 11 22000+) ------------------------------
    // Maximised/fullscreen windows are never rounded by Windows itself, so Azy
    // does not round them either.
    const bool want_rounded = features.rounded_frame && !maximized_or_fullscreen &&
                              target.effective_radius_dip(palette.corner_radius_dip) > 0;
    if (want_rounded && !applied_.rounded) {
        applied_.corner_saved = dwm_query_corner_preference(target.hwnd, applied_.previous_corner);
        if (set_int(target.hwnd, dwm_attr::kWindowCornerPreference, dwm_attr::kCornerRoundSmall)) {
            applied_.rounded = true;
            result.rounded_frame = true;
        } else {
            result.error += "corner preference rejected;";
        }
    } else if (!want_rounded && applied_.rounded) {
        // e.g. the window was maximised since the last apply.
        set_int(target.hwnd, dwm_attr::kWindowCornerPreference,
                applied_.corner_saved ? applied_.previous_corner : dwm_attr::kCornerDefault);
        applied_.rounded = false;
    } else {
        result.rounded_frame = applied_.rounded;
    }

    // --- Backdrop (Windows 11 22H2+, experimental, opt-in) ----------------
    if (features.frame_backdrop) {
        if (!applied_.backdrop) {
            applied_.backdrop_saved = SUCCEEDED(DwmGetWindowAttribute(
                target.hwnd, dwm_attr::kSystemBackdropType, &applied_.previous_backdrop,
                sizeof(applied_.previous_backdrop)));
            if (set_int(target.hwnd, dwm_attr::kSystemBackdropType, dwm_attr::kBackdropMica)) {
                applied_.backdrop = true;
                result.backdrop = true;
            } else {
                result.error += "backdrop rejected;";
            }
        } else {
            result.backdrop = true;
        }
    }

    result.any_failed = !result.error.empty();
    if (result.any_failed) {
        while (!result.error.empty() && result.error.back() == ';') result.error.pop_back();
    }
    return result;
}

void DwmComposer::revert() {
    if (applied_.hwnd == nullptr) return;
    HWND hwnd = applied_.hwnd;

    // Window already gone: nothing to restore, and no DWM call is safe. The same
    // goes for a handle that has been recycled by another window - restoring
    // "Premiere's" previous colours onto somebody else's window would be worse
    // than leaving it alone.
    if (!window_belongs_to(hwnd, applied_.pid)) {
        reset_state();
        return;
    }

    if (applied_.backdrop) {
        set_int(hwnd, dwm_attr::kSystemBackdropType,
                applied_.backdrop_saved ? applied_.previous_backdrop : dwm_attr::kBackdropAuto);
    }
    if (applied_.rounded) {
        set_int(hwnd, dwm_attr::kWindowCornerPreference,
                applied_.corner_saved ? applied_.previous_corner : dwm_attr::kCornerDefault);
    }
    if (applied_.colors) {
        set_color(hwnd, dwm_attr::kCaptionColor, applied_.caption_saved ? applied_.previous_caption : kColorDefault);
        set_color(hwnd, dwm_attr::kBorderColor, applied_.border_saved ? applied_.previous_border : kColorDefault);
        set_color(hwnd, dwm_attr::kTextColor, applied_.text_saved ? applied_.previous_text : kColorDefault);
    }
    if (applied_.dark) {
        const BOOL value = applied_.dark_saved ? applied_.previous_dark : FALSE;
        if (!set_bool(hwnd, dwm_attr::kUseImmersiveDarkMode, value)) {
            set_bool(hwnd, dwm_attr::kUseImmersiveDarkModeLegacy, value);
        }
    }

    log_debug("dwm: frame attributes restored on 0x%p", reinterpret_cast<void*>(hwnd));
    reset_state();
}

}  // namespace win
}  // namespace azy
