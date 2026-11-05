// SPDX-License-Identifier: MIT
#include "superlotis/controller.hpp"

#include <algorithm>
#include <cstdio>

#include "superlotis/fits_writer.hpp"
#include "superlotis/log.hpp"
#include "superlotis/util.hpp"
#include "superlotis/version.hpp"

namespace superlotis {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::BadRequest: return "bad_request";
        case ErrorCode::UnknownCommand: return "unknown_command";
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::OutOfRange: return "out_of_range";
        case ErrorCode::Busy: return "busy";
        case ErrorCode::CameraFailure: return "camera_error";
        case ErrorCode::StorageFailure: return "storage_error";
        case ErrorCode::Unauthorized: return "unauthorized";
        case ErrorCode::NotPermitted: return "not_permitted";
        case ErrorCode::Internal: return "internal_error";
    }
    return "internal_error";
}

std::string_view to_string(CameraState state) noexcept {
    switch (state) {
        case CameraState::Idle: return "idle";
        case CameraState::Exposing: return "exposing";
        case CameraState::Writing: return "writing";
    }
    return "unknown";
}

namespace {

std::string_view imagetyp(FrameType t) {
    switch (t) {
        case FrameType::Object: return "OBJECT";
        case FrameType::Dark: return "DARK";
        case FrameType::Bias: return "BIAS";
    }
    return "UNKNOWN";
}

void check_range(double v, double lo, double hi, const char* what, const char* unit) {
    if (v < lo || v > hi)
        throw CommandError(ErrorCode::OutOfRange, std::string(what) + " must be between " +
                                                      util::fixed(lo, 3) + " and " + util::fixed(hi, 3) +
                                                      " " + unit);
}

/// Runs `fn` against the backend and converts hardware failures into CommandError.
template <typename Fn>
auto hw_call(const char* what, Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const CameraError& e) {
        log::error(what, ": ", e.what());
        throw CommandError(ErrorCode::CameraFailure, e.what());
    }
}

}  // namespace

/// Marks the controller busy for the lifetime of the guard (one exposure or burst).
class CameraController::BusyGuard {
public:
    explicit BusyGuard(std::atomic<bool>& flag) : flag_(flag) {
        bool expected = false;
        if (!flag_.compare_exchange_strong(expected, true))
            throw CommandError(ErrorCode::Busy, "an exposure is already in progress");
    }
    ~BusyGuard() { flag_.store(false); }
    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;

private:
    std::atomic<bool>& flag_;
};

CameraController::CameraController(std::unique_ptr<CameraBackend> backend, Config config)
    : backend_(std::move(backend)),
      config_(std::move(config)),
      exptime_s_(config_.camera.default_exptime_s),
      shutter_(config_.camera.default_shutter) {
    if (!backend_) throw std::invalid_argument("CameraController requires a backend");
    const std::lock_guard<std::mutex> hw(hw_mutex_);
    info_ = backend_->info();
    if (config_.camera.initial_gain) backend_->set_adc_gain(*config_.camera.initial_gain);
    if (config_.camera.initial_setpoint_c)
        backend_->set_temperature_setpoint(*config_.camera.initial_setpoint_c);
    gain_ = backend_->adc_gain();
    refresh_temperature_locked();
}

void CameraController::require_idle(const char* what) const {
    if (busy_.load())
        throw CommandError(ErrorCode::Busy, std::string("cannot change ") + what + " during an exposure");
}

void CameraController::refresh_temperature_locked() {
    const TemperatureStatus t = hw_call("read temperature", [&] { return backend_->temperature(); });
    const std::lock_guard<std::mutex> lock(state_mutex_);
    temperature_ = t;
    temperature_time_ = std::chrono::steady_clock::now();
}

TemperatureStatus CameraController::temperature() {
    // Never block behind an exposure: if the hardware is in use, serve the cache.
    std::unique_lock<std::mutex> hw(hw_mutex_, std::try_to_lock);
    if (hw.owns_lock()) refresh_temperature_locked();
    const std::lock_guard<std::mutex> lock(state_mutex_);
    return temperature_;
}

ControllerStatus CameraController::status() {
    (void)temperature();
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(state_mutex_);
    ControllerStatus s;
    s.state = state_;
    s.exptime_s = exptime_s_;
    s.shutter = shutter_;
    s.gain = gain_;
    s.temperature = temperature_;
    s.temperature_age_s = std::chrono::duration<double>(now - temperature_time_).count();
    if (state_ == CameraState::Exposing) {
        s.elapsed_s = std::chrono::duration<double>(now - exposure_start_).count();
        s.progress = exposure_length_s_ > 0 ? std::min(1.0, s.elapsed_s / exposure_length_s_) : 1.0;
    } else if (state_ == CameraState::Writing) {
        s.progress = 1.0;
    }
    s.frames_written = frames_written_;
    s.last_file = last_file_;
    s.last_error = last_error_;
    return s;
}

void CameraController::set_exptime(double seconds) {
    check_range(seconds, config_.limits.min_exptime_s, config_.limits.max_exptime_s, "exposure time", "s");
    const std::lock_guard<std::mutex> lock(state_mutex_);
    exptime_s_ = seconds;  // applies to the next exposure; safe to change while busy
}

double CameraController::exptime() const {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    return exptime_s_;
}

void CameraController::set_shutter(ShutterMode mode) {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    shutter_ = mode;
}

ShutterMode CameraController::shutter() const {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    return shutter_;
}

void CameraController::set_gain(AdcGain gain) {
    require_idle("gain");
    const std::lock_guard<std::mutex> hw(hw_mutex_);
    hw_call("set gain", [&] { backend_->set_adc_gain(gain); });
    const AdcGain actual = hw_call("read gain", [&] { return backend_->adc_gain(); });
    {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        gain_ = actual;
    }
    if (actual != gain)
        throw CommandError(ErrorCode::CameraFailure,
                           "camera reports gain '" + std::string(to_string(actual)) + "' after setting '" +
                               std::string(to_string(gain)) + "'");
    log::info("ADC gain set to ", to_string(gain));
}

AdcGain CameraController::gain() {
    std::unique_lock<std::mutex> hw(hw_mutex_, std::try_to_lock);
    if (hw.owns_lock()) {
        const AdcGain g = hw_call("read gain", [&] { return backend_->adc_gain(); });
        const std::lock_guard<std::mutex> lock(state_mutex_);
        gain_ = g;
    }
    const std::lock_guard<std::mutex> lock(state_mutex_);
    return gain_;
}

void CameraController::set_setpoint(double celsius) {
    check_range(celsius, config_.limits.min_setpoint_c, config_.limits.max_setpoint_c,
                "temperature set point", "C");
    require_idle("temperature set point");
    const std::lock_guard<std::mutex> hw(hw_mutex_);
    hw_call("set temperature", [&] { backend_->set_temperature_setpoint(celsius); });
    refresh_temperature_locked();
    log::info("Temperature set point changed to ", util::fixed(celsius, 2), " C");
}

std::filesystem::path CameraController::next_path(FrameType type, SystemClock::time_point when) {
    const auto dir = config_.storage.data_dir / util::date_utc(when);
    for (;;) {
        ++sequence_;
        char seq[16];
        std::snprintf(seq, sizeof seq, "%04llu", static_cast<unsigned long long>(sequence_));
        const auto name = config_.storage.prefix + "_" + util::compact_utc(when) + "_" +
                          std::string(to_string(type)) + "_" + seq + ".fits";
        auto path = dir / name;
        if (!std::filesystem::exists(path)) return path;
    }
}

std::filesystem::path CameraController::expose(const ExposureRequest& request) {
    const BusyGuard guard(busy_);
    const std::lock_guard<std::mutex> hw(hw_mutex_);
    return expose_locked(request);
}

std::vector<std::filesystem::path> CameraController::burst(int count, const ExposureRequest& request) {
    if (count < 1 || count > config_.limits.max_burst)
        throw CommandError(ErrorCode::OutOfRange,
                           "burst count must be between 1 and " + std::to_string(config_.limits.max_burst));
    const BusyGuard guard(busy_);
    const std::lock_guard<std::mutex> hw(hw_mutex_);
    std::vector<std::filesystem::path> files;
    files.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        try {
            files.push_back(expose_locked(request));
        } catch (const CommandError& e) {
            throw CommandError(e.code(), "burst stopped after " + std::to_string(i) + " of " +
                                             std::to_string(count) + " frames: " + e.what());
        }
    }
    return files;
}

std::filesystem::path CameraController::expose_locked(const ExposureRequest& request) {
    // Resolve the exposure settings for this frame type. The shutter is forced
    // closed for darks and biases, and biases are always zero-length.
    double exptime = 0.0;
    ShutterMode shutter = ShutterMode::AlwaysClosed;
    {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        switch (request.type) {
            case FrameType::Object:
                exptime = request.exptime_s.value_or(exptime_s_);
                shutter = shutter_;
                break;
            case FrameType::Dark: exptime = request.exptime_s.value_or(exptime_s_); break;
            case FrameType::Bias: exptime = 0.0; break;
        }
    }
    if (request.type != FrameType::Bias)
        check_range(exptime, config_.limits.min_exptime_s, config_.limits.max_exptime_s, "exposure time",
                    "s");

    refresh_temperature_locked();
    TemperatureStatus temp_at_start;
    AdcGain gain_at_start;
    const auto start_wall = SystemClock::now();
    {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        temp_at_start = temperature_;
        gain_at_start = gain_;
        state_ = CameraState::Exposing;
        exposure_start_ = std::chrono::steady_clock::now();
        exposure_length_s_ = exptime;
        last_error_.clear();
    }
    // Always return to Idle, whatever happens below.
    struct StateReset {
        CameraController& c;
        ~StateReset() {
            const std::lock_guard<std::mutex> lock(c.state_mutex_);
            c.state_ = CameraState::Idle;
        }
    } reset{*this};

    auto record_error = [&](const std::string& msg) {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        last_error_ = msg;
    };

    log::info("Exposing ", to_string(request.type), " frame: ", util::fixed(exptime, 3), " s, shutter ",
              to_string(shutter));
    Frame frame;
    try {
        frame = hw_call("acquire", [&] { return backend_->acquire(exptime, shutter); });
    } catch (const CommandError& e) {
        record_error(e.what());
        throw;
    }
    if (!frame.valid()) {
        record_error("camera returned an invalid frame");
        throw CommandError(ErrorCode::CameraFailure, "camera returned an invalid frame");
    }

    {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        state_ = CameraState::Writing;
    }

    fits::Header h;
    h.set("DATE", util::iso8601_utc(SystemClock::now()), "file creation time (UTC)");
    h.set("DATE-OBS", util::iso8601_utc(start_wall), "exposure start time (UTC)");
    h.set("EXPTIME", exptime, "exposure time (s)");
    h.set("IMAGETYP", std::string(imagetyp(request.type)), "frame type");
    if (!request.object.empty()) h.set("OBJECT", request.object, "target name");
    h.set("SHUTTER", std::string(to_string(shutter)), "shutter timing mode");
    h.set("GAINMODE", std::string(to_string(gain_at_start)), "ADC analog gain setting");
    h.set("CCD-TEMP", temp_at_start.reading_c, "sensor temperature at start (C)");
    h.set("SET-TEMP", temp_at_start.setpoint_c, "sensor temperature set point (C)");
    h.set("TEMPLOCK", temp_at_start.locked, "sensor temperature locked at start");
    h.set("BUNIT", "ADU", "pixel units");
    h.set("TELESCOP", config_.fits.telescope, "telescope");
    h.set("INSTRUME", config_.fits.instrument, "instrument");
    if (!config_.fits.observer.empty()) h.set("OBSERVER", config_.fits.observer, "observer");
    h.set("CAMMODEL", info_.model, "camera model");
    h.set("CAMSERNO", info_.serial, "camera serial number");
    h.set("SIMULATE", info_.simulated, "frame from a simulated camera");
    h.set("ORIGIN", std::string("superlotis-camera ") + SUPERLOTIS_VERSION, "software");

    std::filesystem::path path;
    try {
        path = next_path(request.type, start_wall);
        h.set("FRAMENUM", static_cast<long long>(sequence_), "sequence number since server start");
        h.set("FILENAME", path.filename().string(), "original file name");
        fits::write(path, frame, h);
    } catch (const std::exception& e) {
        record_error(e.what());
        log::error("Failed to write FITS file: ", e.what());
        throw CommandError(ErrorCode::StorageFailure, e.what());
    }

    {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        ++frames_written_;
        last_file_ = path.string();
    }
    log::info("Wrote ", path.string());
    return path;
}

}  // namespace superlotis
