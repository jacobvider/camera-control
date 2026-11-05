// SPDX-License-Identifier: MIT
#include "superlotis/config.hpp"

#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>

#include "superlotis/log.hpp"
#include "superlotis/util.hpp"

namespace superlotis {

namespace {

[[noreturn]] void fail(const std::string& where, const std::string& msg) {
    throw ConfigError(where + ": " + msg);
}

double as_double(const std::string& v, const std::string& where) {
    if (auto d = util::parse_double(v)) return *d;
    fail(where, "expected a number, got '" + v + "'");
}

long long as_int(const std::string& v, const std::string& where, long long lo, long long hi) {
    const auto i = util::parse_int(v);
    if (!i) fail(where, "expected an integer, got '" + v + "'");
    if (*i < lo || *i > hi)
        fail(where, "value " + v + " out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    return *i;
}

bool as_bool(const std::string& v, const std::string& where) {
    if (auto b = util::parse_bool(v)) return *b;
    fail(where, "expected true/false, got '" + v + "'");
}

/// Strips one level of matching surrounding quotes.
std::string unquote(std::string_view v) {
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
        return std::string(v.substr(1, v.size() - 2));
    return std::string(v);
}

using Setter = std::function<void(Config&, const std::string& value, const std::string& where)>;

const std::map<std::string, Setter>& setters() {
    static const std::map<std::string, Setter> table = {
        // [server]
        {"server.bind_address", [](Config& c, const std::string& v, const std::string&) { c.server.bind_address = v; }},
        {"server.port", [](Config& c, const std::string& v, const std::string& w) {
             c.server.port = static_cast<std::uint16_t>(as_int(v, w, 0, 65535)); }},
        {"server.max_clients", [](Config& c, const std::string& v, const std::string& w) {
             c.server.max_clients = static_cast<int>(as_int(v, w, 1, 1024)); }},
        {"server.idle_timeout_s", [](Config& c, const std::string& v, const std::string& w) {
             c.server.idle_timeout_s = as_double(v, w); }},
        {"server.auth_token", [](Config& c, const std::string& v, const std::string&) { c.server.auth_token = v; }},
        {"server.allow_remote_shutdown", [](Config& c, const std::string& v, const std::string& w) {
             c.server.allow_remote_shutdown = as_bool(v, w); }},
        // [limits]
        {"limits.min_exptime_s", [](Config& c, const std::string& v, const std::string& w) { c.limits.min_exptime_s = as_double(v, w); }},
        {"limits.max_exptime_s", [](Config& c, const std::string& v, const std::string& w) { c.limits.max_exptime_s = as_double(v, w); }},
        {"limits.min_setpoint_c", [](Config& c, const std::string& v, const std::string& w) { c.limits.min_setpoint_c = as_double(v, w); }},
        {"limits.max_setpoint_c", [](Config& c, const std::string& v, const std::string& w) { c.limits.max_setpoint_c = as_double(v, w); }},
        {"limits.max_burst", [](Config& c, const std::string& v, const std::string& w) {
             c.limits.max_burst = static_cast<int>(as_int(v, w, 1, 100000)); }},
        // [storage]
        {"storage.data_dir", [](Config& c, const std::string& v, const std::string&) { c.storage.data_dir = v; }},
        {"storage.prefix", [](Config& c, const std::string& v, const std::string&) { c.storage.prefix = v; }},
        // [fits]
        {"fits.telescope", [](Config& c, const std::string& v, const std::string&) { c.fits.telescope = v; }},
        {"fits.instrument", [](Config& c, const std::string& v, const std::string&) { c.fits.instrument = v; }},
        {"fits.observer", [](Config& c, const std::string& v, const std::string&) { c.fits.observer = v; }},
        // [camera]
        {"camera.backend", [](Config& c, const std::string& v, const std::string& w) {
             const std::string b = util::to_lower(v);
             if (b != "simulated" && b != "picam") fail(w, "backend must be 'simulated' or 'picam'");
             c.camera.backend = b; }},
        {"camera.default_exptime_s", [](Config& c, const std::string& v, const std::string& w) {
             c.camera.default_exptime_s = as_double(v, w); }},
        {"camera.default_shutter", [](Config& c, const std::string& v, const std::string& w) {
             const auto m = parse_shutter_mode(v);
             if (!m) fail(w, "shutter must be normal, closed or open");
             c.camera.default_shutter = *m; }},
        {"camera.initial_gain", [](Config& c, const std::string& v, const std::string& w) {
             const auto g = parse_adc_gain(v);
             if (!g) fail(w, "gain must be low, medium or high");
             c.camera.initial_gain = *g; }},
        {"camera.initial_setpoint_c", [](Config& c, const std::string& v, const std::string& w) {
             c.camera.initial_setpoint_c = as_double(v, w); }},
        // [picam]
        {"picam.demo", [](Config& c, const std::string& v, const std::string& w) { c.picam.demo = as_bool(v, w); }},
        {"picam.readout_timeout_s", [](Config& c, const std::string& v, const std::string& w) {
             c.picam.readout_timeout_s = as_double(v, w); }},
        // [simulator]
        {"simulator.width", [](Config& c, const std::string& v, const std::string& w) {
             c.simulator.width = static_cast<std::uint32_t>(as_int(v, w, 1, 16384)); }},
        {"simulator.height", [](Config& c, const std::string& v, const std::string& w) {
             c.simulator.height = static_cast<std::uint32_t>(as_int(v, w, 1, 16384)); }},
        {"simulator.seed", [](Config& c, const std::string& v, const std::string& w) {
             c.simulator.seed = static_cast<std::uint64_t>(as_int(v, w, 0, std::numeric_limits<long long>::max())); }},
        {"simulator.time_scale", [](Config& c, const std::string& v, const std::string& w) { c.simulator.time_scale = as_double(v, w); }},
        {"simulator.bias_adu", [](Config& c, const std::string& v, const std::string& w) { c.simulator.bias_adu = as_double(v, w); }},
        {"simulator.read_noise_adu", [](Config& c, const std::string& v, const std::string& w) { c.simulator.read_noise_adu = as_double(v, w); }},
        {"simulator.dark_rate_adu_s", [](Config& c, const std::string& v, const std::string& w) { c.simulator.dark_rate_adu_s = as_double(v, w); }},
        {"simulator.sky_rate_adu_s", [](Config& c, const std::string& v, const std::string& w) { c.simulator.sky_rate_adu_s = as_double(v, w); }},
        {"simulator.ambient_c", [](Config& c, const std::string& v, const std::string& w) { c.simulator.ambient_c = as_double(v, w); }},
        {"simulator.cooldown_tau_s", [](Config& c, const std::string& v, const std::string& w) { c.simulator.cooldown_tau_s = as_double(v, w); }},
        {"simulator.readout_s", [](Config& c, const std::string& v, const std::string& w) { c.simulator.readout_s = as_double(v, w); }},
        // [log]
        {"log.level", [](Config& c, const std::string& v, const std::string& w) {
             if (!log::parse_level(v)) fail(w, "log level must be debug, info, warn or error");
             c.log_level = util::to_lower(v); }},
    };
    return table;
}

}  // namespace

void Config::apply(const std::string& dotted_key, const std::string& value, const std::string& where) {
    const auto& table = setters();
    const auto it = table.find(dotted_key);
    if (it == table.end()) fail(where, "unknown setting '" + dotted_key + "'");
    it->second(*this, value, where + " (" + dotted_key + ")");
}

Config Config::parse(const std::string& text, const std::string& origin) {
    Config cfg;
    std::istringstream in(text);
    std::string raw;
    std::string section;
    int lineno = 0;
    while (std::getline(in, raw)) {
        ++lineno;
        const std::string where = origin + ":" + std::to_string(lineno);
        std::string_view line = util::trim(raw);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[') {
            if (line.back() != ']') fail(where, "malformed section header");
            section = util::to_lower(util::trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) fail(where, "expected 'key = value'");
        if (section.empty()) fail(where, "setting outside of a [section]");
        const std::string key = util::to_lower(util::trim(line.substr(0, eq)));
        const std::string value = unquote(util::trim(line.substr(eq + 1)));
        cfg.apply(section + "." + key, value, where);
    }
    cfg.validate();
    return cfg;
}

Config Config::load(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) throw ConfigError("cannot open config file " + path.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), path.string());
}

void Config::validate() const {
    const auto& l = limits;
    if (l.min_exptime_s < 0 || l.max_exptime_s < l.min_exptime_s)
        throw ConfigError("limits: exposure range is invalid");
    if (l.max_setpoint_c < l.min_setpoint_c) throw ConfigError("limits: set-point range is invalid");
    if (camera.default_exptime_s < l.min_exptime_s || camera.default_exptime_s > l.max_exptime_s)
        throw ConfigError("camera.default_exptime_s is outside [limits]");
    if (camera.initial_setpoint_c &&
        (*camera.initial_setpoint_c < l.min_setpoint_c || *camera.initial_setpoint_c > l.max_setpoint_c))
        throw ConfigError("camera.initial_setpoint_c is outside [limits]");
    if (server.idle_timeout_s <= 0) throw ConfigError("server.idle_timeout_s must be positive");
    if (simulator.time_scale < 0) throw ConfigError("simulator.time_scale must be >= 0");
    if (storage.prefix.empty() || storage.prefix.find_first_of("/ \t") != std::string::npos)
        throw ConfigError("storage.prefix must be non-empty and contain no '/' or whitespace");
}

}  // namespace superlotis
