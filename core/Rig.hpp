// OpenDrums core - a "rig" is the playable, fully loaded kit: one piece per slot,
// samples decoded, MIDI notes resolved. It is immutable once built (except the
// round-robin counters, which only the audio thread touches).
#pragma once

#include "Library.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace od {

struct RigLayer {
    int velMin = 1;
    int velMax = 127;
    std::vector<std::shared_ptr<const Sample>> samples;
    mutable unsigned rr = 0; // round robin position (audio thread only)
};

struct RigArticulation {
    std::string role;
    std::vector<RigLayer> layers;
};

// What the editor needs to draw a slot.
struct SlotView {
    std::string slot;
    std::string type;
    std::string family;
    std::string pieceRef; // "kitId/pieceId" currently loaded in this slot
    std::string label;
    std::filesystem::path imagePath; // absolute, may be empty
    float diameter = 14.0f;          // inches (drawn width of the image)
    float x = 0.5f, y = 0.5f, scale = 1.0f;
};

struct RigSlot {
    SlotView view;
    int strip = -1; // mixer strip for this slot (-1: none)
    int chokeGroup = 0;
    float gain = 1.0f; // linear, from the piece
    float pan = 0.0f;
    float tuneSemitones = 0.0f;
    std::vector<RigArticulation> articulations;
};

struct NoteTarget {
    const RigSlot* slot = nullptr;
    const RigArticulation* art = nullptr;
};

struct Rig {
    std::string kitId;
    std::string kitName;
    std::vector<RigSlot> slots;
    std::array<NoteTarget, 128> notes{};
    std::vector<std::string> warnings;

    Rig() = default;
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
};

// slot id -> "kitId/pieceId". Slots not listed use the kit's own piece.
using Selection = std::map<std::string, std::string>;

// slot id -> role -> MIDI notes. Replaces the default (General MIDI) notes of a role; edited in the Matrix view.
using NoteOverrides = std::map<std::string, std::map<std::string, std::vector<int>>>;

// Loads samples (slow: call from a background/message thread, never from the audio thread).
std::shared_ptr<Rig> buildRig(Library& library, const KitDef& kit, const Selection& selection,
                              const NoteOverrides& overrides = {});

} // namespace od
