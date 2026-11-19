#include "SceneGeometry.hpp"

#include <cmath>

namespace od::geo {

namespace {

constexpr float kVx = 0.5f, kVy = 0.30f, kBackScale = 0.62f; // vanishing point and size of the back wall (1 = front)

uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int s) { return static_cast<uint32_t>(std::min(255.0f, static_cast<float>((c >> s) & 0xff) * f)); };
    return (c & 0xff000000u) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
uint32_t lerpc(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) { const float x = static_cast<float>((a >> s) & 0xff), y = static_cast<float>((b >> s) & 0xff); return static_cast<uint32_t>(x + (y - x) * t) & 0xff; };
    return 0xff000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// The room is a box seen through one-point perspective. "Frame" coordinates (fx, fy) are 0..1 across the front
// plane of the canvas; t = 0 is the front, t = 1 the back wall. P() projects them to canvas fractions.
struct B {
    std::vector<Prim> p;
    float W, H;
    float X(float f) const { return f * W; }
    float Y(float f) const { return f * H; }

    float sc(float t) const { return 1.0f / (1.0f + (1.0f / kBackScale - 1.0f) * t); }
    void P(float fx, float fy, float t, float& x, float& y) const { const float s = sc(t); x = kVx + (fx - kVx) * s; y = kVy + (fy - kVy) * s; }
    float px(float fx, float t) const { float x, y; P(fx, 0, t, x, y); return x; }
    float py(float fy, float t) const { float x, y; P(0, fy, t, x, y); return y; }

    void rect(float x, float y, float w, float h, uint32_t c, float r = 0.0f) {
        Prim q; q.kind = Prim::Kind::RoundRect; q.x1 = X(x); q.y1 = Y(y); q.x2 = X(w); q.y2 = Y(h); q.width = r * H; q.color = c; p.push_back(q);
    }
    void ell(float cx, float cy, float rx, float ry, uint32_t c) {
        Prim q; q.kind = Prim::Kind::Ellipse; q.x1 = X(cx); q.y1 = Y(cy); q.x2 = X(rx); q.y2 = Y(ry); q.color = c; p.push_back(q);
    }
    void line(float x1, float y1, float x2, float y2, float wpx, uint32_t c) {
        Prim q; q.kind = Prim::Kind::Line; q.x1 = X(x1); q.y1 = Y(y1); q.x2 = X(x2); q.y2 = Y(y2); q.width = wpx; q.color = c; p.push_back(q);
    }
    void poly(std::initializer_list<float> pts, uint32_t c) {
        Prim q; q.kind = Prim::Kind::Poly; q.color = c;
        int i = 0;
        for (float v : pts) { q.pts.push_back((i++ % 2 == 0) ? X(v) : Y(v)); }
        p.push_back(q);
    }
    void quad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, uint32_t c) { poly({x1, y1, x2, y2, x3, y3, x4, y4}, c); }

    // surfaces of the box
    void quadP(float ax, float ay, float at, float bx, float by, float bt, float cx, float cy, float ct, float dx, float dy, float dt, uint32_t c) {
        float x1, y1, x2, y2, x3, y3, x4, y4;
        P(ax, ay, at, x1, y1); P(bx, by, bt, x2, y2); P(cx, cy, ct, x3, y3); P(dx, dy, dt, x4, y4);
        quad(x1, y1, x2, y2, x3, y3, x4, y4, c);
    }
    void lw(float t0, float t1, float y0, float y1, uint32_t c) { quadP(0, y0, t0, 0, y0, t1, 0, y1, t1, 0, y1, t0, c); }   // left wall
    void rw(float t0, float t1, float y0, float y1, uint32_t c) { quadP(1, y0, t0, 1, y0, t1, 1, y1, t1, 1, y1, t0, c); }   // right wall
    void fl(float x0, float x1, float t0, float t1, uint32_t c) { quadP(x0, 1, t0, x1, 1, t0, x1, 1, t1, x0, 1, t1, c); }     // floor
    void ce(float x0, float x1, float t0, float t1, uint32_t c) { quadP(x0, 0, t0, x1, 0, t0, x1, 0, t1, x0, 0, t1, c); }     // ceiling
    // rectangle on the back wall, in the wall's own 0..1 coordinates
    void bk(float x, float y, float w, float h, uint32_t c, float r = 0.0f) {
        const float x0 = px(x, 1), x1 = px(x + w, 1), y0 = py(y, 1), y1 = py(y + h, 1);
        rect(x0, y0, x1 - x0, y1 - y0, c, r * kBackScale);
    }
    void bkEll(float cx, float cy, float rx, float ry, uint32_t c) {
        ell(px(cx, 1), py(cy, 1), rx * kBackScale, ry * kBackScale, c);
    }
    void bkPoly(std::initializer_list<float> pts, uint32_t c) {
        Prim q; q.kind = Prim::Kind::Poly; q.color = c;
        int i = 0; float cur = 0;
        for (float v : pts) { if (i++ % 2 == 0) cur = v; else { q.pts.push_back(X(px(cur, 1))); q.pts.push_back(Y(py(v, 1))); } }
        p.push_back(q);
    }
    // a point on the floor -> canvas
    void floorPt(float fx, float t, float& x, float& y) const { P(fx, 1, t, x, y); }
    void floorEll(float fx, float t, float rx, float rdepth, uint32_t c) {   // flat ellipse lying on the floor
        float x, y; floorPt(fx, t, x, y); const float s = sc(t);
        ell(x, y, rx * s, rdepth * s, c);
    }

    // The room shell. Side walls are darker than the back wall and the floor fades towards the back.
    void room(uint32_t ceiling, uint32_t wall, uint32_t back, uint32_t floorFar, uint32_t floorNear, bool roof = true) {
        if (roof) ce(0, 1, 0, 1, ceiling);
        lw(0, 1, 0, 1, shade(wall, 0.82f));
        rw(0, 1, 0, 1, shade(wall, 0.92f));
        bk(0, 0, 1, 1, back);
        const int bands = 10;
        for (int i = 0; i < bands; ++i) {
            const float t0 = static_cast<float>(i) / bands, t1 = static_cast<float>(i + 1) / bands;
            fl(0, 1, t0, t1 + 0.004f, lerpc(floorNear, floorFar, (t0 + t1) * 0.5f));
        }
        // the cartoon edges where the surfaces meet
        float x, y, x2, y2;
        const uint32_t edge = shade(wall, 0.35f);
        P(0, 0, 1, x, y); P(0, 1, 1, x2, y2); line(x, y, x2, y2, 3, edge);
        P(1, 0, 1, x, y); P(1, 1, 1, x2, y2); line(x, y, x2, y2, 3, edge);
        P(0, 1, 1, x, y); P(1, 1, 1, x2, y2); line(x, y, x2, y2, 3, edge);
        if (roof) { P(0, 0, 1, x, y); P(1, 0, 1, x2, y2); line(x, y, x2, y2, 3, edge); }
    }

    // a box standing on the floor: front face at depth t0, back at t1, height hy in frame units
    void cube(float fx0, float fx1, float t0, float t1, float hy, uint32_t c) {
        const float top = 1.0f - hy;
        if (top > kVy) quadP(fx0, top, t0, fx1, top, t0, fx1, top, t1, fx0, top, t1, shade(c, 1.25f));            // top face (below eye level)
        const bool leftOfCentre = (fx0 + fx1) * 0.5f < kVx;
        if (leftOfCentre) quadP(fx1, top, t0, fx1, top, t1, fx1, 1, t1, fx1, 1, t0, shade(c, 0.7f));            // inner side
        else quadP(fx0, top, t0, fx0, top, t1, fx0, 1, t1, fx0, 1, t0, shade(c, 0.7f));
        quadP(fx0, top, t0, fx1, top, t0, fx1, 1, t0, fx0, 1, t0, c);                                             // front face
    }
    void softShadow(float fx, float t, float rx, float rd) { floorEll(fx, t, rx, rd, 0x38000000); }

    // vertical gradient made of bands (sky)
    void sky(float y0, float y1, uint32_t top, uint32_t bot, int bands = 14) {
        for (int i = 0; i < bands; ++i) {
            const float tt = bands > 1 ? static_cast<float>(i) / (bands - 1) : 0.0f;
            const float a = y0 + (y1 - y0) * i / bands, b = y0 + (y1 - y0) * (i + 1) / bands;
            rect(0, a, 1, b - a + 0.004f, lerpc(top, bot, tt));
        }
    }
    void beam(float x, float y, float tx, float spread, uint32_t c) { poly({x, y, tx - spread, 0.86f, tx + spread, 0.86f}, c); }
    void lamp(float x, float y, float r, uint32_t body, uint32_t glow) {
        ell(x, y, r * 2.6f * H / W, r * 2.6f, (glow & 0x00ffffffu) | 0x30000000u);
        ell(x, y, r * H / W, r, body);
    }
    // soft dark edges so the eye goes to the middle, plus the shadow of the kit on the floor
    void finish() {
        for (int i = 0; i < 6; ++i) {
            const float w = 0.10f - i * 0.016f;
            rect(0, 0, w, 1, 0x14000000); rect(1 - w, 0, w, 1, 0x14000000);
        }
        for (int i = 0; i < 4; ++i) rect(0, 1 - 0.05f + i * 0.0f, 1, 0.05f - i * 0.012f, 0x10000000);
        ell(0.50f, 0.865f, 0.38f, 0.055f, 0x2a000000);
        ell(0.50f, 0.865f, 0.30f, 0.04f, 0x26000000);
    }
};

void garage(B& b) {
    b.room(0xff2c2724, 0xff4a3f3a, 0xff52443d, 0xff6a6d73, 0xff8a8d93);
    for (int r = 0; r < 9; ++r)                                    // bricks on the back wall
        for (int c = 0; c < 12; ++c)
            b.bk((c + (r % 2) * 0.5f) / 12.0f + 0.003f, r / 9.0f + 0.004f, 0.078f, 0.101f, (r + c) % 3 == 0 ? 0xff5e4d45 : 0xff56463f, 0.012f);
    for (int j = 0; j < 8; ++j)                                    // bricks on the side walls (perspective)
        for (int r = 0; r < 8; ++r) {
            const uint32_t c = (j + r) % 3 == 0 ? 0xff5a4a42 : 0xff4f4039;
            b.lw(j / 8.0f + 0.005f, (j + 1) / 8.0f - 0.005f, r / 8.0f + 0.006f, (r + 1) / 8.0f - 0.006f, shade(c, 0.82f));
            b.rw(j / 8.0f + 0.005f, (j + 1) / 8.0f - 0.005f, r / 8.0f + 0.006f, (r + 1) / 8.0f - 0.006f, shade(c, 0.92f));
        }
    b.bk(0.13f, 0.10f, 0.74f, 0.74f, 0xff2a2d33, 0.02f);          // roll-up door
    for (int i = 0; i < 9; ++i) b.bk(0.14f, 0.28f + i * 0.062f, 0.72f, 0.052f, i % 2 ? 0xff8a919c : 0xff9aa1ac, 0.01f);
    b.bk(0.14f, 0.11f, 0.72f, 0.15f, 0xff17191d);                  // open gap, dark outside
    b.bkEll(0.30f, 0.18f, 0.016f, 0.03f, 0xfff2d98a);
    for (int i = 0; i < 3; ++i) b.rw(0.38f, 0.92f, 0.30f + i * 0.20f, 0.322f + i * 0.20f, 0xff6b5030);   // shelf on the right wall
    b.rw(0.46f, 0.52f, 0.18f, 0.30f, 0xffb4472f); b.rw(0.60f, 0.68f, 0.22f, 0.30f, 0xff2f6a8f); b.rw(0.76f, 0.88f, 0.20f, 0.30f, 0xff9c7a48);
    b.rw(0.44f, 0.58f, 0.40f, 0.50f, 0xff8a6a3c); b.rw(0.66f, 0.84f, 0.44f, 0.50f, 0xff6a7280);
    b.fl(0, 1, 0.25f, 0.262f, 0x30000000); b.fl(0, 1, 0.50f, 0.512f, 0x30000000); b.fl(0, 1, 0.75f, 0.762f, 0x30000000);   // slab joints
    b.floorEll(0.28f, 0.45f, 0.13f, 0.03f, 0x40000000);            // oil stain
    b.softShadow(0.07f, 0.18f, 0.08f, 0.02f);
    b.cube(0.03f, 0.10f, 0.14f, 0.22f, 0.10f, 0xff3b6b3a);          // paint bucket
    float x, y; b.P(0.5f, 0, 0.5f, x, y);
    b.line(x, y, x, y + 0.14f, 3, 0xff1c1c1c);                       // hanging bulb and its pool of light
    b.beam(x, y + 0.15f, 0.5f, 0.20f, 0x18ffd060);
    b.lamp(x, y + 0.155f, 0.015f, 0xfffff0b0, 0xffffd060);
    b.finish();
}

void studioSmall(B& b) {
    b.room(0xff1c1e24, 0xff2b2e36, 0xff2b2e36, 0xff5a3d24, 0xff7a5532);
    const int cols = 8, rows = 6;                                  // foam wedges on the back wall
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const float cx = (c + 0.5f) / cols, cy = (r + 0.5f) / rows, dx = 0.5f / cols, dy = 0.5f / rows;
            const uint32_t d = (r + c) % 2 ? 0xff3b3f4a : 0xff30333c;
            b.bkPoly({cx, cy - dy, cx + dx, cy, cx, cy + dy, cx - dx, cy}, d);
        }
    for (int j = 0; j < 6; ++j)                                    // foam on the side walls
        for (int r = 0; r < 6; ++r) {
            const uint32_t d = (r + j) % 2 ? 0xff3b3f4a : 0xff30333c;
            const float t0 = j / 6.0f, t1 = (j + 1) / 6.0f, y0 = r / 6.0f, y1 = (r + 1) / 6.0f, tm = (t0 + t1) * 0.5f, ym = (y0 + y1) * 0.5f;
            b.quadP(0, y0, tm, 0, ym, t1, 0, y1, tm, 0, ym, t0, shade(d, 0.85f));
            b.quadP(1, y0, tm, 1, ym, t1, 1, y1, tm, 1, ym, t0, shade(d, 0.95f));
        }
    b.bk(0.34f, 0.18f, 0.32f, 0.40f, 0xff111317, 0.02f);          // window to the control room
    b.bk(0.36f, 0.21f, 0.28f, 0.34f, 0xff2a4a63, 0.015f);
    b.bkPoly({0.36f, 0.55f, 0.52f, 0.21f, 0.60f, 0.21f, 0.44f, 0.55f}, 0x30ffffff);
    b.bkEll(0.10f, 0.14f, 0.025f, 0.04f, 0xffd94a3a);              // red "recording" light
    for (int i = 0; i < 10; ++i) b.fl(i / 10.0f, i / 10.0f + 0.004f, 0, 1, 0xff573b22);    // plank seams along the depth
    b.fl(0.20f, 0.80f, 0.18f, 0.62f, 0xff7a2f3a); b.fl(0.23f, 0.77f, 0.22f, 0.58f, 0xff93414b);   // rug
    b.softShadow(0.92f, 0.45f, 0.06f, 0.02f);
    b.cube(0.86f, 0.98f, 0.38f, 0.52f, 0.30f, 0xff22252b);         // bass trap / amp in the corner
    b.finish();
}

void studioMedium(B& b) {
    b.room(0xff241c17, 0xff3a2f28, 0xff3a2f28, 0xff6a4a2a, 0xff8a6238);
    for (int i = 0; i < 16; ++i) b.bk(i / 16.0f, 0, 0.0625f, 1, i % 2 ? 0xff42362d : 0xff382d26);     // wood slats
    for (int j = 0; j < 14; ++j) {
        const uint32_t c = j % 2 ? 0xff42362d : 0xff382d26;
        b.lw(j / 14.0f, (j + 1) / 14.0f, 0, 1, shade(c, 0.82f)); b.rw(j / 14.0f, (j + 1) / 14.0f, 0, 1, shade(c, 0.92f));
    }
    b.bk(0.08f, 0.12f, 0.84f, 0.50f, 0xff111317, 0.02f);          // big control-room window
    b.bk(0.10f, 0.16f, 0.80f, 0.42f, 0xff2d5472, 0.015f);
    b.bk(0.20f, 0.40f, 0.60f, 0.14f, 0xff1b1d22, 0.02f);           // desk seen through the glass
    for (int i = 0; i < 12; ++i) b.bk(0.23f + i * 0.045f, 0.42f, 0.02f, 0.08f, i % 3 == 0 ? 0xffe8a33d : 0xff5c616c);
    b.bkPoly({0.10f, 0.58f, 0.42f, 0.16f, 0.54f, 0.16f, 0.22f, 0.58f}, 0x26ffffff);
    b.bk(0.02f, 0.18f, 0.05f, 0.10f, 0xffd94a3a, 0.01f);          // REC lamp
    for (int r = 0; r < 8; ++r)                                    // parquet in perspective
        for (int c = 0; c < 8; ++c) {
            const float t0 = r / 8.0f, t1 = (r + 1) / 8.0f, x0 = c / 8.0f + (r % 2) * 0.0625f, x1 = x0 + 0.115f;
            b.fl(std::max(0.0f, x0), std::min(1.0f, x1), t0 + 0.004f, t1 - 0.004f, (r + c) % 2 ? 0xff86603a : 0xff6f4d2b);
        }
    b.softShadow(0.90f, 0.40f, 0.07f, 0.02f);
    b.cube(0.82f, 0.97f, 0.34f, 0.50f, 0.32f, 0xff22252b);         // guitar amp
    float x0, y0, x1, y1;
    b.P(0.84f, 0.72f, 0.34f, x0, y0); b.P(0.95f, 0.88f, 0.34f, x1, y1);
    b.rect(x0, y0, x1 - x0, y1 - y0, 0xff6a7280, 0.006f);
    b.ell((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (x1 - x0) * 0.32f, (y1 - y0) * 0.34f, 0xff14161a);
    b.finish();
}

void studioLarge(B& b) {
    b.room(0xff1c1917, 0xff3d352f, 0xff2f2926, 0xff6a4829, 0xff8a5f36);
    for (int i = 0; i < 6; ++i) b.ce(0, 1, i / 6.0f, i / 6.0f + 0.03f, 0xff14110f);        // beams across the ceiling
    for (int i = 0; i < 4; ++i) {                                   // tall windows on both side walls
        const float t0 = 0.12f + i * 0.22f, t1 = t0 + 0.12f;
        b.lw(t0, t1, 0.18f, 0.72f, 0xff111317); b.lw(t0 + 0.012f, t1 - 0.012f, 0.22f, 0.68f, 0xff5a8fb3);
        b.rw(t0, t1, 0.18f, 0.72f, 0xff111317); b.rw(t0 + 0.012f, t1 - 0.012f, 0.22f, 0.68f, 0xff5a8fb3);
    }
    for (int i = 0; i < 4; ++i) b.bk(0.08f + i * 0.24f, 0.28f, 0.14f, 0.50f, 0xff111317, 0.08f), b.bk(0.095f + i * 0.24f, 0.31f, 0.11f, 0.44f, 0xff4a7fa3, 0.06f);
    for (int i = 0; i < 5; ++i) {                                   // hanging baffles
        const float fx = 0.12f + i * 0.19f, tt = 0.55f;
        b.quadP(fx, 0.0f, tt, fx + 0.09f, 0.0f, tt, fx + 0.09f, 0.26f, tt, fx, 0.26f, tt, i % 2 ? 0xff6a5a49 : 0xff5a4c3d);
    }
    b.fl(0.10f, 0.90f, 0.12f, 0.66f, 0xff7a2f3a); b.fl(0.12f, 0.88f, 0.16f, 0.62f, 0xff9a4854);   // rug
    for (int i = 0; i < 12; ++i) b.fl(i / 12.0f, i / 12.0f + 0.003f, 0, 1, 0xff6a4829);
    b.softShadow(0.86f, 0.40f, 0.12f, 0.025f);
    b.cube(0.74f, 0.98f, 0.30f, 0.56f, 0.30f, 0xff14161a);         // grand piano body
    b.cube(0.76f, 0.80f, 0.34f, 0.38f, 0.10f, 0xff14161a); b.cube(0.92f, 0.96f, 0.34f, 0.38f, 0.10f, 0xff14161a);
    b.finish();
}

void clubSmall(B& b) {
    b.room(0xff1d1417, 0xff3b2b2e, 0xff3b2b2e, 0xff2a2023, 0xff3b2c30);
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 12; ++c)
            b.bk((c + (r % 2) * 0.5f) / 12.0f + 0.003f, r / 9.0f + 0.004f, 0.078f, 0.101f, (r * 3 + c) % 4 == 0 ? 0xff52383b : 0xff473237, 0.012f);
    for (int j = 0; j < 8; ++j)
        for (int r = 0; r < 8; ++r) {
            const uint32_t c = (j * 3 + r) % 4 == 0 ? 0xff52383b : 0xff473237;
            b.lw(j / 8.0f + 0.005f, (j + 1) / 8.0f - 0.005f, r / 8.0f + 0.006f, (r + 1) / 8.0f - 0.006f, shade(c, 0.82f));
            b.rw(j / 8.0f + 0.005f, (j + 1) / 8.0f - 0.005f, r / 8.0f + 0.006f, (r + 1) / 8.0f - 0.006f, shade(c, 0.92f));
        }
    b.bk(0.26f, 0.18f, 0.48f, 0.26f, 0xff17191d, 0.05f);          // neon sign (shapes, no lettering)
    b.bk(0.30f, 0.30f, 0.14f, 0.045f, 0xffff4d8d, 0.02f); b.bk(0.46f, 0.30f, 0.08f, 0.045f, 0xff4dd2ff, 0.02f); b.bk(0.56f, 0.30f, 0.14f, 0.045f, 0xffffd24d, 0.02f);
    b.bkEll(0.5f, 0.32f, 0.38f, 0.30f, 0x18ff4d8d);
    for (int i = 0; i < 14; ++i) {                                   // string lights following the ceiling
        const float t = i / 13.0f;
        float x, y; b.P(i % 2 ? 0.05f : 0.95f, 0.10f + 0.0f, t * 0.9f, x, y);
        b.lamp(x, y, 0.006f, i % 3 == 0 ? 0xffffd24d : i % 3 == 1 ? 0xffff7aa8 : 0xff7ad8ff, 0xffffffff);
    }
    b.fl(0, 1, 0.0f, 0.06f, 0xff6b4a2c);                           // lip of the stage
    for (int i = 0; i < 2; ++i) {                                   // bar stools
        const float fx = i ? 0.92f : 0.08f;
        float x, y, x2, y2; b.P(fx, 1, 0.30f, x, y); b.P(fx, 0.80f, 0.30f, x2, y2);
        b.softShadow(fx, 0.30f, 0.04f, 0.015f);
        b.line(x, y, x2, y2, 5, 0xff7b818d); b.ell(x2, y2, 0.032f * b.sc(0.30f), 0.016f, 0xffa23a3a);
    }
    b.cube(0.86f, 0.98f, 0.40f, 0.52f, 0.26f, 0xff17191d);
    b.finish();
}

void stageLights(B& b, int n, float t, float fy) {
    float x0, y0, x1, y1;
    b.P(0.04f, fy, t, x0, y0); b.P(0.96f, fy, t, x1, y1);
    b.rect(x0, y0 - 0.008f, x1 - x0, 0.016f, 0xff7b818d, 0.006f);   // truss
    const uint32_t cols[] = {0x30ff4d8d, 0x304dd2ff, 0x30ffd24d, 0x3070ff8a};
    for (int i = 0; i < n; ++i) {
        const float fx = 0.10f + 0.80f * i / std::max(1, n - 1);
        float x, y; b.P(fx, fy, t, x, y);
        const float tx = x + (i % 2 ? -0.10f : 0.10f);
        b.beam(x, y + 0.02f, tx, 0.07f * b.sc(t) + 0.02f, cols[i % 4]);
        b.rect(x - 0.012f, y + 0.003f, 0.024f, 0.035f, 0xff22252b, 0.006f);
    }
}

void speakerStack(B& b, bool left, float t0, int n) {
    for (int i = 0; i < n; ++i) {
        const float fx0 = left ? 0.02f : 0.90f, fx1 = fx0 + 0.08f, h = 0.10f, base = 1.0f - i * h;
        const float topY = base - h;
        // front face of one cabinet, with two cones
        float xa, ya, xb, yb; b.P(fx0, topY, t0, xa, ya); b.P(fx1, base, t0, xb, yb);
        float tx, ty; b.P(left ? fx1 : fx0, topY, t0, tx, ty); float tx2, ty2; b.P(left ? fx1 : fx0, topY, t0 + 0.12f, tx2, ty2);
        float bx, by; b.P(left ? fx1 : fx0, base, t0, bx, by); float bx2, by2; b.P(left ? fx1 : fx0, base, t0 + 0.12f, bx2, by2);
        b.quad(tx, ty, tx2, ty2, bx2, by2, bx, by, 0xff121418);                                              // side of the cabinet
        b.rect(xa, ya, xb - xa, yb - ya, 0xff1e2026, 0.006f);
        b.ell((xa + xb) * 0.5f, ya + (yb - ya) * 0.34f, (xb - xa) * 0.26f, (yb - ya) * 0.22f, 0xff3d4048);
        b.ell((xa + xb) * 0.5f, ya + (yb - ya) * 0.75f, (xb - xa) * 0.14f, (yb - ya) * 0.12f, 0xff3d4048);
    }
}

void crowd(B& b, int rows, float t0) {
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < 22; ++c) {
            const float t = t0 - r * 0.07f; // rows nearer the camera are larger and lower
            const float fx = (c + (r % 2) * 0.5f) / 22.0f + 0.01f * std::sin(c * 3.1f + r);
            float x, y; b.P(fx, 1.0f, std::max(0.0f, t), x, y);
            const float s = b.sc(std::max(0.0f, t));
            const uint32_t col = 0xff101215 + static_cast<uint32_t>((r * 6) << 8);
            b.ell(x, y - 0.045f * s, 0.016f * s * b.H / b.W * 1.5f * 1.4f, 0.03f * s * 1.4f, col);
            b.ell(x, y - 0.005f * s, 0.024f * s * b.H / b.W * 1.5f * 1.4f, 0.035f * s * 1.4f, col);
        }
}

void clubMedium(B& b) {
    b.room(0xff120e18, 0xff2e2238, 0xff231a2c, 0xff1f1826, 0xff2d2336);
    for (int j = 0; j < 8; ++j) {                                   // curtains hanging on the side walls
        for (int k = 0; k < 2; ++k) {
            const float t0 = j / 8.0f, t1 = (j + 1) / 8.0f;
            const uint32_t c = (j % 2) ? 0xff7a2230 : 0xff5e1824;
            b.lw(t0, t1, 0, 0.92f, shade(c, 0.8f)); b.rw(t0, t1, 0, 0.92f, shade(c, 0.9f));
        }
    }
    b.bk(0.10f, 0.14f, 0.80f, 0.50f, 0xff17141c, 0.02f);          // backdrop banner with an abstract star
    b.bkPoly({0.50f, 0.20f, 0.54f, 0.34f, 0.66f, 0.34f, 0.56f, 0.43f, 0.60f, 0.57f, 0.50f, 0.49f, 0.40f, 0.57f, 0.44f, 0.43f, 0.34f, 0.34f, 0.46f, 0.34f}, 0xffe8a33d);
    stageLights(b, 4, 0.55f, 0.10f);
    stageLights(b, 3, 0.20f, 0.10f);
    b.softShadow(0.08f, 0.30f, 0.07f, 0.02f); b.softShadow(0.92f, 0.30f, 0.07f, 0.02f);
    speakerStack(b, true, 0.28f, 3);
    speakerStack(b, false, 0.28f, 3);
    b.fl(0, 1, 0.0f, 0.04f, 0xff8a5a2c);                           // stage edge
    crowd(b, 1, 0.0f);
    b.finish();
}

void clubLarge(B& b) {
    b.room(0xff0a0a12, 0xff191528, 0xff0f0f1a, 0xff140f1f, 0xff221a30);
    b.bk(0.08f, 0.10f, 0.84f, 0.58f, 0xff0a0b10, 0.02f);          // LED screen
    for (int i = 0; i < 14; ++i) {
        const float h = 0.08f + 0.40f * std::fabs(std::sin(i * 0.7f));
        b.bk(0.10f + i * 0.058f, 0.64f - h, 0.045f, h, i % 3 == 0 ? 0xffff4d8d : i % 3 == 1 ? 0xff4dd2ff : 0xffffd24d, 0.004f);
    }
    stageLights(b, 7, 0.75f, 0.06f);
    stageLights(b, 6, 0.40f, 0.06f);
    stageLights(b, 5, 0.12f, 0.06f);
    speakerStack(b, true, 0.22f, 4);
    speakerStack(b, false, 0.22f, 4);
    b.fl(0, 1, 0.0f, 0.04f, 0xff3d4048);
    b.ell(0.5f, 0.80f, 0.45f, 0.04f, 0x20ffffff);                  // haze
    crowd(b, 3, 0.10f);
    b.finish();
}

void stadium(B& b) {
    b.sky(0, 0.62f, 0xff0a1030, 0xff3b2a5a, 12);
    for (int i = 0; i < 40; ++i) b.ell(std::fmod(i * 0.173f, 1.0f), 0.02f + std::fmod(i * 0.091f, 0.28f), 0.002f, 0.003f, 0xffffffff);
    // the stands: a back wall of tiers plus two side stands that rise towards the camera
    for (int tier = 0; tier < 6; ++tier) {
        const float y0 = 0.30f + tier * 0.065f, y1 = y0 + 0.062f;
        b.bk(0, (y0 - 0.114f) / 0.62f, 1, (y1 - y0) / 0.62f + 0.004f, tier % 2 ? 0xff1a1d2a : 0xff23273a);
        const uint32_t cols[] = {0xffe8a33d, 0xffff4d8d, 0xff4dd2ff, 0xffffffff, 0xff7a8296};
        for (int c = 0; c < 40; ++c) b.ell(b.px((c + 0.5f) / 40.0f, 1), y0 + 0.03f, 0.0035f, 0.008f, cols[(c * 7 + tier * 3) % 5]);
    }
    for (int tier = 0; tier < 7; ++tier) {
        const float f0 = 0.10f + tier * 0.11f, f1 = f0 + 0.10f;       // each tier a step higher up the wall
        const uint32_t c = tier % 2 ? 0xff1a1d2a : 0xff23273a;
        b.quadP(0, f0 + 0.35f, 0.0f, 0, f0 + 0.35f, 1.0f, 0, f1 + 0.35f, 1.0f, 0, f1 + 0.35f, 0.0f, shade(c, 0.8f));
        b.quadP(1, f0 + 0.35f, 0.0f, 1, f0 + 0.35f, 1.0f, 1, f1 + 0.35f, 1.0f, 1, f1 + 0.35f, 0.0f, shade(c, 0.9f));
    }
    for (int j = 0; j < 8; ++j)                                    // fans along the side stands
        for (int tier = 0; tier < 4; ++tier) {
            const uint32_t cols[] = {0xffe8a33d, 0xffff4d8d, 0xff4dd2ff, 0xffffffff, 0xff7a8296};
            const float t = (j + 0.5f) / 8.0f;
            float x, y; b.P(0, 0.45f + tier * 0.10f, t, x, y);
            b.ell(x + 0.012f, y, 0.004f * b.sc(t), 0.009f * b.sc(t), cols[(j * 7 + tier * 3) % 5]);
            b.P(1, 0.45f + tier * 0.10f, t, x, y);
            b.ell(x - 0.012f, y, 0.004f * b.sc(t), 0.009f * b.sc(t), cols[(j * 5 + tier * 2) % 5]);
        }
    for (int tw = 0; tw < 2; ++tw) {                               // floodlight towers
        const float fx = tw ? 0.97f : 0.03f;
        float x, y, x2, y2; b.P(fx, 1.0f, 0.55f, x, y); b.P(fx, 0.0f, 0.55f, x2, y2);
        b.line(x, y, x2, y2 + 0.04f, 6, 0xff5c616c);
        b.rect(x2 - 0.045f, y2 - 0.01f, 0.09f, 0.07f, 0xff22252b, 0.008f);
        for (int i = 0; i < 6; ++i) b.ell(x2 - 0.032f + (i % 3) * 0.032f, y2 + 0.012f + (i / 3) * 0.032f, 0.010f, 0.014f, 0xfffff6c0);
        b.beam(x2, y2 + 0.06f, tw ? 0.40f : 0.60f, 0.16f, 0x12fff6c0);
    }
    b.bk(0.30f, 0.0f, 0.40f, 0.30f, 0xff0a0b10, 0.02f);           // giant screen
    b.bk(0.32f, 0.03f, 0.36f, 0.24f, 0xff3a2a7a, 0.015f);
    b.bkEll(0.50f, 0.15f, 0.07f, 0.09f, 0xffe8a33d);
    for (int i = 0; i < 3; ++i) {                                  // fireworks
        const float cx = 0.25f + i * 0.25f, cy = 0.16f + (i % 2) * 0.05f;
        for (int k = 0; k < 12; ++k) {
            const float a = k * 6.2832f / 12.0f;
            b.line(cx + std::cos(a) * 0.012f, cy + std::sin(a) * 0.02f, cx + std::cos(a) * 0.045f, cy + std::sin(a) * 0.075f, 3, i == 1 ? 0xffff4d8d : 0xffffd24d);
        }
    }
    for (int i = 0; i < 12; ++i) {                                 // mown grass stripes converging to the horizon
        const uint32_t c = i % 2 ? 0xff2d6a3a : 0xff347a43;
        const float x0 = i / 12.0f, x1 = (i + 1) / 12.0f;
        b.quadP(x0, 1, 0.0f, x1, 1, 0.0f, x1, 1, 1.0f, x0, 1, 1.0f, c);
    }
    for (int j = 1; j < 6; ++j) b.fl(0, 1, j / 6.0f, j / 6.0f + 0.01f, 0x22ffffff);
    float x, y, x2, y2; b.P(0.0f, 1, 1.0f, x, y); b.P(1.0f, 1, 1.0f, x2, y2); b.line(x, y, x2, y2, 3, 0xff1c4a26);
    crowd(b, 1, 0.0f);
    b.finish();
}

} // namespace

std::vector<Prim> buildScene(int room, float w, float h, float* floorY) {
    B b;
    b.W = w; b.H = h;
    switch (room) {
    case 0: garage(b); break;
    case 1: studioSmall(b); break;
    case 2: studioMedium(b); break;
    case 3: studioLarge(b); break;
    case 4: clubSmall(b); break;
    case 5: clubMedium(b); break;
    case 6: clubLarge(b); break;
    default: stadium(b); break;
    }
    if (floorY) *floorY = 0.80f * h;
    return std::move(b.p);
}

} // namespace od::geo
