#pragma once

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace famidec {

// In-place iterative radix-2 FFT (n must be a power of two).
inline void fft(std::vector<std::complex<float>>& x) {
    size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = x[i + k];
                auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Averaged PSD of int8 IQ data, printed as an ASCII chart to stdout.
// Center frequency / sample rate are used only for axis labels.
inline void print_psd(const int8_t* iq, size_t n_samples, double center_hz,
                      double sample_rate) {
    constexpr size_t kFft = 1024;
    size_t segments = n_samples / kFft;
    if (segments == 0) return;
    std::vector<double> psd(kFft, 0.0);
    std::vector<std::complex<float>> buf(kFft);
    for (size_t s = 0; s < segments; ++s) {
        const int8_t* p = iq + s * kFft * 2;
        for (size_t i = 0; i < kFft; ++i) {
            // Hann window
            float w = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) *
                                             i / (kFft - 1));
            buf[i] = std::complex<float>(p[2 * i] / 128.0f,
                                         p[2 * i + 1] / 128.0f) *
                     w;
        }
        fft(buf);
        for (size_t i = 0; i < kFft; ++i) psd[i] += std::norm(buf[i]);
    }
    // Collapse to 64 display bins, fftshifted so DC is centered.
    constexpr int kBins = 64;
    double maxv = 1e-12;
    std::vector<double> bins(kBins, 0.0);
    for (int b = 0; b < kBins; ++b) {
        size_t lo = b * kFft / kBins;
        for (size_t i = lo; i < lo + kFft / kBins; ++i) {
            size_t shifted = (i + kFft / 2) % kFft;  // fftshift
            bins[b] += psd[shifted];
        }
        maxv = std::max(maxv, bins[b]);
    }
    std::printf("PSD  center %.3f MHz, span %.1f MHz  (0 dB = peak)\n",
                center_hz / 1e6, sample_rate / 1e6);
    for (int b = 0; b < kBins; ++b) {
        double freq =
            center_hz + (static_cast<double>(b) / kBins - 0.5) * sample_rate;
        double db = 10.0 * std::log10(bins[b] / maxv);
        int stars = static_cast<int>(std::max(0.0, (db + 60.0)) * 50.0 / 60.0);
        std::printf("%9.3f MHz %7.1f dB |", freq / 1e6, db);
        for (int i = 0; i < stars; ++i) std::putchar('#');
        std::putchar('\n');
    }
}

}  // namespace famidec
