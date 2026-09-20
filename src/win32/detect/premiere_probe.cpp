#include "azy/win32/detect/premiere_probe.hpp"
#include <filesystem>
#include <string>
#include <vector>

#include <winver.h>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace win {
namespace {

struct WindowCandidate {
    HWND hwnd = nullptr;
    long long area = 0;
    bool has_caption = false;
    bool is_maximized = false;
};

BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM param) {
    auto* candidates = reinterpret_cast<std::vector<WindowCandidate>*>(param);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (is_window_cloaked(hwnd)) return TRUE;
    if (is_window_minimized(hwnd)) return TRUE;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    const long long width = rect.right - rect.left;
    const long long height = rect.bottom - rect.top;
    if (width < 160 || height < 120) return TRUE;  // tooltips, hidden helpers

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CHILD) != 0) return TRUE;

    const std::wstring title = window_text(hwnd);
    const std::wstring klass = window_class_name(hwnd);

    WindowCandidate candidate;
    candidate.hwnd = hwnd;
    candidate.area = width * height;
    candidate.has_caption = (style & WS_CAPTION) == WS_CAPTION;
    candidate.is_maximized = is_window_maximized(hwnd) != FALSE;

    // A splash screen or a "Loading..." window is not the editor UI. Requiring
    // either a title or a window style that looks like a frame keeps us from
    // skinning a splash into something odd; it is retried on the next event
    // anyway, and the title usually appears within a few hundred ms.
    (void)title;
    (void)klass;
    candidates->push_back(candidate);
    return TRUE;
}

struct EnumPidParam {
    unsigned long pid = 0;
    std::vector<HWND>* out = nullptr;
};

BOOL CALLBACK enum_pid_proc(HWND hwnd, LPARAM param) {
    auto* data = reinterpret_cast<EnumPidParam*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (static_cast<unsigned long>(pid) != data->pid) return TRUE;
    data->out->push_back(hwnd);
    return TRUE;
}

}  // namespace

const std::vector<std::wstring>& premiere_executable_names_lower() {
    // The Headless variant is Adobe's encoder/watchdog host; it is recognized so
    // it can be explicitly ignored (it has no user interface to skin).
    static const std::vector<std::wstring> names = {
        L"adobe premiere pro.exe",
        L"adobe premiere pro beta.exe",
        L"adobe premiere pro headless.exe",
    };
    return names;
}

const std::vector<std::string>& premiere_executable_names_utf8() {
    static const std::vector<std::string> names = {
        "adobe premiere pro.exe",
        "adobe premiere pro beta.exe",
        "adobe premiere pro headless.exe",
    };
    return names;
}

bool PremiereProbe::read_file_version(const std::wstring& path, Version& out, std::string& raw_text) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return false;
    std::vector<unsigned char> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return false;

    // Preferred: the numeric fixed info, which is locale independent.
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixed_length = 0;
    if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_length) &&
        fixed != nullptr && fixed_length >= sizeof(VS_FIXEDFILEINFO) && fixed->dwSignature == 0xFEEF04BD) {
        out.major = static_cast<int>(HIWORD(fixed->dwFileVersionMS));
        out.minor = static_cast<int>(LOWORD(fixed->dwFileVersionMS));
        out.patch = static_cast<int>(HIWORD(fixed->dwFileVersionLS));
        out.build = static_cast<int>(LOWORD(fixed->dwFileVersionLS));
        return true;
    }

    // Fallback: the string form, which some installers populate instead.
    struct Translation {
        WORD language;
        WORD codepage;
    };
    Translation* translation = nullptr;
    UINT translation_length = 0;
    const wchar_t* query = L"\\StringFileInfo\\040904B0\\FileVersion";
    if (VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation),
                       &translation_length) &&
        translation != nullptr && translation_length >= sizeof(Translation)) {
        wchar_t dynamic_query[128];
        std::swprintf(dynamic_query, 128, L"\\StringFileInfo\\%04x%04x\\FileVersion", translation->language,
                      translation->codepage);
        query = dynamic_query;
    }
    wchar_t* text = nullptr;
    UINT text_length = 0;
    if (VerQueryValueW(buffer.data(), query, reinterpret_cast<void**>(&text), &text_length) && text != nullptr &&
        text_length > 1) {
        raw_text = to_utf8(std::wstring(text, text_length - 1));
        return parse_version(raw_text, out);
    }
    return false;
}

bool PremiereProbe::read_identity(unsigned long pid, ProcessRecord& out) {
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return false;

    std::vector<wchar_t> path_buffer(1024);
    DWORD length = static_cast<DWORD>(path_buffer.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path_buffer.data(), &length)) return false;

    const std::wstring path(path_buffer.data(), length);
    const std::filesystem::path fs_path(path);
    const std::wstring file_name = fs_path.filename().wstring();
    if (!is_premiere_executable(to_utf8(file_name))) return false;

    Version version;
    std::string raw;
    const bool known = read_file_version(path, version, raw);

    out.pid = pid;
    out.exe_name = file_name;
    out.exe_path = path;
    out.version_known = known;
    out.file_version = known ? version : Version{};
    out.product = make_product_info(to_utf8(file_name), to_utf8(path), known, version);
    return true;
}

std::vector<HWND> PremiereProbe::find_top_level_windows(unsigned long pid) {
    std::vector<HWND> windows;
    EnumPidParam param;
    param.pid = pid;
    param.out = &windows;
    EnumWindows(&enum_pid_proc, reinterpret_cast<LPARAM>(&param));
    return windows;
}

HWND PremiereProbe::find_main_window(unsigned long pid) {
    std::vector<WindowCandidate> candidates;
    EnumWindows(&enum_windows_proc, reinterpret_cast<LPARAM>(&candidates));

    WindowCandidate best;
    for (const WindowCandidate& candidate : candidates) {
        DWORD candidate_pid = 0;
        GetWindowThreadProcessId(candidate.hwnd, &candidate_pid);
        if (static_cast<unsigned long>(candidate_pid) != pid) continue;
        // Prefer a framed, maximized, large window: that is the editor UI, not a
        // progress window or a floating panel.
        const int score = (candidate.has_caption ? 1 : 0) + (candidate.is_maximized ? 1 : 0);
        const int best_score = (best.has_caption ? 1 : 0) + (best.is_maximized ? 1 : 0);
        if (best.hwnd == nullptr || score > best_score ||
            (score == best_score && candidate.area > best.area)) {
            best = candidate;
        }
    }
    return best.hwnd;
}

}  // namespace win
}  // namespace azy
