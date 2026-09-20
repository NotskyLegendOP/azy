#include "azy/win32/skin/skin_types.hpp"

namespace azy {
namespace win {

const char* suspend_reason_name(SuspendReason reason) {
    switch (reason) {
        case SuspendReason::None: return "active";
        case SuspendReason::SkinDisabled: return "skin disabled";
        case SuspendReason::OriginalTheme: return "original theme";
        case SuspendReason::NoWindow: return "no Premiere window";
        case SuspendReason::Minimized: return "Premiere minimized";
        case SuspendReason::Inactive: return "Premiere inactive";
        case SuspendReason::Dragging: return "window being dragged";
        case SuspendReason::Moving: return "window just moved";
        case SuspendReason::FullscreenTransition: return "fullscreen transition";
    }
    return "unknown";
}

}  // namespace win
}  // namespace azy
