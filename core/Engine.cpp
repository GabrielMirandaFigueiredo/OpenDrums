#include "Engine.hpp"

#include <algorithm>
#include <cmath>

namespace od {

void DrumEngine::prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    allSoundOff();
}

// ---------------------------------------------------------------- message thread

void DrumEngine::setRig(std::shared_ptr<const Rig> rig) {
    if (!rig) return;
    for (const auto& h : held_)
        if (h.rig == rig) return;
    held_.push_back({rig, false, {}});
    pending_.store(rig.get(), std::memory_order_release);
}

void DrumEngine::collectGarbage() {
    const Rig* a = activeAck_.load(std::memory_order_acquire);
    const Rig* p = pending_.load(std::memory_order_acquire);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = held_.begin(); it != held_.end();) {
        if (it->rig.get() == a || it->rig.get() == p) {
            it->inactiveSet = false;
            ++it;
        } else if (!it->inactiveSet) {
            it->inactiveSet = true;
            it->inactiveSince = now;
            ++it;
        } else if (now - it->inactiveSince > std::chrono::seconds(3)) {
            it = held_.erase(it);
        } else {
            ++it;
        }
    }
}

void DrumEngine::postNote(int note, int velocity) {
    const uint32_t head = qHead_.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % kQueue;
    if (next == qTail_.load(std::memory_order_acquire)) return; // full: drop
    queue_[head].store((static_cast<uint32_t>(note & 0x7F) << 8) | static_cast<uint32_t>(velocity & 0x7F),
                       std::memory_order_relaxed);
    qHead_.store(next, std::memory_order_release);
}

bool DrumEngine::popHit(int& note, int& velocity) {
    const uint32_t tail = hTail_.load(std::memory_order_relaxed);
    if (tail == hHead_.load(std::memory_order_acquire)) return false;
    const uint32_t v = hitQ_[tail].load(std::memory_order_relaxed);
    note = static_cast<int>(v >> 8);
    velocity = static_cast<int>(v & 0x7F);
    hTail_.store((tail + 1) % kHitQueue, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------- audio thread

void DrumEngine::pushHit(int note, int velocity) {
    const uint32_t head = hHead_.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % kHitQueue;
    if (next == hTail_.load(std::memory_order_acquire)) return; // full: drop (animation only)
    hitQ_[head].store((static_cast<uint32_t>(note & 0x7F) << 8) | static_cast<uint32_t>(velocity & 0x7F),
                      std::memory_order_relaxed);
    hHead_.store(next, std::memory_order_release);
}

void DrumEngine::applyPending() {
    const Rig* p = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (!p || p == active_) return;
    for (auto& v : voices_)
        if (v.active) startFade(v, 0.010); // old samples stay alive for >= 3 s, see collectGarbage()
    active_ = p;
    activeAck_.store(p, std::memory_order_release);
}

void DrumEngine::startFade(Voice& v, double seconds) {
    const float step = static_cast<float>(1.0 / std::max(1.0, seconds * sampleRate_));
    if (v.fadeStep <= 0.0f || step > v.fadeStep) v.fadeStep = step;
}

void DrumEngine::drainPosted() {
    uint32_t tail = qTail_.load(std::memory_order_relaxed);
    const uint32_t head = qHead_.load(std::memory_order_acquire);
    while (tail != head) {
        const uint32_t v = queue_[tail].load(std::memory_order_relaxed);
        noteOn(static_cast<int>(v >> 8), static_cast<int>(v & 0x7F));
        tail = (tail + 1) % kQueue;
    }
    qTail_.store(tail, std::memory_order_release);
}

void DrumEngine::noteOn(int note, int velocity, int midiChannel) {
    applyPending();
    if (!active_ || note < 0 || note > 127 || velocity <= 0) return;

    const NoteTarget& target = active_->notes[static_cast<size_t>(note)];
    if (!target.slot || !target.art || target.art->layers.empty()) return;
    const RigSlot& slot = *target.slot;
    const RigArticulation& art = *target.art;
    velocity = std::min(velocity, 127);

    // MIDI channel filter set in the Matrix view (clicks and the groove player always pass).
    if (midiChannel >= 0 && midiChannel < 16 && slot.strip >= 0 &&
        !(chMask_[static_cast<size_t>(slot.strip)].load(std::memory_order_relaxed) & (1u << midiChannel)))
        return;

    // Velocity layer: the one that contains the velocity, otherwise the nearest.
    const RigLayer* layer = nullptr;
    int bestDistance = 1000;
    for (const auto& l : art.layers) {
        const int d = velocity < l.velMin ? l.velMin - velocity : (velocity > l.velMax ? velocity - l.velMax : 0);
        if (d < bestDistance) {
            bestDistance = d;
            layer = &l;
        }
    }
    if (!layer || layer->samples.empty()) return;

    pushHit(note, velocity);

    const Sample* sample = layer->samples[layer->rr % layer->samples.size()].get();
    layer->rr++;

    // Loudness inside the layer (several layers) or across the full range (single layer).
    float velGain;
    if (art.layers.size() > 1) {
        const float t = (velocity - layer->velMin + 0.5f) / static_cast<float>(layer->velMax - layer->velMin + 1);
        velGain = 0.6f + 0.4f * std::clamp(t, 0.0f, 1.0f);
    } else {
        velGain = std::pow(velocity / 127.0f, 0.9f);
    }

    // Choke: a new hit in the group silences the previous ones (hi-hat closed/pedal/open).
    if (slot.chokeGroup > 0)
        for (auto& v : voices_)
            if (v.active && v.chokeGroup == slot.chokeGroup) startFade(v, 0.008);

    // Free voice, otherwise steal the oldest one.
    Voice* voice = nullptr;
    for (auto& v : voices_)
        if (!v.active) {
            voice = &v;
            break;
        }
    if (!voice) {
        voice = &voices_[0];
        for (auto& v : voices_)
            if (v.order < voice->order) voice = &v;
    }

    const float angle = (std::clamp(slot.pan, -1.0f, 1.0f) + 1.0f) * 0.78539816f; // 0..pi/2
    const float panL = std::cos(angle) * 1.41421356f;
    const float panR = std::sin(angle) * 1.41421356f;

    voice->sample = sample;
    voice->pos = 0.0;
    voice->step = (sample->sampleRate / sampleRate_) * std::pow(2.0, slot.tuneSemitones / 12.0);
    voice->gainL = velGain * slot.gain * panL;
    voice->gainR = velGain * slot.gain * panR;
    voice->fade = 1.0f;
    voice->fadeStep = 0.0f;
    voice->chokeGroup = slot.chokeGroup;
    voice->strip = slot.strip >= 0 ? slot.strip : kNumStrips;
    voice->order = ++orderCounter_;
    voice->active = true;
}

void DrumEngine::allSoundOff() {
    for (auto& v : voices_) v.active = false;
}

int DrumEngine::activeVoices() const {
    int n = 0;
    for (const auto& v : voices_)
        if (v.active) ++n;
    return n;
}

void DrumEngine::render(float* l, float* r, int numFrames) {
    float* L[kNumStrips + 1];
    float* R[kNumStrips + 1];
    for (int i = 0; i <= kNumStrips; ++i) {
        L[i] = l;
        R[i] = r;
    }
    renderStrips(L, R, numFrames);
}

void DrumEngine::renderStrips(float* const* L, float* const* R, int numFrames) {
    applyPending();
    const float master = master_.load(std::memory_order_relaxed);

    for (auto& v : voices_) {
        if (!v.active) continue;
        float* l = L[v.strip];
        float* r = R[v.strip];
        const Sample& s = *v.sample;
        const float* left = s.left.data();
        const float* right = s.isStereo() ? s.right.data() : s.left.data();
        const size_t last = s.frames() > 0 ? s.frames() - 1 : 0;

        double pos = v.pos;
        float fade = v.fade;
        bool alive = true;

        for (int i = 0; i < numFrames; ++i) {
            const size_t idx = static_cast<size_t>(pos);
            if (idx >= last) {
                alive = false;
                break;
            }
            const float frac = static_cast<float>(pos - static_cast<double>(idx));
            const float sl = left[idx] + (left[idx + 1] - left[idx]) * frac;
            const float sr = right[idx] + (right[idx + 1] - right[idx]) * frac;

            l[i] += sl * v.gainL * fade * master;
            r[i] += sr * v.gainR * fade * master;

            pos += v.step;
            if (v.fadeStep > 0.0f) {
                fade -= v.fadeStep;
                if (fade <= 0.0f) {
                    alive = false;
                    break;
                }
            }
        }
        v.pos = pos;
        v.fade = fade;
        if (!alive) v.active = false;
    }
}

} // namespace od
