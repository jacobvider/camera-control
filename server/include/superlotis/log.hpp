// SPDX-License-Identifier: MIT
// Minimal thread-safe leveled logger writing timestamped lines to stderr
// (captured by journald when run under systemd).
#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace superlotis::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void set_level(Level level) noexcept;
Level level() noexcept;
std::optional<Level> parse_level(std::string_view name) noexcept;
void write(Level level, const std::string& message);

template <typename... Args>
void emit(Level lvl, Args&&... args) {
    if (lvl < level()) return;
    std::ostringstream os;
    (os << ... << args);
    write(lvl, os.str());
}

template <typename... Args>
void debug(Args&&... a) {
    emit(Level::Debug, std::forward<Args>(a)...);
}
template <typename... Args>
void info(Args&&... a) {
    emit(Level::Info, std::forward<Args>(a)...);
}
template <typename... Args>
void warn(Args&&... a) {
    emit(Level::Warn, std::forward<Args>(a)...);
}
template <typename... Args>
void error(Args&&... a) {
    emit(Level::Error, std::forward<Args>(a)...);
}

}  // namespace superlotis::log
