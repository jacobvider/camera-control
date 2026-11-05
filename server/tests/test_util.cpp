// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include "superlotis/types.hpp"
#include "superlotis/util.hpp"

using namespace superlotis;

TEST(Util, ParseDoubleIsStrict) {
    EXPECT_EQ(util::parse_double("2.5"), 2.5);
    EXPECT_EQ(util::parse_double(" -60 "), -60.0);
    EXPECT_EQ(util::parse_double("1e3"), 1000.0);
    EXPECT_FALSE(util::parse_double("2.5s"));
    EXPECT_FALSE(util::parse_double(""));
    EXPECT_FALSE(util::parse_double("nan"));
    EXPECT_FALSE(util::parse_double("inf"));
    EXPECT_FALSE(util::parse_double("0x10"));
    EXPECT_FALSE(util::parse_double("1e999"));
}

TEST(Util, ParseIntIsStrict) {
    EXPECT_EQ(util::parse_int("42"), 42);
    EXPECT_FALSE(util::parse_int("4.2"));
    EXPECT_FALSE(util::parse_int("99999999999999999999"));
}

TEST(Util, SplitWhitespace) {
    EXPECT_EQ(util::split_whitespace("  a  b\tc "), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_TRUE(util::split_whitespace("   ").empty());
}

TEST(Util, Timestamps) {
    const auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'123LL));
    EXPECT_EQ(util::iso8601_utc(tp), "2023-11-14T22:13:20.123");
    EXPECT_EQ(util::compact_utc(tp), "20231114T221320");
    EXPECT_EQ(util::date_utc(tp), "20231114");
}

TEST(Types, ParseShutterAndGainAcceptNamesAndLegacyCodes) {
    EXPECT_EQ(parse_shutter_mode("normal"), ShutterMode::Normal);
    EXPECT_EQ(parse_shutter_mode("CLOSED"), ShutterMode::AlwaysClosed);
    EXPECT_EQ(parse_shutter_mode("3"), ShutterMode::AlwaysOpen);
    EXPECT_FALSE(parse_shutter_mode("4"));
    EXPECT_EQ(parse_adc_gain("high"), AdcGain::High);
    EXPECT_EQ(parse_adc_gain("1"), AdcGain::Low);
    EXPECT_FALSE(parse_adc_gain("0"));
    EXPECT_EQ(to_string(AdcGain::Medium), "medium");
}
