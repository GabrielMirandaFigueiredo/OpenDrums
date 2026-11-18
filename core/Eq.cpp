#include "Eq.hpp"

namespace od {

void SslEq::prepare(double sampleRate) {
    sr_ = sampleRate > 0 ? sampleRate : 44100.0;
    dirty_ = true;
    reset();
    setParams(p_);
}

void SslEq::reset() {
    for (auto& b : band_) b.reset();
}

void SslEq::setParams(const EqParams& p) {
    if (!dirty_ && p == p_) return;
    p_ = p;
    dirty_ = false;

    band_[0].c = p.lfBell ? designPeaking(sr_, p.lfFreq, p.lfGain, 0.8) : designLowShelf(sr_, p.lfFreq, p.lfGain, 1.0);
    band_[1].c = designPeaking(sr_, p.lmfFreq, p.lmfGain, p.lmfQ);
    band_[2].c = designPeaking(sr_, p.hmfFreq, p.hmfGain, p.hmfQ);
    band_[3].c = p.hfBell ? designPeaking(sr_, p.hfFreq, p.hfGain, 0.8) : designHighShelf(sr_, p.hfFreq, p.hfGain, 1.0);

    const float gains[4] = {p.lfGain, p.lmfGain, p.hmfGain, p.hfGain};
    for (int i = 0; i < 4; ++i) {
        const bool a = std::fabs(gains[i]) >= 0.05f; // a flat band is skipped
        if (a && !active_[i]) band_[i].reset();
        active_[i] = a;
    }
}

void SslEq::process(float* l, float* r, int n) {
    if (!p_.on) return;
    for (int b = 0; b < 4; ++b) {
        if (!active_[b]) continue;
        Biquad& q = band_[b];
        for (int i = 0; i < n; ++i) {
            l[i] = q.process(0, l[i]);
            r[i] = q.process(1, r[i]);
        }
    }
}

} // namespace od
