// SPDX-License-Identifier: MIT
// A physically-plausible software camera: bias + dark current + sky + read
// noise, a first-order cooler model, and optional real-time exposure delays.
#pragma once

#include <chrono>
#include <cstdint>
#include <random>

#include "superlotis/camera_backend.hpp"

namespace superlotis {

struct SimulatorConfig {
    std::uint32_t width = 2048;
    std::uint32_t height = 2048;
    std::uint64_t seed = 0;   ///< 0 = seed from std::random_device
    double time_scale = 1.0;  ///< 1 = real time, 0 = no waiting (tests)
    double bias_adu = 500.0;
    double read_noise_adu = 5.0;
    double dark_rate_adu_s = 0.05;   ///< at the reference temperature
    double dark_ref_temp_c = -60.0;  ///< dark current doubles every 6 C above this
    double sky_rate_adu_s = 20.0;    ///< illumination when the shutter is open
    double ambient_c = 20.0;
    double initial_setpoint_c = -60.0;
    double cooldown_tau_s = 60.0;  ///< cooler e-folding time
    double lock_tolerance_c = 0.5;
    double readout_s = 0.5;  ///< simulated readout time
};

class SimulatedBackend final : public CameraBackend {
public:
    explicit SimulatedBackend(SimulatorConfig config = {});

    [[nodiscard]] CameraInfo info() const override;
    void set_adc_gain(AdcGain gain) override;
    [[nodiscard]] AdcGain adc_gain() override;
    void set_temperature_setpoint(double celsius) override;
    [[nodiscard]] TemperatureStatus temperature() override;
    [[nodiscard]] Frame acquire(double exposure_s, ShutterMode shutter) override;

private:
    using Clock = std::chrono::steady_clock;
    void advance_thermal_model();
    void sleep_scaled(double seconds) const;
    [[nodiscard]] double gain_factor() const noexcept;

    SimulatorConfig config_;
    std::mt19937_64 rng_;
    AdcGain gain_ = AdcGain::High;
    double setpoint_c_;
    double sensor_c_;
    Clock::time_point last_update_;
};

}  // namespace superlotis
