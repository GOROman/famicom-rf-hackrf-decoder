#include <atomic>
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.hpp"
#include "dsp/am_detector.hpp"
#include "dsp/fm_audio.hpp"
#include "dsp/dc_blocker.hpp"
#include "dsp/fir.hpp"
#include "dsp/frame.hpp"
#include "dsp/nco.hpp"
#include "dsp/ntsc_decoder.hpp"
#include "source/file_source.hpp"
#include "source/hackrf_source.hpp"
#include "source/sample_source.hpp"
#include "ui/sdl_audio.hpp"
#include "ui/sdl_display.hpp"
#include "util/spectrum.hpp"

using namespace famidec;

namespace {

void usage() {
    std::printf(
        "famidec - Famicom RF (NTSC-J) HackRF One decoder\n\n"
        "usage: famidec [options]\n"
        "  --channel 1|2         Japan VHF channel preset (default 1)\n"
        "  --freq HZ             explicit video carrier frequency\n"
        "  --input hackrf|file   input source (default hackrf)\n"
        "  --file PATH           .cs8 recording for --input file\n"
        "  --loop                loop file playback\n"
        "  --rate HZ             sample rate (default 10e6)\n"
        "  --offset HZ           tuning offset above carrier (default 2e6)\n"
        "  --lna N --vga N --amp gain settings\n"
        "  --mode color|gray     decode mode (default color)\n"
        "  --detector envelope|sync  AM detector (default envelope)\n"
        "  --sat F --hue DEG     color trims\n"
        "  --no-audio            disable FM audio output\n"
        "  --volume F            audio volume 0..1 (default 0.7)\n"
        "  --record PATH         tee raw IQ to .cs8 while decoding\n"
        "  --dump-composite PATH write post-AGC composite as f32\n"
        "  --dump-frames PREFIX  write decoded frames as PPM (headless)\n"
        "  --frames N            number of frames for --dump-frames (default 30)\n"
        "  --spectrum            print PSD and exit (no video)\n"
        "\nkeys: q/ESC quit, l/L LNA +/-, g/G VGA +/-, c color toggle, s screenshot\n");
}

bool parse_args(int argc, char** argv, Config* cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--channel") {
            int ch = std::atoi(next("--channel"));
            if (ch == 1) cfg->video_carrier_hz = 91.25e6;
            else if (ch == 2) cfg->video_carrier_hz = 97.25e6;
            else { std::fprintf(stderr, "channel must be 1 or 2\n"); return false; }
        } else if (a == "--freq") cfg->video_carrier_hz = std::atof(next("--freq"));
        else if (a == "--input") {
            std::string v = next("--input");
            if (v == "hackrf") cfg->input = Config::Input::HackRF;
            else if (v == "file") cfg->input = Config::Input::File;
            else return false;
        } else if (a == "--file") { cfg->file_path = next("--file"); cfg->input = Config::Input::File; }
        else if (a == "--loop") cfg->loop = true;
        else if (a == "--rate") cfg->sample_rate = std::atof(next("--rate"));
        else if (a == "--offset") cfg->offset_hz = std::atof(next("--offset"));
        else if (a == "--lna") cfg->lna_gain = std::atoi(next("--lna"));
        else if (a == "--vga") cfg->vga_gain = std::atoi(next("--vga"));
        else if (a == "--amp") cfg->amp = true;
        else if (a == "--mode") {
            std::string v = next("--mode");
            cfg->mode = (v == "gray") ? Config::Mode::Gray : Config::Mode::Color;
        } else if (a == "--detector") {
            std::string v = next("--detector");
            cfg->detector = (v == "sync") ? Config::Detector::SyncPLL
                                          : Config::Detector::Envelope;
        } else if (a == "--no-audio") cfg->audio = false;
        else if (a == "--volume") cfg->volume = std::atof(next("--volume"));
        else if (a == "--sat") cfg->saturation = std::atof(next("--sat"));
        else if (a == "--hue") cfg->hue_deg = std::atof(next("--hue"));
        else if (a == "--record") cfg->record_path = next("--record");
        else if (a == "--dump-composite") cfg->dump_composite_path = next("--dump-composite");
        else if (a == "--dump-frames") { cfg->dump_frames_prefix = next("--dump-frames"); cfg->headless = true; }
        else if (a == "--frames") cfg->dump_frame_count = std::atoi(next("--frames"));
        else if (a == "--spectrum") cfg->spectrum = true;
        else if (a == "--help" || a == "-h") { usage(); std::exit(0); }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return false; }
    }
    if (cfg->input == Config::Input::File && cfg->file_path.empty()) {
        std::fprintf(stderr, "--input file requires --file PATH\n");
        return false;
    }
    if (cfg->headless && cfg->dump_frame_count <= 0) cfg->dump_frame_count = 30;
    return true;
}

void write_ppm(const Frame& f, const std::string& path) {
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", Frame::kWidth, Frame::kHeight);
    for (uint32_t px : f.rgba) {
        uint8_t rgb[3] = {static_cast<uint8_t>(px & 0xff),
                          static_cast<uint8_t>((px >> 8) & 0xff),
                          static_cast<uint8_t>((px >> 16) & 0xff)};
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}

std::atomic<bool> g_running{true};

// cs8 IQ bytes -> complex -> DC block -> mix carrier to 0 Hz -> channel LPF
// -> AM detect -> NtscDecoder.
void dsp_thread(const Config& cfg, ISampleSource* src, NtscDecoder* dec,
                std::FILE* record_fp, FmAudioDemod* fm, SdlAudioOut* aout) {
    constexpr size_t kBlockBytes = 1 << 16;  // 32768 complex samples
    std::vector<uint8_t> raw(kBlockBytes);
    std::vector<std::complex<float>> iq(kBlockBytes / 2);
    std::vector<float> comp(kBlockBytes / 2);

    DcBlocker dcb;
    Nco mixer(cfg.offset_hz, cfg.sample_rate);  // shift -offset..: see below
    FirFilterC chan_lpf(
        [&] {
            auto t = design_lowpass(4.3e6, cfg.sample_rate, 63);
            return t;
        }());
    EnvelopeDetector env;
    SyncDetector sync_det(cfg.sample_rate);

    // Carrier sits at -offset in the IQ band; multiply by e^{+j*2pi*offset*t}
    // to bring it to 0 Hz.
    while (g_running.load(std::memory_order_relaxed)) {
        size_t n = src->read(raw.data(), raw.size());
        if (n == 0) break;
        if (record_fp) std::fwrite(raw.data(), 1, n, record_fp);
        size_t ns = n / 2;
        for (size_t i = 0; i < ns; ++i) {
            std::complex<float> c(
                static_cast<int8_t>(raw[2 * i]) / 128.0f,
                static_cast<int8_t>(raw[2 * i + 1]) / 128.0f);
            iq[i] = dcb.process(c) * mixer.next();
        }
        if (fm && aout) {
            static thread_local std::vector<float> audio;
            audio.clear();
            fm->process(iq.data(), ns, audio);
            aout->push(audio.data(), audio.size());
        }
        chan_lpf.process(iq.data(), iq.data(), ns);
        if (cfg.detector == Config::Detector::SyncPLL)
            sync_det.process(iq.data(), comp.data(), ns);
        else
            env.process(iq.data(), comp.data(), ns);
        dec->process(comp.data(), ns);
    }
    g_running.store(false, std::memory_order_relaxed);
}

int run_spectrum(const Config& cfg, ISampleSource* src) {
    // Grab ~0.5 s of IQ and print the PSD.
    size_t n_samples = static_cast<size_t>(cfg.sample_rate / 2);
    std::vector<uint8_t> buf(n_samples * 2);
    size_t got = src->read(buf.data(), buf.size());
    if (got < 4096) {
        std::fprintf(stderr, "not enough samples for PSD\n");
        return 1;
    }
    print_psd(reinterpret_cast<const int8_t*>(buf.data()), got / 2,
              cfg.center_hz(), cfg.sample_rate);
    std::printf("\nexpect: video carrier at %.3f MHz (-%.1f MHz from center), "
                "chroma at %.3f MHz, audio at %.3f MHz\n",
                cfg.video_carrier_hz / 1e6, cfg.offset_hz / 1e6,
                (cfg.video_carrier_hz + 3.579545e6) / 1e6,
                (cfg.video_carrier_hz + 4.5e6) / 1e6);
    uint64_t clipped = src->clipped_samples();
    if (clipped) std::printf("WARNING: %llu clipped samples - reduce gain\n",
                             static_cast<unsigned long long>(clipped));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parse_args(argc, argv, &cfg)) {
        usage();
        return 2;
    }

    std::unique_ptr<ISampleSource> src;
    HackRfSource* hackrf = nullptr;
    if (cfg.input == Config::Input::HackRF) {
        auto h = std::make_unique<HackRfSource>(cfg);
        hackrf = h.get();
        src = std::move(h);
    } else {
        // Pace file playback only when showing a live window.
        src = std::make_unique<FileSource>(cfg, !cfg.headless && !cfg.spectrum);
    }
    if (!src->start()) {
        std::fprintf(stderr, "failed to start input source%s%s\n",
                     hackrf ? ": " : "",
                     hackrf ? hackrf->error().c_str() : "");
        return 1;
    }
    std::printf("input: %s   video carrier %.3f MHz   center %.3f MHz   %.1f MSPS\n",
                cfg.input == Config::Input::HackRF ? "HackRF" : cfg.file_path.c_str(),
                cfg.video_carrier_hz / 1e6, cfg.center_hz() / 1e6,
                cfg.sample_rate / 1e6);

    if (cfg.spectrum) {
        int rc = run_spectrum(cfg, src.get());
        src->stop();
        return rc;
    }

    std::FILE* record_fp = nullptr;
    if (!cfg.record_path.empty()) {
        record_fp = std::fopen(cfg.record_path.c_str(), "wb");
        if (!record_fp) {
            std::fprintf(stderr, "cannot open %s\n", cfg.record_path.c_str());
            return 1;
        }
    }

    TripleBuffer tb;
    NtscDecoder dec(cfg, tb);

    // FM intercarrier audio (window mode only).
    std::unique_ptr<FmAudioDemod> fm;
    SdlAudioOut aout;
    if (cfg.audio && !cfg.headless) {
        auto demod = std::make_unique<FmAudioDemod>(cfg.sample_rate, cfg.volume);
        if (aout.init(static_cast<int>(demod->out_rate())))
            fm = std::move(demod);
        else
            std::fprintf(stderr, "audio device unavailable, continuing muted\n");
    }

    std::thread dsp(dsp_thread, std::cref(cfg), src.get(), &dec, record_fp,
                    fm.get(), aout.ok() ? &aout : nullptr);

    int rc = 0;
    if (cfg.headless) {
        // Dump N frames as PPM, then exit.
        uint64_t last_seq = 0;
        int written = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (written < cfg.dump_frame_count &&
               g_running.load(std::memory_order_relaxed) &&
               std::chrono::steady_clock::now() < deadline) {
            const Frame* f = tb.acquire();
            if (f && f->seq != last_seq) {
                last_seq = f->seq;
                char path[512];
                std::snprintf(path, sizeof(path), "%s%04d.ppm",
                              cfg.dump_frames_prefix.c_str(), written);
                write_ppm(*f, path);
                ++written;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        std::printf("wrote %d frames; decoded lines=%llu coasted=%llu frames=%llu\n",
                    written,
                    static_cast<unsigned long long>(dec.stats().lines.load()),
                    static_cast<unsigned long long>(dec.stats().lines_coasted.load()),
                    static_cast<unsigned long long>(dec.stats().frames.load()));
        if (written == 0) rc = 1;
    } else {
        SdlDisplay disp;
        if (!disp.init("famidec - Famicom RF decoder")) {
            std::fprintf(stderr, "SDL init failed\n");
            g_running.store(false);
            dsp.join();
            return 1;
        }
        Config& mcfg = cfg;
        int shot = 0;
        Frame last_shown;
        bool have_shown = false;
        uint64_t prev_frames = 0;
        auto last_frame_inc = std::chrono::steady_clock::now();
        bool show_help = false;
        float fps = 0.0f;
        uint64_t fps_base_frames = 0;
        auto fps_base_time = std::chrono::steady_clock::now();
        // Nearest VHF channel (real modulators drift a few hundred kHz).
        int channel = 0;
        if (std::abs(cfg.video_carrier_hz - 91.25e6) < 3e6) channel = 1;
        else if (std::abs(cfg.video_carrier_hz - 97.25e6) < 3e6) channel = 2;
        while (g_running.load(std::memory_order_relaxed)) {
            KeyAction act = disp.poll();
            if (act == KeyAction::Quit) break;
            if (hackrf) {
                if (act == KeyAction::GainLnaUp) hackrf->set_gains(hackrf->lna() + 8, hackrf->vga());
                if (act == KeyAction::GainLnaDown) hackrf->set_gains(hackrf->lna() - 8, hackrf->vga());
                if (act == KeyAction::GainVgaUp) hackrf->set_gains(hackrf->lna(), hackrf->vga() + 2);
                if (act == KeyAction::GainVgaDown) hackrf->set_gains(hackrf->lna(), hackrf->vga() - 2);
            }
            if (act == KeyAction::ToggleHelp) show_help = !show_help;
            if (act == KeyAction::ToggleColor)
                mcfg.mode = (mcfg.mode == Config::Mode::Color)
                                ? Config::Mode::Gray
                                : Config::Mode::Color;
            const Frame* f = tb.acquire();
            if (f) {
                last_shown = *f;
                have_shown = true;
            }
            if (act == KeyAction::Screenshot && have_shown) {
                char path[64];
                std::snprintf(path, sizeof(path), "famidec_%03d.bmp", shot++);
                disp.screenshot(last_shown, path);
                std::printf("saved %s\n", path);
            }
            OsdStats st;
            st.line_locked = dec.stats().line_locked.load();
            st.burst_amp = dec.stats().burst_amp.load();
            st.ring_fill = src->ring_fill();
            st.dropped = src->dropped_bytes();
            st.clipped = src->clipped_samples();
            st.frames = dec.stats().frames.load();
            if (hackrf) { st.lna = hackrf->lna(); st.vga = hackrf->vga(); }
            // V-SYNC considered locked while real frames keep arriving.
            auto now = std::chrono::steady_clock::now();
            if (st.frames > prev_frames) {
                prev_frames = st.frames;
                last_frame_inc = now;
            }
            st.vsync_locked = (now - last_frame_inc) < std::chrono::milliseconds(500);
            // Decoded-frame rate over a rolling ~1 s window.
            double win = std::chrono::duration<double>(now - fps_base_time).count();
            if (win >= 1.0) {
                fps = static_cast<float>(
                    static_cast<double>(st.frames - fps_base_frames) / win);
                fps_base_frames = st.frames;
                fps_base_time = now;
            }
            st.fps = fps;
            st.show_help = show_help;
            // Video latency: samples captured since the displayed frame's
            // vsync, plus ~one USB transfer (13 ms) of hardware buffering.
            if (st.vsync_locked) {
                uint64_t rx_samples = src->total_bytes() / 2;
                uint64_t vsync_pos = dec.stats().frame_sample_pos.load();
                if (rx_samples > vsync_pos)
                    st.video_latency_ms = static_cast<float>(
                        static_cast<double>(rx_samples - vsync_pos) /
                        cfg.sample_rate * 1000.0 + 13.0);
            }
            st.audio_latency_ms = aout.ok() ? aout.queued_ms() : 0.0f;
            st.freq_mhz = cfg.video_carrier_hz / 1e6;
            st.audio_mhz = (cfg.video_carrier_hz + 4.5e6) / 1e6;
            st.channel = channel;
            disp.render(f, st);
        }
    }

    g_running.store(false, std::memory_order_relaxed);
    src->stop();
    dsp.join();
    if (record_fp) std::fclose(record_fp);
    return rc;
}
