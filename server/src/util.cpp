// SPDX-License-Identifier: MIT
#include "superlotis/util.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace superlotis::util {

std::string_view trim(std::string_view s) noexcept {
    const char* ws = " \t\r\n\f\v";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> split_whitespace(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const std::size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

std::optional<double> parse_double(std::string_view s) noexcept {
    const std::string buf(trim(s));
    if (buf.empty()) return std::nullopt;
    // Reject hex floats, "inf", "nan" and friends: only digits, sign, '.', exponent.
    for (char c : buf) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' ||
              c == 'E'))
            return std::nullopt;
    }
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(buf.c_str(), &end);
    if (errno != 0 || end != buf.c_str() + buf.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<long long> parse_int(std::string_view s) noexcept {
    const std::string buf(trim(s));
    if (buf.empty()) return std::nullopt;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(buf.c_str(), &end, 10);
    if (errno != 0 || end != buf.c_str() + buf.size()) return std::nullopt;
    return v;
}

std::optional<bool> parse_bool(std::string_view s) noexcept {
    const std::string v = to_lower(trim(s));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return std::nullopt;
}

namespace {
std::tm to_utc_tm(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm out{};
    gmtime_r(&t, &out);
    return out;
}
}  // namespace

std::string iso8601_utc(std::chrono::system_clock::time_point tp) {
    const std::tm tm = to_utc_tm(tp);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count() < 0 ? ms.count() + 1000 : ms.count()));
    return buf;
}

std::string compact_utc(std::chrono::system_clock::time_point tp) {
    const std::tm tm = to_utc_tm(tp);
    char buf[24];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%S", &tm);
    return buf;
}

std::string date_utc(std::chrono::system_clock::time_point tp) {
    const std::tm tm = to_utc_tm(tp);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y%m%d", &tm);
    return buf;
}

std::string fixed(double value, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    return buf;
}

}  // namespace superlotis::util
