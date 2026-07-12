#pragma once

#include <algorithm>
#include <cmath>

namespace famidec {

// Tracks sync-tip and blanking levels of the raw (detected, positive-going
// sync because of negative modulation) composite and converts to IRE:
// sync tip = -40 IRE, blanking = 0 IRE, white ~ +100 IRE.
class Agc {
public:
    // Bootstrap from raw extrema over an observation window (several dozen
    // lines) before trusting the levels; refined per line afterwards.
    static constexpr int kBootstrapSamples = 50000;  // 5 ms at 10 MSPS

    void bootstrap(float raw) {
        peak_ = std::max(peak_, raw);
        floor_ = std::min(floor_, raw);
        if (++boot_count_ >= kBootstrapSamples && peak_ > floor_ + 1e-4f) {
            tip_ = peak_;
            // Full modulation range tip..white spans 140 IRE.
            blank_ = peak_ - (peak_ - floor_) * (40.0f / 140.0f);
            seeded_ = true;
        }
    }

    bool seeded() const { return seeded_; }

    // Per-line refinement with measured sync tip and back-porch average.
    void update(float tip_meas, float blank_meas) {
        if (tip_meas <= blank_meas) return;  // implausible, skip
        tip_ = tip_ + 0.05f * (tip_meas - tip_);
        blank_ = blank_ + 0.05f * (blank_meas - blank_);
        seeded_ = true;
    }

    // Per-line refinement from measurements taken in the IRE domain (i.e.
    // after conversion with the current mapping): invert the mapping to get
    // the implied raw levels, then track them.
    void update_from_ire(float tip_ire, float blank_ire) {
        update(from_ire(tip_ire), from_ire(blank_ire));
    }

    float from_ire(float ire) const {
        float span = tip_ - blank_;
        return blank_ - ire * span / 40.0f;
    }

    float to_ire(float raw) const {
        float span = tip_ - blank_;  // 40 IRE worth of raw amplitude
        if (span < 1e-6f) return 0.0f;
        return (blank_ - raw) * (40.0f / span);
    }

    // Gain for band-pass filtered raw (the blank offset is DC-rejected by
    // the filter). Negative: IRE is inverted relative to raw.
    float chroma_scale() const {
        float span = tip_ - blank_;
        if (span < 1e-6f) return 0.0f;
        return -40.0f / span;
    }

    float tip() const { return tip_; }
    float blank() const { return blank_; }

private:
    float tip_ = 1.0f;
    float blank_ = 0.7f;
    float peak_ = 0.0f;
    float floor_ = 1e9f;
    int boot_count_ = 0;
    bool seeded_ = false;
};

}  // namespace famidec
