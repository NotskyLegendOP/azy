// Azy Skin — application layer: settings persistence.
//
// Owns the in-memory Settings, the INI file and the write-suppression logic
// that keeps Azy's own saves from being mistaken for user edits.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "azy/core/settings.hpp"

namespace azy {
namespace app {

class SettingsStore {
public:
    struct LoadResult {
        bool file_existed = false;
        std::vector<std::string> warnings;
    };

    // Loads settings.ini, falling back to defaults (and creating the file on the
    // first save) when it is missing or unreadable.
    LoadResult load();

    // Saves atomically and remembers the file's write time so our own write does
    // not trigger a reload.
    bool save(std::string* error = nullptr);

    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }

    void reset_to_defaults();

    // True when the file changed on disk since the last load/save.
    bool changed_on_disk() const;

    const std::filesystem::path& path() const { return path_; }
    std::string last_save_error() const { return last_error_; }

private:
    std::filesystem::path path_;
    Settings settings_;
    std::string last_error_;
    unsigned long long last_write_time_ = 0;
    bool loaded_ = false;
};

}  // namespace app
}  // namespace azy
