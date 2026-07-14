#pragma once

#include <atomic>
#include <string>

#include <libhackrf/hackrf.h>

#include "../config.hpp"
#include "../dsp/ring_buffer.hpp"
#include "sample_source.hpp"

namespace famidec {

class HackRfSource : public ISampleSource {
public:
    explicit HackRfSource(const Config& cfg);
    ~HackRfSource() override;

    bool start() override;
    void stop() override;
    size_t read(uint8_t* buf, size_t len) override;

    uint64_t dropped_bytes() const override {
        return dropped_.load(std::memory_order_relaxed);
    }
    uint64_t total_bytes() const override {
        return total_.load(std::memory_order_relaxed);
    }
    uint64_t clipped_samples() const override {
        return clipped_.load(std::memory_order_relaxed);
    }
    float ring_fill() const override;

    bool set_gains(int lna, int vga);
    int lna() const { return lna_; }
    int vga() const { return vga_; }

    const std::string& error() const { return error_; }

private:
    static int rx_callback(hackrf_transfer* transfer);

    const Config& cfg_;
    hackrf_device* dev_ = nullptr;
    SpscRing ring_;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> clipped_{0};
    std::atomic<bool> running_{false};
    int lna_, vga_;
    std::string error_;
};

}  // namespace famidec
