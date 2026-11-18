// OpenDrums core - the groove library (folders of MIDI clips) and the clip player.
//
// On disk:   <root>/<Style>/<meter bpm>/<NN Section>/<clip>.mid
//   e.g.     Grooves/Funk/4-4 100bpm/02 Verso/Verso 03.mid
// The folder names carry the meter and tempo ("4-4 100bpm" = 4/4 at 100 BPM) and the section
// order ("02 Verso"). The browser shows them as Style > Meter/BPM > Section > clips.
#pragma once

#include "Smf.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace od {

struct GrooveClipInfo {
    std::string name;        // file name without extension
    std::filesystem::path path;
    std::string relPath;     // relative to the library root, '/' separated (favourites are saved by this)
};

struct GrooveSection {
    std::string name;        // "Verso" (order prefix removed)
    std::vector<GrooveClipInfo> clips;
};

struct GrooveMeter {
    std::string name;        // "4/4 · 100 BPM"
    int tsNum = 4, tsDen = 4, bpm = 120;
    std::vector<GrooveSection> sections;
};

struct GrooveStyle {
    std::string name;
    std::vector<GrooveMeter> meters;
    int clipCount() const;
};

class GrooveLibrary {
public:
    // Replaces the content with what is found under `root`.
    void scan(const std::filesystem::path& root);

    const std::vector<GrooveStyle>& styles() const { return styles_; }
    const std::filesystem::path& root() const { return root_; }
    int totalClips() const;

    // Every clip whose "style meter section name" contains all the words of `query` (case-insensitive).
    struct Hit {
        const GrooveStyle* style;
        const GrooveMeter* meter;
        const GrooveSection* section;
        const GrooveClipInfo* clip;
    };
    std::vector<Hit> search(const std::string& query, size_t maxResults = 400) const;
    const GrooveClipInfo* findByRelPath(const std::string& rel) const;

private:
    std::filesystem::path root_;
    std::vector<GrooveStyle> styles_;
};

// Plays a clip in a loop, sample-accurate. The host (or the plugin) calls process() once per audio block.
class GroovePlayer {
public:
    void setClip(std::shared_ptr<const MidiClip> clip); // also rewinds
    void start() { playing_ = clip_ != nullptr; pos_ = 0.0; idx_ = 0; }
    void stop() { playing_ = false; }
    bool isPlaying() const { return playing_; }
    const MidiClip* clip() const { return clip_.get(); }

    // 0..1 position inside the loop (for a playhead in the UI).
    float position() const { return clip_ && clip_->lengthTicks > 0 ? static_cast<float>(pos_ / clip_->lengthTicks) : 0.0f; }

    // Advances numFrames at `bpm` and calls emit(offsetInBlock, note, velocity) for every note that falls in the block.
    template <class Emit>
    void process(int numFrames, double sampleRate, double bpm, Emit&& emit) {
        if (!playing_ || !clip_ || bpm <= 0.0 || sampleRate <= 0.0) return;
        const double ticksPerSample = bpm * clip_->ppq / (60.0 * sampleRate);
        const double length = static_cast<double>(clip_->lengthTicks);
        int done = 0;
        while (done < numFrames) {
            const double left = length - pos_;
            const int toEnd = std::max(1, static_cast<int>(std::ceil(left / ticksPerSample - 1e-9)));
            const int chunk = std::min(numFrames - done, toEnd);
            const double endPos = pos_ + chunk * ticksPerSample;
            while (idx_ < clip_->notes.size() && clip_->notes[idx_].tick < endPos - 1e-9) {
                const auto& n = clip_->notes[idx_];
                const int offset = done + std::clamp(static_cast<int>((n.tick - pos_) / ticksPerSample), 0, chunk - 1);
                emit(offset, n.note, n.velocity);
                ++idx_;
            }
            pos_ = endPos;
            done += chunk;
            if (pos_ >= length - 1e-9) {
                pos_ -= length;
                if (pos_ < 0.0) pos_ = 0.0;
                idx_ = 0;
            }
        }
    }

private:
    std::shared_ptr<const MidiClip> clip_;
    bool playing_ = false;
    double pos_ = 0.0;
    size_t idx_ = 0;
};

} // namespace od
