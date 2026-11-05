// SPDX-License-Identifier: MIT
// Line-based text protocol (v2). See docs/protocol.md for the full spec.
//
//   request  := command [SP arg]* LF          e.g. "expose exptime=5 object=M31"
//             | command "=" value LF          legacy form, e.g. "exptime=5"
//   response := "OK" [SP payload] LF
//             | "ERR" SP code SP message LF
//
// Payload values in key=value form are percent-encoded (space, '%', '=', ','
// and non-printable bytes), so they never contain separators.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "superlotis/controller.hpp"

namespace superlotis::protocol {

struct Request {
    std::string command;  ///< lower-cased
    std::vector<std::string> positional;
    std::map<std::string, std::string> named;  ///< keys lower-cased
};

/// Parses one request line. Returns nullopt for blank lines.
/// Throws CommandError(BadRequest) for malformed input.
std::optional<Request> parse_request(std::string_view line);

struct Response {
    bool ok = true;
    ErrorCode code = ErrorCode::Internal;
    std::string text;  ///< payload (OK) or message (ERR)
    bool close_connection = false;
    bool shutdown_server = false;

    static Response success(std::string payload = {}) {
        return Response{true, ErrorCode::Internal, std::move(payload)};
    }
    static Response failure(ErrorCode code, std::string message) {
        return Response{false, code, std::move(message)};
    }
    /// Serialized form including the trailing newline.
    [[nodiscard]] std::string wire() const;
};

std::string percent_encode(std::string_view value);
std::string percent_decode(std::string_view value);

/// Builds a "k1=v1 k2=v2" payload with percent-encoded values.
class KeyValues {
public:
    KeyValues& add(const std::string& key, const std::string& value);
    KeyValues& add(const std::string& key, const char* value) { return add(key, std::string(value)); }
    KeyValues& add(const std::string& key, double value, int decimals);
    KeyValues& add(const std::string& key, bool value);
    KeyValues& add(const std::string& key, unsigned long long value);
    [[nodiscard]] std::string str() const { return out_; }

private:
    std::string out_;
};

/// Per-connection state.
struct Session {
    bool authenticated = false;
    std::string peer;
};

/// Translates requests into controller calls. Thread-safe: one handler is
/// shared by all connections.
class CommandHandler {
public:
    CommandHandler(CameraController& controller, ServerConfig server_config);

    /// Handles one raw request line. Never throws.
    Response handle_line(std::string_view line, Session& session);
    Response handle(const Request& request, Session& session);

    [[nodiscard]] static const std::vector<std::string>& commands();

private:
    CameraController& camera_;
    ServerConfig server_;
};

}  // namespace superlotis::protocol
