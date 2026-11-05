// SPDX-License-Identifier: MIT
// Core value types shared by the camera backends, controller and protocol.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace superlotis {

/// Shutter behaviour during an acquisition. Values match PicamShutterTimingMode.
enum class ShutterMode : int { Normal = 1, AlwaysClosed = 2, AlwaysOpen = 3 };

/// ADC analog gain. Values match PicamAdcAnalogGain.
enum class AdcGain : int { Low = 1, Medium = 2, High = 3 };

/// What kind of calibration/science frame is being taken (drives IMAGETYP).
enum class FrameType { Object, Dark, Bias };

std::string_view to_string(ShutterMode mode) noexcept;
std::string_view to_string(AdcGain gain) noexcept;
std::string_view to_string(FrameType type) noexcept;

/// Accepts names ("normal", "closed", "open") or legacy numeric codes ("1".."3").
std::optional<ShutterMode> parse_shutter_mode(std::string_view text) noexcept;
/// Accepts names ("low", "medium", "high") or legacy numeric codes ("1".."3").
std::optional<AdcGain> parse_adc_gain(std::string_view text) noexcept;

/// A single 16-bit image, row-major, `width` pixels per row.
struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint16_t> pixels;

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height;
    }
};

struct CameraInfo {
    std::string model;
    std::string serial;
    std::string sensor;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool simulated = false;
};

struct TemperatureStatus {
    double reading_c = 0.0;
    double setpoint_c = 0.0;
    bool locked = false;
};

/// Raised by backends when the hardware or SDK reports a failure.
class CameraError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace superlotis
