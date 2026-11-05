// SPDX-License-Identifier: MIT
#include "superlotis/log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

#include "superlotis/util.hpp"

namespace superlotis::log {

namespace {
std::atomic<Level> g_level{Level::Info};
std::mutex g_mutex;

const char* name(Level l) {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}
}  // namespace

void set_level(Level level) noexcept {
    g_level.store(level);
}
Level level() noexcept {
    return g_level.load();
}

std::optional<Level> parse_level(std::string_view s) noexcept {
    const std::string v = util::to_lower(s);
    if (v == "debug") return Level::Debug;
    if (v == "info") return Level::Info;
    if (v == "warn" || v == "warning") return Level::Warn;
    if (v == "error") return Level::Error;
    return std::nullopt;
}

void write(Level lvl, const std::string& message) {
    const std::string ts = util::iso8601_utc(std::chrono::system_clock::now());
    const std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "%sZ %s %s\n", ts.c_str(), name(lvl), message.c_str());
    std::fflush(stderr);
}

}  // namespace superlotis::log
