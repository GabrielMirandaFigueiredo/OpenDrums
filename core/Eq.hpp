// OpenDrums core - 4-band channel EQ modelled on the layout of an SSL-style console strip:
//   HF  : shelf or bell (button), frequency + gain
//   HMF : bell, frequency + gain + Q
//   LMF : bell, frequency + gain + Q
//   LF  : shelf or bell (button), frequency + gain
// This is an original implementation (RBJ biquads); it imitates the control layout, not the
// analogue circuit of any specific console.
#pragma once

#include "Dsp.hpp"

namespace od {

struct EqParams {
    bool on = false;
    float hfFreq = 8000.0f, hfGain = 0.0f;   // 1.5 .. 16 kHz, +-15 dB
    bool hfBell = false;
    float hmfFreq = 3000.0f, hmfGain = 0.0f, hmfQ = 1.0f; // 0.6 .. 7 kHz
    float lmfFreq = 800.0f, lmfGain = 0.0f, lmfQ = 1.0f;  // 0.2 .. 2.5 kHz
    float lfFreq = 100.0f, lfGain = 0.0f;    // 30 .. 450 Hz, +-15 dB
    bool lfBell = false;

    bool operator==(const EqParams& o) const {
        return on == o.on && hfFreq == o.hfFreq && hfGain == o.hfGain && hfBell == o.hfBell &&
               hmfFreq == o.hmfFreq && hmfGain == o.hmfGain && hmfQ == o.hmfQ && lmfFreq == o.lmfFreq &&
               lmfGain == o.lmfGain && lmfQ == o.lmfQ && lfFreq == o.lfFreq && lfGain == o.lfGain &&
               lfBell == o.lfBell;
    }
};

class SslEq {
public:
    void prepare(double sampleRate);
    void setParams(const EqParams& p); // cheap when nothing changed
    void process(float* l, float* r, int numFrames);
    void reset();

private:
    double sr_ = 44100.0;
    EqParams p_;
    bool dirty_ = true;
    bool active_[4] = {false, false, false, false};
    Biquad band_[4]; // 0 LF, 1 LMF, 2 HMF, 3 HF
};

} // namespace od
