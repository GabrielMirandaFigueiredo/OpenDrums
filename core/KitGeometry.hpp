// OpenDrums core - geometry of the kit view: stands, feet, kick pedal and hit animations.
//
// This module has no GUI dependency. It turns "a piece sits in this rectangle and was hit
// t seconds ago" into
//   * `behind`: drawing primitives for the hardware that goes behind the piece's picture
//               (stands, legs, spurs, rubber feet)
//   * `front` : primitives drawn over the picture (kick pedal + beater, hi-hat pedal, head glow)
//   * `piece` : a transform to apply to the piece's picture (squash, wobble, dip)
// The JUCE plugin draws these with juce::Graphics, the offline preview tool writes them as
// SVG, so what you see in the preview is what the plugin draws.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace od::geo {

// How many inches of kit fit across the kit view (a 22" bass drum is ~22% of the width).
constexpr float kViewWidthInches = 100.0f;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

struct Prim {
    enum class Kind { Line, Ellipse, RoundRect, Poly };
    Kind kind = Kind::Line;
    // Line: (x1,y1)-(x2,y2), `width` = thickness, round caps
    // Ellipse: centre (x1,y1), radii (x2,y2)
    // RoundRect: corner (x1,y1), size (x2,y2), `width` = corner radius
    // Poly: `pts` = x0,y0,x1,y1,...
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float width = 1.0f;
    uint32_t color = 0xffffffff; // ARGB
    std::vector<float> pts;
};

// Scale (sx, sy) then rotate (radians, clockwise) both about (px, py), then translate by (dx, dy).
struct Xform {
    float dx = 0, dy = 0, sx = 1, sy = 1, rot = 0, px = 0, py = 0;
    bool isIdentity() const;
};

struct HitAnim {
    float t = 1.0e9f;      // seconds since the hit (huge = at rest)
    float amp = 0.0f;      // 0..1, from the velocity
    std::string role;      // articulation: "hit", "closed", "pedal", "open", "rimshot", ...
};

struct SlotGeometry {
    std::vector<Prim> behind;
    std::vector<Prim> front;
    Xform piece;
    float glow = 0.0f;   // 0..1 (already drawn as a primitive in `front`; exposed for tests)
    float beater = 0.0f; // kick: 0 = cocked back, 1 = touching the head
    float press = 0.0f;  // kick / hi-hat pedal: 0 = up, 1 = fully pressed
};

// Amplitude (0..1) for a MIDI velocity.
float ampForVelocity(int velocity);

// Rectangle of a piece. `aspect` = picture height / width; `diameter` in inches.
Rect computeBounds(float x, float y, float diameterInches, float scale, float aspect, float viewW, float viewH);

// After this many seconds a hit animation has settled and the view can stop its timer.
float animDuration(const std::string& family);

// `slot`: kick, snare, tom1, floor, hihat, crash1, ride, ...   `family`: kick, snare, tom, hihat, cymbal, perc.
SlotGeometry buildSlot(const std::string& slot, const std::string& family, const Rect& bounds, float viewH,
                       const HitAnim& anim);

} // namespace od::geo
