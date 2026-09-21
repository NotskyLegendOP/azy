// Azy Skin — portable core: lightweight logger.
//
// Deliberately small and non-chatty: state changes are logged, steady-state
// ticks are not (see the repeat-suppression below, which collapses a repeated
// message into a single "… (xN)" line instead of one line per frame).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace azy {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

const char* log_level_name(LogLevel level);

class Logger {
public:
    static Logger& instance();

    // Opens the log file, rotating it first when it is larger than max_bytes.
    // Safe to call when file logging is unavailable: Azy keeps running, it just
    // logs to the debugger (OutputDebugString) instead.
    bool open(const std::filesystem::path& file, size_t max_bytes = 512 * 1024);

    void close();
    bool is_open() const { return open_; }
    void set_min_level(LogLevel level) { min_level_ = level; }
    LogLevel min_level() const { return min_level_; }

    void write(LogLevel level, const std::string& message);

    // Flushes a pending "… (xN)" summary. Called on shutdown and on toggle.
    void flush_pending();

    const std::filesystem::path& path() const { return path_; }

private:
    Logger() = default;
    std::string timestamp() const;

    bool open_ = false;
    LogLevel min_level_ = LogLevel::Info;
    std::filesystem::path path_;
    std::size_t bytes_written_ = 0;
    std::size_t max_bytes_ = 512 * 1024;

    // Repeat suppression.
    std::string last_message_;
    int repeat_count_ = 0;
    void emit(const std::string& line);
};

// Convenience wrappers. printf-style formatting, no allocation when the level
// is below the configured minimum.
void log_debug(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);

}  // namespace azy
