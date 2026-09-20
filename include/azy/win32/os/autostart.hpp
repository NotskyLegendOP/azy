// Azy Skin — Win32 layer: "Start with Windows".
//
// Implemented with a single HKCU\...\Run value. No service, no scheduled task,
// no helper process: the key is the entire mechanism, and removing it (which
// the uninstaller does) leaves nothing behind.
#pragma once

#include <string>

namespace azy {
namespace win {

// Writes/removes the Run entry pointing at this executable with --tray.
bool set_autostart_enabled(bool enabled, std::string* error = nullptr);

// False when the entry is absent (also used to reconcile the setting after the
// user removes it manually).
bool autostart_enabled();

// True when we were launched by the Run entry (or with an explicit --tray flag).
bool launched_at_startup();

}  // namespace win
}  // namespace azy
