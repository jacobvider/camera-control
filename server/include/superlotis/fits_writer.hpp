// SPDX-License-Identifier: MIT
// Dependency-free writer for single-HDU 16-bit unsigned FITS images
// (FITS Standard 4.0). Unsigned data is stored as BITPIX=16 with BZERO=32768,
// which is how CFITSIO, astropy and DS9 represent uint16 images.
#pragma once

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "superlotis/types.hpp"

namespace superlotis::fits {

/// A FITS header keyword value.
using Value = std::variant<bool, long long, double, std::string>;

struct Card {
    std::string keyword;
    Value value;
    std::string comment;
};

class Header {
public:
    /// Adds or replaces a keyword. Throws std::invalid_argument for keywords
    /// that are not valid FITS (1-8 chars of A-Z, 0-9, '-', '_') or reserved
    /// structural keywords (SIMPLE, BITPIX, NAXIS*, BZERO, BSCALE, END...).
    // One exact overload per value type: implicit conversions into a
    // std::variant (e.g. double -> int, const char* -> bool) are a classic trap.
    void set(const std::string& keyword, bool value, std::string comment = {});
    void set(const std::string& keyword, int value, std::string comment = {});
    void set(const std::string& keyword, long long value, std::string comment = {});
    void set(const std::string& keyword, double value, std::string comment = {});
    void set(const std::string& keyword, const std::string& value, std::string comment = {});
    void set(const std::string& keyword, const char* value, std::string comment = {});
    void add_history(const std::string& text);
    void add_comment(const std::string& text);

    [[nodiscard]] const std::vector<Card>& cards() const noexcept { return cards_; }
    [[nodiscard]] const std::vector<std::string>& commentary() const noexcept { return commentary_; }

private:
    void set_value(const std::string& keyword, Value value, std::string comment);
    std::vector<Card> cards_;
    std::vector<std::string> commentary_;  // pre-formatted HISTORY/COMMENT cards
};

/// Formats one 80-character header card. Exposed for testing.
std::string format_card(const Card& card);

/// Serializes a complete FITS file (header + data, each padded to 2880 bytes).
std::vector<char> encode(const Frame& frame, const Header& header);

/// Writes `frame` to `path` atomically (temp file + rename). Refuses to
/// overwrite an existing file. Throws std::runtime_error on failure.
void write(const std::filesystem::path& path, const Frame& frame, const Header& header);

}  // namespace superlotis::fits
