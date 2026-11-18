#include "Reverb.hpp"

#include "Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace od {

namespace {

const std::array<ReverbRoom, kNumRooms> kRooms = {{
    // id, name, rt60, predelay, size, damping, early, taps{ms, gain}
    {"garage", "Garagem", 0.90f, 3.0f, 0.55f, 0.20f, 0.60f,
     {{{7, .70f}, {11, .60f}, {17, .55f}, {23, .50f}, {31, .40f}, {41, .35f}, {53, .30f}, {67, .25f}}}},
    {"studio-s", "Studio pequeno", 0.35f, 2.0f, 0.40f, 0.55f, 0.50f,
     {{{3, .60f}, {5, .50f}, {8, .40f}, {11, .30f}, {15, .25f}, {19, .20f}, {24, .15f}, {29, .10f}}}},
    {"studio-m", "Studio médio", 0.60f, 6.0f, 0.60f, 0.50f, 0.50f,
     {{{5, .60f}, {8, .50f}, {12, .45f}, {17, .40f}, {23, .30f}, {29, .25f}, {37, .20f}, {45, .15f}}}},
    {"studio-l", "Studio grande", 0.95f, 12.0f, 0.85f, 0.45f, 0.45f,
     {{{8, .55f}, {13, .50f}, {19, .45f}, {27, .40f}, {36, .35f}, {46, .30f}, {58, .25f}, {71, .20f}}}},
    {"club-s", "Club pequeno", 0.80f, 10.0f, 0.70f, 0.60f, 0.45f,
     {{{9, .60f}, {14, .55f}, {21, .50f}, {29, .40f}, {39, .35f}, {51, .30f}, {64, .25f}, {80, .20f}}}},
    {"club-m", "Club médio", 1.30f, 18.0f, 0.95f, 0.55f, 0.40f,
     {{{12, .60f}, {19, .55f}, {28, .50f}, {38, .45f}, {51, .40f}, {66, .35f}, {83, .30f}, {104, .25f}}}},
    {"club-l", "Club grande", 1.90f, 28.0f, 1.20f, 0.50f, 0.40f,
     {{{16, .60f}, {25, .55f}, {37, .50f}, {50, .45f}, {66, .40f}, {85, .35f}, {108, .30f}, {135, .25f}}}},
    {"stadium", "Estádio", 3.40f, 65.0f, 1.60f, 0.40f, 0.45f,
     {{{22, .45f}, {38, .40f}, {61, .35f}, {92, .30f}, {140, .30f}, {205, .45f}, {240, .40f}, {310, .30f}}}},
}};

// Freeverb tunings (samples at 44.1 kHz); the right channel is offset for stereo width.
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[4] = {556, 441, 341, 225};
constexpr int kStereoSpread = 23;

} // namespace

const ReverbRoom& reverbRoom(int index) { return kRooms[static_cast<size_t>(std::clamp(index, 0, kNumRooms - 1))]; }

int reverbRoomIndexForId(const char* id) {
    for (int i = 0; i < kNumRooms; ++i)
        if (std::strcmp(kRooms[static_cast<size_t>(i)].id, id) == 0) return i;
    return -1;
}

void Reverb::prepare(double sampleRate) {
    sr_ = sampleRate > 0 ? sampleRate : 44100.0;
    prepared_ = true;
    dl_.assign(static_cast<size_t>(sr_ * 0.9), 0.0f); // room for pre-delay + the longest early tap
    builtRoom_ = -1;
    rebuild();
    setParams(p_);
}

void Reverb::reset() {
    std::fill(dl_.begin(), dl_.end(), 0.0f);
    for (auto& c : combL_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.filt = 0; }
    for (auto& c : combR_) { std::fill(c.buf.begin(), c.buf.end(), 0.0f); c.filt = 0; }
    for (auto& a : apL_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    for (auto& a : apR_) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    hpState_[0] = hpState_[1] = lpState_[0] = lpState_[1] = 0.0f;
}

void Reverb::rebuild() {
    const ReverbRoom& room = reverbRoom(p_.room);
    const double scale = sr_ / 44100.0 * room.size;
    for (int i = 0; i < kCombs; ++i) {
        const int lenL = std::max(8, static_cast<int>(kCombTuning[i] * scale));
        const int lenR = std::max(8, static_cast<int>((kCombTuning[i] + kStereoSpread) * scale));
        combL_[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(lenL), 0.0f);
        combL_[static_cast<size_t>(i)].len = lenL;
        combL_[static_cast<size_t>(i)].idx = 0;
        combL_[static_cast<size_t>(i)].filt = 0.0f;
        combR_[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(lenR), 0.0f);
        combR_[static_cast<size_t>(i)].len = lenR;
        combR_[static_cast<size_t>(i)].idx = 0;
        combR_[static_cast<size_t>(i)].filt = 0.0f;
    }
    for (int i = 0; i < kAllpass; ++i) {
        const int lenL = std::max(4, static_cast<int>(kAllpassTuning[i] * sr_ / 44100.0));
        const int lenR = std::max(4, static_cast<int>((kAllpassTuning[i] + kStereoSpread) * sr_ / 44100.0));
        apL_[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(lenL), 0.0f);
        apL_[static_cast<size_t>(i)].len = lenL;
        apL_[static_cast<size_t>(i)].idx = 0;
        apR_[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(lenR), 0.0f);
        apR_[static_cast<size_t>(i)].len = lenR;
        apR_[static_cast<size_t>(i)].idx = 0;
    }
    builtRoom_ = p_.room;
}

void Reverb::setParams(const ReverbParams& pIn) {
    if (!prepared_) {
        p_ = pIn;
        return;
    }
    ReverbParams p = pIn;
    p.room = std::clamp(p.room, 0, kNumRooms - 1);
    p.decayScale = std::clamp(p.decayScale, 0.3f, 3.0f);
    const bool roomChanged = p.room != builtRoom_;
    p_ = p;
    if (roomChanged) rebuild();

    const ReverbRoom& room = reverbRoom(p.room);
    damp_ = std::clamp(room.damping + (p.tone - 0.5f) * 0.8f, 0.0f, 0.92f);
    // The damping filters make the broadband tail decay faster than the feedback alone says
    // (measured: about 0.82 - 0.2 * damping of the target), so the target is raised to compensate;
    // that way "decay" really is the RT60 in seconds (checked in tests/core_test.cpp).
    const float rt60 = std::max(0.1f, room.rt60 * p.decayScale) / (0.82f - 0.2f * damp_);
    // Comb feedback from RT60: after one trip through a delay of L seconds the level falls by
    // 60 dB * L / RT60.
    for (int i = 0; i < kCombs; ++i) {
        for (auto* c : {&combL_[static_cast<size_t>(i)], &combR_[static_cast<size_t>(i)]}) {
            const float seconds = static_cast<float>(c->len / sr_);
            c->fb = std::pow(10.0f, -3.0f * seconds / rt60);
        }
    }
    earlyLevel_ = room.earlyLevel;
    width_ = std::clamp(p.width, 0.0f, 1.0f);

    predelaySamples_ = static_cast<int>((room.predelayMs + std::clamp(p.extraPredelayMs, 0.0f, 150.0f)) * 0.001 * sr_);
    for (size_t i = 0; i < taps_.size(); ++i) {
        taps_[i].samples = static_cast<int>(room.taps[i][0] * 0.001 * sr_ * (0.6 + 0.4 * room.size));
        taps_[i].gain = room.taps[i][1];
    }

    hpCoef_ = 1.0f - std::exp(-2.0f * 3.14159265f * std::clamp(p.lowCutHz, 10.0f, 1000.0f) / static_cast<float>(sr_));
    lpCoef_ = 1.0f - std::exp(-2.0f * 3.14159265f * std::clamp(p.highCutHz, 1000.0f, 20000.0f) / static_cast<float>(sr_));
}

void Reverb::process(const float* inL, const float* inR, float* outL, float* outR, int n) {
    const int dlSize = static_cast<int>(dl_.size());
    constexpr float kFixedGain = 0.015f; // Freeverb input scaling
    constexpr float kWetScale = 3.0f;
    const float wet1 = 0.5f * (1.0f + width_);
    const float wet2 = 0.5f * (1.0f - width_);

    for (int i = 0; i < n; ++i) {
        const float mono = (inL[i] + inR[i]) * 0.5f;
        dl_[static_cast<size_t>(dlIdx_)] = mono;

        auto tapAt = [&](int delay) {
            int pos = dlIdx_ - delay;
            if (pos < 0) pos += dlSize;
            return dl_[static_cast<size_t>(pos)];
        };

        const float lateIn = tapAt(std::min(predelaySamples_, dlSize - 1)) * kFixedGain;

        // late tail
        float lateL = 0.0f, lateR = 0.0f;
        for (int c = 0; c < kCombs; ++c) {
            Comb& cl = combL_[static_cast<size_t>(c)];
            Comb& cr = combR_[static_cast<size_t>(c)];
            const float ol = cl.buf[static_cast<size_t>(cl.idx)];
            cl.filt = ol * (1.0f - damp_) + cl.filt * damp_;
            cl.buf[static_cast<size_t>(cl.idx)] = lateIn + cl.filt * cl.fb;
            if (++cl.idx >= cl.len) cl.idx = 0;
            lateL += ol;

            const float orr = cr.buf[static_cast<size_t>(cr.idx)];
            cr.filt = orr * (1.0f - damp_) + cr.filt * damp_;
            cr.buf[static_cast<size_t>(cr.idx)] = lateIn + cr.filt * cr.fb;
            if (++cr.idx >= cr.len) cr.idx = 0;
            lateR += orr;
        }
        for (int a = 0; a < kAllpass; ++a) {
            Allpass& al = apL_[static_cast<size_t>(a)];
            const float bl = al.buf[static_cast<size_t>(al.idx)];
            const float yl = -lateL + bl;
            al.buf[static_cast<size_t>(al.idx)] = lateL + bl * 0.5f;
            if (++al.idx >= al.len) al.idx = 0;
            lateL = yl;

            Allpass& ar = apR_[static_cast<size_t>(a)];
            const float br = ar.buf[static_cast<size_t>(ar.idx)];
            const float yr = -lateR + br;
            ar.buf[static_cast<size_t>(ar.idx)] = lateR + br * 0.5f;
            if (++ar.idx >= ar.len) ar.idx = 0;
            lateR = yr;
        }
        lateL *= kWetScale;
        lateR *= kWetScale;

        // early reflections: taps alternate between the two channels
        float earlyL = 0.0f, earlyR = 0.0f;
        for (size_t t = 0; t < taps_.size(); ++t) {
            const int delay = predelaySamples_ + taps_[t].samples;
            if (delay >= dlSize) continue;
            const float v = tapAt(delay) * taps_[t].gain;
            if (t % 2 == 0) { earlyL += v; earlyR += v * 0.35f; }
            else { earlyR += v; earlyL += v * 0.35f; }
        }

        float l = lateL + earlyL * earlyLevel_;
        float r = lateR + earlyR * earlyLevel_;
        const float wl = l * wet1 + r * wet2;
        const float wr = r * wet1 + l * wet2;

        // wet low-cut (one-pole high-pass) and high-cut (one-pole low-pass)
        float ch[2] = {wl, wr};
        for (int c = 0; c < 2; ++c) {
            hpState_[c] += hpCoef_ * (ch[c] - hpState_[c]);
            const float hp = ch[c] - hpState_[c];
            lpState_[c] += lpCoef_ * (hp - lpState_[c]);
            ch[c] = lpState_[c];
        }
        outL[i] = ch[0];
        outR[i] = ch[1];

        if (++dlIdx_ >= dlSize) dlIdx_ = 0;
    }
}

} // namespace od
