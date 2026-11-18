// OpenDrums core - kit description (kit.json) data model.
//
// Concepts
//   Piece   : one physical drum/cymbal with its samples and its picture (image).
//   Slot    : a place on the kit (kick, snare, tom1, hihat, ...). A slot owns the
//             MIDI mapping, the choke group and the screen position. Any piece of a
//             compatible family can be dropped into any slot, from any kit.
//   Role    : an articulation name inside a piece ("hit", "rimshot", "closed", ...).
//             Slots map MIDI notes to roles; a piece that lacks a role falls back to "hit".
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace od {

struct Layer {
    int velMin = 1;
    int velMax = 127;
    std::vector<std::string> files; // round robin set, relative to the kit folder
};

struct Articulation {
    std::vector<Layer> layers; // sorted by velMin
};

struct PieceDef {
    std::string id;
    std::string type;  // kick, snare, tom, hihat, crash, ride, splash, china, perc
    std::string label; // human readable ("Caixa 14x5,5 metal"); only states what is known
    std::string image; // relative to the kit folder (SVG/PNG); may be empty
    float diameter = 14.0f; // inches; the editor draws the image this wide, so a swapped-in
                            // piece keeps its real size on screen
    float gainDb = 0.0f;
    float pan = 0.0f;  // -1..1
    float tune = 0.0f; // semitones
    std::map<std::string, Articulation> articulations;
};

struct SlotLayout {
    std::string slot;  // kick, snare, tom1, ...
    std::string piece; // id of a piece in this kit
    float x = 0.5f;    // centre, 0..1 of the kit view
    float y = 0.5f;
    float scale = 1.0f; // perspective tweak on top of the piece's own diameter
    std::map<std::string, std::vector<int>> midi; // optional overrides: role -> notes
};

struct KitDef {
    std::string id;
    std::string name;
    std::string author;
    std::string license;
    std::string description;
    std::filesystem::path root;
    std::vector<PieceDef> pieces;
    std::vector<SlotLayout> layout;

    const PieceDef* findPiece(const std::string& pieceId) const;
};

// Defaults for the well known slots (General MIDI drum map).
struct SlotDefaults {
    std::string slot;
    std::string family;
    int chokeGroup = 0;
    std::map<std::string, std::vector<int>> midi;
};

const SlotDefaults* findSlotDefaults(const std::string& slot);

// Pieces may be swapped between slots of the same family (crash/ride/splash/china = "cymbal").
std::string typeFamily(const std::string& type);

// Parses kit.json. Returns false and fills `error` if the file is unusable.
bool loadKitDef(const std::filesystem::path& kitJson, KitDef& out, std::string& error);

} // namespace od
