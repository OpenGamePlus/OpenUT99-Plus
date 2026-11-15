#include "collision.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ut {

namespace {

const uint8_t kNotCsg = 0x1;
const float kEpsilon = 0.03125f;  // keeps the stop a little outside the surface
const int kMaxDepth = 4096;

bool leaf(const Model& m, int32_t n) { return n < 0 || size_t(n) >= m.nodes.size(); }

float dist(const Node& n, Vec3 p) { return n.plane[0] * p.x + n.plane[1] * p.y + n.plane[2] * p.z - n.plane[3]; }

Vec3 lerp(Vec3 a, Vec3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// Earliest entry of a box (half size e) moving a -> b into the convex hull at leaf_hulls[at] (planes, then box),
// each plane pushed out by the box. Entering: *t, *n. Already inside on every plane: *t = 0 and the nearest
// surface's normal if the move goes deeper through it, else *inside. Returns false if the move misses the hull.
bool hull_entry(const Model& m, size_t at, Vec3 a, Vec3 b, Vec3 e, float* t, Vec3* n, bool* inside) {
    float t_in = -1, t_out = 2, nearest = -1e30f;
    Vec3 n_in{0, 0, 0}, n_near{0, 0, 0};
    auto plane = [&](Vec3 pn, float d) {
        float off = std::fabs(pn.x) * e.x + std::fabs(pn.y) * e.y + std::fabs(pn.z) * e.z;
        float d0 = pn.x * a.x + pn.y * a.y + pn.z * a.z - d - off, d1 = pn.x * b.x + pn.y * b.y + pn.z * b.z - d - off;
        if (d0 >= 0 && d1 >= 0) return false;  // wholly in front of this plane: outside the hull
        if (d0 >= 0) {
            float tt = (d0 - kEpsilon) / (d0 - d1);
            if (tt > t_in) t_in = tt, n_in = pn;
        } else {
            if (d1 >= 0) t_out = std::fmin(t_out, d0 / (d0 - d1));
            if (d0 > nearest) nearest = d0, n_near = pn;
        }
        return true;
    };
    size_t i = at;
    for (; m.leaf_hulls[i] != -1; i++) {
        int32_t ref = m.leaf_hulls[i];
        const Node& nd = m.nodes[size_t(ref & 0x3FFFFFFF)];
        float k = (ref & 0x40000000) ? -1.f : 1.f;
        if (!plane({k * nd.plane[0], k * nd.plane[1], k * nd.plane[2]}, k * nd.plane[3])) return false;
    }
    float box[6];
    for (int j = 0; j < 6; j++) std::memcpy(&box[j], &m.leaf_hulls[i + 1 + size_t(j)], 4);
    if (!plane({-1, 0, 0}, -box[0]) || !plane({0, -1, 0}, -box[1]) || !plane({0, 0, -1}, -box[2]) ||
        !plane({1, 0, 0}, box[3]) || !plane({0, 1, 0}, box[4]) || !plane({0, 0, 1}, box[5]))
        return false;
    if (t_in < 0) {  // starts inside
        Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
        if (d.x * n_near.x + d.y * n_near.y + d.z * n_near.z < 0) return *t = 0, *n = n_near, true;
        return *inside = true, false;
    }
    if (t_in > t_out) return false;
    *t = std::fmax(t_in, 0.f), *n = n_in;
    return true;
}

struct Sweep {
    const Model& m;
    Vec3 extent;
    Hit hit;
    Vec3 start, end;  // the whole move, for exact tests against leaf hulls

    // Segment [t1, t2] (positions p1, p2) inside node n; entry is the plane through which this piece started.
    // Returns true once a hit is recorded; near pieces are visited first, so the first hit is the earliest.
    // parent: the node whose child n is (-1 at the root).
    bool check(int32_t n, float t1, float t2, Vec3 p1, Vec3 p2, bool outside, Vec3 entry, int depth, int32_t parent = -1) {
        if (depth > kMaxDepth) throw std::runtime_error("BSP too deep");
        if (leaf(m, n)) {
            if (outside) return false;
            // The expanded planes reach past convex edges; the leaf's hull (its planes and bounding box, which act
            // as bevels) says whether the box really enters this solid.
            // (Models without hulls, such as synthetic test models with zeroed nodes, keep the expanded test.)
            if (parent >= 0 && !m.leaf_hulls.empty() && m.nodes[size_t(parent)].collision_bound >= 0) {
                float t = 1;
                Vec3 nrm{0, 0, 0};
                bool inside = false;
                if (!hull_entry(m, size_t(m.nodes[size_t(parent)].collision_bound), start, end, extent, &t, &nrm, &inside)) {
                    if (inside) hit.start_solid = true;
                    return false;
                }
                hit.hit = true;
                hit.time = t;
                hit.normal = nrm;
                return true;
            }
            if (entry.x == 0 && entry.y == 0 && entry.z == 0) {  // the box starts overlapping this solid: it may leave
                hit.start_solid = true;
                return false;
            }
            hit.hit = true;
            hit.time = t1;
            hit.normal = entry;
            return true;
        }
        const Node& nd = m.nodes[size_t(n)];
        bool back_outside = outside && (nd.flags & kNotCsg);
        float off = std::fabs(nd.plane[0]) * extent.x + std::fabs(nd.plane[1]) * extent.y + std::fabs(nd.plane[2]) * extent.z;
        float d1 = dist(nd, p1), d2 = dist(nd, p2);
        if (d1 >= off && d2 >= off) return check(nd.front, t1, t2, p1, p2, true, entry, depth + 1, n);
        if (d1 < -off && d2 < -off) return check(nd.back, t1, t2, p1, p2, back_outside, entry, depth + 1, n);
        // Crosses the slab around the plane: the near side up to where the box leaves it, then the far side.
        bool front_first = d1 > d2 || (d1 == d2 && d1 >= 0);
        float f1 = 1, f2 = 0;
        if (d1 != d2) {
            float inv = 1 / (d1 - d2);
            f1 = front_first ? (d1 + off + kEpsilon) * inv : (d1 - off - kEpsilon) * inv;
            f2 = front_first ? (d1 - off - kEpsilon) * inv : (d1 + off + kEpsilon) * inv;
        }
        f1 = std::fmin(std::fmax(f1, 0.f), 1.f), f2 = std::fmin(std::fmax(f2, 0.f), 1.f);
        Vec3 n_out{nd.plane[0], nd.plane[1], nd.plane[2]};
        Vec3 n_in{-n_out.x, -n_out.y, -n_out.z};
        int32_t near = front_first ? nd.front : nd.back, far = front_first ? nd.back : nd.front;
        bool near_out = front_first ? true : back_outside, far_out = front_first ? back_outside : true;
        if (check(near, t1, t1 + (t2 - t1) * f1, p1, lerp(p1, p2, f1), near_out, entry, depth + 1, n)) return true;
        // Entering the far side through the plane (the surface faces the near side). Moving parallel inside the slab
        // is not entering: the box already overlapped the far side at its start.
        Vec3 far_entry = d1 == d2 ? entry : front_first ? n_out : n_in;
        return check(far, t1 + (t2 - t1) * f2, t2, lerp(p1, p2, f2), p2, far_out, far_entry, depth + 1, n);
    }
};

}  // namespace

bool ray_cylinder(Vec3 a, Vec3 b, Vec3 c, float r, float h, float* t_hit, Vec3* normal) {
    Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z};
    float ox = a.x - c.x, oy = a.y - c.y;
    float qa = d.x * d.x + d.y * d.y, qb = 2 * (ox * d.x + oy * d.y), qc = ox * ox + oy * oy - r * r;
    float t = -1;
    Vec3 n{0, 0, 0};
    if (qc <= 0) {
        if (std::fabs(a.z - c.z) <= h) return false;  // starts inside
        t = 0;  // above or below: the caps
    } else if (qa > 0) {
        float disc = qb * qb - 4 * qa * qc;
        if (disc < 0) return false;
        t = (-qb - std::sqrt(disc)) / (2 * qa);
        n = {(ox + d.x * t) / r, (oy + d.y * t) / r, 0};
    }
    if (t < 0 || t > *t_hit) return false;
    float z = a.z + d.z * t;
    if (z > c.z + h || z < c.z - h) {  // passes above or below the side: through a cap?
        if (d.z == 0) return false;
        float cap = z > c.z + h ? c.z + h : c.z - h;
        t = (cap - a.z) / d.z;
        if (t < 0 || t > *t_hit) return false;
        float x = a.x + d.x * t - c.x, y = a.y + d.y * t - c.y;
        if (x * x + y * y > r * r) return false;
        n = {0, 0, cap > c.z ? 1.f : -1.f};
    }
    *t_hit = t, *normal = n;
    return true;
}

bool point_solid(const Model& m, Vec3 p) {
    bool outside = true;
    int32_t n = 0;
    for (int g = 0; !leaf(m, n); g++) {
        if (g > kMaxDepth) throw std::runtime_error("BSP too deep");
        const Node& nd = m.nodes[size_t(n)];
        if (dist(nd, p) >= 0) {
            outside = true;
            n = nd.front;
        } else {
            outside = outside && (nd.flags & kNotCsg);
            n = nd.back;
        }
    }
    return m.nodes.empty() ? false : !outside;
}

Hit trace(const Model& m, Vec3 start, Vec3 end, Vec3 extent) {
    Sweep s{m, extent, {}, start, end};
    if (m.nodes.empty()) return s.hit;
    s.check(0, 0, 1, start, end, true, Vec3{0, 0, 0}, 0);
    if (s.hit.hit) {  // pull back so the box rests just outside the surface
        float len = std::sqrt((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y) +
                              (end.z - start.z) * (end.z - start.z));
        if (len > 0) s.hit.time = std::fmax(0.f, s.hit.time - kEpsilon / len);
    }
    return s.hit;
}

Hit trace_placed(const Model& m, Vec3 loc, Vec3 pivot, const float axes[3][3], Vec3 start, Vec3 end, Vec3 extent) {
    auto local = [&](Vec3 p) {
        Vec3 d{p.x - loc.x, p.y - loc.y, p.z - loc.z};
        const float* x = axes[0];
        const float* y = axes[1];
        const float* z = axes[2];
        return Vec3{d.x * x[0] + d.y * x[1] + d.z * x[2] + pivot.x, d.x * y[0] + d.y * y[1] + d.z * y[2] + pivot.y,
                    d.x * z[0] + d.y * z[1] + d.z * z[2] + pivot.z};
    };
    Hit h = trace(m, local(start), local(end), extent);
    Vec3 n = h.normal;
    for (int k = 0; k < 3; k++) (&h.normal.x)[k] = n.x * axes[0][k] + n.y * axes[1][k] + n.z * axes[2][k];
    return h;
}

}  // namespace ut
