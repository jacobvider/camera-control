// SPDX-License-Identifier: MIT
// CameraController owns the backend and all acquisition state. It validates
// every request, serializes hardware access, writes FITS files, and lets
// status queries run while an exposure is in progress.
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "superlotis/camera_backend.hpp"
#include "superlotis/config.hpp"

namespace superlotis {

/// Machine-readable error categories, sent over the wire as `ERR <code> ...`.
enum class ErrorCode {
    BadRequest,
    UnknownCommand,
    InvalidArgument,
    OutOfRange,
    Busy,
    CameraFailure,
    StorageFailure,
    Unauthorized,
    NotPermitted,
    Internal,
};

std::string_view to_string(ErrorCode code) noexcept;

class CommandError : public std::runtime_error {
public:
    CommandError(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

enum class CameraState { Idle, Exposing, Writing };
std::string_view to_string(CameraState state) noexcept;

struct ControllerStatus {
    CameraState state = CameraState::Idle;
    double exptime_s = 0.0;
    ShutterMode shutter = ShutterMode::Normal;
    AdcGain gain = AdcGain::High;
    TemperatureStatus temperature;
    double temperature_age_s = 0.0;  ///< how stale the cached reading is
    double progress = 0.0;           ///< 0..1 during an exposure
    double elapsed_s = 0.0;
    std::uint64_t frames_written = 0;
    std::string last_file;
    std::string last_error;
};

struct ExposureRequest {
    FrameType type = FrameType::Object;
    std::optional<double> exptime_s;  ///< override the current setting for this frame
    std::string object;               ///< OBJECT keyword (optional)
};

class CameraController {
public:
    using SystemClock = std::chrono::system_clock;

    CameraController(std::unique_ptr<CameraBackend> backend, Config config);

    [[nodiscard]] CameraInfo info() const { return info_; }
    [[nodiscard]] ControllerStatus status();
    [[nodiscard]] const Config& config() const noexcept { return config_; }

    void set_exptime(double seconds);
    [[nodiscard]] double exptime() const;
    void set_shutter(ShutterMode mode);
    [[nodiscard]] ShutterMode shutter() const;
    void set_gain(AdcGain gain);
    [[nodiscard]] AdcGain gain();
    void set_setpoint(double celsius);
    /// Live reading when the camera is idle; cached reading during an exposure.
    [[nodiscard]] TemperatureStatus temperature();

    /// Takes one frame and writes it to disk. Blocks until the file is written.
    /// Throws CommandError(Busy) if another exposure is already running.
    std::filesystem::path expose(const ExposureRequest& request);
    /// Takes `count` frames back to back. Stops at the first failure.
    std::vector<std::filesystem::path> burst(int count, const ExposureRequest& request);

    [[nodiscard]] bool busy() const noexcept { return busy_.load(); }

private:
    class BusyGuard;
    std::filesystem::path next_path(FrameType type, SystemClock::time_point when);
    std::filesystem::path expose_locked(const ExposureRequest& request);
    void refresh_temperature_locked();
    void require_idle(const char* what) const;

    std::unique_ptr<CameraBackend> backend_;
    Config config_;
    CameraInfo info_;

    std::mutex hw_mutex_;             ///< serializes every backend call
    mutable std::mutex state_mutex_;  ///< protects the fields below
    std::atomic<bool> busy_{false};

    double exptime_s_;
    ShutterMode shutter_;
    AdcGain gain_ = AdcGain::High;
    TemperatureStatus temperature_{};
    std::chrono::steady_clock::time_point temperature_time_{};
    CameraState state_ = CameraState::Idle;
    std::chrono::steady_clock::time_point exposure_start_{};
    double exposure_length_s_ = 0.0;
    std::uint64_t frames_written_ = 0;
    std::uint64_t sequence_ = 0;
    std::string last_file_;
    std::string last_error_;
};

}  // namespace superlotis
