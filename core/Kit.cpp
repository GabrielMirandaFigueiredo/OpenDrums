#include "Kit.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace od {

using json = nlohmann::json;

const PieceDef* KitDef::findPiece(const std::string& pieceId) const {
    for (const auto& p : pieces)
        if (p.id == pieceId) return &p;
    return nullptr;
}

const SlotDefaults* findSlotDefaults(const std::string& slot) {
    static const std::vector<SlotDefaults> table = {
        {"kick", "kick", 0, {{"hit", {36, 35}}}},
        {"snare", "snare", 0, {{"hit", {38}}, {"rimshot", {40}}, {"sidestick", {37}}}},
        {"tom1", "tom", 0, {{"hit", {50}}}},
        {"tom2", "tom", 0, {{"hit", {48, 47}}}},
        {"tom3", "tom", 0, {{"hit", {45}}}},
        {"floor", "tom", 0, {{"hit", {43, 41}}}},
        {"hihat", "hihat", 1, {{"closed", {42}}, {"pedal", {44}}, {"open", {46}}}},
        {"crash1", "cymbal", 0, {{"hit", {49}}}},
        {"crash2", "cymbal", 0, {{"hit", {57}}}},
        {"ride", "cymbal", 0, {{"hit", {51, 59}}, {"bell", {53}}}},
        {"splash", "cymbal", 0, {{"hit", {55}}}},
        {"china", "cymbal", 0, {{"hit", {52}}}},
        {"cowbell", "perc", 0, {{"hit", {56}}}},
        {"tambourine", "perc", 0, {{"hit", {54}}}},
    };
    for (const auto& s : table)
        if (s.slot == slot) return &s;
    return nullptr;
}

std::string typeFamily(const std::string& type) {
    if (type == "crash" || type == "ride" || type == "splash" || type == "china" || type == "cymbal")
        return "cymbal";
    return type;
}

namespace {

std::string str(const json& j, const char* key, const std::string& def = {}) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
}

float num(const json& j, const char* key, float def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<float>() : def;
}

void parseMidi(const json& j, std::map<std::string, std::vector<int>>& out) {
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it) {
        std::vector<int> notes;
        if (it.value().is_number_integer()) {
            notes.push_back(it.value().get<int>());
        } else if (it.value().is_array()) {
            for (const auto& n : it.value())
                if (n.is_number_integer()) notes.push_back(n.get<int>());
        }
        notes.erase(std::remove_if(notes.begin(), notes.end(), [](int n) { return n < 0 || n > 127; }),
                    notes.end());
        if (!notes.empty()) out[it.key()] = std::move(notes);
    }
}

} // namespace

bool loadKitDef(const std::filesystem::path& kitJson, KitDef& out, std::string& error) {
    std::ifstream in(kitJson);
    if (!in) {
        error = "cannot open " + kitJson.string();
        return false;
    }
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        error = "invalid JSON in " + kitJson.string();
        return false;
    }

    KitDef kit;
    kit.root = kitJson.parent_path();
    kit.id = str(j, "id");
    kit.name = str(j, "name", kit.id);
    kit.author = str(j, "author");
    kit.license = str(j, "license");
    kit.description = str(j, "description");
    if (kit.id.empty() || kit.id.find('/') != std::string::npos) {
        error = "kit.json needs an \"id\" without '/': " + kitJson.string();
        return false;
    }

    if (j.contains("pieces") && j["pieces"].is_array()) {
        for (const auto& pj : j["pieces"]) {
            PieceDef p;
            p.id = str(pj, "id");
            if (p.id.empty() || p.id.find('/') != std::string::npos) continue;
            p.type = str(pj, "type", "perc");
            p.label = str(pj, "label", p.id);
            p.image = str(pj, "image");
            p.diameter = std::clamp(num(pj, "diameter", 14.0f), 4.0f, 40.0f);
            p.gainDb = num(pj, "gainDb", 0.0f);
            p.pan = std::clamp(num(pj, "pan", 0.0f), -1.0f, 1.0f);
            p.tune = num(pj, "tune", 0.0f);

            if (pj.contains("articulations") && pj["articulations"].is_object()) {
                for (auto it = pj["articulations"].begin(); it != pj["articulations"].end(); ++it) {
                    Articulation art;
                    const auto& layers = it.value().contains("layers") ? it.value()["layers"] : json::array();
                    for (const auto& lj : layers) {
                        Layer l;
                        l.velMin = std::clamp(static_cast<int>(num(lj, "velMin", 1)), 1, 127);
                        l.velMax = std::clamp(static_cast<int>(num(lj, "velMax", 127)), l.velMin, 127);
                        if (lj.contains("files") && lj["files"].is_array())
                            for (const auto& f : lj["files"])
                                if (f.is_string()) l.files.push_back(f.get<std::string>());
                        if (!l.files.empty()) art.layers.push_back(std::move(l));
                    }
                    std::sort(art.layers.begin(), art.layers.end(),
                              [](const Layer& a, const Layer& b) { return a.velMin < b.velMin; });
                    if (!art.layers.empty()) p.articulations[it.key()] = std::move(art);
                }
            }
            kit.pieces.push_back(std::move(p));
        }
    }

    if (j.contains("layout") && j["layout"].is_array()) {
        for (const auto& sj : j["layout"]) {
            SlotLayout s;
            s.slot = str(sj, "slot");
            s.piece = str(sj, "piece");
            if (s.slot.empty()) continue;
            s.x = num(sj, "x", 0.5f);
            s.y = num(sj, "y", 0.5f);
            s.scale = std::clamp(num(sj, "scale", 1.0f), 0.2f, 3.0f);
            if (sj.contains("midi")) parseMidi(sj["midi"], s.midi);
            kit.layout.push_back(std::move(s));
        }
    }

    if (kit.layout.empty()) {
        error = "kit has no layout: " + kitJson.string();
        return false;
    }
    out = std::move(kit);
    return true;
}

} // namespace od
