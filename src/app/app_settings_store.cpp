#include "azy/app/app_settings_store.hpp"
#include <filesystem>
#include <string>

#include "azy/core/log.hpp"
#include "azy/core/strings.hpp"
#include "azy/win32/os/win_util.hpp"

namespace azy {
namespace app {
namespace {

unsigned long long file_write_time(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER stamp{};
    stamp.LowPart = data.ftLastWriteTime.dwLowDateTime;
    stamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return stamp.QuadPart;
}

}  // namespace

SettingsStore::LoadResult SettingsStore::load() {
    LoadResult result;
    path_ = win::settings_path();

    std::string text;
    if (win::read_text_file(path_, text)) {
        result.file_existed = true;
        settings_ = Settings::from_ini(text, &result.warnings);
        for (const std::string& warning : result.warnings) {
            log_warn("settings: %s", warning.c_str());
        }
    } else {
        settings_ = Settings();
        settings_.clamp();
    }
    last_write_time_ = file_write_time(path_);
    loaded_ = true;
    log_info("settings loaded from %s", win::to_utf8(path_.wstring()).c_str());
    return result;
}

bool SettingsStore::save(std::string* error) {
    settings_.clamp();
    std::string local_error;
    if (!win::write_text_file_atomic(path_, settings_.to_ini())) {
        local_error = "could not write " + win::to_utf8(path_.wstring());
        last_error_ = local_error;
        if (error) *error = local_error;
        log_warn("settings: %s", local_error.c_str());
        return false;
    }
    last_write_time_ = file_write_time(path_);
    last_error_.clear();
    return true;
}

void SettingsStore::reset_to_defaults() {
    // Keep the "start with Windows" preference: removing it would silently
    // disable autostart, which is not what "reset configuration" means.
    const bool keep_autostart = settings_.start_with_windows;
    settings_ = Settings();
    settings_.start_with_windows = keep_autostart;
    settings_.clamp();
    log_info("configuration reset to defaults");
}

bool SettingsStore::changed_on_disk() const {
    if (!loaded_) return false;
    const unsigned long long current = file_write_time(path_);
    if (current == 0) return false;
    return current != last_write_time_;
}

}  // namespace app
}  // namespace azy
