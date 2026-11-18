// Draws the kit view of a kit as an SVG, with the exact geometry the plugin uses
// (core/KitGeometry): stands, feet, kick pedal and hit animations. Convert the SVG to PNG
// with rsvg-convert.
//
// usage: preview_kit <kits_folder> <kit_id> <out.svg> [options...]
//   slot=kitId/pieceId        swap a piece, e.g. snare=demo-tight/snare
//   hit=slot:role:t:velocity  freeze an animation, e.g. hit=kick:hit:0:120  (t in seconds)
//   viewbox=x,y,w,h           crop (in view pixels; the full view is 936 x 492)
#include "KitGeometry.hpp"
#include "SceneGeometry.hpp"
#include "Rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr float kViewW = 936.0f;
constexpr float kViewH = 492.0f;

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string hex(uint32_t argb, float& opacity) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%06x", argb & 0xffffffu);
    opacity = static_cast<float>((argb >> 24) & 0xff) / 255.0f;
    return buf;
}

std::string primToSvg(const od::geo::Prim& p) {
    using Kind = od::geo::Prim::Kind;
    float op = 1.0f;
    const std::string col = hex(p.color, op);
    std::ostringstream o;
    switch (p.kind) {
    case Kind::Line:
        o << "<line x1=\"" << num(p.x1) << "\" y1=\"" << num(p.y1) << "\" x2=\"" << num(p.x2) << "\" y2=\""
          << num(p.y2) << "\" stroke=\"" << col << "\" stroke-opacity=\"" << num(op) << "\" stroke-width=\""
          << num(p.width) << "\" stroke-linecap=\"round\"/>";
        break;
    case Kind::Ellipse:
        o << "<ellipse cx=\"" << num(p.x1) << "\" cy=\"" << num(p.y1) << "\" rx=\"" << num(p.x2) << "\" ry=\""
          << num(p.y2) << "\" fill=\"" << col << "\" fill-opacity=\"" << num(op) << "\"/>";
        break;
    case Kind::RoundRect:
        o << "<rect x=\"" << num(p.x1) << "\" y=\"" << num(p.y1) << "\" width=\"" << num(p.x2) << "\" height=\""
          << num(p.y2) << "\" rx=\"" << num(p.width) << "\" fill=\"" << col << "\" fill-opacity=\"" << num(op)
          << "\"/>";
        break;
    case Kind::Poly:
        o << "<polygon points=\"";
        for (size_t i = 0; i + 1 < p.pts.size(); i += 2) o << num(p.pts[i]) << "," << num(p.pts[i + 1]) << " ";
        o << "\" fill=\"" << col << "\" fill-opacity=\"" << num(op) << "\"/>";
        break;
    }
    return o.str();
}

struct Picture {
    float w = 100, h = 100;
    std::string inner;
    bool ok = false;
};

Picture loadSvg(const std::filesystem::path& file) {
    Picture pic;
    std::ifstream in(file);
    if (!in) return pic;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto open = text.find("<svg");
    const auto close = text.rfind("</svg>");
    if (open == std::string::npos || close == std::string::npos) return pic;
    const auto gt = text.find('>', open);
    const auto vb = text.find("viewBox=\"", open);
    if (vb != std::string::npos) {
        float x, y, w, h;
        if (std::sscanf(text.c_str() + vb + 9, "%f %f %f %f", &x, &y, &w, &h) == 4) {
            pic.w = w;
            pic.h = h;
        }
    }
    pic.inner = text.substr(gt + 1, close - gt - 1);
    pic.ok = true;
    return pic;
}

std::string xformToSvg(const od::geo::Xform& x) {
    const float deg = x.rot * 180.0f / 3.14159265f;
    return "translate(" + num(x.dx) + " " + num(x.dy) + ") translate(" + num(x.px) + " " + num(x.py) +
           ") rotate(" + num(deg) + ") scale(" + num(x.sx) + " " + num(x.sy) + ") translate(" + num(-x.px) +
           " " + num(-x.py) + ")";
}

const char* slotName(const std::string& s) {
    static const std::map<std::string, const char*> names = {
        {"kick", "Bumbo"},     {"snare", "Caixa"},     {"tom1", "Tom 1"},      {"tom2", "Tom 2"},
        {"tom3", "Tom 3"},     {"floor", "Surdo"},     {"hihat", "Chimbal"},   {"crash1", "Ataque 1"},
        {"crash2", "Ataque 2"}, {"ride", "Condução"}, {"splash", "Splash"},   {"china", "China"},
        {"cowbell", "Cowbell"}, {"tambourine", "Pandeiro"}};
    auto it = names.find(s);
    return it != names.end() ? it->second : s.c_str();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: preview_kit <kits_folder> <kit_id> <out.svg> [slot=kit/piece] [hit=slot:role:t:vel] "
                    "[viewbox=x,y,w,h]\n");
        return 2;
    }
    od::Library lib;
    lib.scan(argv[1]);
    const od::KitDef* kit = lib.findKit(argv[2]);
    if (!kit) {
        std::printf("kit '%s' not found\n", argv[2]);
        return 1;
    }

    od::Selection sel;
    int room = -1;
    std::map<std::string, od::geo::HitAnim> hits;
    float vbx = 0, vby = 0, vbw = kViewW, vbh = kViewH;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        const auto eq = a.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = a.substr(0, eq), val = a.substr(eq + 1);
        if (key == "hit") {
            std::vector<std::string> f;
            std::stringstream ss(val);
            for (std::string s; std::getline(ss, s, ':');) f.push_back(s);
            if (f.size() == 4) {
                od::geo::HitAnim h;
                h.role = f[1];
                h.t = static_cast<float>(std::atof(f[2].c_str()));
                h.amp = od::geo::ampForVelocity(std::atoi(f[3].c_str()));
                hits[f[0]] = h;
            }
        } else if (key == "room") {
            room = std::atoi(val.c_str());
        } else if (key == "viewbox") {
            std::sscanf(val.c_str(), "%f,%f,%f,%f", &vbx, &vby, &vbw, &vbh);
        } else {
            sel[key] = val;
        }
    }

    auto rig = od::buildRig(lib, *kit, sel);
    for (const auto& w : rig->warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());

    struct Item {
        od::SlotView view;
        Picture pic;
    };
    std::vector<Item> items;
    for (const auto& s : rig->slots) {
        if (s.view.pieceRef.empty()) continue;
        Item it;
        it.view = s.view;
        it.pic = loadSvg(s.view.imagePath);
        items.push_back(std::move(it));
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.view.y < b.view.y; });

    std::ostringstream o;
    const float scale = 1.5f;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(vbw * scale) << "\" height=\""
      << num(vbh * scale) << "\" viewBox=\"" << num(vbx) << " " << num(vby) << " " << num(vbw) << " " << num(vbh)
      << "\" font-family=\"DejaVu Sans, sans-serif\">\n";
    o << "<defs><radialGradient id=\"bg\" cx=\"50%\" cy=\"50%\" r=\"75%\"><stop offset=\"0\" stop-color=\"#23262d\"/>"
         "<stop offset=\"1\" stop-color=\"#111317\"/></radialGradient></defs>\n";
    o << "<rect x=\"0\" y=\"0\" width=\"" << num(kViewW) << "\" height=\"" << num(kViewH)
      << "\" fill=\"url(#bg)\"/>\n";
    if (room >= 0)
        for (const auto& p : od::geo::buildScene(room, kViewW, kViewH)) o << primToSvg(p) << "\n";
    o << "<ellipse cx=\"" << num(kViewW / 2) << "\" cy=\"" << num(kViewH / 2 + kViewH * 0.1f) << "\" rx=\""
      << num(kViewW * 0.43f) << "\" ry=\"" << num(kViewH * 0.31f) << "\" fill=\"#ffffff\" fill-opacity=\"0.08\"/>\n";

    std::vector<std::string> captions;
    for (const auto& it : items) {
        const float aspect = it.pic.ok ? it.pic.h / it.pic.w : 1.0f;
        const auto b = od::geo::computeBounds(it.view.x, it.view.y, it.view.diameter, it.view.scale, aspect, kViewW,
                                              kViewH);
        od::geo::HitAnim anim;
        auto h = hits.find(it.view.slot);
        if (h != hits.end()) anim = h->second;
        const auto g = od::geo::buildSlot(it.view.slot, it.view.family, b, kViewH, anim);

        for (const auto& p : g.behind) o << primToSvg(p) << "\n";
        o << "<g transform=\"" << xformToSvg(g.piece) << "\">";
        if (it.pic.ok) {
            o << "<svg x=\"" << num(b.x) << "\" y=\"" << num(b.y) << "\" width=\"" << num(b.w) << "\" height=\""
              << num(b.h) << "\" viewBox=\"0 0 " << num(it.pic.w) << " " << num(it.pic.h) << "\">" << it.pic.inner
              << "</svg>";
        } else {
            o << "<ellipse cx=\"" << num(b.cx()) << "\" cy=\"" << num(b.cy()) << "\" rx=\"" << num(b.w / 2)
              << "\" ry=\"" << num(b.h / 2) << "\" fill=\"#4a4f5a\"/>";
        }
        o << "</g>\n";
        for (const auto& p : g.front) o << primToSvg(p) << "\n";

        const bool swapped = sel.count(it.view.slot) > 0;
        if (swapped) {
            o << "<rect x=\"" << num(b.x - 3) << "\" y=\"" << num(b.y - 3) << "\" width=\"" << num(b.w + 6)
              << "\" height=\"" << num(b.h + 6)
              << "\" rx=\"6\" fill=\"none\" stroke=\"#e8a33d\" stroke-width=\"2\"/>\n";
            captions.push_back("<circle cx=\"" + num(b.right() - 1) + "\" cy=\"" + num(b.y + 2) +
                               "\" r=\"4.5\" fill=\"#e8a33d\"/>");
        }
        captions.push_back("<text x=\"" + num(b.cx()) + "\" y=\"" + num(b.bottom() + 15) + "\" fill=\"" +
                           (swapped ? "#e8a33d" : "#bdbdbd") + "\" font-size=\"12\" text-anchor=\"middle\">" +
                           slotName(it.view.slot) + "</text>");
    }
    for (const auto& c : captions) o << c << "\n";
    o << "</svg>\n";

    std::ofstream out(argv[3]);
    out << o.str();
    std::printf("wrote %s\n", argv[3]);
    return out ? 0 : 1;
}
