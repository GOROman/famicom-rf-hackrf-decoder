#include "sdl_display.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace famidec {

namespace {

// Minimal 5x7 bitmap font (rows top-down, 5 LSBs used, bit4 = left column).
const uint8_t* glyph5x7(char c) {
    static const uint8_t kDigits[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    };
    static const uint8_t kColon[7] = {0x00,0x04,0x00,0x00,0x04,0x00,0x00};
    static const uint8_t kDot[7]   = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    static const uint8_t kDash[7]  = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    static const uint8_t kSpace[7] = {0,0,0,0,0,0,0};
    static const uint8_t kLetters[26][7] = {
        /*A*/{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        /*B*/{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        /*C*/{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        /*D*/{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        /*E*/{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        /*F*/{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        /*G*/{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        /*H*/{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        /*I*/{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        /*J*/{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
        /*K*/{0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        /*L*/{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        /*M*/{0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        /*N*/{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        /*O*/{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        /*P*/{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        /*Q*/{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        /*R*/{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        /*S*/{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
        /*T*/{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        /*U*/{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        /*V*/{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
        /*W*/{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        /*X*/{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        /*Y*/{0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        /*Z*/{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    };
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'];
    switch (c) {
        case ':': return kColon;
        case '.': return kDot;
        case '-': return kDash;
        default: return kSpace;
    }
}

// CRT emulation: barrel distortion + scanlines + vignette, done as a
// precomputed per-pixel LUT (source index + 8-bit gain) so the per-frame
// cost is a single indexed copy with a multiply.
struct CrtLut {
    struct Entry {
        int32_t src;   // source pixel index, -1 = outside tube
        uint16_t gain; // 0..256 brightness
    };
    std::vector<Entry> map;

    CrtLut() {
        map.resize(static_cast<size_t>(Frame::kWidth) * Frame::kHeight);
        const double cx = Frame::kWidth / 2.0, cy = Frame::kHeight / 2.0;
        const double k1 = 0.055;  // barrel strength
        for (int y = 0; y < Frame::kHeight; ++y) {
            for (int x = 0; x < Frame::kWidth; ++x) {
                double nx = (x - cx) / cx, ny = (y - cy) / cy;
                double r2 = nx * nx + ny * ny;
                double f = 1.0 + k1 * r2;
                int sx = static_cast<int>(cx + nx * f * cx + 0.5);
                int sy = static_cast<int>(cy + ny * f * cy + 0.5);
                Entry& e = map[static_cast<size_t>(y) * Frame::kWidth + x];
                if (sx < 0 || sx >= Frame::kWidth || sy < 0 ||
                    sy >= Frame::kHeight) {
                    e.src = -1;
                    e.gain = 0;
                } else {
                    e.src = sy * Frame::kWidth + sx;
                    double vig = 1.0 - 0.18 * r2 * r2;    // corner shading
                    double scan = (sy & 1) ? 0.72 : 1.0;  // scanlines
                    double g = std::max(0.0, vig * scan);
                    e.gain = static_cast<uint16_t>(g * 256.0 + 0.5);
                }
            }
        }
    }
};

void apply_crt(const Frame& in, Frame& out) {
    static const CrtLut lut;
    const uint32_t* src = in.rgba.data();
    uint32_t* dst = out.rgba.data();
    const size_t n = lut.map.size();
    for (size_t i = 0; i < n; ++i) {
        const CrtLut::Entry& e = lut.map[i];
        if (e.src < 0) {
            dst[i] = 0xff000000u;
            continue;
        }
        uint32_t p = src[e.src];
        uint32_t r = ((p & 0xffu) * e.gain) >> 8;
        uint32_t g = (((p >> 8) & 0xffu) * e.gain) >> 8;
        uint32_t b = (((p >> 16) & 0xffu) * e.gain) >> 8;
        dst[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
}

constexpr int kFontScale = 2;
constexpr int kCharW = 6 * kFontScale;  // 5 px glyph + 1 px spacing

// Superimpose text directly into the frame pixels so it goes through the
// same path as the video (CRT distortion, scanlines), like a real TV OSD.
void frame_text(Frame& f, int x, int y, const std::string& text, uint8_t r,
                uint8_t g, uint8_t b, int scale = kFontScale) {
    uint32_t px32 = 0xff000000u | (static_cast<uint32_t>(b) << 16) |
                    (static_cast<uint32_t>(g) << 8) | r;
    for (size_t ci = 0; ci < text.size(); ++ci) {
        const uint8_t* gl = glyph5x7(text[ci]);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col) {
                if (!(gl[row] & (0x10 >> col))) continue;
                int bx = x + static_cast<int>(ci) * 6 * scale + col * scale;
                int by = y + row * scale;
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) {
                        int xx = bx + dx, yy = by + dy;
                        if (xx < 0 || xx >= Frame::kWidth || yy < 0 ||
                            yy >= Frame::kHeight)
                            continue;
                        f.rgba[static_cast<size_t>(yy) * Frame::kWidth + xx] =
                            px32;
                    }
            }
    }
}

void draw_text(SDL_Renderer* ren, int x, int y, const std::string& text,
               uint8_t r, uint8_t g, uint8_t b, int scale = kFontScale) {
    SDL_SetRenderDrawColor(ren, r, g, b, 255);
    for (size_t ci = 0; ci < text.size(); ++ci) {
        const uint8_t* gl = glyph5x7(text[ci]);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (gl[row] & (0x10 >> col)) {
                    SDL_Rect px{x + static_cast<int>(ci) * 6 * scale +
                                    col * scale,
                                y + row * scale, scale, scale};
                    SDL_RenderFillRect(ren, &px);
                }
    }
}

}  // namespace

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
                case SDLK_h:
                    return KeyAction::ToggleHelp;
                case SDLK_RIGHT:
                    return KeyAction::FreqUp;
                case SDLK_LEFT:
                    return KeyAction::FreqDown;
                case SDLK_UP:
                    return KeyAction::FreqUpBig;
                case SDLK_DOWN:
                    return KeyAction::FreqDownBig;
                case SDLK_r:
                    return KeyAction::ToggleCrt;
                case SDLK_v:
                    return KeyAction::ToggleRecord;
                case SDLK_o:
                    return KeyAction::ToggleHud;
                case SDLK_f:
                    return KeyAction::ToggleRadio;
                default:
                    break;
            }
        }
    }
    return KeyAction::None;
}

void SdlDisplay::render(const Frame* frame, const OsdStats& stats) {
    if (frame) {
        last_frame_ = *frame;
        have_frame_ = true;
    }
    if (have_frame_) {
        // Superimpose the OSD into the frame itself (inset toward the
        // lower right, like a real TV) so CRT distortion applies to it.
        osd_frame_ = last_frame_;
        if (stats.radio) {
            frame_text(osd_frame_, 48, 40, "FM", 40, 255, 80, 6);
        } else if (stats.channel > 0) {
            char ch[8];
            std::snprintf(ch, sizeof(ch), "CH%d", stats.channel);
            frame_text(osd_frame_, 48, 40, ch, 40, 255, 80, 6);
        }
        if (stats.recording) {
            char recl[16];
            int s = static_cast<int>(stats.rec_seconds);
            std::snprintf(recl, sizeof(recl), "REC %d:%02d", s / 60, s % 60);
            frame_text(osd_frame_, 48, 96, recl, 255, 60, 60, 3);
        }
        if (stats.show_hud) {
        char l1[48], l2[48], l3[48];
        std::snprintf(l1, sizeof(l1), "V-SYNC:%s H-SYNC:%s %.1fFPS",
                      stats.vsync_locked ? "OK" : "--",
                      stats.line_locked ? "OK" : "--", stats.fps);
        if (stats.radio)
            std::snprintf(l2, sizeof(l2), "FM:%.2fMHz", stats.freq_mhz);
        else
            std::snprintf(l2, sizeof(l2), "VHF:%.2fMHz AUD:%.2fMHz",
                          stats.freq_mhz, stats.audio_mhz);
        std::snprintf(l3, sizeof(l3), "DELAY V:%.0fms A:%.0fms",
                      stats.video_latency_ms, stats.audio_latency_ms);
        std::string t1(l1), t2(l2), t3(l3);
        const int right = Frame::kWidth - 28;
        frame_text(osd_frame_,
                   right - static_cast<int>(t1.size()) * kCharW, 28, t1, 255,
                   220, 0);
        frame_text(osd_frame_,
                   right - static_cast<int>(t2.size()) * kCharW,
                   28 + 8 * kFontScale, t2, 255, 220, 0);
        frame_text(osd_frame_,
                   right - static_cast<int>(t3.size()) * kCharW,
                   28 + 16 * kFontScale, t3, 255, 220, 0);
        }
        if (stats.crt) {
            apply_crt(osd_frame_, crt_frame_);
            SDL_UpdateTexture(tex_, nullptr, crt_frame_.rgba.data(),
                              Frame::kWidth * 4);
        } else {
            SDL_UpdateTexture(tex_, nullptr, osd_frame_.rgba.data(),
                              Frame::kWidth * 4);
        }
    }
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    if (stats.show_help) {
        static const char* kHelp[] = {
            "KEYS",
            "Q ESC   QUIT",
            "L       LNA GAIN UP",
            "SHIFT L LNA GAIN DOWN",
            "G       VGA GAIN UP",
            "SHIFT G VGA GAIN DOWN",
            "C       COLOR - GRAY",
            "S       SCREENSHOT",
            "H       HELP ON - OFF",
            "LEFT RIGHT  TUNE 50KHZ",
            "UP DOWN     TUNE 1MHZ",
            "R       CRT MODE",
            "V       RECORD IQ START-STOP",
            "O       HUD ON - OFF",
            "F       FM RADIO ON - OFF",
        };
        const int n = static_cast<int>(sizeof(kHelp) / sizeof(kHelp[0]));
        int bw = 23 * kCharW + 32;
        int bh = n * 9 * kFontScale + 32;
        SDL_Rect box{(Frame::kWidth - bw) / 2, (Frame::kHeight - bh) / 2, bw,
                     bh};
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 200);
        SDL_RenderFillRect(ren_, &box);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_NONE);
        for (int i = 0; i < n; ++i)
            draw_text(ren_, box.x + 16, box.y + 16 + i * 9 * kFontScale,
                      kHelp[i], i == 0 ? 255 : 220, i == 0 ? 220 : 220,
                      i == 0 ? 0 : 220);
    }
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
