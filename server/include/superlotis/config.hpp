// SPDX-License-Identifier: MIT
// Server configuration: INI-style file ("[section]" + "key = value"), with
// strict validation. Unknown keys are errors so typos can't go unnoticed.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include "superlotis/picam_backend.hpp"
#include "superlotis/simulated_backend.hpp"
#include "superlotis/types.hpp"

namespace superlotis {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ServerConfig {
    std::string bind_address = "127.0.0.1";  ///< loopback by default; set explicitly to expose
    std::uint16_t port = 6972;
    int max_clients = 8;
    double idle_timeout_s = 600.0;  ///< close silent connections after this long
    std::string auth_token;         ///< empty = no authentication
    bool allow_remote_shutdown = false;
};

struct LimitsConfig {
    double min_exptime_s = 0.0;
    double max_exptime_s = 240.0;
    double min_setpoint_c = -80.0;
    double max_setpoint_c = 90.0;
    int max_burst = 100;
};

struct StorageConfig {
    std::filesystem::path data_dir = "data";
    std::string prefix = "superlotis";
};

struct FitsConfig {
    std::string telescope = "SuperLOTIS";
    std::string instrument = "Sophia 2048B-UV";
    std::string observer;
};

struct CameraSettingsConfig {
    std::string backend = "simulated";  ///< "simulated" or "picam"
    double default_exptime_s = 1.0;
    ShutterMode default_shutter = ShutterMode::Normal;
    std::optional<AdcGain> initial_gain;
    std::optional<double> initial_setpoint_c;
};

struct Config {
    ServerConfig server;
    LimitsConfig limits;
    StorageConfig storage;
    FitsConfig fits;
    CameraSettingsConfig camera;
    SimulatorConfig simulator;
    PicamConfig picam;
    std::string log_level = "info";

    /// Loads and validates a configuration file.
    static Config load(const std::filesystem::path& path);
    /// Parses configuration text; `origin` is used in error messages.
    static Config parse(const std::string& text, const std::string& origin = "<config>");
    /// Applies a single "section.key" = value override (used for CLI flags).
    void apply(const std::string& dotted_key, const std::string& value,
               const std::string& where = "override");
    /// Cross-field validation. Throws ConfigError.
    void validate() const;
};

}  // namespace superlotis
