#include "SceneGeometry.hpp"

#include <cmath>

namespace od::geo {

namespace {

struct B {
    std::vector<Prim> p;
    float W, H;
    float X(float f) const { return f * W; }
    float Y(float f) const { return f * H; }

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
    // vertical gradient made of bands
    void sky(float y0, float y1, uint32_t top, uint32_t bot, int bands = 14) {
        auto ch = [](uint32_t c, int s) { return static_cast<float>((c >> s) & 0xff); };
        for (int i = 0; i < bands; ++i) {
            const float t = bands > 1 ? static_cast<float>(i) / (bands - 1) : 0.0f;
            auto mix = [&](int s) { return static_cast<uint32_t>(ch(top, s) + (ch(bot, s) - ch(top, s)) * t) & 0xff; };
            const uint32_t c = 0xff000000u | (mix(16) << 16) | (mix(8) << 8) | mix(0);
            const float a = y0 + (y1 - y0) * i / bands, b = y0 + (y1 - y0) * (i + 1) / bands;
            rect(0, a, 1, b - a + 0.004f, c);
        }
    }
    void cone(float x, float y, float tx, float spread, uint32_t c) { poly({x, y, tx - spread, 0.80f, tx + spread, 0.80f}, c); }
    void lamp(float x, float y, float r, uint32_t body, uint32_t glow) {
        ell(x, y, r * 2.6f * H / W, r * 2.6f, glow & 0x30ffffffu | 0x30000000u);
        ell(x, y, r * H / W, r, body);
    }
};

constexpr uint32_t kOutline = 0xff14161a;

void garage(B& b) {
    b.rect(0, 0, 1, 0.80f, 0xff4a3f3a);                       // brick wall base
    for (int r = 0; r < 14; ++r)                                 // brick rows
        for (int c = 0; c < 24; ++c) {
            const float x = (c + (r % 2 ? 0.5f : 0.0f)) / 24.0f, y = r * 0.057f;
            b.rect(x + 0.002f, y + 0.003f, 0.038f, 0.050f, (r + c) % 3 == 0 ? 0xff5a4a42 : 0xff52443d, 0.008f);
        }
    b.rect(0.18f, 0.08f, 0.64f, 0.56f, 0xff2a2d33, 0.01f);     // roll-up door frame
    for (int i = 0; i < 9; ++i)
        b.rect(0.19f, 0.20f + i * 0.047f, 0.62f, 0.040f, i % 2 ? 0xff8a919c : 0xff9aa1ac, 0.006f);
    b.rect(0.19f, 0.09f, 0.62f, 0.10f, 0xff17191d);              // open gap: dark outside
    b.ell(0.30f, 0.14f, 0.012f, 0.02f, 0xfff2d98a);
    b.rect(0.80f, 0.20f, 0.14f, 0.38f, 0xff3d4048, 0.01f);      // shelf
    for (int i = 0; i < 3; ++i) b.rect(0.81f, 0.27f + i * 0.12f, 0.12f, 0.012f, 0xff6b5030);
    b.rect(0.82f, 0.18f, 0.04f, 0.09f, 0xffb4472f, 0.006f);
    b.rect(0.88f, 0.21f, 0.04f, 0.06f, 0xff2f6a8f, 0.006f);
    b.rect(0.83f, 0.30f, 0.07f, 0.07f, 0xff9c7a48, 0.006f);
    b.rect(0.82f, 0.42f, 0.10f, 0.07f, 0xff8a6a3c, 0.006f);
    b.line(0.50f, 0.0f, 0.50f, 0.07f, 3, 0xff1c1c1c);           // hanging bulb
    b.lamp(0.50f, 0.085f, 0.014f, 0xfffff0b0, 0xffffd060);
    b.rect(0, 0.80f, 1, 0.20f, 0xff6a6d73);                      // concrete floor
    b.rect(0, 0.80f, 1, 0.012f, 0xff54575d);
    b.ell(0.30f, 0.93f, 0.12f, 0.025f, 0x40000000);              // oil stain
    b.rect(0.06f, 0.52f, 0.07f, 0.11f, 0xff3b6b3a, 0.012f);     // paint bucket
    b.rect(0.06f, 0.50f, 0.07f, 0.02f, 0xff2a4f29, 0.01f);
}

void studioSmall(B& b) {
    b.rect(0, 0, 1, 0.80f, 0xff2b2e36);
    const int cols = 12, rows = 7;                               // foam wedges
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const float cx = (c + 0.5f) / cols, cy = (r + 0.5f) / rows * 0.78f;
            const uint32_t d = (r + c) % 2 ? 0xff3b3f4a : 0xff30333c;
            b.poly({cx, cy - 0.052f, cx + 0.04f, cy, cx, cy + 0.052f, cx - 0.04f, cy}, d);
        }
    b.rect(0.36f, 0.12f, 0.28f, 0.32f, 0xff111317, 0.01f);      // small window to the control room
    b.rect(0.37f, 0.14f, 0.26f, 0.28f, 0xff2a4a63, 0.008f);
    b.poly({0.37f, 0.42f, 0.55f, 0.14f, 0.63f, 0.14f, 0.45f, 0.42f}, 0x30ffffff);
    b.ell(0.12f, 0.12f, 0.012f, 0.018f, 0xffd94a3a);            // red light
    b.rect(0, 0.80f, 1, 0.20f, 0xff6b4a2c);                      // wood floor
    for (int i = 0; i < 12; ++i) b.line(i / 12.0f, 0.80f, i / 12.0f - 0.02f, 1.0f, 2, 0xff573b22);
    b.rect(0.18f, 0.84f, 0.64f, 0.13f, 0xff7a2f3a, 0.02f);      // rug
    b.rect(0.20f, 0.86f, 0.60f, 0.09f, 0xff93414b, 0.02f);
    b.line(0.09f, 0.50f, 0.09f, 0.93f, 5, 0xff7b818d);           // mic stand + pop filter
    b.ell(0.09f, 0.40f, 0.025f, 0.06f, 0xff111317);
    b.ell(0.09f, 0.40f, 0.019f, 0.047f, 0xff3d4048);
}

void studioMedium(B& b) {
    b.rect(0, 0, 1, 0.80f, 0xff3a2f28);
    for (int i = 0; i < 20; ++i) b.rect(i / 20.0f, 0, 0.040f, 0.80f, i % 2 ? 0xff42362d : 0xff382d26);  // wood slats
    b.rect(0.10f, 0.10f, 0.80f, 0.36f, 0xff111317, 0.01f);     // big control-room window
    b.rect(0.11f, 0.12f, 0.78f, 0.32f, 0xff2d5472, 0.008f);
    b.rect(0.20f, 0.30f, 0.60f, 0.10f, 0xff1b1d22, 0.01f);       // desk silhouette seen through glass
    for (int i = 0; i < 12; ++i) b.rect(0.23f + i * 0.045f, 0.31f, 0.02f, 0.06f, i % 3 == 0 ? 0xffe8a33d : 0xff5c616c);
    b.poly({0.11f, 0.44f, 0.40f, 0.12f, 0.50f, 0.12f, 0.21f, 0.44f}, 0x26ffffff);
    b.rect(0.05f, 0.18f, 0.03f, 0.06f, 0xffd94a3a, 0.006f);     // REC lamp
    b.ell(0.065f, 0.21f, 0.008f, 0.014f, 0xffff8a7a);
    b.rect(0, 0.80f, 1, 0.20f, 0xff7a5430);                      // parquet
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 16; ++c)
            b.rect(c / 16.0f + (r % 2) * 0.03f, 0.80f + r * 0.05f, 0.058f, 0.046f, (r + c) % 2 ? 0xff86603a : 0xff6f4d2b, 0.004f);
    b.rect(0.80f, 0.55f, 0.13f, 0.26f, 0xff22252b, 0.01f);      // guitar amp
    b.rect(0.82f, 0.60f, 0.09f, 0.18f, 0xff6a7280, 0.006f);
    b.ell(0.865f, 0.69f, 0.03f, 0.06f, 0xff14161a);
}

void studioLarge(B& b) {
    b.sky(0, 0.80f, 0xff2a2623, 0xff3d352f);
    for (int i = 0; i < 6; ++i) {                                // ceiling beams
        b.rect(i / 6.0f, 0, 0.012f, 0.80f, 0xff1c1917);
    }
    b.rect(0, 0, 1, 0.07f, 0xff1c1917);
    for (int i = 0; i < 5; ++i)                                   // hanging baffles
        b.rect(0.12f + i * 0.19f, 0.07f, 0.09f, 0.22f, i % 2 ? 0xff6a5a49 : 0xff5a4c3d, 0.01f);
    for (int i = 0; i < 4; ++i)                                   // tall windows
        { b.rect(0.08f + i * 0.24f, 0.36f, 0.10f, 0.40f, 0xff111317, 0.05f); b.rect(0.09f + i * 0.24f, 0.38f, 0.08f, 0.36f, 0xff4a7fa3, 0.04f); }
    b.rect(0, 0.80f, 1, 0.20f, 0xff825a34);
    for (int i = 0; i < 20; ++i) b.line(i / 20.0f, 0.80f, i / 20.0f - 0.05f, 1.0f, 2, 0xff6a4829);
    b.rect(0.74f, 0.58f, 0.22f, 0.08f, 0xff14161a, 0.02f);      // grand piano
    b.poly({0.76f, 0.58f, 0.95f, 0.58f, 0.99f, 0.64f, 0.76f, 0.64f}, 0xff14161a);
    b.line(0.78f, 0.66f, 0.78f, 0.80f, 5, 0xff14161a);
    b.line(0.93f, 0.66f, 0.93f, 0.80f, 5, 0xff14161a);
    b.rect(0.12f, 0.86f, 0.76f, 0.11f, 0xff7a2f3a, 0.02f);       // large rug
    b.rect(0.14f, 0.88f, 0.72f, 0.07f, 0xff9a4854, 0.02f);
}

void clubSmall(B& b) {
    b.rect(0, 0, 1, 0.80f, 0xff3b2b2e);
    for (int r = 0; r < 12; ++r)
        for (int c = 0; c < 20; ++c)
            b.rect((c + (r % 2) * 0.5f) / 20.0f + 0.002f, r * 0.066f + 0.004f, 0.046f, 0.058f, (r * 3 + c) % 4 == 0 ? 0xff52383b : 0xff473237, 0.008f);
    b.rect(0.30f, 0.14f, 0.40f, 0.16f, 0xff17191d, 0.03f);      // neon sign (shapes, no lettering)
    b.rect(0.32f, 0.17f, 0.36f, 0.10f, 0x00000000, 0.02f);
    b.line(0.34f, 0.22f, 0.44f, 0.22f, 7, 0xffff4d8d); b.line(0.47f, 0.22f, 0.53f, 0.22f, 7, 0xff4dd2ff); b.line(0.56f, 0.22f, 0.66f, 0.22f, 7, 0xffffd24d);
    b.ell(0.5f, 0.22f, 0.26f, 0.13f, 0x18ff4d8d);
    for (int i = 0; i < 14; ++i) {                                // string lights
        const float x = i / 13.0f, y = 0.05f + 0.03f * std::sin(i * 0.9f);
        b.lamp(x, y, 0.005f, i % 3 == 0 ? 0xffffd24d : i % 3 == 1 ? 0xffff7aa8 : 0xff7ad8ff, 0xffffffff);
    }
    b.rect(0, 0.80f, 1, 0.20f, 0xff2a2023);                      // low riser
    b.rect(0, 0.80f, 1, 0.025f, 0xff6b4a2c);
    for (int i = 0; i < 2; ++i) {                                 // bar stools
        const float x = 0.08f + i * 0.84f;
        b.ell(x, 0.60f, 0.035f, 0.02f, 0xffa23a3a);
        b.line(x, 0.62f, x, 0.80f, 5, 0xff7b818d);
    }
    b.rect(0.86f, 0.42f, 0.10f, 0.12f, 0xff17191d, 0.01f);
    for (int i = 0; i < 3; ++i) b.rect(0.87f, 0.44f + i * 0.035f, 0.08f, 0.022f, i == 1 ? 0xffe8a33d : 0xff5c616c, 0.004f);
}

void stageLights(B& b, int n, bool truss, float y) {
    if (truss) b.rect(0.05f, y, 0.90f, 0.02f, 0xff7b818d, 0.006f);
    const uint32_t cols[] = {0x30ff4d8d, 0x304dd2ff, 0x30ffd24d, 0x3070ff8a};
    for (int i = 0; i < n; ++i) {
        const float x = 0.10f + 0.80f * i / std::max(1, n - 1);
        const float tx = x + (i % 2 ? -0.10f : 0.10f);
        b.cone(x, y + 0.02f, tx, 0.07f, cols[i % 4]);
        b.rect(x - 0.012f, y + 0.005f, 0.024f, 0.035f, 0xff22252b, 0.006f);
    }
}

void speakers(B& b, float x, int n) {
    for (int i = 0; i < n; ++i) {
        const float y = 0.80f - (i + 1) * 0.14f;
        b.rect(x, y, 0.09f, 0.135f, 0xff1b1d22, 0.008f);
        b.ell(x + 0.045f, y + 0.045f, 0.022f * b.H / b.W * 1.6f, 0.032f, 0xff3d4048);
        b.ell(x + 0.045f, y + 0.10f, 0.012f * b.H / b.W * 1.6f, 0.018f, 0xff3d4048);
    }
}

void crowd(B& b, int rows, float base) {
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < 26; ++c) {
            const float x = (c + (r % 2) * 0.5f) / 26.0f + 0.012f * std::sin(c * 3.1f + r);
            const float y = base + r * 0.045f;
            const uint32_t col = 0xff101215 + static_cast<uint32_t>((r * 6) << 8);
            b.ell(x, y, 0.016f * b.H / b.W * 1.5f, 0.032f, col);
            b.ell(x, y + 0.05f, 0.022f * b.H / b.W * 1.5f, 0.035f, col);
        }
}

void clubMedium(B& b) {
    b.sky(0, 0.80f, 0xff1c1626, 0xff2e2238);
    b.rect(0.00f, 0, 0.12f, 0.80f, 0xff6e1f2c);                  // side curtains
    b.rect(0.88f, 0, 0.12f, 0.80f, 0xff6e1f2c);
    for (int i = 0; i < 6; ++i) { b.line(0.02f + i * 0.02f, 0, 0.02f + i * 0.02f, 0.80f, 3, 0xff55161f); b.line(0.90f + i * 0.02f, 0, 0.90f + i * 0.02f, 0.80f, 3, 0xff55161f); }
    stageLights(b, 4, true, 0.08f);
    b.rect(0.20f, 0.30f, 0.60f, 0.30f, 0xff17141c, 0.01f);       // backdrop banner with abstract star
    b.poly({0.50f, 0.34f, 0.53f, 0.43f, 0.62f, 0.43f, 0.55f, 0.49f, 0.58f, 0.57f, 0.50f, 0.52f, 0.42f, 0.57f, 0.45f, 0.49f, 0.38f, 0.43f, 0.47f, 0.43f}, 0xffe8a33d);
    speakers(b, 0.13f, 2);
    speakers(b, 0.78f, 2);
    b.rect(0, 0.80f, 1, 0.20f, 0xff241d2b);
    b.rect(0, 0.80f, 1, 0.02f, 0xff8a5a2c);
    crowd(b, 1, 0.97f);
}

void clubLarge(B& b) {
    b.sky(0, 0.80f, 0xff0f0f1a, 0xff231a33);
    b.rect(0.12f, 0.12f, 0.76f, 0.46f, 0xff0a0b10, 0.012f);      // LED screen
    for (int i = 0; i < 14; ++i) {
        const float h = 0.08f + 0.28f * std::fabs(std::sin(i * 0.7f));
        b.rect(0.14f + i * 0.052f, 0.56f - h, 0.040f, h, i % 3 == 0 ? 0xffff4d8d : i % 3 == 1 ? 0xff4dd2ff : 0xffffd24d, 0.004f);
    }
    stageLights(b, 7, true, 0.04f);
    speakers(b, 0.02f, 4);
    speakers(b, 0.89f, 4);
    b.rect(0, 0.80f, 1, 0.20f, 0xff1c1626);
    b.rect(0, 0.80f, 1, 0.025f, 0xff3d4048);
    b.ell(0.5f, 0.82f, 0.45f, 0.03f, 0x20ffffff);                 // haze
    crowd(b, 3, 0.90f);
}

void stadium(B& b) {
    b.sky(0, 0.55f, 0xff0a1030, 0xff3b2a5a, 12);
    for (int i = 0; i < 40; ++i) b.ell(std::fmod(i * 0.173f, 1.0f), 0.02f + std::fmod(i * 0.091f, 0.30f), 0.002f, 0.003f, 0xffffffff);
    b.poly({0, 0.55f, 0.18f, 0.34f, 0.82f, 0.34f, 1, 0.55f, 1, 0.80f, 0, 0.80f}, 0xff1a1d2a);   // stands
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 60; ++c) {
            const float y = 0.40f + r * 0.065f;
            const float x = (c + 0.5f) / 60.0f;
            if (y < 0.34f + (0.5f - std::fabs(x - 0.5f)) * 0.0f) continue;
            const uint32_t cols[] = {0xffe8a33d, 0xffff4d8d, 0xff4dd2ff, 0xffffffff, 0xff7a8296};
            b.ell(x, y, 0.004f, 0.009f, cols[(c * 7 + r * 3) % 5]);
        }
    for (int t = 0; t < 2; ++t) {                                 // floodlight towers
        const float x = t ? 0.91f : 0.09f;
        b.line(x, 0.12f, x, 0.62f, 6, 0xff5c616c);
        b.rect(x - 0.045f, 0.06f, 0.09f, 0.07f, 0xff22252b, 0.008f);
        for (int i = 0; i < 6; ++i) b.ell(x - 0.032f + (i % 3) * 0.032f, 0.083f + (i / 3) * 0.032f, 0.010f, 0.014f, 0xfffff6c0);
        b.poly({x, 0.12f, t ? 0.45f : 0.55f, 0.80f, t ? 0.70f : 0.30f, 0.80f}, 0x14fff6c0);
    }
    b.rect(0.32f, 0.10f, 0.36f, 0.16f, 0xff0a0b10, 0.012f);      // giant screen
    b.rect(0.33f, 0.115f, 0.34f, 0.13f, 0xff3a2a7a, 0.008f);
    b.ell(0.50f, 0.18f, 0.06f * b.H / b.W * 2.2f, 0.045f, 0xffe8a33d);
    for (int i = 0; i < 3; ++i) {                                 // fireworks
        const float cx = 0.25f + i * 0.25f, cy = 0.20f + (i % 2) * 0.06f;
        for (int k = 0; k < 12; ++k) {
            const float a = k * 6.2832f / 12.0f;
            b.line(cx + std::cos(a) * 0.012f, cy + std::sin(a) * 0.02f, cx + std::cos(a) * 0.045f, cy + std::sin(a) * 0.075f, 3, i == 1 ? 0xffff4d8d : 0xffffd24d);
        }
    }
    b.rect(0, 0.80f, 1, 0.20f, 0xff2d6a3a);                       // grass
    for (int i = 0; i < 8; ++i) b.rect(i / 8.0f, 0.80f, 0.0625f, 0.20f, i % 2 ? 0xff2d6a3a : 0xff347a43);
    b.rect(0, 0.80f, 1, 0.012f, 0xff1c4a26);
    crowd(b, 1, 0.97f);
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
