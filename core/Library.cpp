#include "Library.hpp"

#include <algorithm>

namespace od {

namespace fs = std::filesystem;

void Library::scan(const fs::path& folder) {
    kits_.clear();
    warnings_.clear();
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.clear();
    }

    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        warnings_.push_back("kits folder not found: " + folder.string());
        return;
    }

    std::vector<fs::path> candidates;
    if (fs::exists(folder / "kit.json", ec)) candidates.push_back(folder / "kit.json");
    for (const auto& entry : fs::directory_iterator(folder, ec))
        if (entry.is_directory(ec) && fs::exists(entry.path() / "kit.json", ec))
            candidates.push_back(entry.path() / "kit.json");
    std::sort(candidates.begin(), candidates.end());

    for (const auto& path : candidates) {
        KitDef kit;
        std::string error;
        if (!loadKitDef(path, kit, error)) {
            warnings_.push_back(error);
            continue;
        }
        if (findKit(kit.id)) {
            warnings_.push_back("duplicate kit id '" + kit.id + "' ignored: " + path.string());
            continue;
        }
        kits_.push_back(std::make_shared<const KitDef>(std::move(kit)));
    }
}

const KitDef* Library::findKit(const std::string& id) const {
    for (const auto& k : kits_)
        if (k->id == id) return k.get();
    return nullptr;
}

Library::PieceHit Library::findPiece(const std::string& ref) const {
    const auto slash = ref.find('/');
    if (slash == std::string::npos) return {};
    const KitDef* kit = findKit(ref.substr(0, slash));
    if (!kit) return {};
    const PieceDef* piece = kit->findPiece(ref.substr(slash + 1));
    return piece ? PieceHit{kit, piece} : PieceHit{};
}

std::vector<Library::PieceHit> Library::piecesOfFamily(const std::string& family) const {
    std::vector<PieceHit> out;
    for (const auto& k : kits_)
        for (const auto& p : k->pieces)
            if (typeFamily(p.type) == family) out.push_back({k.get(), &p});
    return out;
}

std::shared_ptr<const Sample> Library::sample(const KitDef& kit, const std::string& relativePath,
                                              std::string& error) {
    const fs::path full = (kit.root / relativePath).lexically_normal();
    const std::string key = full.string();
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
    }
    auto s = loadWav(full, error);
    if (s) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_[key] = s;
    }
    return s;
}

} // namespace od
