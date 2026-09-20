// Azy Skin — Win32 layer: the input-safety contract.
//
// Requirement: every pixel Azy puts on screen must be invisible to the mouse and
// keyboard. That is achieved structurally rather than by filtering events after
// the fact — the composition surface is created with WS_EX_TRANSPARENT (hit
// testing passes through), WS_EX_NOACTIVATE (it can never be activated or take
// focus) and WS_EX_TOOLWINDOW (no taskbar entry, no Alt+Tab entry).
//
// Code in this file checks that contract at runtime, so a mistake (or a future
// Windows build that behaves differently) is caught and reported instead of
// silently stealing a click from the timeline.
#pragma once

#include <string>

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {
namespace input_guard {

// Extended styles every Azy visual surface must have.
constexpr LONG_PTR kRequiredExStyles =
    WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

// Verifies click-through + no-activate on a live window. Returns false and
// fills `error` when the window could ever receive a mouse click or take focus.
bool verify(HWND hwnd, std::string* error);

// Human readable description of the guarantee, used in the log at startup.
const char* contract_description();

// Diagnostics: how many times Windows asked us to hit-test the surface. Must
// stay 0 in practice, because WS_EX_TRANSPARENT answers before our window
// procedure is ever called.
void note_hit_test();
unsigned long long hit_test_count();

}  // namespace input_guard
}  // namespace win
}  // namespace azy
