// OpenDrums core - kit library: scans a folder for kits and caches decoded samples.
#pragma once

#include "Kit.hpp"
#include "Sample.hpp"

#include <map>
#include <mutex>

namespace od {

class Library {
public:
    struct PieceHit {
        const KitDef* kit = nullptr;
        const PieceDef* piece = nullptr;
        explicit operator bool() const { return kit && piece; }
        std::string ref() const { return kit->id + "/" + piece->id; }
    };

    // Replaces the list of kits with the ones found in `folder` (<folder>/<kit>/kit.json).
    void scan(const std::filesystem::path& folder);

    const std::vector<std::shared_ptr<const KitDef>>& kits() const { return kits_; }
    const KitDef* findKit(const std::string& id) const;

    // "kitId/pieceId" -> piece (empty hit when unknown).
    PieceHit findPiece(const std::string& ref) const;

    // All pieces, from every kit, that fit a slot of the given family.
    std::vector<PieceHit> piecesOfFamily(const std::string& family) const;

    // Decoded sample, cached. Safe to call from a loader thread.
    std::shared_ptr<const Sample> sample(const KitDef& kit, const std::string& relativePath,
                                         std::string& error);

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::vector<std::shared_ptr<const KitDef>> kits_;
    std::vector<std::string> warnings_;
    std::mutex cacheMutex_;
    std::map<std::string, std::shared_ptr<const Sample>> cache_;
};

} // namespace od
