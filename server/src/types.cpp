// SPDX-License-Identifier: MIT
#include "superlotis/types.hpp"

#include "superlotis/util.hpp"

namespace superlotis {

std::string_view to_string(ShutterMode mode) noexcept {
    switch (mode) {
        case ShutterMode::Normal: return "normal";
        case ShutterMode::AlwaysClosed: return "closed";
        case ShutterMode::AlwaysOpen: return "open";
    }
    return "unknown";
}

std::string_view to_string(AdcGain gain) noexcept {
    switch (gain) {
        case AdcGain::Low: return "low";
        case AdcGain::Medium: return "medium";
        case AdcGain::High: return "high";
    }
    return "unknown";
}

std::string_view to_string(FrameType type) noexcept {
    switch (type) {
        case FrameType::Object: return "object";
        case FrameType::Dark: return "dark";
        case FrameType::Bias: return "bias";
    }
    return "unknown";
}

std::optional<ShutterMode> parse_shutter_mode(std::string_view text) noexcept {
    const std::string s = util::to_lower(util::trim(text));
    if (s == "normal" || s == "1") return ShutterMode::Normal;
    if (s == "closed" || s == "always_closed" || s == "2") return ShutterMode::AlwaysClosed;
    if (s == "open" || s == "always_open" || s == "3") return ShutterMode::AlwaysOpen;
    return std::nullopt;
}

std::optional<AdcGain> parse_adc_gain(std::string_view text) noexcept {
    const std::string s = util::to_lower(util::trim(text));
    if (s == "low" || s == "1") return AdcGain::Low;
    if (s == "medium" || s == "2") return AdcGain::Medium;
    if (s == "high" || s == "3") return AdcGain::High;
    return std::nullopt;
}

}  // namespace superlotis
