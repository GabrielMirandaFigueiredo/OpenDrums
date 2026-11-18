#include "Rig.hpp"

#include "Mixer.hpp"

#include <cmath>

namespace od {

namespace {

// Role lookup: exact role, then "hit", then whatever the piece has.
const Articulation* pickArticulation(const PieceDef& piece, const std::string& role) {
    auto it = piece.articulations.find(role);
    if (it != piece.articulations.end()) return &it->second;
    it = piece.articulations.find("hit");
    if (it != piece.articulations.end()) return &it->second;
    if (!piece.articulations.empty()) return &piece.articulations.begin()->second;
    return nullptr;
}

} // namespace

std::shared_ptr<Rig> buildRig(Library& library, const KitDef& kit, const Selection& selection,
                              const NoteOverrides& overrides) {
    auto rig = std::make_shared<Rig>();
    rig->kitId = kit.id;
    rig->kitName = kit.name;
    rig->slots.reserve(kit.layout.size());

    // Notes already claimed, so two slots do not silently fight over the same key.
    std::array<std::string, 128> noteOwner;

    for (const auto& layout : kit.layout) {
        RigSlot slot;
        slot.view.slot = layout.slot;
        slot.view.x = layout.x;
        slot.view.y = layout.y;
        slot.view.scale = layout.scale;
        slot.strip = stripIndexForSlot(layout.slot);

        const SlotDefaults* defaults = findSlotDefaults(layout.slot);
        slot.chokeGroup = defaults ? defaults->chokeGroup : 0;

        // Which piece goes in this slot?
        std::string ref = kit.id + "/" + layout.piece;
        auto sel = selection.find(layout.slot);
        if (sel != selection.end()) ref = sel->second;
        Library::PieceHit hit = library.findPiece(ref);
        if (!hit && sel != selection.end()) { // stale selection: go back to the kit's own piece
            rig->warnings.push_back("slot '" + layout.slot + "': piece '" + ref + "' not found, using kit default");
            ref = kit.id + "/" + layout.piece;
            hit = library.findPiece(ref);
        }
        if (!hit) {
            rig->warnings.push_back("slot '" + layout.slot + "': piece '" + ref + "' not found");
            rig->slots.push_back(std::move(slot));
            continue;
        }

        const PieceDef& piece = *hit.piece;
        slot.view.type = piece.type;
        slot.view.family = defaults ? defaults->family : typeFamily(piece.type);
        slot.view.pieceRef = hit.ref();
        slot.view.label = piece.label;
        slot.view.diameter = piece.diameter;
        if (!piece.image.empty()) slot.view.imagePath = (hit.kit->root / piece.image).lexically_normal();
        slot.gain = std::pow(10.0f, piece.gainDb / 20.0f);
        slot.pan = piece.pan;
        slot.tuneSemitones = piece.tune;

        // MIDI map: defaults, overridden per role by the kit layout.
        std::map<std::string, std::vector<int>> midi;
        if (defaults) midi = defaults->midi;
        for (const auto& kv : layout.midi) midi[kv.first] = kv.second;
        if (auto ov = overrides.find(layout.slot); ov != overrides.end())
            for (const auto& kv : ov->second) midi[kv.first] = kv.second;

        // Roles that resolve to the same articulation share nothing but the decoded samples (cached).
        for (const auto& [role, notes] : midi) {
            const Articulation* art = pickArticulation(piece, role);
            if (!art) continue;
            RigArticulation ra;
            ra.role = role;
            for (const auto& layer : art->layers) {
                RigLayer rl;
                rl.velMin = layer.velMin;
                rl.velMax = layer.velMax;
                for (const auto& file : layer.files) {
                    std::string error;
                    auto s = library.sample(*hit.kit, file, error);
                    if (s) rl.samples.push_back(std::move(s));
                    else rig->warnings.push_back(error);
                }
                if (!rl.samples.empty()) ra.layers.push_back(std::move(rl));
            }
            if (!ra.layers.empty()) slot.articulations.push_back(std::move(ra));
        }
        rig->slots.push_back(std::move(slot));
    }

    // Resolve MIDI notes now that the slot vector will not move any more.
    for (auto& slot : rig->slots) {
        const SlotLayout* layout = nullptr;
        for (const auto& l : kit.layout)
            if (l.slot == slot.view.slot) layout = &l;
        const SlotDefaults* defaults = findSlotDefaults(slot.view.slot);
        std::map<std::string, std::vector<int>> midi;
        if (defaults) midi = defaults->midi;
        if (layout)
            for (const auto& kv : layout->midi) midi[kv.first] = kv.second;
        if (auto ov = overrides.find(slot.view.slot); ov != overrides.end())
            for (const auto& kv : ov->second) midi[kv.first] = kv.second;

        for (const auto& art : slot.articulations) {
            auto it = midi.find(art.role);
            if (it == midi.end()) continue;
            for (int note : it->second) {
                if (!noteOwner[note].empty() && noteOwner[note] != slot.view.slot)
                    rig->warnings.push_back("MIDI note " + std::to_string(note) + " used by '" +
                                            noteOwner[note] + "' and '" + slot.view.slot + "'");
                noteOwner[note] = slot.view.slot;
                rig->notes[note] = {&slot, &art};
            }
        }
    }
    return rig;
}

} // namespace od
