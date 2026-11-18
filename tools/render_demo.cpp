// Offline renderer: plays a 4-bar rock beat through the engine and writes a WAV.
// usage: render_demo <kits_folder> <kit_id> <out.wav> [slot=kitId/pieceId ...]
//   e.g. render_demo kits demo-rock out.wav snare=demo-tight/snare
#include "Engine.hpp"
#include "Rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Ev {
    double beat;
    int note;
    int vel;
};

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: render_demo <kits_folder> <kit_id> <out.wav> [slot=kitId/pieceId ...]\n");
        return 2;
    }
    od::Library lib;
    lib.scan(argv[1]);
    const od::KitDef* kit = lib.findKit(argv[2]);
    if (!kit) {
        std::printf("kit '%s' not found\n", argv[2]);
        return 1;
    }
    od::Selection sel;
    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        auto eq = a.find('=');
        if (eq != std::string::npos) sel[a.substr(0, eq)] = a.substr(eq + 1);
    }
    auto rig = od::buildRig(lib, *kit, sel);
    for (const auto& w : rig->warnings) std::printf("warning: %s\n", w.c_str());

    // 4 bars of 4/4: basic rock groove, last bar has a tom fill.
    std::vector<Ev> ev;
    ev.push_back({0, 49, 115}); // crash on the downbeat
    for (int bar = 0; bar < 4; ++bar) {
        const double b = bar * 4.0;
        for (int e = 0; e < 8; ++e) { // eighth-note hi-hat, accented on beats
            const bool lastFill = (bar == 3 && e >= 4);
            if (lastFill) continue;
            const bool open = (e == 7 && bar != 3);
            ev.push_back({b + e * 0.5, open ? 46 : 42, e % 2 == 0 ? 95 : 62});
        }
        ev.push_back({b + 0.0, 36, 112});
        ev.push_back({b + 2.5, 36, 100});
        ev.push_back({b + 1.0, 38, 108});
        ev.push_back({b + 3.0, 38, 112});
        if (bar < 3) ev.push_back({b + 2.0, 36, 104});
    }
    // Fill on the last bar: 16ths on the toms, then a crash.
    const int fill[] = {50, 50, 48, 48, 43, 43, 41, 41}; // tom1, tom2, floor (demo kits have no tom3)
    for (int i = 0; i < 8; ++i) ev.push_back({12.0 + 2.0 + i * 0.25, fill[i], 70 + i * 7});
    ev.push_back({16.0, 49, 120});
    ev.push_back({16.0, 36, 120});
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.beat < b.beat; });

    const double sr = 44100.0, bpm = 100.0;
    const double secPerBeat = 60.0 / bpm;
    const size_t total = static_cast<size_t>((16.0 * secPerBeat + 3.0) * sr);
    std::vector<float> L(total, 0.0f), R(total, 0.0f);

    od::DrumEngine eng;
    eng.prepare(sr);
    eng.setMasterGain(0.5f);
    eng.setRig(rig);

    size_t pos = 0, next = 0;
    const int block = 256;
    while (pos < total) {
        // process events that fall inside this block, splitting at event positions
        size_t blockEnd = std::min(total, pos + block);
        while (pos < blockEnd) {
            size_t evPos = blockEnd;
            if (next < ev.size()) evPos = std::min(blockEnd, static_cast<size_t>(ev[next].beat * secPerBeat * sr));
            if (evPos > pos) {
                eng.render(L.data() + pos, R.data() + pos, static_cast<int>(evPos - pos));
                pos = evPos;
            }
            while (next < ev.size() && static_cast<size_t>(ev[next].beat * secPerBeat * sr) <= pos) {
                eng.noteOn(ev[next].note, ev[next].vel);
                ++next;
            }
            if (evPos == blockEnd) break;
        }
        pos = blockEnd;
    }

    float peak = 0;
    for (size_t i = 0; i < total; ++i) peak = std::max({peak, std::abs(L[i]), std::abs(R[i])});
    std::string err;
    if (!od::writeWav16(argv[3], L, R, sr, err)) {
        std::printf("%s\n", err.c_str());
        return 1;
    }
    std::printf("rendered %.1f s, peak %.2f (%.1f dBFS) -> %s\n", total / sr, peak, 20 * std::log10(peak + 1e-9f), argv[3]);
    return 0;
}
