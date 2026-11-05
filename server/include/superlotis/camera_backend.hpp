// SPDX-License-Identifier: MIT
// Hardware abstraction. The controller only talks to cameras through this
// interface, so the whole server can be built and tested without the PICam SDK.
#pragma once

#include "superlotis/types.hpp"

namespace superlotis {

/// A camera backend. Implementations are NOT required to be thread-safe:
/// CameraController serializes every call.
class CameraBackend {
public:
    virtual ~CameraBackend() = default;

    [[nodiscard]] virtual CameraInfo info() const = 0;

    virtual void set_adc_gain(AdcGain gain) = 0;
    [[nodiscard]] virtual AdcGain adc_gain() = 0;

    virtual void set_temperature_setpoint(double celsius) = 0;
    [[nodiscard]] virtual TemperatureStatus temperature() = 0;

    /// Blocks until one frame has been exposed and read out.
    /// Exposure time and shutter mode are passed explicitly on every call so
    /// a frame can never silently use stale settings.
    [[nodiscard]] virtual Frame acquire(double exposure_s, ShutterMode shutter) = 0;
};

}  // namespace superlotis
