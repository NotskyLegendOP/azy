// Azy Skin — portable core: small string helpers shared by the settings
// parser, the logger and the Win32 layer. No Windows dependencies.
#pragma once

#include <string>
#include <vector>

namespace azy {

std::string trim(const std::string& s);
std::string to_lower(std::string s);
bool iequals(const std::string& a, const std::string& b);
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);

std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& parts, const char* sep);

// Strict-ish integer parse: the whole (trimmed) string must be numeric.
bool parse_int(const std::string& s, int& out);
bool parse_bool(const std::string& s, bool& out);  // true/1/yes/on, false/0/no/off

// UTF-8 <-> UTF-16 (portable implementation; the Win32 layer uses these
// instead of MultiByteToWideChar so behaviour is identical in unit tests).
std::wstring utf8_to_utf16(const std::string& s);
std::string utf16_to_utf8(const std::wstring& s);

// snprintf-style formatting into std::string.
std::string str_format(const char* fmt, ...);

}  // namespace azy
