// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>

#include <numeric>

#include "superlotis/simulated_backend.hpp"

using namespace superlotis;

namespace {
SimulatorConfig fast() {
    SimulatorConfig c;
    c.width = 64;
    c.height = 32;
    c.seed = 42;
    c.time_scale = 0.0;
    return c;
}
double mean(const Frame& f) {
    return std::accumulate(f.pixels.begin(), f.pixels.end(), 0.0) / static_cast<double>(f.pixels.size());
}
}  // namespace

TEST(SimulatedBackend, FrameHasConfiguredGeometry) {
    SimulatedBackend cam(fast());
    const Frame f = cam.acquire(0.0, ShutterMode::AlwaysClosed);
    EXPECT_TRUE(f.valid());
    EXPECT_EQ(f.width, 64u);
    EXPECT_EQ(f.height, 32u);
    EXPECT_TRUE(cam.info().simulated);
}

TEST(SimulatedBackend, BiasLevelAndSignalScaleWithExposureAndShutter) {
    SimulatedBackend cam(fast());
    const double bias = mean(cam.acquire(0.0, ShutterMode::AlwaysClosed));
    const double dark = mean(cam.acquire(100.0, ShutterMode::AlwaysClosed));
    const double light = mean(cam.acquire(10.0, ShutterMode::Normal));
    EXPECT_NEAR(bias, 500.0, 2.0);
    EXPECT_GT(dark, bias);
    EXPECT_GT(light, bias + 100.0);
}

TEST(SimulatedBackend, GainChangesSignal) {
    SimulatedBackend cam(fast());
    cam.set_adc_gain(AdcGain::High);
    const double high = mean(cam.acquire(10.0, ShutterMode::Normal));
    cam.set_adc_gain(AdcGain::Low);
    EXPECT_EQ(cam.adc_gain(), AdcGain::Low);
    const double low = mean(cam.acquire(10.0, ShutterMode::Normal));
    EXPECT_GT(high - 500.0, 2.0 * (low - 500.0));
}

TEST(SimulatedBackend, DeterministicWithSeed) {
    SimulatedBackend a(fast()), b(fast());
    EXPECT_EQ(a.acquire(1.0, ShutterMode::Normal).pixels, b.acquire(1.0, ShutterMode::Normal).pixels);
}

TEST(SimulatedBackend, CoolerSettlesOnSetpoint) {
    SimulatedBackend cam(fast());
    cam.set_temperature_setpoint(-75.0);
    const auto t = cam.temperature();
    EXPECT_DOUBLE_EQ(t.setpoint_c, -75.0);
    EXPECT_NEAR(t.reading_c, -75.0, 0.5);
    EXPECT_TRUE(t.locked);
}

TEST(SimulatedBackend, CoolerTakesTimeInRealTimeMode) {
    SimulatorConfig c = fast();
    c.time_scale = 1.0;
    c.cooldown_tau_s = 3600.0;
    SimulatedBackend cam(c);
    const auto t = cam.temperature();
    EXPECT_GT(t.reading_c, 15.0);
    EXPECT_FALSE(t.locked);
}

TEST(SimulatedBackend, RejectsNegativeExposure) {
    SimulatedBackend cam(fast());
    EXPECT_THROW((void)cam.acquire(-1.0, ShutterMode::Normal), CameraError);
}
