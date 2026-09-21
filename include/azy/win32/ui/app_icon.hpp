// Azy Skin — Win32 layer: the application icon.
//
// Drawn once at runtime with GDI instead of shipping an .ico: a dark rounded
// square with a lighter "A". It keeps the repository free of binary assets and
// gives the tray, the message box and the settings window the same identity.
#pragma once

#include "azy/win32/os/win_compat.hpp"

namespace azy {
namespace win {

// Returns a shared icon owned by Azy (do not destroy). Never null; falls back to
// the generic application icon if drawing fails.
HICON app_icon();

// Frees the icon created by app_icon() (called at shutdown).
void destroy_app_icon();

}  // namespace win
}  // namespace azy
