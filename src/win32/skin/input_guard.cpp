#include "azy/win32/skin/input_guard.hpp"
#include <atomic>
#include <string>

#include "azy/core/strings.hpp"

namespace azy {
namespace win {
namespace input_guard {
namespace {

std::atomic<unsigned long long> g_hit_tests{0};

}  // namespace

bool verify(HWND hwnd, std::string* error) {
    if (hwnd == nullptr) {
        if (error) *error = "surface window was not created";
        return false;
    }

    const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR missing = kRequiredExStyles & ~ex_style;
    if (missing != 0) {
        if (error) {
            *error = str_format("surface is missing required window styles (0x%llX)",
                                static_cast<unsigned long long>(missing));
        }
        return false;
    }

    // WS_EX_TRANSPARENT documented behaviour: the window is skipped during hit
    // testing, so the click goes to whatever is underneath (Premiere). Together
    // with WS_EX_NOACTIVATE the surface cannot be focused either.
    return true;
}

const char* contract_description() {
    return "visual surfaces: WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW "
           "(click-through, never activated, never in Alt+Tab)";
}

void note_hit_test() { g_hit_tests.fetch_add(1); }
unsigned long long hit_test_count() { return g_hit_tests.load(); }

}  // namespace input_guard
}  // namespace win
}  // namespace azy
