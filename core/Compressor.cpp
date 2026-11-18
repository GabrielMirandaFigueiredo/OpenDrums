#include "Compressor.hpp"

namespace od {

void Compressor::prepare(double sampleRate) {
    sr_ = sampleRate > 0 ? sampleRate : 44100.0;
    setParams(p_);
    reset();
}

void Compressor::setParams(const CompParams& p) {
    p_ = p;
    const double attackMs = p.fastAttack ? 1.0 : 30.0;
    atk_ = static_cast<float>(std::exp(-1.0 / (attackMs * 0.001 * sr_)));
    rel_ = static_cast<float>(std::exp(-1.0 / (std::max(5.0f, p.releaseMs) * 0.001 * sr_)));
}

void Compressor::process(float* l, float* r, int n) {
    if (!p_.on) {
        grDb_ = 0.0f;
        return;
    }
    const float thr = p_.thresholdDb;
    const float ratio = std::max(1.0f, p_.ratio);
    const float knee = std::max(0.0f, p_.kneeDb);
    const float slope = 1.0f - 1.0f / ratio;
    const float makeup = p_.makeupDb;

    float gr = grDb_;
    for (int i = 0; i < n; ++i) {
        const float level = gainToDb(std::max(std::fabs(l[i]), std::fabs(r[i])));
        const float over = level - thr;
        float target;
        if (2.0f * over < -knee) target = 0.0f;
        else if (knee > 0.0f && 2.0f * std::fabs(over) <= knee) {
            const float x = over + knee * 0.5f;
            target = slope * x * x / (2.0f * knee);
        } else target = slope * over;

        gr = target > gr ? atk_ * gr + (1.0f - atk_) * target : rel_ * gr + (1.0f - rel_) * target;
        const float g = dbToGain(makeup - gr);
        l[i] *= g;
        r[i] *= g;
    }
    grDb_ = gr;
}

} // namespace od
