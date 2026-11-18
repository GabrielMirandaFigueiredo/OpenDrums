// OpenDrums core - Standard MIDI File reader (format 0 and 1) for groove clips.
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace od {

struct ClipNote {
    int tick = 0;
    int note = 0;
    int velocity = 0;
};

struct MidiClip {
    int ppq = 480;
    double bpm = 120.0;          // first tempo event (or 120)
    int tsNum = 4, tsDen = 4;    // first time signature event (or 4/4)
    int lengthTicks = 0;         // end of track, rounded up to whole bars
    std::vector<ClipNote> notes; // note-ons only, sorted by tick

    int ticksPerBar() const { return ppq * 4 * tsNum / tsDen; }
    int bars() const { return std::max(1, lengthTicks / std::max(1, ticksPerBar())); }
};

// Returns nullptr and fills `error` when the file is not a usable MIDI file.
std::shared_ptr<const MidiClip> loadMidiClip(const std::filesystem::path& file, std::string& error);

// Small picture of a clip: one dot per note, for the groove browser.
struct ThumbDot {
    float x = 0.0f;   // 0..1 along the clip
    int row = 0;      // 0 kick, 1 snare, 2 toms, 3 hi-hat, 4 cymbals, 5 other
    float strength = 1.0f; // 0..1 from the velocity
};
constexpr int kThumbRows = 6;
std::vector<ThumbDot> makeThumbnail(const MidiClip& clip);

} // namespace od
