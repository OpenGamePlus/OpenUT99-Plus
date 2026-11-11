#include "level.h"

#include <cstring>
#include <stdexcept>

namespace ut {

static Vec3 vec(Reader& r) {
    float x = r.f32(), y = r.f32();
    return {x, y, r.f32()};
}

static uint64_t u64(Reader& r) {
    uint64_t lo = r.u32();
    return lo | uint64_t(r.u32()) << 32;
}

static size_t count(Reader& r) {
    int32_t n = r.index();
    if (n < 0) throw std::runtime_error("negative array count");
    return size_t(n);
}

static void read_model_into(Reader& r, Model& m) {
    r.need(25 + 16);
    r.pos += 25 + 16;  // bounding box, bounding sphere
    for (size_t i = count(r); i--;) m.vectors.push_back(vec(r));
    for (size_t i = count(r); i--;) m.points.push_back(vec(r));
    for (size_t i = count(r); i--;) {
        Node n;
        for (float& f : n.plane) f = r.f32();
        n.zone_mask = u64(r);
        n.flags = r.u8();
        n.vert_pool = r.index(), n.surf = r.index(), n.back = r.index(), n.front = r.index();
        n.plane_index = r.index(), n.collision_bound = r.index(), n.render_bound = r.index();
        n.zone[0] = r.u8(), n.zone[1] = r.u8(), n.num_vertices = r.u8();
        n.leaf[0] = r.i32(), n.leaf[1] = r.i32();
        m.nodes.push_back(n);
    }
    for (size_t i = count(r); i--;) {
        Surf s;
        s.texture = r.index();
        s.flags = r.u32();
        s.base = r.index(), s.normal = r.index(), s.texture_u = r.index(), s.texture_v = r.index();
        s.light_map = r.index(), s.brush_poly = r.index();
        s.pan_u = int16_t(r.u16()), s.pan_v = int16_t(r.u16());
        s.actor = r.index();
        m.surfs.push_back(s);
    }
    for (size_t i = count(r); i--;) {
        int32_t point = r.index();
        m.verts.push_back({point, r.index()});
    }
    r.i32();  // shared sides
    for (int32_t i = r.i32(); i-- > 0;) {
        m.zone_actors.push_back(r.index());
        r.need(16);
        r.pos += 16;  // zone actor, connectivity, visibility
    }
    m.polys = r.index();
    for (size_t i = count(r); i--;) {
        LightMap l;
        l.data_offset = r.i32();
        l.pan = vec(r);
        l.uclamp = r.index(), l.vclamp = r.index();
        l.uscale = r.f32(), l.vscale = r.f32();
        l.light_actors = r.i32();
        m.lightmaps.push_back(l);
    }
    size_t bits = count(r);
    r.need(bits);
    m.light_bits.assign(r.data.begin() + long(r.pos), r.data.begin() + long(r.pos + bits));
    r.pos += bits;
    for (size_t i = count(r); i--;) {
        r.need(25);
        r.pos += 25;  // bounds
    }
    for (size_t i = count(r); i--;) r.i32();  // leaf hulls
    for (size_t i = count(r); i--;) {
        r.index(), r.index(), r.index();
        u64(r);  // leaves
    }
    for (size_t i = count(r); i--;) m.lights.push_back(r.index());
    r.u32(), r.u32();  // root outside, linked
}

Model read_model(const Package& p, const Export& e) {
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    Model m;
    read_model_into(r, m);
    if (r.pos != size_t(e.offset) + size_t(e.size)) throw std::runtime_error("model size mismatch");
    for (const Surf& s : m.surfs)
        if (s.base < 0 || size_t(s.base) >= m.points.size() || s.texture_u < 0 || size_t(s.texture_u) >= m.vectors.size() ||
            s.texture_v < 0 || size_t(s.texture_v) >= m.vectors.size())
            throw std::runtime_error("surface index out of range");
    for (const Node& n : m.nodes)
        if (n.surf < 0 || size_t(n.surf) >= m.surfs.size() || n.vert_pool < 0 ||
            size_t(n.vert_pool) + n.num_vertices > m.verts.size())
            throw std::runtime_error("node index out of range");
    // Level models carry vert entries past the points array; no node references them (checked on all retail maps).
    for (const Node& n : m.nodes)
        for (int i = 0; i < n.num_vertices; i++) {
            int32_t pt = m.verts[size_t(n.vert_pool + i)].point;
            if (pt < 0 || size_t(pt) >= m.points.size()) throw std::runtime_error("node vertex out of range");
        }
    return m;
}

size_t model_end(const Package& p, const Export& e) {
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    Model m;
    read_model_into(r, m);
    return r.pos;
}

Level read_level(const Package& p, const Export& e) {
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    Level lv;
    uint32_t num = r.u32();
    r.u32();  // max
    for (uint32_t i = 0; i < num; i++) lv.actors.push_back(r.index());
    for (int i = 0; i < 4; i++) r.string();  // URL protocol, host, map, portal
    for (size_t i = count(r); i--;) r.string();  // URL options
    r.i32(), r.i32();                            // port, valid
    lv.model = r.index();
    return lv;  // reach specs, text blocks, travel info: not needed by the engine yet
}

static float dot_sub(const Vec3& p, const Vec3& base, const Vec3& axis) {
    return (p.x - base.x) * axis.x + (p.y - base.y) * axis.y + (p.z - base.z) * axis.z;
}

std::vector<Vertex> node_triangles(const Model& m, const Node& n, int usize, int vsize) {
    const Surf& s = m.surfs[size_t(n.surf)];
    const Vec3 &base = m.points[size_t(s.base)], &tu = m.vectors[size_t(s.texture_u)], &tv = m.vectors[size_t(s.texture_v)];
    auto vertex = [&](int i) {
        const Vec3& p = m.points[size_t(m.verts[size_t(n.vert_pool + i)].point)];
        return Vertex{p.x, p.y, p.z, (dot_sub(p, base, tu) + s.pan_u) / float(usize), (dot_sub(p, base, tv) + s.pan_v) / float(vsize), 0, 0};
    };
    std::vector<Vertex> out;
    for (int i = 2; i < n.num_vertices; i++) {
        out.push_back(vertex(0));
        out.push_back(vertex(i - 1));
        out.push_back(vertex(i));
    }
    return out;
}

}  // namespace ut
