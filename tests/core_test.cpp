// Tests for the OpenDrums core (no JUCE needed).
// usage: core_test <kits_folder>   (folder produced by tools/make_demo_kits.py)
#include "Engine.hpp"
#include "KitGeometry.hpp"
#include "Rig.hpp"
#include "Grooves.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static float peakOf(od::DrumEngine& e, int frames) {
    std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
    e.render(l.data(), r.data(), frames);
    float p = 0;
    for (int i = 0; i < frames; ++i) p = std::max({p, std::fabs(l[i]), std::fabs(r[i])});
    return p;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: core_test <kits_folder>\n");
        return 2;
    }
    od::Library lib;
    lib.scan(argv[1]);

    std::printf("library\n");
    CHECK(lib.kits().size() >= 2);
    CHECK(lib.warnings().empty());
    CHECK(lib.findKit("demo-rock") != nullptr);
    CHECK(lib.findPiece("demo-tight/snare"));
    CHECK(!lib.findPiece("nope/snare"));
    CHECK(lib.piecesOfFamily("cymbal").size() >= 2 * 3); // crash1, crash2, ride in each kit
    CHECK(lib.piecesOfFamily("tom").size() >= 2 * 3);
    CHECK(od::typeFamily("ride") == "cymbal" && od::typeFamily("tom") == "tom");

    const od::KitDef* rock = lib.findKit("demo-rock");
    auto rig = od::buildRig(lib, *rock, {});

    std::printf("rig\n");
    for (const auto& w : rig->warnings) std::printf("  warning: %s\n", w.c_str());
    CHECK(rig->warnings.empty());
    CHECK(rig->slots.size() == 9);
    // The demo kit has no tom3 slot, so GM note 45 must stay unmapped.
    for (int n : {35, 36, 37, 38, 40, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51, 53, 57, 59}) {
        if (!rig->notes[n].slot) std::printf("  note %d is not mapped\n", n);
        CHECK(rig->notes[n].slot != nullptr);
    }
    CHECK(rig->notes[45].slot == nullptr);
    CHECK(rig->notes[36].slot->view.slot == "kick");
    CHECK(rig->notes[42].slot->chokeGroup == 1 && rig->notes[46].slot->chokeGroup == 1);
    CHECK(rig->notes[36].slot->view.diameter == 22.0f);
    CHECK(!rig->notes[38].slot->view.imagePath.empty());

    std::printf("engine\n");
    od::DrumEngine eng;
    eng.prepare(44100.0);
    eng.setMasterGain(1.0f);
    eng.setRig(rig);

    eng.noteOn(36, 100);
    CHECK(eng.activeVoices() == 1);
    CHECK(peakOf(eng, 512) > 0.05f);

    // Velocity layers: soft hit is clearly quieter than a hard hit.
    eng.allSoundOff();
    eng.noteOn(38, 20);
    const float soft = peakOf(eng, 2048);
    eng.allSoundOff();
    eng.noteOn(38, 125);
    const float hard = peakOf(eng, 2048);
    std::printf("  snare peak soft=%.3f hard=%.3f\n", soft, hard);
    CHECK(soft > 0.0f && hard > soft * 2.0f);

    // Round robin advances.
    eng.allSoundOff();
    const unsigned rrBefore = rig->notes[36].art->layers[2].rr;
    eng.noteOn(36, 120);
    eng.noteOn(36, 120);
    CHECK(rig->notes[36].art->layers[2].rr == rrBefore + 2);

    // Hi-hat choke: closed hit silences the open one quickly.
    eng.allSoundOff();
    eng.noteOn(46, 110); // open
    CHECK(eng.activeVoices() == 1);
    peakOf(eng, 256);
    eng.noteOn(42, 110); // closed -> chokes open
    CHECK(eng.activeVoices() == 2);
    peakOf(eng, 1024); // > 8 ms: the open voice must be gone
    CHECK(eng.activeVoices() == 1);

    // Voice ends by itself.
    eng.allSoundOff();
    eng.noteOn(37, 100); // sidestick, 0.12 s
    peakOf(eng, 44100);
    CHECK(eng.activeVoices() == 0);

    // Sample-rate conversion: 48 kHz host plays the same sound with ~9% more frames.
    {
        od::DrumEngine e48;
        e48.prepare(48000.0);
        e48.setMasterGain(1.0f);
        e48.setRig(rig);
        e48.noteOn(37, 100);
        int frames = 0;
        std::vector<float> l(64), r(64);
        while (e48.activeVoices() > 0 && frames < 48000) {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            e48.render(l.data(), r.data(), 64);
            frames += 64;
        }
        const double expected = 0.12 * 48000.0;
        std::printf("  48k playback of 0.12 s sample: %d frames (expected ~%.0f)\n", frames, expected);
        CHECK(std::fabs(frames - expected) < 200.0);
    }

    std::printf("piece swapping\n");
    od::Selection sel;
    sel["snare"] = "demo-tight/snare";
    sel["ride"] = "demo-tight/crash1"; // a crash used as ride: roles fall back to "hit"
    auto swapped = od::buildRig(lib, *rock, sel);
    for (const auto& w : swapped->warnings) std::printf("  warning: %s\n", w.c_str());
    CHECK(swapped->notes[38].slot->view.pieceRef == "demo-tight/snare");
    CHECK(swapped->notes[38].slot->view.diameter == 13.0f);
    CHECK(swapped->notes[36].slot->view.pieceRef == "demo-rock/kick");
    CHECK(swapped->notes[53].art != nullptr); // bell note still resolves (falls back to "hit")
    CHECK(swapped->notes[38].slot->view.imagePath.string().find("demo-tight") != std::string::npos);

    // Stale selection falls back to the kit's own piece, with a warning.
    od::Selection stale;
    stale["snare"] = "gone-kit/snare";
    auto fallback = od::buildRig(lib, *rock, stale);
    CHECK(!fallback->warnings.empty());
    CHECK(fallback->notes[38].slot->view.pieceRef == "demo-rock/snare");

    // Hot swap while voices are playing: no crash, old voices fade, new rig is used.
    eng.allSoundOff();
    eng.noteOn(49, 120);
    eng.noteOn(36, 120);
    peakOf(eng, 256);
    eng.setRig(swapped);
    peakOf(eng, 1024); // 23 ms: faded out
    CHECK(eng.activeVoices() == 0);
    eng.noteOn(38, 100);
    CHECK(eng.activeVoices() == 1);
    eng.collectGarbage();
    eng.collectGarbage();

    // Audition queue from the UI.
    eng.allSoundOff();
    eng.postNote(36, 100);
    eng.drainPosted();
    CHECK(eng.activeVoices() == 1);

    // Hit events for the animated kit view: only notes that really start a sound are reported.
    std::printf("hit events\n");
    {
        int n = 0, v = 0;
        while (eng.popHit(n, v)) {}
        eng.allSoundOff();
        eng.noteOn(36, 100);
        CHECK(eng.popHit(n, v) && n == 36 && v == 100);
        CHECK(!eng.popHit(n, v));
        eng.noteOn(45, 100); // no tom3 in the demo kits: unmapped, silent, no event
        CHECK(!eng.popHit(n, v));
        for (int i = 0; i < 300; ++i) eng.noteOn(42, 90); // overflow must not crash or block
        int count = 0;
        while (eng.popHit(n, v)) ++count;
        CHECK(count > 0 && count <= 127);
        eng.allSoundOff();
    }

    // Geometry: stands, pedal and animation curves (shared by the plugin and tools/preview_kit).
    std::printf("geometry\n");
    {
        using namespace od::geo;
        const float VW = 936.0f, VH = 492.0f;
        CHECK(ampForVelocity(127) == 1.0f);
        CHECK(ampForVelocity(1) >= 0.25f && ampForVelocity(30) < ampForVelocity(100));

        const Rect kick = computeBounds(0.5f, 0.5f, 22.0f, 1.0f, 1.0f, VW, VH);
        CHECK(std::fabs(kick.w - 22.0f * VW / kViewWidthInches) < 0.01f);
        CHECK(std::fabs(kick.cx() - VW * 0.5f) < 0.01f && std::fabs(kick.cy() - VH * 0.5f) < 0.01f);

        auto finite = [](const std::vector<Prim>& prims, float maxY) {
            for (const auto& p : prims) {
                for (float c : {p.x1, p.y1, p.x2, p.y2, p.width})
                    if (!std::isfinite(c)) return false;
                if (p.kind == Prim::Kind::Poly)
                    for (float c : p.pts)
                        if (!std::isfinite(c)) return false;
                if (p.kind == Prim::Kind::Poly) {
                    for (size_t i = 1; i < p.pts.size(); i += 2)
                        if (p.pts[i] > maxY) return false;
                } else if (p.kind == Prim::Kind::Ellipse) {
                    if (p.y1 + p.y2 > maxY) return false;
                } else if (p.kind == Prim::Kind::Line) {
                    if (p.y1 > maxY || p.y2 > maxY) return false;
                }
            }
            return true;
        };

        for (const auto& slot : rig->slots) {
            const Rect b = computeBounds(slot.view.x, slot.view.y, slot.view.diameter, slot.view.scale, 0.8f, VW, VH);
            const std::string& id = slot.view.slot;
            const std::string& fam = slot.view.family;

            HitAnim rest;
            const auto g0 = buildSlot(id, fam, b, VH, rest);
            CHECK(g0.piece.isIdentity());
            CHECK(g0.glow == 0.0f);
            CHECK(!g0.behind.empty() || fam == "perc");
            CHECK(finite(g0.behind, VH + 1.0f) && finite(g0.front, VH + 1.0f));

            HitAnim hit;
            hit.t = 0.0f;
            hit.amp = 1.0f;
            hit.role = "hit";
            const auto g1 = buildSlot(id, fam, b, VH, hit);
            CHECK(!g1.piece.isIdentity()); // every piece reacts when hit
            CHECK(finite(g1.behind, VH + 1.0f) && finite(g1.front, VH + 1.0f));

            hit.t = animDuration(fam); // settled: deviations are below visibility
            const auto g2 = buildSlot(id, fam, b, VH, hit);
            CHECK(std::fabs(g2.piece.rot) < 0.002f && std::fabs(g2.piece.sx - 1) < 0.002f &&
                  std::fabs(g2.piece.sy - 1) < 0.002f && std::fabs(g2.piece.dy) < 0.02f);

            // a soft hit moves less than a hard one
            HitAnim soft = hit;
            soft.t = 0.0f;
            soft.amp = ampForVelocity(20);
            const auto gs = buildSlot(id, fam, b, VH, soft);
            const float dHard = std::fabs(g1.piece.sx - 1) + std::fabs(g1.piece.sy - 1) + std::fabs(g1.piece.dy) + std::fabs(g1.piece.rot);
            const float dSoft = std::fabs(gs.piece.sx - 1) + std::fabs(gs.piece.sy - 1) + std::fabs(gs.piece.dy) + std::fabs(gs.piece.rot);
            CHECK(dSoft < dHard);
        }

        // kick pedal: beater touches the head at the hit and is back at rest afterwards
        const auto kickSlot = rig->notes[36].slot;
        const Rect kb = computeBounds(kickSlot->view.x, kickSlot->view.y, kickSlot->view.diameter, 1.0f, 1.0f, VW, VH);
        HitAnim kh;
        kh.t = 0.0f;
        kh.amp = 1.0f;
        kh.role = "hit";
        const auto k0 = buildSlot("kick", "kick", kb, VH, kh);
        CHECK(k0.beater > 0.99f && k0.press > 0.99f);
        kh.t = 2.0f;
        const auto k1 = buildSlot("kick", "kick", kb, VH, kh);
        CHECK(k1.beater < 0.01f && k1.press < 0.01f);
        CHECK(k0.front.size() > 5 && k0.behind.size() == 4); // pedal parts; two spurs with feet

        // hi-hat pedal: "pedal" presses fully, "closed" partly, "open" keeps the foot up
        const Rect hb = computeBounds(0.13f, 0.47f, 14.0f, 1.0f, 0.28f, VW, VH);
        HitAnim hh;
        hh.t = 0.0f;
        hh.amp = 1.0f;
        hh.role = "pedal";
        CHECK(buildSlot("hihat", "hihat", hb, VH, hh).press > 0.99f);
        hh.role = "closed";
        const float closedPress = buildSlot("hihat", "hihat", hb, VH, hh).press;
        CHECK(closedPress > 0.4f && closedPress < 0.7f);
        hh.role = "open";
        CHECK(buildSlot("hihat", "hihat", hb, VH, hh).press == 0.0f);
    }

    // ------------------------------------------------------------------ mixer DSP
    constexpr double kPi = 3.14159265358979323846;
    std::printf("eq\n");
    {
        auto toneDb = [&](od::SslEq& eq, double sr, double f) {
            const int N = static_cast<int>(sr);
            std::vector<float> l(N), r(N);
            for (int i = 0; i < N; ++i) l[i] = r[i] = 0.25f * static_cast<float>(std::sin(2 * kPi * f * i / sr));
            eq.reset();
            eq.process(l.data(), r.data(), N);
            double out = 0, in = 0;
            for (int i = N / 2; i < N; ++i) {
                out += static_cast<double>(l[i]) * l[i];
                const double x = 0.25 * std::sin(2 * kPi * f * i / sr);
                in += x * x;
            }
            return 10.0 * std::log10(out / in);
        };
        od::SslEq eq;
        eq.prepare(48000.0);
        od::EqParams p;
        p.hmfGain = 12.0f; // not "on" yet: must stay transparent
        eq.setParams(p);
        CHECK(std::fabs(toneDb(eq, 48000, 1000)) < 0.01);

        p = od::EqParams{};
        p.on = true; p.hmfFreq = 3000; p.hmfGain = 12; p.hmfQ = 1.0f;
        eq.setParams(p);
        const double hmf = toneDb(eq, 48000, 3000);
        std::printf("  HMF bell +12 dB @3k: at 3k %.2f, at 100 Hz %.2f, at 12k %.2f\n", hmf, toneDb(eq, 48000, 100), toneDb(eq, 48000, 12000));
        CHECK(std::fabs(hmf - 12.0) < 0.6 && std::fabs(toneDb(eq, 48000, 100)) < 0.6 && std::fabs(toneDb(eq, 48000, 12000)) < 1.5);

        p = od::EqParams{};
        p.on = true; p.lfFreq = 100; p.lfGain = 10; p.lfBell = false;
        eq.setParams(p);
        const double lo30 = toneDb(eq, 48000, 30), hi5k = toneDb(eq, 48000, 5000);
        std::printf("  LF shelf +10 dB @100: 30 Hz %.2f, 5 kHz %.2f\n", lo30, hi5k);
        CHECK(lo30 > 8.0 && std::fabs(hi5k) < 0.5);
        p.lfBell = true;
        eq.setParams(p);
        CHECK(std::fabs(toneDb(eq, 48000, 100) - 10.0) < 0.7 && toneDb(eq, 48000, 25) < 3.0 && std::fabs(toneDb(eq, 48000, 5000)) < 0.3);

        p = od::EqParams{};
        p.on = true; p.hfFreq = 8000; p.hfGain = 8; p.hfBell = false;
        eq.setParams(p);
        CHECK(toneDb(eq, 48000, 16000) > 6.5 && std::fabs(toneDb(eq, 48000, 200)) < 0.5);
        p.hfBell = true;
        eq.setParams(p);
        CHECK(std::fabs(toneDb(eq, 48000, 8000) - 8.0) < 0.7 && std::fabs(toneDb(eq, 48000, 200)) < 0.5);

        p = od::EqParams{};
        p.on = true; p.lmfFreq = 800; p.lmfGain = -9; p.lmfQ = 2.0f;
        eq.setParams(p);
        CHECK(std::fabs(toneDb(eq, 48000, 800) + 9.0) < 0.6);
    }

    std::printf("compressor\n");
    {
        od::Compressor c;
        c.prepare(48000.0);
        od::CompParams cp;
        cp.on = true; cp.thresholdDb = -24; cp.ratio = 4; cp.makeupDb = 0; cp.kneeDb = 0; cp.releaseMs = 100; cp.fastAttack = true;
        c.setParams(cp);
        std::vector<float> l(48000, 0.5f), r(48000, 0.5f);
        c.process(l.data(), r.data(), 48000);
        const float expected = 0.5f * std::pow(10.0f, -13.5f / 20.0f); // (1 - 1/4) * (-6.02 - -24) = 13.5 dB
        std::printf("  steady state %.4f (expected %.4f), GR %.2f dB\n", l.back(), expected, c.gainReductionDb());
        CHECK(std::fabs(l.back() / expected - 1.0f) < 0.05f && std::fabs(c.gainReductionDb() - 13.5f) < 0.5f);

        // make-up gain
        od::Compressor cm;
        cm.prepare(48000.0);
        cp.makeupDb = 6;
        cm.setParams(cp);
        std::vector<float> m(48000, 0.5f), m2(48000, 0.5f);
        cm.process(m.data(), m2.data(), 48000);
        CHECK(std::fabs(m.back() / l.back() - 2.0f) < 0.1f);

        // fast attack bites sooner than slow attack
        auto gainAfter = [&](bool fast, int samples) {
            od::Compressor k;
            k.prepare(48000.0);
            od::CompParams q = cp;
            q.makeupDb = 0; q.fastAttack = fast;
            k.setParams(q);
            std::vector<float> a(4800, 0.5f), b(4800, 0.5f);
            k.process(a.data(), b.data(), 4800);
            return a[static_cast<size_t>(samples)] / 0.5f;
        };
        const float gf = gainAfter(true, 240), gs = gainAfter(false, 240);
        std::printf("  gain after 5 ms: fast %.3f, slow %.3f\n", gf, gs);
        CHECK(gf < gs * 0.8f);

        // release: a longer release keeps the gain reduced for longer
        auto grAfterRelease = [&](float releaseMs) {
            od::Compressor k;
            k.prepare(48000.0);
            od::CompParams q = cp;
            q.makeupDb = 0; q.releaseMs = releaseMs;
            k.setParams(q);
            std::vector<float> a(24000, 0.5f), b(24000, 0.5f);
            k.process(a.data(), b.data(), 24000);
            std::vector<float> c2(4800, 0.001f), d2(4800, 0.001f); // 100 ms of silence-ish
            k.process(c2.data(), d2.data(), 4800);
            return k.gainReductionDb();
        };
        const float short100 = grAfterRelease(50), long100 = grAfterRelease(500);
        std::printf("  GR 100 ms after the loud part: release 50 ms %.2f dB, 500 ms %.2f dB\n", short100, long100);
        CHECK(short100 < long100 * 0.5f);

        od::Compressor off;
        off.prepare(48000.0);
        od::CompParams o;
        o.on = false;
        off.setParams(o);
        std::vector<float> z(100, 0.7f), z2(100, 0.7f);
        off.process(z.data(), z2.data(), 100);
        CHECK(z[50] == 0.7f);
    }

    std::printf("reverb\n");
    {
        auto measureRt = [&](int room, float decay, double sr, int* firstNonZero = nullptr) {
            od::Reverb rv;
            rv.prepare(sr);
            od::ReverbParams p;
            p.room = room; p.decayScale = decay; p.lowCutHz = 10; p.highCutHz = 20000;
            rv.setParams(p);
            const int N = static_cast<int>(sr * 9);
            std::vector<float> in(static_cast<size_t>(N), 0.0f), oL(static_cast<size_t>(N)), oR(static_cast<size_t>(N));
            in[0] = 1.0f;
            for (int i = 0; i < N; i += 512) {
                const int n = std::min(512, N - i);
                rv.process(in.data() + i, in.data() + i, oL.data() + i, oR.data() + i, n);
            }
            for (int i = 0; i < N; ++i)
                if (!std::isfinite(oL[static_cast<size_t>(i)]) || !std::isfinite(oR[static_cast<size_t>(i)])) return -1.0;
            if (firstNonZero) {
                *firstNonZero = -1;
                for (int i = 0; i < N; ++i)
                    if (std::fabs(oL[static_cast<size_t>(i)]) > 1e-7f || std::fabs(oR[static_cast<size_t>(i)]) > 1e-7f) { *firstNonZero = i; break; }
            }
            std::vector<double> e(static_cast<size_t>(N));
            double acc = 0;
            for (int i = N - 1; i >= 0; --i) { acc += static_cast<double>(oL[static_cast<size_t>(i)]) * oL[static_cast<size_t>(i)]; e[static_cast<size_t>(i)] = acc; }
            int t5 = -1, t25 = -1;
            for (int i = 0; i < N; ++i) {
                const double db = 10.0 * std::log10(e[static_cast<size_t>(i)] / e[0] + 1e-30);
                if (t5 < 0 && db <= -5.0) t5 = i;
                if (t25 < 0 && db <= -25.0) { t25 = i; break; }
            }
            if (t5 < 0 || t25 < 0) return -2.0;
            return 3.0 * (t25 - t5) / sr;
        };

        double rt[od::kNumRooms];
        for (int i = 0; i < od::kNumRooms; ++i) {
            rt[i] = measureRt(i, 1.0f, 44100.0);
            std::printf("  %-15s target RT60 %.2f s, measured %.2f s\n", od::reverbRoom(i).name, od::reverbRoom(i).rt60, rt[i]);
            CHECK(rt[i] > 0.0);
            CHECK(rt[i] > od::reverbRoom(i).rt60 * 0.85 && rt[i] < od::reverbRoom(i).rt60 * 1.15);
        }
        // the rooms must be ordered by size: studio S < M < L and club S < M < L < stadium
        CHECK(rt[1] < rt[2] && rt[2] < rt[3]);
        CHECK(rt[4] < rt[5] && rt[5] < rt[6] && rt[6] < rt[7]);
        CHECK(rt[1] < rt[0] && rt[0] < rt[7]); // garage between the smallest studio and the stadium

        // decay scale doubles the decay; the result does not depend on the host sample rate
        const double rt2 = measureRt(5, 2.0f, 44100.0);
        CHECK(rt2 / rt[5] > 1.6 && rt2 / rt[5] < 2.4);
        const double rt48 = measureRt(5, 1.0f, 48000.0);
        CHECK(std::fabs(rt48 / rt[5] - 1.0) < 0.1);

        // pre-delay: nothing comes out before it
        int first = 0;
        measureRt(7, 1.0f, 44100.0, &first);
        std::printf("  stadium: first output at %.1f ms (pre-delay %.0f ms)\n", first * 1000.0 / 44100.0, od::reverbRoom(7).predelayMs);
        CHECK(first >= static_cast<int>(od::reverbRoom(7).predelayMs * 0.001 * 44100.0 * 0.95));
        CHECK(od::reverbRoomIndexForId("club-m") == 5 && od::reverbRoomIndexForId("nope") == -1);
    }

    std::printf("mixer\n");
    {
        struct Harness {
            enum { N = 16384 };
            std::vector<std::vector<float>> inL, inR, out;
            od::Mixer mixer;
            od::MixParams p;
            Harness() {
                inL.assign(od::kNumStrips + 1, std::vector<float>(N, 0.0f));
                inR = inL;
                out.assign(od::kNumOutChannels, std::vector<float>(N, 0.0f));
                mixer.prepare(48000.0);
            }
            void fill(int strip, float v) {
                std::fill(inL[static_cast<size_t>(strip)].begin(), inL[static_cast<size_t>(strip)].end(), v);
                std::fill(inR[static_cast<size_t>(strip)].begin(), inR[static_cast<size_t>(strip)].end(), v);
            }
            void run() {
                for (auto& o : out) std::fill(o.begin(), o.end(), 0.0f);
                std::vector<float*> L, R, O;
                for (auto& v : inL) L.push_back(v.data());
                for (auto& v : inR) R.push_back(v.data());
                for (auto& v : out) O.push_back(v.data());
                mixer.setParams(p);
                mixer.process(L.data(), R.data(), O.data(), N);
            }
            float at(int ch, int i = N - 1) const { return out[static_cast<size_t>(ch)][static_cast<size_t>(i)]; }
        };
        auto near = [](float a, float b, float tol = 0.002f) { return std::fabs(a - b) < tol; };

        {
            Harness h;
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.5f) && near(h.at(1), 0.5f) && near(h.at(2), 0.0f));
            CHECK(near(h.at(0, 511), 0.5f) && near(h.at(0, 512), 0.5f) && near(h.at(0, 1500), 0.5f)); // chunk boundaries

            h.p.strip[0].mute = true;
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.0f));
            h.p.strip[0].mute = false;

            h.p.strip[1].solo = true; // soloing the snare silences the kick
            h.fill(0, 0.5f);
            h.fill(1, 0.2f);
            h.run();
            CHECK(near(h.at(0), 0.2f));
            h.p.strip[1].solo = false;
            h.fill(1, 0.0f);

            h.p.strip[0].phase = true;
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), -0.5f));
            h.p.strip[0].phase = false;

            h.p.strip[0].faderDb = -6.0f;
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.5f * std::pow(10.0f, -6.0f / 20.0f)));
            h.p.strip[0].faderDb = 0.0f;

            h.p.strip[0].pan = -1.0f; // hard left, constant power: 0.5 * sqrt(2) on the left, nothing on the right
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.5f * 1.41421356f, 0.003f) && near(h.at(1), 0.0f, 0.003f));
            h.p.strip[0].pan = 0.0f;

            h.p.masterDb = -6.0f;
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.5f * std::pow(10.0f, -6.0f / 20.0f)));
            h.p.masterDb = 0.0f;

            // routing to a direct stereo pair and to a mono output (bypassing the master)
            h.p.strip[0].outMask = 1u << (od::kRouteStereoFirst + 1); // 3/4
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.0f) && near(h.at(2), 0.5f) && near(h.at(3), 0.5f));
            h.p.strip[0].outMask = 1u << (od::kRouteMonoFirst + 4); // mono 5
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(4), 0.5f) && near(h.at(0), 0.0f) && near(h.at(5), 0.0f));
            h.p.strip[0].outMask = 1u | (1u << (od::kRouteStereoFirst + 2)); // master AND 5/6
            h.fill(0, 0.5f);
            h.run();
            CHECK(near(h.at(0), 0.5f) && near(h.at(4), 0.5f) && near(h.at(5), 0.5f));
            h.p.strip[0].outMask = od::kDefaultRoute;

            // audio from a slot with no strip goes to the master
            h.fill(0, 0.0f);
            h.fill(od::kNumStrips, 0.3f);
            h.run();
            CHECK(near(h.at(0), 0.3f));
            h.fill(od::kNumStrips, 0.0f);

            // meters read and reset
            h.mixer.takeStripPeak(0); // meters keep the maximum since they were last read
            h.fill(0, 0.5f);
            h.run();
            const float pk = h.mixer.takeStripPeak(0);
            CHECK(near(pk, 0.5f) && h.mixer.takeStripPeak(0) == 0.0f);
        }
        {
            // sends: with the dry signal not routed anywhere, only the effect returns are heard
            Harness h;
            h.p.strip[0].outMask = 0;
            h.inL[0][0] = h.inR[0][0] = 1.0f;
            h.run();
            double dry = 0;
            for (int i = 0; i < Harness::N; ++i) dry += std::fabs(h.out[0][static_cast<size_t>(i)]);
            CHECK(dry == 0.0);

            h.p.strip[0].sendReverb = 1.0f;
            h.p.reverb.rv.room = 7;
            h.p.reverb.rv.extraPredelayMs = 0;
            std::fill(h.inL[0].begin(), h.inL[0].end(), 0.0f);
            std::fill(h.inR[0].begin(), h.inR[0].end(), 0.0f);
            h.inL[0][0] = h.inR[0][0] = 1.0f;
            h.run();
            double early = 0, late = 0;
            for (int i = 0; i < Harness::N; ++i) (i < 3100 ? early : late) += std::fabs(h.out[0][static_cast<size_t>(i)]);
            std::printf("  reverb send: energy before the stadium pre-delay %.6f, after %.6f\n", early, late);
            CHECK(early == 0.0 && late > 1e-4); // pre-delay 65 ms = 3120 samples at 48 kHz

            Harness hp;
            hp.p.strip[0].outMask = 0;
            hp.p.strip[0].sendParallel = 1.0f;
            hp.fill(0, 0.5f);
            hp.run();
            // 0.5 -> 21 dB of gain reduction (thr -30, ratio 8) + 6 dB make-up, return -8 dB = 0.0355
            CHECK(near(hp.at(0), 0.0355f, 0.006f));
        }
    }

    std::printf("engine strips, MIDI channels and note overrides\n");
    {
        od::DrumEngine e2;
        e2.prepare(44100.0);
        e2.setMasterGain(1.0f);
        e2.setRig(rig);
        std::vector<std::vector<float>> bl(od::kNumStrips + 1, std::vector<float>(512, 0.0f)), br = bl;
        std::vector<float*> L, R;
        for (auto& v : bl) L.push_back(v.data());
        for (auto& v : br) R.push_back(v.data());
        auto peakOfStrip = [&](int s) {
            float p = 0;
            for (float x : bl[static_cast<size_t>(s)]) p = std::max(p, std::fabs(x));
            return p;
        };

        e2.noteOn(36, 127); // kick -> strip 0
        e2.renderStrips(L.data(), R.data(), 512);
        CHECK(peakOfStrip(0) > 0.01f && peakOfStrip(1) == 0.0f && peakOfStrip(od::kNumStrips) == 0.0f);
        e2.allSoundOff();
        for (auto& v : bl) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : br) std::fill(v.begin(), v.end(), 0.0f);
        e2.noteOn(38, 127); // snare -> strip 1
        e2.renderStrips(L.data(), R.data(), 512);
        CHECK(peakOfStrip(1) > 0.01f && peakOfStrip(0) == 0.0f);

        e2.allSoundOff();
        e2.setChannelMask(0, 0x0002); // the kick answers to MIDI channel 2 only
        e2.noteOn(36, 100, 0);
        CHECK(e2.activeVoices() == 0);
        e2.noteOn(36, 100, 1);
        CHECK(e2.activeVoices() == 1);
        e2.noteOn(36, 100, -1); // clicks and grooves are not filtered
        CHECK(e2.activeVoices() == 2);

        od::NoteOverrides ov;
        ov["snare"]["hit"] = {60};
        auto remapped = od::buildRig(lib, *rock, {}, ov);
        CHECK(remapped->notes[60].slot != nullptr && remapped->notes[60].slot->view.slot == "snare");
        CHECK(remapped->notes[38].slot == nullptr && remapped->notes[40].slot != nullptr);
        CHECK(od::stripIndexForSlot("hihat") == 6 && od::stripIndexForSlot("nope") == -1);
        CHECK(od::routeName(0) == "Master" && od::routeName(2) == "Stereo 3/4" && od::routeName(13) == "Mono 5");
    }

    std::printf("grooves\n");
    if (const char* gdir = std::getenv("OPENDRUMS_GROOVES")) {
        od::GrooveLibrary gl;
        gl.scan(gdir);
        std::printf("  %zu styles, %d clips\n", gl.styles().size(), gl.totalClips());
        CHECK(gl.styles().size() >= 12 && gl.totalClips() > 1000);
        const auto* st = &gl.styles().front();
        const auto& clipInfo = st->meters.front().sections.front().clips.front();
        std::string err;
        auto clip = od::loadMidiClip(clipInfo.path, err);
        CHECK(clip != nullptr);
        if (clip) {
            CHECK(clip->ppq == 480 && !clip->notes.empty() && clip->lengthTicks % clip->ticksPerBar() == 0);
            CHECK(!od::makeThumbnail(*clip).empty());
            od::GroovePlayer gp;
            gp.setClip(clip);
            gp.start();
            int count = 0, last = -1; bool ordered = true;
            const double sr = 48000, bpm = 120;
            const int total = static_cast<int>(sr * 60.0 / bpm * 4 * clip->bars() * 2); // two loops
            for (int done = 0; done < total; done += 256)
                gp.process(256, sr, bpm, [&](int off, int, int) { int g = done + off; ordered = ordered && g >= last; last = g; ++count; });
            std::printf("  clip notes %zu, played in 2 loops %d\n", clip->notes.size(), count);
            CHECK(ordered);
            CHECK(std::abs(count - 2 * static_cast<int>(clip->notes.size())) <= 1);
        }
        CHECK(!gl.search("funk verso").empty() && gl.search("zzzz").empty());
        CHECK(gl.findByRelPath(clipInfo.relPath) != nullptr);
    }

    std::printf("robustness\n");
    std::string loadError;
    CHECK(od::loadWav("/nonexistent.wav", loadError) == nullptr);
    CHECK(!loadError.empty());

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
