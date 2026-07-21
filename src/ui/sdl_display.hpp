#pragma once

#include <string>

#include <SDL2/SDL.h>

#include "../dsp/frame.hpp"

namespace famidec {

struct OsdStats {
    bool line_locked = false;   // H-SYNC
    bool vsync_locked = false;  // V-SYNC (frames advancing)
    float burst_amp = 0.0f;
    float ring_fill = 0.0f;
    uint64_t dropped = 0;
    uint64_t clipped = 0;
    uint64_t frames = 0;
    int lna = 0, vga = 0;
    double freq_mhz = 0.0;      // video carrier
    double audio_mhz = 0.0;     // FM audio carrier (video +4.5 MHz)
    int channel = 0;            // 1/2 (nearest VHF channel), 0 = unknown
    float fps = 0.0f;           // decoded frames per second
    float video_latency_ms = 0.0f;  // capture -> displayed frame
    float audio_latency_ms = 0.0f;  // queued audio
    bool show_help = false;
    bool crt = false;  // CRT (barrel + scanline + vignette) emulation
    bool recording = false;
    float rec_seconds = 0.0f;
    bool show_hud = true;  // top-right status lines
    bool radio = false;    // broadcast FM radio mode
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
    ToggleHelp,
    FreqUp,      // right arrow: +50 kHz
    FreqDown,    // left arrow: -50 kHz
    FreqUpBig,   // up arrow: +1 MHz
    FreqDownBig, // down arrow: -1 MHz
    ToggleCrt,
    ToggleRecord,
    ToggleHud,
    ToggleRadio,
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
    Frame osd_frame_;
    Frame crt_frame_;
    bool have_frame_ = false;
};

}  // namespace famidec
