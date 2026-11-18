#include "KitGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace od::geo {

namespace {

using Kind = Prim::Kind;

constexpr uint32_t kPole = 0xff7b818d;
constexpr uint32_t kPoleHi = 0xffc3c8d2;
constexpr uint32_t kChrome = 0xffd5d9e0;
constexpr uint32_t kRubber = 0xff3d4048;
constexpr uint32_t kFelt = 0xfff1ebde;
constexpr uint32_t kFeltShade = 0xffd2c9b4;
constexpr uint32_t kPlate = 0xff2b2e35;
constexpr uint32_t kPlateTop = 0xff6a7180;
constexpr uint32_t kSpring = 0xff8a909c;

Prim line(float x1, float y1, float x2, float y2, float w, uint32_t c) {
    Prim p;
    p.kind = Kind::Line;
    p.x1 = x1; p.y1 = y1; p.x2 = x2; p.y2 = y2; p.width = w; p.color = c;
    return p;
}
Prim ellipse(float cx, float cy, float rx, float ry, uint32_t c) {
    Prim p;
    p.kind = Kind::Ellipse;
    p.x1 = cx; p.y1 = cy; p.x2 = rx; p.y2 = ry; p.color = c;
    return p;
}
Prim rrect(float x, float y, float w, float h, float r, uint32_t c) {
    Prim p;
    p.kind = Kind::RoundRect;
    p.x1 = x; p.y1 = y; p.x2 = w; p.y2 = h; p.width = r; p.color = c;
    return p;
}
Prim poly(std::initializer_list<float> pts, uint32_t c) {
    Prim p;
    p.kind = Kind::Poly;
    p.pts.assign(pts);
    p.color = c;
    return p;
}

float decay(float t, float k) { return std::exp(-k * t); }
float lerp(float a, float b, float k) { return a + (b - a) * k; }

// Lowest y where a stand's hub may sit so that the rubber feet still fit inside the view.
float groundLimit(float y, float viewH) { return std::min(y, viewH * 0.90f); }

// A pole from `topY` to a hub at `hubY`, with a tripod base and rubber feet.
void tripod(std::vector<Prim>& out, float cx, float topY, float hubY, float spread, float poleW, float viewH) {
    const float drop = 0.028f * viewH;
    const float maxY = viewH - 6.0f;
    out.push_back(line(cx, topY, cx, hubY, poleW, kPole));
    out.push_back(line(cx - poleW * 0.22f, topY, cx - poleW * 0.22f, hubY, poleW * 0.28f, kPoleHi));
    const float ex[3] = {cx - spread, cx + spread, cx + spread * 0.12f};
    const float ey[3] = {std::min(hubY + drop, maxY), std::min(hubY + drop, maxY),
                         std::min(hubY + drop * 2.4f, maxY)};
    for (int i = 0; i < 3; ++i) out.push_back(line(cx, hubY, ex[i], ey[i], poleW * 0.85f, kPole));
    out.push_back(ellipse(cx, hubY, poleW * 1.15f, poleW * 0.8f, kChrome));
    for (int i = 0; i < 3; ++i) out.push_back(ellipse(ex[i], ey[i], poleW * 1.5f, poleW * 0.8f, kRubber));
}

// A foot pedal seen from the front-top: a sloping plate. `press` 0..1 moves the toe edge down.
void footPlate(std::vector<Prim>& out, float cx, float farY, float nearY, float farHalf, float nearHalf,
               float press, float travel) {
    const float far = farY + press * travel;
    const float th = std::max(2.0f, (nearY - far) * 0.12f); // plate thickness
    out.push_back(poly({cx - farHalf, far + th, cx + farHalf, far + th, cx + nearHalf, nearY + th,
                        cx - nearHalf, nearY + th},
                       kPlate));
    out.push_back(poly({cx - farHalf, far, cx + farHalf, far, cx + nearHalf, nearY, cx - nearHalf, nearY},
                       kPlateTop));
}

uint32_t glowColor(float glow) {
    const float a = std::clamp(glow * 0.40f, 0.0f, 1.0f);
    return (static_cast<uint32_t>(a * 255.0f) << 24) | 0x00ffffffu;
}

} // namespace

bool Xform::isIdentity() const {
    return std::fabs(dx) < 1e-3f && std::fabs(dy) < 1e-3f && std::fabs(sx - 1) < 1e-4f &&
           std::fabs(sy - 1) < 1e-4f && std::fabs(rot) < 1e-4f;
}

float ampForVelocity(int velocity) {
    const float v = std::clamp(static_cast<float>(velocity) / 127.0f, 0.0f, 1.0f);
    return std::clamp(std::pow(v, 0.8f), 0.25f, 1.0f);
}

Rect computeBounds(float x, float y, float diameterInches, float scale, float aspect, float viewW, float viewH) {
    const float pxPerInch = viewW / kViewWidthInches;
    Rect r;
    r.w = diameterInches * pxPerInch * scale;
    r.h = r.w * (aspect > 0.0f ? aspect : 1.0f);
    r.x = x * viewW - r.w * 0.5f;
    r.y = y * viewH - r.h * 0.5f;
    return r;
}

float animDuration(const std::string& family) {
    if (family == "cymbal") return 2.0f;
    if (family == "hihat") return 1.8f;
    if (family == "kick") return 0.8f;
    return 0.9f;
}

SlotGeometry buildSlot(const std::string& slot, const std::string& family, const Rect& b, float viewH,
                       const HitAnim& a) {
    SlotGeometry g;
    const float t = std::max(0.0f, a.t);
    const float amp = std::clamp(a.amp, 0.0f, 1.0f);
    const float cx = b.cx(), cy = b.cy();
    const float poleW = std::clamp(b.w * 0.036f, 3.5f, 8.0f);

    // ------------------------------------------------------------------ drums
    auto drumAnim = [&] {
        const float s = amp * decay(t, 10.0f) * std::cos(t * 42.0f);
        g.piece = {0.0f, 0.0f, 1.0f + 0.03f * s, 1.0f - 0.065f * s, 0.0f, cx, b.bottom()};
        g.glow = amp * decay(t, 8.0f);
    };

    if (family == "kick") {
        const float s = amp * decay(t, 11.0f) * std::cos(t * 46.0f);
        g.piece = {0.0f, 0.0f, 1.0f + 0.02f * s, 1.0f + 0.02f * s, 0.0f, cx, cy};
        g.glow = amp * decay(t, 9.0f);

        const float u = b.w;
        const float ground = groundLimit(b.bottom() + 0.012f * viewH, viewH);

        // spurs (the two angled legs under the shell) with rubber feet
        for (int side = -1; side <= 1; side += 2) {
            const float sx0 = cx + side * 0.36f * u, sy0 = b.y + 0.80f * u;
            const float sx1 = cx + side * 0.50f * u, sy1 = ground;
            g.behind.push_back(line(sx0, sy0, sx1, sy1, poleW * 1.3f, kPole));
            g.behind.push_back(ellipse(sx1, sy1, poleW * 1.7f, poleW * 0.9f, kRubber));
        }

        // pedal: plate + hinge + beater that swings from "cocked" to the head
        g.beater = decay(t, 12.0f) * (0.6f + 0.4f * amp);
        g.press = decay(t, 9.0f);
        const float farY = ground - 0.15f * u;
        const float nearY = ground + 0.005f * u;
        footPlate(g.front, cx, farY, nearY, 0.055f * u, 0.085f * u, g.press, 0.022f * u);

        const float hingeY = farY + g.press * 0.022f * u - 0.005f * u;
        const float restY = b.bottom() - 0.215f * u;   // ball cocked back, near the plate
        const float hitY = b.y + 0.64f * u;            // the dot painted on the head
        const float ballY = lerp(restY, hitY, g.beater);
        const float ballR = 0.05f * u * lerp(1.15f, 0.92f, g.beater); // closer to the viewer when cocked
        g.front.push_back(line(cx, hingeY, cx, ballY, 0.022f * u, kChrome));
        g.front.push_back(ellipse(cx, hingeY, 0.022f * u, 0.014f * u, kChrome));
        g.front.push_back(ellipse(cx, ballY, ballR, ballR, kFeltShade));
        g.front.push_back(ellipse(cx - ballR * 0.12f, ballY - ballR * 0.12f, ballR * 0.8f, ballR * 0.8f, kFelt));

        g.front.push_back(ellipse(cx, cy, 0.43f * u, 0.43f * u, glowColor(g.glow)));
    }
    // ------------------------------------------------------------------ snare
    else if (family == "snare") {
        drumAnim();
        const float ground = groundLimit(b.bottom() + 0.09f * viewH, viewH);
        const float spread = std::clamp(0.30f * (ground - b.bottom()), 26.0f, 90.0f);
        tripod(g.behind, cx, b.bottom() - 0.04f * b.h, ground, spread, poleW, viewH);
        // basket arms cradling the shell
        const float armY = b.bottom() + 0.30f * (ground - b.bottom());
        g.behind.push_back(line(cx, armY, b.x + 0.22f * b.w, b.bottom() - 0.14f * b.h, poleW * 0.8f, kPole));
        g.behind.push_back(line(cx, armY, b.right() - 0.22f * b.w, b.bottom() - 0.14f * b.h, poleW * 0.8f, kPole));
        g.behind.push_back(ellipse(cx, armY, poleW * 1.2f, poleW * 0.9f, kChrome));
        g.front.push_back(ellipse(cx, b.y + 0.17f * b.w, 0.43f * b.w, 0.13f * b.w, glowColor(g.glow)));
    }
    // ------------------------------------------------------------------ toms
    else if (family == "tom" && slot == "floor") {
        drumAnim();
        const float ground = groundLimit(b.bottom() + 0.075f * viewH, viewH);
        // three legs: the middle one is mostly hidden behind the shell
        g.behind.push_back(line(cx, b.bottom() - 0.25f * b.h, cx + 0.02f * b.w, ground - 0.02f * viewH,
                                poleW * 1.2f, kPole));
        g.behind.push_back(ellipse(cx + 0.02f * b.w, ground - 0.02f * viewH, poleW * 1.6f, poleW * 0.85f, kRubber));
        for (int side = -1; side <= 1; side += 2) {
            const float x0 = cx + side * 0.40f * b.w, y0 = b.bottom() - 0.30f * b.h;
            const float x1 = cx + side * 0.46f * b.w, y1 = ground;
            g.behind.push_back(line(x0, y0, x1, y1, poleW * 1.3f, kPole));
            g.behind.push_back(ellipse(x1, y1, poleW * 1.7f, poleW * 0.9f, kRubber));
        }
        g.front.push_back(ellipse(cx, b.y + 0.17f * b.w, 0.43f * b.w, 0.13f * b.w, glowColor(g.glow)));
    } else if (family == "tom") {
        drumAnim();
        const float ground = groundLimit(b.bottom() + 0.14f * viewH, viewH);
        const float spread = std::clamp(0.30f * (ground - b.bottom()), 24.0f, 90.0f);
        tripod(g.behind, cx, b.bottom() - 0.05f * b.h, ground, spread, poleW, viewH);
        g.behind.push_back(rrect(cx - 0.06f * b.w, b.bottom() - 0.14f * b.h, 0.12f * b.w, 0.14f * b.h + 0.004f * viewH,
                                 2.0f, kChrome)); // tom holder clamp (hidden behind the shell)
        g.front.push_back(ellipse(cx, b.y + 0.17f * b.w, 0.43f * b.w, 0.13f * b.w, glowColor(g.glow)));
    }
    // ------------------------------------------------------------------ hi-hat
    else if (family == "hihat") {
        const bool open = a.role == "open";
        const bool pedal = a.role == "pedal";
        g.press = (pedal ? 1.0f : (open ? 0.0f : 0.55f)) * decay(t, 8.0f) * (0.5f + 0.5f * amp);
        if (open) {
            const float w = amp * decay(t, 3.5f) * std::sin(t * 26.0f);
            g.piece = {0.0f, 0.5f * amp * decay(t, 12.0f), 1.0f, 1.0f + 0.08f * w, 0.07f * w, cx, cy};
        } else {
            const float dip = 4.5f * amp * decay(t, 13.0f); // top cymbal clamps down onto the bottom one
            g.piece = {0.0f, dip, 1.0f, 1.0f, 0.025f * amp * decay(t, 10.0f) * std::sin(t * 50.0f), cx, cy};
        }
        g.glow = 0.0f; // no glow on cymbals: the tilt/dip is the feedback (a flat glow would not follow it)

        const float ground = groundLimit(cy + 0.30f * viewH, viewH);
        const float spread = std::clamp(0.30f * (ground - cy), 22.0f, 90.0f);
        tripod(g.behind, cx, cy, ground, spread, poleW, viewH);

        const float u = std::max(b.w, 90.0f) * 0.9f;
        const float px = cx + 0.10f * u;
        const float farY = ground + 0.012f * viewH;
        const float nearY = std::min(ground + 0.075f * viewH, viewH - 8.0f);
        g.behind.push_back(line(cx, ground, px, farY + g.press * 0.030f * viewH, poleW * 0.5f, kSpring));
        footPlate(g.front, px, farY, nearY, 0.09f * u, 0.14f * u, g.press, 0.030f * viewH);
    }
    // ------------------------------------------------------------------ cymbals
    else if (family == "cymbal") {
        const float w = amp * decay(t, 2.6f);
        g.piece = {0.0f, 3.0f * amp * decay(t, 14.0f), 1.0f, 1.0f + 0.12f * w * std::sin(t * 20.0f + 1.1f),
                   0.14f * w * std::sin(t * 20.0f), cx, cy};
        g.glow = 0.0f;
        const float ground = groundLimit(cy + 0.42f * viewH, viewH);
        const float spread = std::clamp(0.28f * (ground - cy), 24.0f, 110.0f);
        tripod(g.behind, cx, cy, ground, spread, poleW, viewH);
    }
    // ------------------------------------------------------------------ anything else
    else {
        drumAnim();
        g.front.push_back(ellipse(cx, cy, 0.45f * b.w, 0.45f * b.h, glowColor(g.glow)));
    }
    return g;
}

} // namespace od::geo
