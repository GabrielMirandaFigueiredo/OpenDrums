#include "Mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace od {

namespace {
const char* const kStripIds[kNumStrips] = {"kick",   "snare",  "tom1",   "tom2",  "tom3",
                                           "floor",  "hihat",  "crash1", "crash2", "ride",
                                           "splash", "china",  "cowbell", "tambourine"};

void storeMax(std::atomic<float>& a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}
} // namespace

const char* stripId(int index) { return kStripIds[std::clamp(index, 0, kNumStrips - 1)]; }

int stripIndexForSlot(const std::string& slotId) {
    for (int i = 0; i < kNumStrips; ++i)
        if (slotId == kStripIds[i]) return i;
    return -1;
}

std::string routeName(int bit) {
    if (bit == kRouteMaster) return "Master";
    if (bit >= kRouteStereoFirst && bit < kRouteMonoFirst) {
        const int k = bit - kRouteStereoFirst;
        return "Stereo " + std::to_string(k * 2 + 1) + "/" + std::to_string(k * 2 + 2);
    }
    if (bit >= kRouteMonoFirst && bit < kNumRoutes) return "Mono " + std::to_string(bit - kRouteMonoFirst + 1);
    return "?";
}

void Mixer::prepare(double sampleRate) {
    sr_ = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& e : eq_) e.prepare(sr_);
    for (auto& c : comp_) c.prepare(sr_);
    parComp_.prepare(sr_);
    reverb_.prepare(sr_);
    setParams(p_);
}

void Mixer::setParams(const MixParams& p) {
    p_ = p;
    for (int i = 0; i < kNumStrips; ++i) {
        eq_[static_cast<size_t>(i)].setParams(p.strip[static_cast<size_t>(i)].eq);
        comp_[static_cast<size_t>(i)].setParams(p.strip[static_cast<size_t>(i)].comp);
    }
    CompParams pc = p.parallel.comp;
    pc.on = true;
    parComp_.setParams(pc);
    reverb_.setParams(p.reverb.rv);
}

void Mixer::process(float* const* inL, float* const* inR, float* const* out, int numFrames) {
    int done = 0;
    while (done < numFrames) {
        const int n = std::min(kMaxBlock, numFrames - done);
        float* cl[kNumStrips + 1];
        float* cr[kNumStrips + 1];
        float* co[kNumOutChannels];
        for (int i = 0; i <= kNumStrips; ++i) {
            cl[i] = inL[i] ? inL[i] + done : nullptr;
            cr[i] = inR[i] ? inR[i] + done : nullptr;
        }
        for (int i = 0; i < kNumOutChannels; ++i) co[i] = out[i] + done;
        processChunk(cl, cr, co, n);
        done += n;
    }
}

void Mixer::processChunk(float* const* inL, float* const* inR, float* const* out, int n) {
    std::fill_n(mL_.begin(), n, 0.0f);
    std::fill_n(mR_.begin(), n, 0.0f);
    std::fill_n(rvL_.begin(), n, 0.0f);
    std::fill_n(rvR_.begin(), n, 0.0f);
    std::fill_n(paL_.begin(), n, 0.0f);
    std::fill_n(paR_.begin(), n, 0.0f);

    bool anySolo = false;
    for (const auto& s : p_.strip) anySolo = anySolo || s.solo;

    for (int s = 0; s < kNumStrips; ++s) {
        float* l = inL[s];
        float* r = inR[s];
        if (!l || !r) continue;
        const StripParams& sp = p_.strip[static_cast<size_t>(s)];

        if (sp.phase)
            for (int i = 0; i < n; ++i) { l[i] = -l[i]; r[i] = -r[i]; }
        eq_[static_cast<size_t>(s)].process(l, r, n);
        comp_[static_cast<size_t>(s)].process(l, r, n);
        stripGr_[static_cast<size_t>(s)].store(comp_[static_cast<size_t>(s)].gainReductionDb(), std::memory_order_relaxed);

        const bool muted = sp.mute || (anySolo && !sp.solo);
        const float fader = (sp.faderDb <= -59.9f || muted) ? 0.0f : dbToGain(sp.faderDb);
        const float angle = (std::clamp(sp.pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f;
        const float gL = fader * std::cos(angle) * 1.41421356f;
        const float gR = fader * std::sin(angle) * 1.41421356f;
        const float revSend = sp.sendReverb * sp.sendReverb;
        const float parSend = sp.sendParallel * sp.sendParallel;

        float peak = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float ol = l[i] * gL, orr = r[i] * gR;
            peak = std::max(peak, std::max(std::fabs(ol), std::fabs(orr)));
            if (revSend > 0.0f) { rvL_[static_cast<size_t>(i)] += ol * revSend; rvR_[static_cast<size_t>(i)] += orr * revSend; }
            if (parSend > 0.0f) { paL_[static_cast<size_t>(i)] += ol * parSend; paR_[static_cast<size_t>(i)] += orr * parSend; }
            const uint32_t mask = sp.outMask;
            if (mask & 1u) { mL_[static_cast<size_t>(i)] += ol; mR_[static_cast<size_t>(i)] += orr; }
            for (int k = 0; k < 8; ++k)
                if (mask & (1u << (kRouteStereoFirst + k))) { out[2 * k][i] += ol; out[2 * k + 1][i] += orr; }
            const float mono = (ol + orr) * 0.5f;
            for (int k = 0; k < 16; ++k)
                if (mask & (1u << (kRouteMonoFirst + k))) out[k][i] += mono;
        }
        storeMax(stripPeak_[static_cast<size_t>(s)], peak);
    }

    // audio from slots that have no strip goes straight to the master bus
    if (inL[kNumStrips] && inR[kNumStrips])
        for (int i = 0; i < n; ++i) { mL_[static_cast<size_t>(i)] += inL[kNumStrips][i]; mR_[static_cast<size_t>(i)] += inR[kNumStrips][i]; }

    // parallel compression bus
    parComp_.process(paL_.data(), paR_.data(), n);
    parGr_.store(parComp_.gainReductionDb(), std::memory_order_relaxed);
    const float parReturn = dbToGain(p_.parallel.returnDb);
    for (int i = 0; i < n; ++i) { mL_[static_cast<size_t>(i)] += paL_[static_cast<size_t>(i)] * parReturn; mR_[static_cast<size_t>(i)] += paR_[static_cast<size_t>(i)] * parReturn; }

    // reverb bus
    reverb_.process(rvL_.data(), rvR_.data(), wL_.data(), wR_.data(), n);
    const float revReturn = dbToGain(p_.reverb.returnDb);
    for (int i = 0; i < n; ++i) { mL_[static_cast<size_t>(i)] += wL_[static_cast<size_t>(i)] * revReturn; mR_[static_cast<size_t>(i)] += wR_[static_cast<size_t>(i)] * revReturn; }

    // master
    const float master = dbToGain(p_.masterDb);
    float pl = 0.0f, pr = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float l = mL_[static_cast<size_t>(i)] * master, r = mR_[static_cast<size_t>(i)] * master;
        out[0][i] += l;
        out[1][i] += r;
        pl = std::max(pl, std::fabs(l));
        pr = std::max(pr, std::fabs(r));
    }
    storeMax(masterPeak_[0], pl);
    storeMax(masterPeak_[1], pr);
}

} // namespace od
