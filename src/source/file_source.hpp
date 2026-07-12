#pragma once

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "../config.hpp"
#include "sample_source.hpp"

namespace famidec {

// Plays back a .cs8 recording (interleaved int8 IQ, e.g. from
// hackrf_transfer). Optionally loops and paces at the real sample rate.
class FileSource : public ISampleSource {
public:
    FileSource(const Config& cfg, bool pace_realtime)
        : cfg_(cfg), pace_(pace_realtime) {}
    ~FileSource() override { stop(); }

    bool start() override {
        fp_ = std::fopen(cfg_.file_path.c_str(), "rb");
        start_time_ = std::chrono::steady_clock::now();
        return fp_ != nullptr;
    }

    void stop() override {
        if (fp_) {
            std::fclose(fp_);
            fp_ = nullptr;
        }
    }

    size_t read(uint8_t* buf, size_t len) override {
        if (!fp_) return 0;
        size_t got = std::fread(buf, 1, len, fp_);
        while (got < len && cfg_.loop) {
            std::rewind(fp_);
            size_t more = std::fread(buf + got, 1, len - got, fp_);
            if (more == 0) break;
            got += more;
        }
        if (pace_ && got > 0) {
            total_bytes_ += got;
            double elapsed_target =
                static_cast<double>(total_bytes_ / 2) / cfg_.sample_rate;
            auto target = start_time_ + std::chrono::duration_cast<
                                            std::chrono::steady_clock::duration>(
                                            std::chrono::duration<double>(
                                                elapsed_target));
            std::this_thread::sleep_until(target);
        }
        return got;
    }

private:
    const Config& cfg_;
    bool pace_;
    std::FILE* fp_ = nullptr;
    uint64_t total_bytes_ = 0;
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace famidec
