// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>

#include "superlotis/fits_writer.hpp"

using namespace superlotis;
namespace fs = std::filesystem;

namespace {
Frame small_frame() {
    Frame f;
    f.width = 3;
    f.height = 2;
    f.pixels = {0, 1, 32767, 32768, 65534, 65535};
    return f;
}
}  // namespace

TEST(FitsCard, FormatsFixedFormatValues) {
    std::string expected = "EXPTIME = " + std::string(17, ' ') + "1.5 / s";
    expected.resize(80, ' ');
    EXPECT_EQ(fits::format_card({"EXPTIME", 1.5, "s"}), expected);
    const std::string b = fits::format_card({"LOCKED", true, ""});
    EXPECT_EQ(b.size(), 80u);
    EXPECT_EQ(b[29], 'T');  // logical values sit in column 30
    const std::string i = fits::format_card({"NAXIS1", 2048LL, ""});
    EXPECT_EQ(i.substr(26, 4), "2048");
}

TEST(FitsCard, IntegralDoublesKeepADecimalPoint) {
    EXPECT_NE(fits::format_card({"EXPTIME", 2.0, ""}).find("2.0"), std::string::npos);
}

TEST(FitsCard, StringsArePaddedAndQuotesEscaped) {
    EXPECT_EQ(fits::format_card({"OBJECT", std::string("M31"), ""}).substr(0, 20), "OBJECT  = 'M31     '");
    EXPECT_NE(fits::format_card({"OBSERVER", std::string("O'Neil"), ""}).find("'O''Neil '"),
              std::string::npos);
}

TEST(FitsCard, LongStringsAreTruncatedWithoutSplittingAnEscapedQuote) {
    const std::string card = fits::format_card({"NOTE", std::string(67, 'a') + "'b", ""});
    EXPECT_EQ(card.size(), 80u);
    const auto inner = card.substr(11, card.rfind('\'') - 11);
    std::size_t quotes = 0;
    for (char c : inner) quotes += (c == '\'');
    EXPECT_EQ(quotes % 2, 0u) << card;
}

TEST(FitsHeader, RejectsInvalidAndReservedKeywords) {
    fits::Header h;
    EXPECT_THROW(h.set("lowercase", 1), std::invalid_argument);
    EXPECT_THROW(h.set("TOOLONGKEY", 1), std::invalid_argument);
    EXPECT_THROW(h.set("BITPIX", 8), std::invalid_argument);
    EXPECT_THROW(h.set("BZERO", 0), std::invalid_argument);
    EXPECT_THROW(h.set("BAD", std::nan("")), std::invalid_argument);
    EXPECT_NO_THROW(h.set("DATE-OBS", "2026-01-01T00:00:00"));
}

TEST(FitsHeader, SetReplacesExistingKeyword) {
    fits::Header h;
    h.set("FOO", 1);
    h.set("FOO", 2);
    ASSERT_EQ(h.cards().size(), 1u);
    EXPECT_EQ(std::get<long long>(h.cards()[0].value), 2);
}

TEST(FitsHeader, ValueTypesAreNotSilentlyConverted) {
    fits::Header h;
    h.set("D", 1.25);
    h.set("B", true);
    h.set("S", "text");
    EXPECT_TRUE(std::holds_alternative<double>(h.cards()[0].value));
    EXPECT_TRUE(std::holds_alternative<bool>(h.cards()[1].value));
    EXPECT_TRUE(std::holds_alternative<std::string>(h.cards()[2].value));
}

TEST(FitsEncode, ProducesBlockAlignedBigEndianUnsignedData) {
    fits::Header h;
    h.set("TEST", 1);
    const auto bytes = fits::encode(small_frame(), h);
    ASSERT_EQ(bytes.size() % 2880, 0u);
    ASSERT_EQ(bytes.size(), 2 * 2880u);
    const std::string header(bytes.begin(), bytes.begin() + 2880);
    EXPECT_EQ(header.substr(0, 30), "SIMPLE  =                    T");
    EXPECT_NE(header.find("BZERO   =                32768"), std::string::npos);
    EXPECT_NE(header.find("END     "), std::string::npos);
    // stored = physical - 32768, big-endian
    auto at = [&](std::size_t px) {
        const auto hi = static_cast<unsigned char>(bytes[2880 + 2 * px]);
        const auto lo = static_cast<unsigned char>(bytes[2880 + 2 * px + 1]);
        return static_cast<int>(static_cast<std::int16_t>((hi << 8) | lo)) + 32768;
    };
    EXPECT_EQ(at(0), 0);
    EXPECT_EQ(at(1), 1);
    EXPECT_EQ(at(2), 32767);
    EXPECT_EQ(at(3), 32768);  // the original SHORT_IMG writer overflowed here
    EXPECT_EQ(at(5), 65535);
    for (std::size_t i = 2880 + 12; i < bytes.size(); ++i) ASSERT_EQ(bytes[i], '\0');
}

TEST(FitsEncode, RejectsInconsistentFrames) {
    Frame f = small_frame();
    f.pixels.pop_back();
    EXPECT_THROW(fits::encode(f, {}), std::invalid_argument);
}

TEST(FitsWrite, RefusesToOverwriteAndLeavesNoTempFiles) {
    const fs::path dir = fs::temp_directory_path() / ("slcam_fits_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const fs::path p = dir / "nested" / "a.fits";
    fits::write(p, small_frame(), {});
    EXPECT_TRUE(fs::exists(p));
    EXPECT_EQ(fs::file_size(p), 2 * 2880u);
    EXPECT_THROW(fits::write(p, small_frame(), {}), std::runtime_error);
    EXPECT_FALSE(fs::exists(p.string() + ".part"));
    fs::remove_all(dir);
}
