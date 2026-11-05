// SPDX-License-Identifier: MIT
// Test double that records every call and can be told to fail or to block
// mid-exposure, so concurrency behaviour can be tested deterministically.
#pragma once

#include <condition_variable>
#include <mutex>
#include <vector>

#include "superlotis/camera_backend.hpp"
#include "superlotis/config.hpp"

namespace superlotis::test_support {

struct AcquireCall {
    double exposure_s;
    ShutterMode shutter;
};

class SpyBackend final : public CameraBackend {
public:
    struct State {
        std::mutex mu;
        std::condition_variable cv;
        std::vector<AcquireCall> acquires;
        AdcGain gain = AdcGain::High;
        double setpoint = -60.0;
        double reading = -60.0;
        bool locked = true;
        bool fail_acquire = false;
        bool ignore_gain_writes = false;  ///< simulate the original "setter does nothing" bug
        bool hold = false;                ///< block inside acquire() until released
        bool in_acquire = false;
        int temperature_reads = 0;
    };

    explicit SpyBackend(std::shared_ptr<State> s) : s_(std::move(s)) {}

    CameraInfo info() const override { return {"Spy", "SPY1", "spy", 8, 4, true}; }
    void set_adc_gain(AdcGain g) override {
        std::lock_guard<std::mutex> l(s_->mu);
        if (!s_->ignore_gain_writes) s_->gain = g;
    }
    AdcGain adc_gain() override {
        std::lock_guard<std::mutex> l(s_->mu);
        return s_->gain;
    }
    void set_temperature_setpoint(double c) override {
        std::lock_guard<std::mutex> l(s_->mu);
        s_->setpoint = c;
    }
    TemperatureStatus temperature() override {
        std::lock_guard<std::mutex> l(s_->mu);
        ++s_->temperature_reads;
        return {s_->reading, s_->setpoint, s_->locked};
    }
    Frame acquire(double exposure_s, ShutterMode shutter) override {
        std::unique_lock<std::mutex> l(s_->mu);
        s_->acquires.push_back({exposure_s, shutter});
        s_->in_acquire = true;
        s_->cv.notify_all();
        s_->cv.wait(l, [&] { return !s_->hold; });
        s_->in_acquire = false;
        if (s_->fail_acquire) throw CameraError("spy: simulated hardware fault");
        Frame f;
        f.width = 8;
        f.height = 4;
        f.pixels.assign(32, 0);
        for (std::size_t i = 0; i < f.pixels.size(); ++i) f.pixels[i] = static_cast<std::uint16_t>(i * 2000);
        return f;
    }

private:
    std::shared_ptr<State> s_;
};

inline void release(SpyBackend::State& s) {
    std::lock_guard<std::mutex> l(s.mu);
    s.hold = false;
    s.cv.notify_all();
}

inline void wait_until_acquiring(SpyBackend::State& s) {
    std::unique_lock<std::mutex> l(s.mu);
    s.cv.wait(l, [&] { return s.in_acquire; });
}

}  // namespace superlotis::test_support
