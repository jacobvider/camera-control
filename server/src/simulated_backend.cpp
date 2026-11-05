// SPDX-License-Identifier: MIT
#include "superlotis/simulated_backend.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace superlotis {

namespace {
std::uint64_t make_seed(std::uint64_t seed) {
    if (seed != 0) return seed;
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32U) ^ rd();
}
}  // namespace

SimulatedBackend::SimulatedBackend(SimulatorConfig config)
    : config_(config),
      rng_(make_seed(config.seed)),
      setpoint_c_(config.initial_setpoint_c),
      sensor_c_(config.ambient_c),
      last_update_(Clock::now()) {
    if (config_.width == 0 || config_.height == 0)
        throw CameraError("simulator: width and height must be positive");
}

CameraInfo SimulatedBackend::info() const {
    return CameraInfo{"Simulated Sophia 2048B-UV",
                      "SIM00001",
                      "simulated 2048x2048 CCD",
                      config_.width,
                      config_.height,
                      true};
}

void SimulatedBackend::set_adc_gain(AdcGain gain) {
    gain_ = gain;
}
AdcGain SimulatedBackend::adc_gain() {
    return gain_;
}

void SimulatedBackend::set_temperature_setpoint(double celsius) {
    advance_thermal_model();
    setpoint_c_ = celsius;
}

TemperatureStatus SimulatedBackend::temperature() {
    advance_thermal_model();
    const bool locked = std::abs(sensor_c_ - setpoint_c_) <= config_.lock_tolerance_c;
    return TemperatureStatus{sensor_c_, setpoint_c_, locked};
}

void SimulatedBackend::advance_thermal_model() {
    const auto now = Clock::now();
    double dt = std::chrono::duration<double>(now - last_update_).count();
    last_update_ = now;
    // With time_scale == 0 the simulation runs "infinitely fast": settle instantly.
    if (config_.time_scale <= 0.0) {
        sensor_c_ = setpoint_c_;
        return;
    }
    dt /= config_.time_scale;
    const double alpha = 1.0 - std::exp(-dt / std::max(config_.cooldown_tau_s, 1e-6));
    sensor_c_ += (setpoint_c_ - sensor_c_) * alpha;
}

void SimulatedBackend::sleep_scaled(double seconds) const {
    if (config_.time_scale <= 0.0 || seconds <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds * config_.time_scale));
}

double SimulatedBackend::gain_factor() const noexcept {
    // Higher analog gain -> more ADU per electron.
    switch (gain_) {
        case AdcGain::Low: return 0.25;
        case AdcGain::Medium: return 0.5;
        case AdcGain::High: return 1.0;
    }
    return 1.0;
}

Frame SimulatedBackend::acquire(double exposure_s, ShutterMode shutter) {
    if (!(exposure_s >= 0.0)) throw CameraError("simulator: negative exposure time");
    sleep_scaled(exposure_s);
    const TemperatureStatus t = temperature();
    sleep_scaled(config_.readout_s);

    const double dark_rate =
        config_.dark_rate_adu_s * std::pow(2.0, (t.reading_c - config_.dark_ref_temp_c) / 6.0);
    const double sky_rate = (shutter == ShutterMode::AlwaysClosed) ? 0.0 : config_.sky_rate_adu_s;
    const double signal = (dark_rate + sky_rate) * exposure_s * gain_factor();

    Frame frame;
    frame.width = config_.width;
    frame.height = config_.height;
    frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);

    // Shot noise (Gaussian approximation of Poisson) plus read noise.
    const double sigma = std::sqrt(signal + config_.read_noise_adu * config_.read_noise_adu);
    std::normal_distribution<double> noise(config_.bias_adu + signal, sigma);
    for (auto& px : frame.pixels) {
        const double v = std::clamp(std::round(noise(rng_)), 0.0, 65535.0);
        px = static_cast<std::uint16_t>(v);
    }
    return frame;
}

}  // namespace superlotis
