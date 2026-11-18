// OpenDrums core - algorithmic room reverb with 8 ready-made spaces.
//
// Structure: the Freeverb layout (parallel damped comb filters into series all-pass filters;
// Freeverb by Jezar at Dreampoint is public domain) extended with
//   * a pre-delay line,
//   * a multi-tap early-reflection pattern per room (for the stadium: strong distant slap echoes),
//   * comb feedback computed from a target RT60, so "decay" means seconds,
//   * low-cut / high-cut on the wet signal and a stereo width control.
// Everything here is original code written for OpenDrums.
#pragma once

#include <array>
#include <vector>

namespace od {

constexpr int kNumRooms = 8;

struct ReverbRoom {
    const char* id;        // stable id, used in saved state
    const char* name;      // pt-BR display name
    float rt60;            // seconds
    float predelayMs;
    float size;            // scales the comb/all-pass lengths
    float damping;         // 0 = bright (hard walls) .. 1 = dark (soft walls)
    float earlyLevel;      // level of the early reflections
    std::array<std::array<float, 2>, 8> taps; // {time ms, gain}
};

const ReverbRoom& reverbRoom(int index);
int reverbRoomIndexForId(const char* id); // -1 if unknown

struct ReverbParams {
    int room = 2;
    float decayScale = 1.0f;     // 0.5 .. 2 times the room's RT60
    float extraPredelayMs = 0.0f; // 0 .. 150
    float tone = 0.5f;           // 0 = brighter .. 1 = darker (0.5 = the room as designed)
    float lowCutHz = 80.0f;
    float highCutHz = 12000.0f;
    float width = 1.0f;          // 0 .. 1

    bool operator==(const ReverbParams& o) const {
        return room == o.room && decayScale == o.decayScale && extraPredelayMs == o.extraPredelayMs &&
               tone == o.tone && lowCutHz == o.lowCutHz && highCutHz == o.highCutHz && width == o.width;
    }
};

class Reverb {
public:
    void prepare(double sampleRate);
    void setParams(const ReverbParams& p);
    // Wet signal only. Input and output may be the same buffers.
    void process(const float* inL, const float* inR, float* outL, float* outR, int numFrames);
    void reset();

private:
    static constexpr int kCombs = 8;
    static constexpr int kAllpass = 4;

    struct Comb {
        std::vector<float> buf;
        int len = 1, idx = 0;
        float fb = 0.0f, filt = 0.0f;
    };
    struct Allpass {
        std::vector<float> buf;
        int len = 1, idx = 0;
    };

    void rebuild(); // after a change that needs new delay lengths

    double sr_ = 44100.0;
    ReverbParams p_;
    bool prepared_ = false;
    float damp_ = 0.3f;
    float earlyLevel_ = 0.5f;
    float width_ = 1.0f;

    std::array<Comb, kCombs> combL_, combR_;
    std::array<Allpass, kAllpass> apL_, apR_;

    std::vector<float> dl_; // mono input delay line (pre-delay + early taps)
    int dlIdx_ = 0;
    int predelaySamples_ = 0;
    struct Tap {
        int samples = 0;
        float gain = 0.0f;
    };
    std::array<Tap, 8> taps_;

    float hpState_[2] = {0, 0}, lpState_[2] = {0, 0};
    float hpCoef_ = 0.0f, lpCoef_ = 1.0f;
    int builtRoom_ = -1;
    float builtDecay_ = 0.0f;
};

} // namespace od
