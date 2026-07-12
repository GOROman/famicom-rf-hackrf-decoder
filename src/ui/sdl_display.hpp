#pragma once

#include <string>

#include <SDL2/SDL.h>

#include "../dsp/frame.hpp"

namespace famidec {

struct OsdStats {
    bool line_locked = false;
    float burst_amp = 0.0f;
    float ring_fill = 0.0f;
    uint64_t dropped = 0;
    uint64_t clipped = 0;
    uint64_t frames = 0;
    int lna = 0, vga = 0;
};

// Key actions the main loop should react to.
enum class KeyAction {
    None,
    Quit,
    GainLnaUp,
    GainLnaDown,
    GainVgaUp,
    GainVgaDown,
    ToggleColor,
    Screenshot,
};

class SdlDisplay {
public:
    bool init(const std::string& title);
    ~SdlDisplay();

    // Poll events; returns first pending action.
    KeyAction poll();

    void render(const Frame* frame, const OsdStats& stats);
    bool screenshot(const Frame& frame, const std::string& path);

private:
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    Frame last_frame_;
    bool have_frame_ = false;
};

}  // namespace famidec
