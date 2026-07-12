#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace famidec {

// Single-producer single-consumer lock-free byte ring buffer.
// Capacity must be a power of two.
class SpscRing {
public:
    explicit SpscRing(size_t capacity) : buf_(capacity), mask_(capacity - 1) {
        // capacity must be power of two
    }

    size_t capacity() const { return buf_.size(); }

    size_t readable() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_relaxed);
    }

    size_t writable() const {
        return capacity() - (head_.load(std::memory_order_relaxed) -
                             tail_.load(std::memory_order_acquire));
    }

    // Producer: push all-or-nothing. Returns false (drop) if not enough room.
    bool push(const uint8_t* data, size_t len) {
        if (writable() < len) return false;
        uint64_t h = head_.load(std::memory_order_relaxed);
        size_t idx = h & mask_;
        size_t first = std::min(len, buf_.size() - idx);
        std::memcpy(buf_.data() + idx, data, first);
        std::memcpy(buf_.data(), data + first, len - first);
        head_.store(h + len, std::memory_order_release);
        return true;
    }

    // Consumer: pop up to len bytes, returns bytes actually popped.
    size_t pop(uint8_t* out, size_t len) {
        size_t avail = readable();
        size_t n = std::min(avail, len);
        if (n == 0) return 0;
        uint64_t t = tail_.load(std::memory_order_relaxed);
        size_t idx = t & mask_;
        size_t first = std::min(n, buf_.size() - idx);
        std::memcpy(out, buf_.data() + idx, first);
        std::memcpy(out + first, buf_.data(), n - first);
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<uint8_t> buf_;
    size_t mask_;
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
};

}  // namespace famidec
