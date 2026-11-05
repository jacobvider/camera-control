// SPDX-License-Identifier: MIT
// Teledyne Princeton Instruments PICam backend (Sophia 2048B-UV).
// Only compiled when CMake is configured with -DSUPERLOTIS_WITH_PICAM=ON.
#pragma once

#include <memory>

#include "superlotis/camera_backend.hpp"

namespace superlotis {

struct PicamConfig {
    /// Use the SDK's built-in demo camera instead of real hardware. Must be
    /// requested explicitly: the server never falls back to it on its own.
    bool demo = false;
    /// Extra time allowed for readout on top of the exposure time.
    double readout_timeout_s = 30.0;
};

class PicamBackend final : public CameraBackend {
public:
    explicit PicamBackend(PicamConfig config);
    ~PicamBackend() override;
    PicamBackend(const PicamBackend&) = delete;
    PicamBackend& operator=(const PicamBackend&) = delete;

    [[nodiscard]] CameraInfo info() const override;
    void set_adc_gain(AdcGain gain) override;
    [[nodiscard]] AdcGain adc_gain() override;
    void set_temperature_setpoint(double celsius) override;
    [[nodiscard]] TemperatureStatus temperature() override;
    [[nodiscard]] Frame acquire(double exposure_s, ShutterMode shutter) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace superlotis
