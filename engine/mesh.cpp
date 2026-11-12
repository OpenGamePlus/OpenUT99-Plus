#include "mesh.h"

#include <stdexcept>

namespace ut {

Vec3 unpack_vert(uint32_t v) {
    auto sx = [](uint32_t x, int bits) { return float(int32_t(x << (32 - bits)) >> (32 - bits)); };
    return {sx(v & 0x7FF, 11), sx((v >> 11) & 0x7FF, 11), sx(v >> 22, 10)};
}

static Vec3 vec(Reader& r) {
    float x = r.f32(), y = r.f32();
    return {x, y, r.f32()};
}

static size_t count(Reader& r) {
    int32_t n = r.index();
    if (n < 0) throw std::runtime_error("negative array count");
    return size_t(n);
}

static std::string name_at(const Package& p, int32_t i) {
    if (i < 0 || size_t(i) >= p.names.size()) throw std::runtime_error("bad name index in mesh");
    return p.names[size_t(i)];
}

static LodMesh::AnimSeq read_seq(const Package& p, Reader& r) {  // name, group, start, frames, notifys, rate
    LodMesh::AnimSeq s;
    s.name = name_at(p, r.index());
    s.group = name_at(p, r.index());
    s.start = r.i32(), s.frames = r.i32();
    for (size_t n = count(r); n--;) r.f32(), r.index();  // notifys: time, function name
    s.rate = r.f32();
    return s;
}

LodMesh read_lod_mesh(const Package& p, const Export& e) {
    size_t end = 0;
    read_properties(p, e, &end);
    Reader r(p.data, end);
    LodMesh m;
    r.need(41);
    r.pos += 41;  // primitive
    std::vector<uint8_t> verts = lazy_bytes(r, p.version, 4);
    lazy_bytes(r, p.version, 20);  // tris (empty in LodMesh)
    for (size_t n = count(r); n--;) m.seqs.push_back(read_seq(p, r));
    lazy_bytes(r, p.version, 8);  // connects
    r.need(41);
    r.pos += 41;  // bounds
    lazy_bytes(r, p.version, 4);  // vert links
    for (size_t n = count(r); n--;) m.textures.push_back(r.index());
    for (size_t n = count(r); n--;) r.need(25), r.pos += 25;  // bounding boxes
    for (size_t n = count(r); n--;) r.need(16), r.pos += 16;  // bounding spheres
    int32_t frame_verts = r.i32();
    r.i32(), r.u32(), r.u32();  // anim frames, AND / OR flags
    m.scale = vec(r), m.origin = vec(r);
    for (int32_t& a : m.rot_origin) a = r.i32();
    r.i32(), r.i32();  // cur poly, cur vertex
    for (size_t n = count(r); n--;) r.f32();  // texture LOD
    auto u16s = [&]() { for (size_t n = count(r); n--;) r.u16(); };
    u16s(), u16s();  // collapse points, face level
    for (size_t n = count(r); n--;) {
        LodMesh::Face f;
        for (uint16_t& w : f.wedge) w = r.u16();
        f.material = r.u16();
        m.faces.push_back(f);
    }
    u16s();  // collapse wedges
    for (size_t n = count(r); n--;) {
        LodMesh::Wedge w;
        w.vertex = r.u16();
        w.u = r.u8(), w.v = r.u8();
        m.wedges.push_back(w);
    }
    for (size_t n = count(r); n--;) {
        uint32_t flags = r.u32();
        m.materials.push_back({flags, r.i32()});
    }
    for (size_t n = count(r); n--;) r.u16(), r.u16(), r.u16(), r.u16();  // special faces
    r.i32();  // model verts
    m.special_verts = r.i32();
    r.need(28);
    r.pos += 28;  // LOD parameters
    u16s();       // remap anim verts
    if (r.pos != size_t(e.offset) + size_t(e.size)) throw std::runtime_error("lod mesh size mismatch");
    if (frame_verts < 0 || size_t(frame_verts) * 4 > verts.size()) throw std::runtime_error("bad frame size");  // 0: empty mesh (seen in a cache package)
    m.frame_verts = frame_verts;
    m.frames = frame_verts ? int(verts.size() / 4 / size_t(frame_verts)) : 0;
    Reader vr(verts);
    for (size_t i = 0; i < size_t(m.frames) * size_t(frame_verts); i++) m.verts.push_back(unpack_vert(vr.u32()));
    for (const LodMesh::Wedge& w : m.wedges)
        if (size_t(w.vertex) + size_t(m.special_verts) >= size_t(frame_verts)) throw std::runtime_error("wedge vertex out of range");
    for (const LodMesh::Face& f : m.faces)
        for (uint16_t w : f.wedge)
            if (w >= m.wedges.size()) throw std::runtime_error("face wedge out of range");
    return m;
}

}  // namespace ut
