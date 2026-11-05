// SPDX-License-Identifier: MIT
// Small string/number helpers. Strict parsing: the whole string must be consumed.
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace superlotis::util {

std::string_view trim(std::string_view s) noexcept;
std::string to_lower(std::string_view s);
std::vector<std::string> split_whitespace(std::string_view s);

/// Parses a finite decimal number; rejects trailing garbage, NaN and infinity.
std::optional<double> parse_double(std::string_view s) noexcept;
/// Parses a base-10 integer; rejects trailing garbage and overflow.
std::optional<long long> parse_int(std::string_view s) noexcept;
std::optional<bool> parse_bool(std::string_view s) noexcept;

/// ISO-8601 UTC timestamp with millisecond precision, e.g. 2026-11-05T18:02:03.123
std::string iso8601_utc(std::chrono::system_clock::time_point tp);
/// Compact UTC timestamp for filenames, e.g. 20261105T180203
std::string compact_utc(std::chrono::system_clock::time_point tp);
/// UTC date, e.g. 20261105
std::string date_utc(std::chrono::system_clock::time_point tp);

/// Formats a double with a fixed number of decimals ("%.Nf").
std::string fixed(double value, int decimals);

}  // namespace superlotis::util
