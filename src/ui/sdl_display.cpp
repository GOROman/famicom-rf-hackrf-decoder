#include "sdl_display.hpp"

#include <cstdio>

namespace famidec {

bool SdlDisplay::init(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    win_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, Frame::kWidth,
                            Frame::kHeight,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win_) return false;
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED |
                                            SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, 0);
    if (!ren_) return false;
    SDL_RenderSetLogicalSize(ren_, Frame::kWidth, Frame::kHeight);
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ABGR8888,
                             SDL_TEXTUREACCESS_STREAMING, Frame::kWidth,
                             Frame::kHeight);
    return tex_ != nullptr;
}

SdlDisplay::~SdlDisplay() {
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

KeyAction SdlDisplay::poll() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return KeyAction::Quit;
        if (ev.type == SDL_KEYDOWN) {
            bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
            switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    return KeyAction::Quit;
                case SDLK_l:
                    return shift ? KeyAction::GainLnaDown : KeyAction::GainLnaUp;
                case SDLK_g:
                    return shift ? KeyAction::GainVgaDown : KeyAction::GainVgaUp;
                case SDLK_c:
                    return KeyAction::ToggleColor;
                case SDLK_s:
                    return KeyAction::Screenshot;
                default:
                    break;
            }
        }
    }
    return KeyAction::None;
}

void SdlDisplay::render(const Frame* frame, const OsdStats& stats) {
    if (frame) {
        SDL_UpdateTexture(tex_, nullptr, frame->rgba.data(),
                          Frame::kWidth * 4);
        last_frame_ = *frame;
        have_frame_ = true;
    }
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    // Minimal OSD: lock indicator (green/red square) and ring-fill bar.
    SDL_Rect lock{8, 8, 12, 12};
    if (stats.line_locked)
        SDL_SetRenderDrawColor(ren_, 0, 220, 0, 255);
    else
        SDL_SetRenderDrawColor(ren_, 220, 0, 0, 255);
    SDL_RenderFillRect(ren_, &lock);
    SDL_Rect burst{24, 8, 12, 12};
    if (stats.burst_amp >= 3.0f)
        SDL_SetRenderDrawColor(ren_, 0, 220, 0, 255);
    else
        SDL_SetRenderDrawColor(ren_, 180, 180, 0, 255);
    SDL_RenderFillRect(ren_, &burst);
    SDL_Rect fill_bg{8, 26, 100, 6};
    SDL_SetRenderDrawColor(ren_, 60, 60, 60, 255);
    SDL_RenderFillRect(ren_, &fill_bg);
    SDL_Rect fill{8, 26, static_cast<int>(stats.ring_fill * 100.0f), 6};
    SDL_SetRenderDrawColor(ren_, 0, 160, 220, 255);
    SDL_RenderFillRect(ren_, &fill);
    SDL_RenderPresent(ren_);
}

bool SdlDisplay::screenshot(const Frame& frame, const std::string& path) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(frame.rgba.data()), Frame::kWidth,
        Frame::kHeight, 32, Frame::kWidth * 4, SDL_PIXELFORMAT_ABGR8888);
    if (!surf) return false;
    bool ok = SDL_SaveBMP(surf, path.c_str()) == 0;
    SDL_FreeSurface(surf);
    return ok;
}

}  // namespace famidec
