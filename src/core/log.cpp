#include "azy/core/log.hpp"
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include <ctime>

#include "azy/core/strings.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace azy {
namespace {

std::string vformat(const char* fmt, va_list args) {
    char stack_buf[1024];
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, copy);
    va_end(copy);
    if (needed < 0) return std::string();
    if (static_cast<size_t>(needed) < sizeof(stack_buf)) return std::string(stack_buf);
    std::string big(static_cast<size_t>(needed) + 1, '\0');
    std::vsnprintf(&big[0], big.size(), fmt, args);
    big.resize(static_cast<size_t>(needed));
    return big;
}

}  // namespace

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO ";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

bool Logger::open(const std::filesystem::path& file, size_t max_bytes) {
    close();
    path_ = file;
    max_bytes_ = max_bytes < 4096 ? 4096 : max_bytes;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    // Rotate once (no chains of old logs): azy.log -> azy.log.1
    std::error_code size_ec;
    const auto size = std::filesystem::file_size(file, size_ec);
    if (!size_ec && size > max_bytes_) {
        std::filesystem::path old = file;
        old += ".1";
        std::filesystem::remove(old, ec);
        std::filesystem::rename(file, old, ec);
    }

    std::FILE* probe = std::fopen(file.string().c_str(), "ab");
    if (!probe) {
        open_ = false;
        return false;
    }
    std::fseek(probe, 0, SEEK_END);
    const long end = std::ftell(probe);
    bytes_written_ = end > 0 ? static_cast<size_t>(end) : 0;
    std::fclose(probe);
    open_ = true;
    return true;
}

void Logger::close() {
    flush_pending();
    open_ = false;
    last_message_.clear();
    repeat_count_ = 0;
}

std::string Logger::timestamp() const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

void Logger::emit(const std::string& line) {
    if (open_) {
        // Stop growing once the cap is reached; the file rotates next launch.
        if (bytes_written_ + line.size() + 1 > max_bytes_) return;
        if (std::FILE* f = std::fopen(path_.string().c_str(), "ab")) {
            std::fwrite(line.data(), 1, line.size(), f);
            std::fputc('\n', f);
            std::fclose(f);
            bytes_written_ += line.size() + 1;
        }
    }
#ifdef _WIN32
    else {
        std::string wide_input = line + "\n";
        const std::wstring wide = utf8_to_utf16(wide_input);
        OutputDebugStringW(wide.c_str());
    }
#endif
}

void Logger::flush_pending() {
    if (repeat_count_ > 0 && !last_message_.empty()) {
        const std::string summary =
            str_format("%s [repeated %d times]", last_message_.c_str(), repeat_count_);
        emit(timestamp() + " " + summary);
        repeat_count_ = 0;
    }
}

void Logger::write(LogLevel level, const std::string& message) {
    if (level < min_level_) return;

    // Collapse identical consecutive messages: Premiere state changes usually
    // arrive in bursts, and a log full of identical lines is useless.
    if (message == last_message_) {
        ++repeat_count_;
        return;
    }
    flush_pending();
    last_message_ = message;
    repeat_count_ = 0;
    emit(timestamp() + " " + log_level_name(level) + " " + message);
}

void log_debug(const char* fmt, ...) {
    Logger& logger = Logger::instance();
    if (LogLevel::Debug < logger.min_level()) return;
    va_list args;
    va_start(args, fmt);
    logger.write(LogLevel::Debug, vformat(fmt, args));
    va_end(args);
}

void log_info(const char* fmt, ...) {
    Logger& logger = Logger::instance();
    if (LogLevel::Info < logger.min_level()) return;
    va_list args;
    va_start(args, fmt);
    logger.write(LogLevel::Info, vformat(fmt, args));
    va_end(args);
}

void log_warn(const char* fmt, ...) {
    Logger& logger = Logger::instance();
    if (LogLevel::Warn < logger.min_level()) return;
    va_list args;
    va_start(args, fmt);
    logger.write(LogLevel::Warn, vformat(fmt, args));
    va_end(args);
}

void log_error(const char* fmt, ...) {
    Logger& logger = Logger::instance();
    if (LogLevel::Error < logger.min_level()) return;
    va_list args;
    va_start(args, fmt);
    logger.write(LogLevel::Error, vformat(fmt, args));
    va_end(args);
}

}  // namespace azy
