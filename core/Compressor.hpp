// OpenDrums core - feed-forward stereo-linked compressor with soft knee.
// Controls follow a channel-strip compressor: threshold, ratio, make-up, release and a
// fast/slow attack switch. Original implementation.
#pragma once

#include "Dsp.hpp"

namespace od {

struct CompParams {
    bool on = false;
    float thresholdDb = -18.0f; // -40 .. 0
    float ratio = 4.0f;         // 1 .. 20
    float makeupDb = 0.0f;      // 0 .. 24
    float releaseMs = 150.0f;   // 20 .. 1200
    bool fastAttack = true;     // fast ~ 1 ms, slow ~ 30 ms
    float kneeDb = 6.0f;

    bool operator==(const CompParams& o) const {
        return on == o.on && thresholdDb == o.thresholdDb && ratio == o.ratio && makeupDb == o.makeupDb &&
               releaseMs == o.releaseMs && fastAttack == o.fastAttack && kneeDb == o.kneeDb;
    }
};

class Compressor {
public:
    void prepare(double sampleRate);
    void setParams(const CompParams& p);
    void process(float* l, float* r, int numFrames); // in place; does nothing when off
    float gainReductionDb() const { return grDb_; }  // for meters (>= 0)
    void reset() { grDb_ = 0.0f; }

private:
    double sr_ = 44100.0;
    CompParams p_;
    float atk_ = 0.0f, rel_ = 0.0f;
    float grDb_ = 0.0f;
};

} // namespace od
