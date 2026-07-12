#pragma once

#include <cstddef>
#include <cstdint>

namespace famidec {

// Common interface for live HackRF input and .cs8 file playback so the whole
// decode pipeline is identical for both.
class ISampleSource {
public:
    virtual ~ISampleSource() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    // Blocking read of interleaved int8 IQ bytes. Returns bytes read;
    // 0 means end of stream.
    virtual size_t read(uint8_t* buf, size_t len) = 0;

    virtual uint64_t dropped_bytes() const { return 0; }
    virtual uint64_t clipped_samples() const { return 0; }
    virtual float ring_fill() const { return 0.0f; }
};

}  // namespace famidec
