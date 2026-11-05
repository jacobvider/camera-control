// SPDX-License-Identifier: MIT
// Controller behaviour, including regression tests for defects in the
// original SuperLOTIS server (see CHANGELOG "Fixed").
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <set>

#include "spy_backend.hpp"
#include "superlotis/controller.hpp"

using namespace superlotis;
using namespace superlotis::test_support;
namespace fs = std::filesystem;

namespace {

class ControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = fs::temp_directory_path() / ("slcam_ctl_" + std::to_string(::getpid()) + "_" +
                                           ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir);
        cfg.storage.data_dir = dir;
        state = std::make_shared<SpyBackend::State>();
    }
    void TearDown() override { fs::remove_all(dir); }

    CameraController make() { return CameraController(std::make_unique<SpyBackend>(state), cfg); }

    static std::string read_header(const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::string s(2880, '\0');
        f.read(s.data(), 2880);
        return s;
    }
    static std::string card(const std::string& header, const std::string& key) {
        for (std::size_t i = 0; i + 80 <= header.size(); i += 80) {
            std::string k = header.substr(i, 8);
            k.erase(k.find_last_not_of(' ') + 1);
            if (k == key) return header.substr(i, 80);
        }
        return {};
    }

    fs::path dir;
    Config cfg;
    std::shared_ptr<SpyBackend::State> state;
};

}  // namespace

// Original bug: expose() hardcoded EXP_TIME 6000 ms and ignored `exptime=`.
TEST_F(ControllerTest, ExposeUsesConfiguredExposureTime) {
    auto c = make();
    c.set_exptime(2.5);
    c.expose({FrameType::Object, std::nullopt, ""});
    ASSERT_EQ(state->acquires.size(), 1u);
    EXPECT_DOUBLE_EQ(state->acquires[0].exposure_s, 2.5);
    EXPECT_EQ(state->acquires[0].shutter, ShutterMode::Normal);
}

TEST_F(ControllerTest, PerFrameExposureOverrideDoesNotChangeDefault) {
    auto c = make();
    c.set_exptime(1.0);
    c.expose({FrameType::Object, 7.0, ""});
    EXPECT_DOUBLE_EQ(state->acquires.back().exposure_s, 7.0);
    EXPECT_DOUBLE_EQ(c.exptime(), 1.0);
}

// Original bug: dark() never closed the shutter.
TEST_F(ControllerTest, DarkFramesForceShutterClosed) {
    auto c = make();
    c.set_shutter(ShutterMode::AlwaysOpen);
    c.set_exptime(3.0);
    c.expose({FrameType::Dark, std::nullopt, ""});
    EXPECT_EQ(state->acquires.back().shutter, ShutterMode::AlwaysClosed);
    EXPECT_DOUBLE_EQ(state->acquires.back().exposure_s, 3.0);
    EXPECT_EQ(c.shutter(), ShutterMode::AlwaysOpen) << "dark must not change the user's shutter setting";
}

// Original bug: bias() took a 100 ms exposure with whatever shutter mode was set.
TEST_F(ControllerTest, BiasFramesAreZeroLengthAndClosed) {
    auto c = make();
    c.set_exptime(30.0);
    c.set_shutter(ShutterMode::AlwaysOpen);
    c.expose({FrameType::Bias, 5.0, ""});
    EXPECT_DOUBLE_EQ(state->acquires.back().exposure_s, 0.0);
    EXPECT_EQ(state->acquires.back().shutter, ShutterMode::AlwaysClosed);
}

// Original bug: set_analog_gain() wrote the *current* gain back, so it never changed.
TEST_F(ControllerTest, GainSetterChangesGainAndGetterReportsIt) {
    auto c = make();
    c.set_gain(AdcGain::Low);
    EXPECT_EQ(state->gain, AdcGain::Low);
    EXPECT_EQ(c.gain(), AdcGain::Low);
}

TEST_F(ControllerTest, GainSetterDetectsHardwareThatIgnoresTheWrite) {
    auto c = make();
    state->ignore_gain_writes = true;
    try {
        c.set_gain(AdcGain::Low);
        FAIL() << "expected CommandError";
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), ErrorCode::CameraFailure);
    }
}

// Original bug: the set point could only be changed when the temperature was already locked.
TEST_F(ControllerTest, SetpointCanBeChangedWhileUnlocked) {
    auto c = make();
    state->locked = false;
    c.set_setpoint(-70.0);
    EXPECT_DOUBLE_EQ(state->setpoint, -70.0);
}

TEST_F(ControllerTest, OutOfRangeValuesAreRejected) {
    auto c = make();
    auto code_of = [](auto&& fn) {
        try {
            fn();
        } catch (const CommandError& e) {
            return e.code();
        }
        return ErrorCode::Internal;
    };
    EXPECT_EQ(code_of([&] { c.set_exptime(-1); }), ErrorCode::OutOfRange);
    EXPECT_EQ(code_of([&] { c.set_exptime(241); }), ErrorCode::OutOfRange);
    EXPECT_EQ(code_of([&] { c.set_setpoint(-100); }), ErrorCode::OutOfRange);
    EXPECT_EQ(code_of([&] { c.expose({FrameType::Object, 500.0, ""}); }), ErrorCode::OutOfRange);
    EXPECT_EQ(code_of([&] { c.burst(0, {}); }), ErrorCode::OutOfRange);
    EXPECT_TRUE(state->acquires.empty());
}

// Original bug: every frame was written to the same "exposure_file" and overwritten.
TEST_F(ControllerTest, EveryFrameGetsAUniqueFile) {
    auto c = make();
    std::set<fs::path> files;
    for (int i = 0; i < 3; ++i) files.insert(c.expose({}));
    for (const auto& f : c.burst(3, {})) files.insert(f);
    EXPECT_EQ(files.size(), 6u);
    for (const auto& f : files) {
        EXPECT_TRUE(fs::exists(f)) << f;
        EXPECT_EQ(f.extension(), ".fits");
    }
}

// Original bug: EXPTIME was written in milliseconds and DATE-OBS at conversion time.
TEST_F(ControllerTest, FitsHeaderUsesSecondsAndRecordsSettings) {
    auto c = make();
    c.set_exptime(1.5);
    c.set_gain(AdcGain::Medium);
    const auto path = c.expose({FrameType::Object, std::nullopt, "NGC1234"});
    const std::string h = read_header(path);
    EXPECT_NE(card(h, "EXPTIME").find("1.5"), std::string::npos) << card(h, "EXPTIME");
    EXPECT_NE(card(h, "OBJECT").find("'NGC1234 '"), std::string::npos);
    EXPECT_NE(card(h, "IMAGETYP").find("'OBJECT  '"), std::string::npos);
    EXPECT_NE(card(h, "GAINMODE").find("'medium  '"), std::string::npos);
    EXPECT_NE(card(h, "TEMPLOCK").find("T /"), std::string::npos);
    EXPECT_FALSE(card(h, "DATE-OBS").empty());
    EXPECT_EQ(fs::file_size(path) % 2880, 0u);
}

TEST_F(ControllerTest, AcquisitionFailureIsReportedAndStateRecovers) {
    auto c = make();
    state->fail_acquire = true;
    try {
        c.expose({});
        FAIL();
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), ErrorCode::CameraFailure);
    }
    const auto s = c.status();
    EXPECT_EQ(s.state, CameraState::Idle);
    EXPECT_NE(s.last_error.find("hardware fault"), std::string::npos);
    EXPECT_FALSE(c.busy());
    state->fail_acquire = false;
    EXPECT_NO_THROW(c.expose({}));
    EXPECT_TRUE(c.status().last_error.empty());
}

TEST_F(ControllerTest, StatusDoesNotBlockDuringExposureAndSecondExposureIsBusy) {
    auto c = make();
    state->hold = true;
    c.set_exptime(10.0);
    auto pending = std::async(std::launch::async, [&] { return c.expose({}); });
    wait_until_acquiring(*state);

    const ControllerStatus s = c.status();  // must return immediately
    EXPECT_EQ(s.state, CameraState::Exposing);
    EXPECT_TRUE(c.busy());
    EXPECT_NO_THROW(c.set_exptime(2.0));  // affects the *next* frame only

    try {
        c.expose({});
        FAIL() << "second exposure should be rejected";
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), ErrorCode::Busy);
    }
    try {
        c.set_gain(AdcGain::Low);
        FAIL() << "gain change during exposure should be rejected";
    } catch (const CommandError& e) {
        EXPECT_EQ(e.code(), ErrorCode::Busy);
    }

    release(*state);
    EXPECT_TRUE(fs::exists(pending.get()));
    EXPECT_DOUBLE_EQ(state->acquires[0].exposure_s, 10.0);
    EXPECT_EQ(c.status().state, CameraState::Idle);
    EXPECT_EQ(c.status().frames_written, 1u);
}

TEST_F(ControllerTest, InitialSettingsFromConfigAreApplied) {
    cfg.camera.initial_gain = AdcGain::Medium;
    cfg.camera.initial_setpoint_c = -75.0;
    cfg.camera.default_exptime_s = 4.0;
    auto c = make();
    EXPECT_EQ(state->gain, AdcGain::Medium);
    EXPECT_DOUBLE_EQ(state->setpoint, -75.0);
    EXPECT_DOUBLE_EQ(c.exptime(), 4.0);
}
