// OpenDrums core - sample loading (WAV only in v1; FLAC/OGG planned).
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace od {

// An immutable, fully decoded sample. Mono samples keep `right` empty.
struct Sample {
    double sampleRate = 44100.0;
    std::vector<float> left;
    std::vector<float> right;

    size_t frames() const { return left.size(); }
    bool isStereo() const { return !right.empty(); }
};

// Loads PCM (8/16/24/32-bit) or IEEE-float (32-bit) WAV files.
// Files with more than two channels keep only the first two.
// Returns nullptr and fills `error` on failure.
std::shared_ptr<const Sample> loadWav(const std::filesystem::path& file, std::string& error);

// Writes a 16-bit PCM stereo WAV (used by the offline renderer and tests).
bool writeWav16(const std::filesystem::path& file, const std::vector<float>& left,
                const std::vector<float>& right, double sampleRate, std::string& error);

} // namespace od
