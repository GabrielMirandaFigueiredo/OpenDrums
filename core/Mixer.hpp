// OpenDrums core - the mixer: one channel strip per drum piece, a parallel-compression bus,
// a reverb bus and a master, with routing to extra outputs.
//
// Strip chain:  phase -> EQ (SSL style) -> compressor -> fader -> mute/solo -> pan
//               -> sends (reverb, parallel compression) -> outputs
// Outputs: bit 0 = Master (through the master fader, 1/2); bits 1..8 = direct stereo pairs
//          1/2 .. 15/16; bits 9..24 = direct mono outputs 1..16. A strip may feed several.
#pragma once

#include "Compressor.hpp"
#include "Eq.hpp"
#include "Reverb.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace od {

constexpr int kNumStrips = 14;
constexpr int kNumOutChannels = 16;
constexpr int kRouteMaster = 0;
constexpr int kRouteStereoFirst = 1;  // 8 pairs
constexpr int kRouteMonoFirst = 9;    // 16 mono outputs
constexpr int kNumRoutes = 25;
constexpr uint32_t kDefaultRoute = 1u;

// Canonical strip order; a kit slot maps to a strip by its id.
const char* stripId(int index);
int stripIndexForSlot(const std::string& slotId); // -1 when the slot has no strip
std::string routeName(int bit);                   // "Master", "Stereo 3/4", "Mono 5"

struct StripParams {
    float faderDb = 0.0f; // -60 .. +6
    float pan = 0.0f;     // -1 .. 1
    bool mute = false, solo = false, phase = false;
    CompParams comp;
    EqParams eq;
    float sendReverb = 0.0f;   // 0 .. 1
    float sendParallel = 0.0f; // 0 .. 1
    uint32_t outMask = kDefaultRoute;
};

struct ParallelBusParams {
    CompParams comp;           // a squashing default; `on` is ignored (the bus always compresses)
    float returnDb = -8.0f;
    ParallelBusParams() {
        comp.on = true;
        comp.thresholdDb = -30.0f;
        comp.ratio = 8.0f;
        comp.makeupDb = 6.0f;
        comp.releaseMs = 120.0f;
        comp.fastAttack = true;
    }
};

struct ReverbBusParams {
    ReverbParams rv;
    float returnDb = -8.0f;
};

struct MixParams {
    std::array<StripParams, kNumStrips> strip;
    ParallelBusParams parallel;
    ReverbBusParams reverb;
    float masterDb = 0.0f;
};

class Mixer {
public:
    static constexpr int kMaxBlock = 512;

    void prepare(double sampleRate);
    void setParams(const MixParams& p);

    // `inL/inR` hold kNumStrips + 1 stereo buffers (the last one is audio from slots without a
    // strip; it goes straight to the master). The strip buffers are processed IN PLACE.
    // `out` holds kNumOutChannels buffers; the result is ADDED to them (clear them first).
    void process(float* const* inL, float* const* inR, float* const* out, int numFrames);

    // Meters (linear peak since last read / dB of gain reduction); written by the audio thread.
    float takeStripPeak(int strip) { return stripPeak_[static_cast<size_t>(strip)].exchange(0.0f); }
    float takeMasterPeak(int ch) { return masterPeak_[static_cast<size_t>(ch)].exchange(0.0f); }
    float stripGainReductionDb(int strip) const { return stripGr_[static_cast<size_t>(strip)].load(); }
    float parallelGainReductionDb() const { return parGr_.load(); }

private:
    void processChunk(float* const* inL, float* const* inR, float* const* out, int n);

    double sr_ = 44100.0;
    MixParams p_;
    std::array<SslEq, kNumStrips> eq_;
    std::array<Compressor, kNumStrips> comp_;
    Compressor parComp_;
    Reverb reverb_;

    std::array<float, kMaxBlock> mL_{}, mR_{}, rvL_{}, rvR_{}, paL_{}, paR_{}, wL_{}, wR_{};

    std::array<std::atomic<float>, kNumStrips> stripPeak_{};
    std::array<std::atomic<float>, kNumStrips> stripGr_{};
    std::array<std::atomic<float>, 2> masterPeak_{};
    std::atomic<float> parGr_{0.0f};
};

} // namespace od
