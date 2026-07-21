#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "fir.hpp"
#include "nco.hpp"

namespace famidec {

// Streaming decimating FIR: convolves at a stride, carrying history and
// decimation phase across blocks.
template <typename Sample>
class DecimFir {
public:
    DecimFir(std::vector<float> taps, int decim)
        : taps_(std::move(taps)), decim_(decim), hist_(taps_.size() - 1, Sample{}) {}

    void process(const Sample* in, size_t n, std::vector<Sample>& out) {
        size_t nh = taps_.size() - 1;
        work_.resize(nh + n);
        std::copy(hist_.begin(), hist_.end(), work_.begin());
        std::copy(in, in + n, work_.begin() + static_cast<long>(nh));
        size_t nt = taps_.size();
        size_t i = phase_;
        while (i < n) {
            Sample acc{};
            const Sample* w = work_.data() + i;
            for (size_t k = 0; k < nt; ++k) acc += w[k] * taps_[nt - 1 - k];
            out.push_back(acc);
            i += static_cast<size_t>(decim_);
        }
        phase_ = i - n;
        std::copy(work_.end() - static_cast<long>(nh), work_.end(), hist_.begin());
    }

private:
    std::vector<float> taps_;
    int decim_;
    size_t phase_ = 0;
    std::vector<Sample> hist_;
    std::vector<Sample> work_;
};

// Mono FM demodulator over the post-mixer IQ stream. Two presets:
//  - tv():  TV intercarrier audio at video +4.5 MHz, +-25 kHz deviation,
//           75 us de-emphasis.
//  - wfm(): broadcast FM at 0 Hz (station tuned to baseband), +-75 kHz
//           deviation, 50 us de-emphasis (Japan).
class FmAudioDemod {
public:
    FmAudioDemod(double fs, double volume, double carrier_offset_hz,
                 double if_cutoff_hz, double deviation_hz, double deemph_tau)
        : nco_(-carrier_offset_hz, fs),
          stage1_(design_lowpass(if_cutoff_hz, fs, 331), 25),
          stage2_(design_lowpass(15e3, fs / 25.0, 65), 8),
          fs2_(fs / 25.0),
          volume_(static_cast<float>(volume)) {
        deemph_a_ =
            1.0f - std::exp(-1.0f / static_cast<float>(fs2_ * deemph_tau));
        dc_a_ = static_cast<float>(2.0 * M_PI * 20.0 / fs2_);
        disc_gain_ = static_cast<float>(fs2_ / (2.0 * M_PI * deviation_hz));
    }

    static FmAudioDemod tv(double fs, double volume) {
        return FmAudioDemod(fs, volume, 4.5e6, 130e3, 25e3, 75e-6);
    }
    static FmAudioDemod wfm(double fs, double volume) {
        return FmAudioDemod(fs, volume, 0.0, 110e3, 75e3, 50e-6);
    }

    double out_rate() const { return fs2_ / 8.0; }  // 50 kHz at 10 MSPS

    // Returns demodulated audio samples appended to `out`.
    void process(const std::complex<float>* in, size_t n,
                 std::vector<float>& out) {
        mixed_.resize(n);
        for (size_t i = 0; i < n; ++i) mixed_[i] = in[i] * nco_.next();
        d1_.clear();
        stage1_.process(mixed_.data(), n, d1_);
        demod_.clear();
        demod_.reserve(d1_.size());
        for (auto& x : d1_) {
            std::complex<float> p = x * std::conj(prev_);
            prev_ = x;
            float a = std::atan2(p.imag(), p.real()) * disc_gain_;
            deemph_ += deemph_a_ * (a - deemph_);
            dc_ += dc_a_ * (deemph_ - dc_);
            demod_.push_back(deemph_ - dc_);
        }
        size_t before = out.size();
        stage2_.process(demod_.data(), demod_.size(), out);
        for (size_t i = before; i < out.size(); ++i) {
            float v = out[i] * volume_;
            out[i] = std::fmax(-1.0f, std::fmin(1.0f, v));
        }
    }

private:
    Nco nco_;
    DecimFir<std::complex<float>> stage1_;
    DecimFir<float> stage2_;
    double fs2_;
    float volume_;
    float deemph_a_, dc_a_, disc_gain_;
    std::complex<float> prev_{1.0f, 0.0f};
    float deemph_ = 0.0f, dc_ = 0.0f;
    std::vector<std::complex<float>> mixed_, d1_;
    std::vector<float> demod_;
};

}  // namespace famidec
