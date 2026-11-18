// OpenDrums core - the sample playback engine.
//
// Threading
//   message thread : setRig(), collectGarbage(), postNote(), setMasterGain()
//   audio thread   : noteOn(), drainPosted(), render(), allSoundOff()
// A new rig is handed over with one atomic pointer; the audio thread never allocates,
// locks or frees memory. Old rigs are kept alive by the message thread for a few
// seconds after the switch (voices fade out in 10 ms), then released by collectGarbage().
#pragma once

#include "Mixer.hpp"
#include "Rig.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace od {

class DrumEngine {
public:
    static constexpr int kMaxVoices = 64;

    DrumEngine() {
        for (auto& m : chMask_) m.store(0xFFFF);
    }

    void prepare(double sampleRate);

    // ---- message thread ----
    void setRig(std::shared_ptr<const Rig> rig);
    void collectGarbage(); // call regularly (e.g. 1 Hz timer)
    void postNote(int note, int velocity); // audition from the UI (single producer)
    void setMasterGain(float linear) { master_.store(linear, std::memory_order_relaxed); }
    // Notes that actually started a sound since the last call (for the animated kit view).
    // Returns false when the queue is empty. Message thread only.
    bool popHit(int& note, int& velocity);

    // Which MIDI channels (bit 0 = channel 1) may trigger a strip. Default: all.
    void setChannelMask(int strip, uint16_t mask) {
        if (strip >= 0 && strip < kNumStrips) chMask_[static_cast<size_t>(strip)].store(mask, std::memory_order_relaxed);
    }

    // ---- audio thread ----
    // `midiChannel` is 0..15 for notes from the host, -1 for audition clicks and the groove player.
    void noteOn(int note, int velocity, int midiChannel = -1);
    void drainPosted();
    void allSoundOff();
    // Adds the output of all voices into l/r (stereo, n frames).
    void render(float* l, float* r, int numFrames);
    // Same, but every strip gets its own buffers: L/R hold kNumStrips + 1 buffers (the last one
    // is for slots without a strip). Voices are ADDED to the buffers.
    void renderStrips(float* const* L, float* const* R, int numFrames);

    int activeVoices() const;

private:
    struct Voice {
        const Sample* sample = nullptr;
        double pos = 0.0;
        double step = 1.0;
        float gainL = 0.0f, gainR = 0.0f;
        float fade = 1.0f;
        float fadeStep = 0.0f; // > 0 while fading out
        int chokeGroup = 0;
        int strip = kNumStrips; // output strip (kNumStrips = no strip)
        uint64_t order = 0;
        bool active = false;
    };

    void applyPending();
    void startFade(Voice& v, double seconds);

    struct Held {
        std::shared_ptr<const Rig> rig;
        bool inactiveSet = false;
        std::chrono::steady_clock::time_point inactiveSince;
    };

    double sampleRate_ = 44100.0;
    std::array<Voice, kMaxVoices> voices_{};
    uint64_t orderCounter_ = 0;

    const Rig* active_ = nullptr;      // audio thread
    std::atomic<const Rig*> activeAck_{nullptr};
    std::atomic<const Rig*> pending_{nullptr};
    std::vector<Held> held_;           // message thread only
    std::atomic<float> master_{0.8f};
    std::array<std::atomic<uint16_t>, kNumStrips> chMask_{};

    // UI -> audio note queue (single producer, single consumer)
    static constexpr uint32_t kQueue = 64;
    std::array<std::atomic<uint32_t>, kQueue> queue_{};
    std::atomic<uint32_t> qHead_{0};
    std::atomic<uint32_t> qTail_{0};

    // audio -> UI hit events (single producer, single consumer)
    static constexpr uint32_t kHitQueue = 128;
    std::array<std::atomic<uint32_t>, kHitQueue> hitQ_{};
    std::atomic<uint32_t> hHead_{0};
    std::atomic<uint32_t> hTail_{0};
    void pushHit(int note, int velocity);
};

} // namespace od
