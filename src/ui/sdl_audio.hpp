#pragma once

#include <cstddef>
#include <vector>

#include <SDL2/SDL.h>

namespace famidec {

// Push-model mono float audio output. push() is safe to call from the DSP
// thread (SDL_QueueAudio is thread-safe per device).
class SdlAudioOut {
public:
    bool init(int sample_rate) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        SDL_AudioSpec want{};
        want.freq = sample_rate;
        want.format = AUDIO_F32SYS;
        want.channels = 1;
        want.samples = 1024;
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, 0);
        if (dev_ == 0) return false;
        rate_ = sample_rate;
        SDL_PauseAudioDevice(dev_, 0);
        return true;
    }

    ~SdlAudioOut() {
        if (dev_) SDL_CloseAudioDevice(dev_);
    }

    bool ok() const { return dev_ != 0; }

    float queued_ms() const {
        if (!dev_) return 0.0f;
        return static_cast<float>(SDL_GetQueuedAudioSize(dev_)) /
               sizeof(float) / static_cast<float>(rate_) * 1000.0f;
    }

    void push(const float* data, size_t n) {
        if (!dev_ || n == 0) return;
        // Keep queued audio near ~100 ms: startup bursts (DSP catching up on
        // the ring backlog) and clock drift otherwise accumulate as fixed
        // playback latency. Hard-resync if far over, soft-drop just above.
        const Uint32 target =
            static_cast<Uint32>(rate_) / 10 * sizeof(float);  // 100 ms
        Uint32 queued = SDL_GetQueuedAudioSize(dev_);
        if (queued > target * 2) {
            SDL_ClearQueuedAudio(dev_);
            queued = 0;
        }
        if (queued > target) return;
        SDL_QueueAudio(dev_, data, static_cast<Uint32>(n * sizeof(float)));
    }

private:
    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    int rate_ = 48000;
};

}  // namespace famidec
