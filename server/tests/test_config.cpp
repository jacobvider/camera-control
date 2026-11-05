// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include "superlotis/config.hpp"

using namespace superlotis;

TEST(Config, DefaultsAreSafe) {
    const Config c;
    EXPECT_EQ(c.server.bind_address, "127.0.0.1") << "must not listen on all interfaces by default";
    EXPECT_EQ(c.server.port, 6972);
    EXPECT_FALSE(c.server.allow_remote_shutdown);
    EXPECT_EQ(c.camera.backend, "simulated");
    EXPECT_FALSE(c.picam.demo) << "demo camera must be opt-in";
    EXPECT_NO_THROW(c.validate());
}

TEST(Config, ParsesSectionsCommentsAndQuotes) {
    const Config c = Config::parse(R"(
# comment
[server]
bind_address = 0.0.0.0
port = 7000
auth_token = "s3cret"
allow_remote_shutdown = yes

[camera]
backend = picam
default_exptime_s = 2.5
default_shutter = closed
initial_gain = medium
initial_setpoint_c = -70

; another comment
[storage]
data_dir = /data/superlotis
prefix = slotis

[picam]
demo = true
)");
    EXPECT_EQ(c.server.bind_address, "0.0.0.0");
    EXPECT_EQ(c.server.port, 7000);
    EXPECT_EQ(c.server.auth_token, "s3cret");
    EXPECT_TRUE(c.server.allow_remote_shutdown);
    EXPECT_EQ(c.camera.backend, "picam");
    EXPECT_DOUBLE_EQ(c.camera.default_exptime_s, 2.5);
    EXPECT_EQ(c.camera.default_shutter, ShutterMode::AlwaysClosed);
    EXPECT_EQ(c.camera.initial_gain, AdcGain::Medium);
    EXPECT_EQ(c.camera.initial_setpoint_c, -70.0);
    EXPECT_EQ(c.storage.data_dir, "/data/superlotis");
    EXPECT_TRUE(c.picam.demo);
}

TEST(Config, UnknownKeysAndBadValuesAreErrorsWithLineNumbers) {
    try {
        Config::parse("[server]\nprot = 7000\n", "test.conf");
        FAIL();
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("test.conf:2"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("server.prot"), std::string::npos);
    }
    EXPECT_THROW(Config::parse("[server]\nport = 70000\n"), ConfigError);
    EXPECT_THROW(Config::parse("[server]\nport = abc\n"), ConfigError);
    EXPECT_THROW(Config::parse("[camera]\nbackend = magic\n"), ConfigError);
    EXPECT_THROW(Config::parse("port = 1\n"), ConfigError);
    EXPECT_THROW(Config::parse("[server\n"), ConfigError);
}

TEST(Config, CrossFieldValidation) {
    EXPECT_THROW(Config::parse("[camera]\ndefault_exptime_s = 500\n"), ConfigError);
    EXPECT_THROW(Config::parse("[limits]\nmin_setpoint_c = 0\nmax_setpoint_c = -10\n"), ConfigError);
    EXPECT_THROW(Config::parse("[storage]\nprefix = a/b\n"), ConfigError);
}

TEST(Config, ApplyOverride) {
    Config c;
    c.apply("simulator.time_scale", "0");
    EXPECT_DOUBLE_EQ(c.simulator.time_scale, 0.0);
    EXPECT_THROW(c.apply("nope.key", "1"), ConfigError);
}
