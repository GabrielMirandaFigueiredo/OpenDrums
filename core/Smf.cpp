#include "Smf.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace od {

namespace {

uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok() const { return p < end; }
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4 && p < end; ++i) {
            const uint8_t b = *p++;
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    }
};

} // namespace

std::shared_ptr<const MidiClip> loadMidiClip(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open " + file.string();
        return nullptr;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 14 || std::memcmp(bytes.data(), "MThd", 4) != 0) {
        error = "not a MIDI file: " + file.string();
        return nullptr;
    }
    const uint32_t headerLen = be32(bytes.data() + 4);
    const int format = be16(bytes.data() + 8);
    const int tracks = be16(bytes.data() + 10);
    const int division = be16(bytes.data() + 12);
    if (division & 0x8000 || division == 0 || format > 1) {
        error = "unsupported MIDI timing or format: " + file.string();
        return nullptr;
    }

    auto clip = std::make_shared<MidiClip>();
    clip->ppq = division;
    bool gotTempo = false, gotSig = false;
    int endTick = 0;

    size_t pos = 8 + headerLen;
    for (int t = 0; t < tracks && pos + 8 <= bytes.size(); ++t) {
        if (std::memcmp(bytes.data() + pos, "MTrk", 4) != 0) break;
        const uint32_t len = be32(bytes.data() + pos + 4);
        Reader r{bytes.data() + pos + 8, bytes.data() + std::min(bytes.size(), pos + 8 + len)};
        pos += 8 + len;

        int tick = 0;
        uint8_t running = 0;
        while (r.ok()) {
            tick += static_cast<int>(r.vlq());
            if (!r.ok()) break;
            uint8_t status = *r.p;
            if (status & 0x80) ++r.p;
            else status = running;
            if (status == 0xFF) { // meta
                if (!r.ok()) break;
                const uint8_t type = *r.p++;
                const uint32_t mlen = r.vlq();
                if (r.p + mlen > r.end) break;
                if (type == 0x51 && mlen == 3 && !gotTempo) {
                    const uint32_t us = (r.p[0] << 16) | (r.p[1] << 8) | r.p[2];
                    if (us > 0) clip->bpm = 60000000.0 / us;
                    gotTempo = true;
                } else if (type == 0x58 && mlen >= 2 && !gotSig) {
                    clip->tsNum = std::max(1, static_cast<int>(r.p[0]));
                    clip->tsDen = 1 << std::min(6, static_cast<int>(r.p[1]));
                    gotSig = true;
                } else if (type == 0x2F) {
                    endTick = std::max(endTick, tick);
                }
                r.p += mlen;
            } else if (status == 0xF0 || status == 0xF7) { // sysex
                r.p += std::min<size_t>(r.vlq(), static_cast<size_t>(r.end - r.p));
            } else {
                running = status;
                const int kind = status & 0xF0;
                const int dataBytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                if (r.p + dataBytes > r.end) break;
                const int d1 = r.p[0], d2 = dataBytes == 2 ? r.p[1] : 0;
                r.p += dataBytes;
                if (kind == 0x90 && d2 > 0) clip->notes.push_back({tick, d1, d2});
                endTick = std::max(endTick, tick);
            }
        }
    }

    if (clip->notes.empty()) {
        error = "no notes in " + file.string();
        return nullptr;
    }
    std::stable_sort(clip->notes.begin(), clip->notes.end(),
                     [](const ClipNote& a, const ClipNote& b) { return a.tick < b.tick; });
    const int bar = clip->ticksPerBar();
    clip->lengthTicks = std::max(bar, ((endTick + bar - 1) / bar) * bar);
    return clip;
}

std::vector<ThumbDot> makeThumbnail(const MidiClip& clip) {
    auto rowFor = [](int n) {
        switch (n) {
        case 35: case 36: return 0;
        case 37: case 38: case 39: case 40: return 1;
        case 41: case 43: case 45: case 47: case 48: case 50: return 2;
        case 42: case 44: case 46: return 3;
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 4;
        default: return 5;
        }
    };
    std::vector<ThumbDot> dots;
    dots.reserve(clip.notes.size());
    const float len = static_cast<float>(std::max(1, clip.lengthTicks));
    for (const auto& n : clip.notes)
        dots.push_back({static_cast<float>(n.tick) / len, rowFor(n.note), static_cast<float>(n.velocity) / 127.0f});
    return dots;
}

} // namespace od
