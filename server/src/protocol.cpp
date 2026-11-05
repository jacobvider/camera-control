// SPDX-License-Identifier: MIT
#include "superlotis/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "superlotis/log.hpp"
#include "superlotis/util.hpp"
#include "superlotis/version.hpp"

namespace superlotis::protocol {

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

std::string percent_encode(std::string_view value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c >= 0x7F || c == '%' || c == '=' || c == ',') {
            out += '%';
            out += hex[c >> 4U];
            out += hex[c & 0x0FU];
        } else {
            out += ch;
        }
    }
    return out;
}

std::string percent_decode(std::string_view value) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = nibble(value[i + 1]);
            const int lo = nibble(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

KeyValues& KeyValues::add(const std::string& key, const std::string& value) {
    if (!out_.empty()) out_ += ' ';
    out_ += key;
    out_ += '=';
    out_ += value.empty() ? std::string("-") : percent_encode(value);
    return *this;
}
KeyValues& KeyValues::add(const std::string& key, double value, int decimals) {
    return add(key, util::fixed(value, decimals));
}
KeyValues& KeyValues::add(const std::string& key, bool value) {
    return add(key, std::string(value ? "1" : "0"));
}
KeyValues& KeyValues::add(const std::string& key, unsigned long long value) {
    return add(key, std::to_string(value));
}

std::string Response::wire() const {
    std::string line;
    if (ok) {
        line = text.empty() ? "OK" : "OK " + text;
    } else {
        line = "ERR " + std::string(to_string(code)) + " " + (text.empty() ? "error" : text);
    }
    for (char& c : line)
        if (c == '\n' || c == '\r') c = ' ';
    return line + "\n";
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

std::optional<Request> parse_request(std::string_view line) {
    line = util::trim(line);
    if (line.empty()) return std::nullopt;
    for (const char c : line) {
        const auto u = static_cast<unsigned char>(c);
        if ((u < 0x20 && c != '\t') || u == 0x7F)
            throw CommandError(ErrorCode::BadRequest, "request contains control characters");
    }

    std::vector<std::string> tokens = util::split_whitespace(line);
    Request req;
    std::string first = tokens.front();
    // Legacy form "cmd=value" (as used by the original server).
    if (const auto eq = first.find('='); eq != std::string::npos) {
        req.command = util::to_lower(first.substr(0, eq));
        const std::string arg = first.substr(eq + 1);
        if (!arg.empty()) req.positional.push_back(arg);
    } else {
        req.command = util::to_lower(first);
    }
    if (req.command.empty()) throw CommandError(ErrorCode::BadRequest, "missing command name");

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (const auto eq = t.find('='); eq != std::string::npos) {
            const std::string key = util::to_lower(t.substr(0, eq));
            if (key.empty()) throw CommandError(ErrorCode::BadRequest, "empty option name in '" + t + "'");
            if (req.named.count(key))
                throw CommandError(ErrorCode::BadRequest, "duplicate option '" + key + "'");
            req.named[key] = t.substr(eq + 1);
        } else {
            req.positional.push_back(t);
        }
    }
    return req;
}

// ---------------------------------------------------------------------------
// Command handling
// ---------------------------------------------------------------------------

namespace {

void expect_positional(const Request& r, std::size_t max) {
    if (r.positional.size() > max)
        throw CommandError(ErrorCode::BadRequest,
                           "'" + r.command + "' takes at most " + std::to_string(max) + " argument(s)");
}

void allow_named(const Request& r, std::initializer_list<const char*> allowed) {
    for (const auto& [key, _] : r.named) {
        bool ok = false;
        for (const char* a : allowed) ok = ok || key == a;
        if (!ok)
            throw CommandError(ErrorCode::InvalidArgument,
                               "unknown option '" + key + "' for '" + r.command + "'");
    }
}

double number(const std::string& text, const char* what) {
    if (auto v = util::parse_double(text)) return *v;
    throw CommandError(ErrorCode::InvalidArgument,
                       std::string(what) + " must be a number, got '" + text + "'");
}

std::string object_name(const std::string& name) {
    if (name.empty() || name.size() > 64)
        throw CommandError(ErrorCode::InvalidArgument, "object name must be 1-64 characters");
    for (const char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '+' || c == '.' ||
              c == ':'))
            throw CommandError(ErrorCode::InvalidArgument,
                               "object name may only contain letters, digits and _ - + . :");
    }
    return name;
}

ExposureRequest exposure_request(const Request& r, FrameType type) {
    ExposureRequest req;
    req.type = type;
    if (auto it = r.named.find("exptime"); it != r.named.end()) req.exptime_s = number(it->second, "exptime");
    if (auto it = r.named.find("object"); it != r.named.end()) req.object = object_name(it->second);
    return req;
}

std::string temperature_payload(const TemperatureStatus& t) {
    return KeyValues()
        .add("reading", t.reading_c, 2)
        .add("setpoint", t.setpoint_c, 2)
        .add("locked", t.locked)
        .str();
}

bool tokens_equal(const std::string& a, const std::string& b) {
    // Constant-time comparison so the token can't be guessed byte by byte.
    unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
    const std::size_t n = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char ca = i < a.size() ? a[i] : 0;
        const char cb = i < b.size() ? b[i] : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }
    return diff == 0;
}

}  // namespace

const std::vector<std::string>& CommandHandler::commands() {
    static const std::vector<std::string> list = {
        "help",     "ping",   "version", "info", "status", "exptime", "shutter", "gain",    "temp",
        "setpoint", "expose", "dark",    "bias", "burst",  "auth",    "quit",    "shutdown"};
    return list;
}

CommandHandler::CommandHandler(CameraController& controller, ServerConfig server_config)
    : camera_(controller), server_(std::move(server_config)) {}

Response CommandHandler::handle_line(std::string_view line, Session& session) {
    try {
        const auto req = parse_request(line);
        if (!req) return Response::failure(ErrorCode::BadRequest, "empty request");
        return handle(*req, session);
    } catch (const CommandError& e) {
        return Response::failure(e.code(), e.what());
    } catch (const std::exception& e) {
        log::error("Unhandled error for '", std::string(util::trim(line)), "': ", e.what());
        return Response::failure(ErrorCode::Internal, e.what());
    }
}

Response CommandHandler::handle(const Request& r, Session& session) {
    const std::string& cmd = r.command;

    // Commands that never need authentication.
    if (cmd == "ping") {
        expect_positional(r, 0);
        return Response::success("pong");
    }
    if (cmd == "quit") {
        Response resp = Response::success("bye");
        resp.close_connection = true;
        return resp;
    }
    if (cmd == "help") {
        std::string list;
        for (const auto& c : commands()) list += (list.empty() ? "" : ",") + c;
        return Response::success(KeyValues().add("commands", list).add("docs", "docs/protocol.md").str());
    }
    if (cmd == "auth") {
        expect_positional(r, 1);
        if (server_.auth_token.empty()) return Response::success("auth=disabled");
        if (r.positional.size() != 1 || !tokens_equal(r.positional[0], server_.auth_token)) {
            log::warn("Failed authentication attempt from ", session.peer);
            return Response::failure(ErrorCode::Unauthorized, "invalid token");
        }
        session.authenticated = true;
        return Response::success("authenticated");
    }
    if (!server_.auth_token.empty() && !session.authenticated) {
        if (std::find(commands().begin(), commands().end(), cmd) == commands().end())
            return Response::failure(ErrorCode::UnknownCommand, "unknown command '" + cmd + "' (try 'help')");
        return Response::failure(ErrorCode::Unauthorized, "send 'auth <token>' first");
    }

    if (cmd == "version") {
        expect_positional(r, 0);
        return Response::success(KeyValues()
                                     .add("version", std::string(SUPERLOTIS_VERSION))
                                     .add("protocol", std::to_string(SUPERLOTIS_PROTOCOL_VERSION))
                                     .str());
    }
    if (cmd == "info") {
        expect_positional(r, 0);
        const CameraInfo i = camera_.info();
        return Response::success(KeyValues()
                                     .add("model", i.model)
                                     .add("serial", i.serial)
                                     .add("sensor", i.sensor)
                                     .add("width", static_cast<unsigned long long>(i.width))
                                     .add("height", static_cast<unsigned long long>(i.height))
                                     .add("simulated", i.simulated)
                                     .add("backend", camera_.config().camera.backend)
                                     .add("version", std::string(SUPERLOTIS_VERSION))
                                     .str());
    }
    if (cmd == "status") {
        expect_positional(r, 0);
        const ControllerStatus s = camera_.status();
        return Response::success(KeyValues()
                                     .add("state", std::string(to_string(s.state)))
                                     .add("exptime", s.exptime_s, 3)
                                     .add("shutter", std::string(to_string(s.shutter)))
                                     .add("gain", std::string(to_string(s.gain)))
                                     .add("temp", s.temperature.reading_c, 2)
                                     .add("setpoint", s.temperature.setpoint_c, 2)
                                     .add("locked", s.temperature.locked)
                                     .add("temp_age", s.temperature_age_s, 1)
                                     .add("progress", s.progress, 3)
                                     .add("elapsed", s.elapsed_s, 2)
                                     .add("frames", static_cast<unsigned long long>(s.frames_written))
                                     .add("last_file", s.last_file)
                                     .add("last_error", s.last_error)
                                     .str());
    }
    if (cmd == "exptime") {
        expect_positional(r, 1);
        allow_named(r, {});
        if (!r.positional.empty()) camera_.set_exptime(number(r.positional[0], "exptime"));
        return Response::success(util::fixed(camera_.exptime(), 3));
    }
    if (cmd == "shutter" || cmd == "shutter_mode") {
        expect_positional(r, 1);
        if (!r.positional.empty()) {
            const auto m = parse_shutter_mode(r.positional[0]);
            if (!m) throw CommandError(ErrorCode::InvalidArgument, "shutter must be normal, closed or open");
            camera_.set_shutter(*m);
        }
        return Response::success(std::string(to_string(camera_.shutter())));
    }
    if (cmd == "gain" || cmd == "analog_gain") {
        expect_positional(r, 1);
        if (!r.positional.empty()) {
            const auto g = parse_adc_gain(r.positional[0]);
            if (!g) throw CommandError(ErrorCode::InvalidArgument, "gain must be low, medium or high");
            camera_.set_gain(*g);
        }
        return Response::success(std::string(to_string(camera_.gain())));
    }
    if (cmd == "temp") {
        // Legacy "temp=<C>" changed the set point; keep that working.
        expect_positional(r, 1);
        if (!r.positional.empty()) camera_.set_setpoint(number(r.positional[0], "temperature"));
        return Response::success(temperature_payload(camera_.temperature()));
    }
    if (cmd == "setpoint") {
        expect_positional(r, 1);
        if (!r.positional.empty()) camera_.set_setpoint(number(r.positional[0], "set point"));
        return Response::success(util::fixed(camera_.temperature().setpoint_c, 2));
    }
    if (cmd == "expose" || cmd == "dark" || cmd == "bias") {
        const FrameType type = cmd == "expose" ? FrameType::Object
                               : cmd == "dark" ? FrameType::Dark
                                               : FrameType::Bias;
        expect_positional(r, 0);
        if (type == FrameType::Object)
            allow_named(r, {"exptime", "object"});
        else if (type == FrameType::Dark)
            allow_named(r, {"exptime"});
        else
            allow_named(r, {});
        const auto path = camera_.expose(exposure_request(r, type));
        return Response::success(
            KeyValues().add("file", path.string()).add("type", std::string(to_string(type))).str());
    }
    if (cmd == "burst") {
        expect_positional(r, 1);
        allow_named(r, {"exptime", "object"});
        if (r.positional.empty())
            throw CommandError(ErrorCode::InvalidArgument, "usage: burst <count> [exptime=S] [object=NAME]");
        const auto n = util::parse_int(r.positional[0]);
        if (!n) throw CommandError(ErrorCode::InvalidArgument, "burst count must be an integer");
        if (*n < 1 || *n > camera_.config().limits.max_burst)
            throw CommandError(ErrorCode::OutOfRange, "burst count must be between 1 and " +
                                                          std::to_string(camera_.config().limits.max_burst));
        const auto files = camera_.burst(static_cast<int>(*n), exposure_request(r, FrameType::Object));
        std::string joined;
        for (const auto& f : files) joined += (joined.empty() ? "" : ",") + percent_encode(f.string());
        return Response::success("count=" + std::to_string(files.size()) + " files=" + joined);
    }
    if (cmd == "shutdown" || cmd == "exit") {
        if (!server_.allow_remote_shutdown)
            return Response::failure(ErrorCode::NotPermitted,
                                     "remote shutdown is disabled (server.allow_remote_shutdown)");
        log::warn("Shutdown requested by ", session.peer);
        Response resp = Response::success("shutting down");
        resp.close_connection = true;
        resp.shutdown_server = true;
        return resp;
    }
    return Response::failure(ErrorCode::UnknownCommand, "unknown command '" + cmd + "' (try 'help')");
}

}  // namespace superlotis::protocol
