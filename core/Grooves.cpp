#include "Grooves.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace od {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "02 Verso" -> "Verso"
std::string stripOrderPrefix(const std::string& name) {
    size_t i = 0;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i > 0 && i < name.size() && name[i] == ' ') return name.substr(i + 1);
    return name;
}

// "4-4 100bpm" -> 4/4, 100
bool parseMeterFolder(const std::string& name, GrooveMeter& m) {
    int num = 0, den = 0, bpm = 0;
    if (std::sscanf(name.c_str(), "%d-%d %dbpm", &num, &den, &bpm) != 3 || num <= 0 || den <= 0 || bpm <= 0)
        return false;
    m.tsNum = num;
    m.tsDen = den;
    m.bpm = bpm;
    m.name = std::to_string(num) + "/" + std::to_string(den) + " \xC2\xB7 " + std::to_string(bpm) + " BPM";
    return true;
}

std::vector<fs::path> sortedChildren(const fs::path& dir, bool wantDirs) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (wantDirs ? e.is_directory(ec) : (e.is_regular_file(ec) && lower(e.path().extension().string()) == ".mid"))
            out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

int GrooveStyle::clipCount() const {
    int n = 0;
    for (const auto& m : meters)
        for (const auto& s : m.sections) n += static_cast<int>(s.clips.size());
    return n;
}

int GrooveLibrary::totalClips() const {
    int n = 0;
    for (const auto& s : styles_) n += s.clipCount();
    return n;
}

void GrooveLibrary::scan(const fs::path& root) {
    root_ = root;
    styles_.clear();
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;

    for (const auto& styleDir : sortedChildren(root, true)) {
        GrooveStyle style;
        style.name = stripOrderPrefix(styleDir.filename().string());

        std::vector<GrooveMeter> meters;
        for (const auto& meterDir : sortedChildren(styleDir, true)) {
            GrooveMeter meter;
            if (!parseMeterFolder(meterDir.filename().string(), meter)) continue;
            for (const auto& sectionDir : sortedChildren(meterDir, true)) {
                GrooveSection section;
                section.name = stripOrderPrefix(sectionDir.filename().string());
                for (const auto& file : sortedChildren(sectionDir, false)) {
                    GrooveClipInfo clip;
                    clip.name = file.stem().string();
                    clip.path = file;
                    clip.relPath = fs::relative(file, root, ec).generic_string();
                    section.clips.push_back(std::move(clip));
                }
                if (!section.clips.empty()) meter.sections.push_back(std::move(section));
            }
            if (!meter.sections.empty()) meters.push_back(std::move(meter));
        }
        // 4/4 first, then by tempo
        std::stable_sort(meters.begin(), meters.end(), [](const GrooveMeter& a, const GrooveMeter& b) {
            const int ka = a.tsNum * 100 / a.tsDen, kb = b.tsNum * 100 / b.tsDen;
            const bool a44 = a.tsNum == 4 && a.tsDen == 4, b44 = b.tsNum == 4 && b.tsDen == 4;
            if (a44 != b44) return a44;
            if (ka != kb) return ka < kb;
            return a.bpm < b.bpm;
        });
        style.meters = std::move(meters);
        if (!style.meters.empty()) styles_.push_back(std::move(style));
    }
}

std::vector<GrooveLibrary::Hit> GrooveLibrary::search(const std::string& query, size_t maxResults) const {
    std::vector<std::string> words;
    {
        std::string w;
        for (char c : lower(query)) {
            if (c == ' ') { if (!w.empty()) words.push_back(w); w.clear(); }
            else w += c;
        }
        if (!w.empty()) words.push_back(w);
    }
    std::vector<Hit> hits;
    if (words.empty()) return hits;
    for (const auto& st : styles_)
        for (const auto& me : st.meters)
            for (const auto& se : me.sections)
                for (const auto& cl : se.clips) {
                    const std::string hay = lower(st.name + " " + me.name + " " + se.name + " " + cl.name);
                    bool all = true;
                    for (const auto& w : words) all = all && hay.find(w) != std::string::npos;
                    if (all) {
                        hits.push_back({&st, &me, &se, &cl});
                        if (hits.size() >= maxResults) return hits;
                    }
                }
    return hits;
}

const GrooveClipInfo* GrooveLibrary::findByRelPath(const std::string& rel) const {
    for (const auto& st : styles_)
        for (const auto& me : st.meters)
            for (const auto& se : me.sections)
                for (const auto& cl : se.clips)
                    if (cl.relPath == rel) return &cl;
    return nullptr;
}

void GroovePlayer::setClip(std::shared_ptr<const MidiClip> clip) {
    clip_ = std::move(clip);
    pos_ = 0.0;
    idx_ = 0;
    if (!clip_) playing_ = false;
}

} // namespace od
