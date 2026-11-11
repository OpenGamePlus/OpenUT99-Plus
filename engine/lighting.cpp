#include "lighting.h"

#include <algorithm>
#include <cmath>

namespace ut {

static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

Vec3 luxel_position(const Model& m, const Surf& s, const LightMap& lm, int i, int j) {
    const Vec3 &tu = m.vectors[size_t(s.texture_u)], &tv = m.vectors[size_t(s.texture_v)], &n = m.vectors[size_t(s.normal)];
    const Vec3& base = m.points[size_t(s.base)];
    float u = lm.pan.x + float(i) * lm.uscale, v = lm.pan.y + float(j) * lm.vscale;
    // Solve x · tu = u, x · tv = v, x · n = 0 (Cramer, via the inverse's columns).
    Vec3 c0 = cross(tv, n), c1 = cross(n, tu);
    float det = dot(tu, c0);
    if (std::fabs(det) < 1e-12f) return base;
    return {base.x + (u * c0.x + v * c1.x) / det, base.y + (u * c0.y + v * c1.y) / det, base.z + (u * c0.z + v * c1.z) / det};
}

int lightmap_lights(const Model& m, const LightMap& lm) {
    int n = 0;
    if (lm.light_actors < 0) return 0;  // -1: no lights (retail)
    for (size_t i = size_t(lm.light_actors); i < m.lights.size() && m.lights[i] != 0; i++) n++;
    return n;
}

bool shadow_bit(const Model& m, const LightMap& lm, int light, int i, int j) {
    size_t row = size_t(lm.uclamp + 7) / 8;
    size_t at = size_t(lm.data_offset) + size_t(light) * row * size_t(lm.vclamp) + size_t(j) * row + size_t(i) / 8;
    return at < m.light_bits.size() && (m.light_bits[at] >> (i % 8)) & 1;
}

Vec3 light_colour(int hue, int saturation, int brightness) {
    float h = float(hue) / 256.f * 6.f, s = 1.f - float(saturation) / 255.f, v = float(brightness) / 255.f;
    int sector = int(h) % 6;
    float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (sector) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

float light_falloff(float d, int radius) {
    float r = 25.f * float(radius + 1), x = d / r;
    return x >= 1.f ? 0.f : 1.f - x * x;
}

bool Atlas::place(int w, int h, int* x, int* y) {
    if (w < 1 || h < 1 || w > width_) return false;
    if (shelf_x_ + w > width_) shelf_y_ += shelf_h_, shelf_x_ = 0, shelf_h_ = 0;
    *x = shelf_x_, *y = shelf_y_;
    shelf_x_ += w;
    shelf_h_ = std::max(shelf_h_, h);
    return true;
}

}  // namespace ut
