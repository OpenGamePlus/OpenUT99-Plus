// Level geometry: Model (BSP) and Level. Spec: docs/formats/level.md; reference: tools/umodel.py
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "upkg.h"

namespace ut {

struct Node {
    float plane[4];
    uint64_t zone_mask;
    uint8_t flags;
    int32_t vert_pool, surf, back, front, plane_index, collision_bound, render_bound;
    uint8_t zone[2], num_vertices;
    int32_t leaf[2];
};

struct Surf {
    int32_t texture;  // object ref
    uint32_t flags;   // poly flags
    int32_t base, normal, texture_u, texture_v, light_map, brush_poly;  // base: points; others: vectors; light_map -1 = none
    int16_t pan_u, pan_v;
    int32_t actor;
};

struct Vert { int32_t point, side; };

struct LightMap {
    int32_t data_offset;
    Vec3 pan;
    int32_t uclamp, vclamp;
    float uscale, vscale;
    int32_t light_actors;  // first index into Model::lights; list ends at a 0 entry
};

struct Model {
    std::vector<Vec3> vectors, points;
    std::vector<Node> nodes;
    std::vector<Surf> surfs;
    std::vector<Vert> verts;
    std::vector<LightMap> lightmaps;
    std::vector<uint8_t> light_bits;
    std::vector<int32_t> lights;
    std::vector<int32_t> zone_actors;  // object refs, index = zone number
    int32_t polys = 0;
};

struct ReachSpec {  // a path between two navigation points (built by the editor)
    int32_t distance, start, end;  // start, end: object refs
    int32_t collision_radius, collision_height, reach_flags;
    uint8_t pruned;
};

struct Level {
    std::vector<int32_t> actors;  // object refs, 0 = deleted slot
    int32_t model = 0;
    std::vector<ReachSpec> reach_specs;
};

// Poly flag bits (public UE1 names, unverified).
enum : uint32_t {
    PF_Invisible = 0x1, PF_Masked = 0x2, PF_Translucent = 0x4, PF_Modulated = 0x40, PF_FakeBackdrop = 0x80,
    PF_TwoSided = 0x100, PF_Unlit = 0x400000, PF_Portal = 0x4000000,
};

Model read_model(const Package& p, const Export& e);  // validated with validate_model; throws on bad data
// Throws std::runtime_error unless every index the engine follows is in range (package data is untrusted).
void validate_model(const Model& m);
Level read_level(const Package& p, const Export& e);
size_t model_end(const Package& p, const Export& e);  // offset after the Model data (for cross-checks)

struct Vertex { float x, y, z, u, v, lu = 0, lv = 0; };  // lu, lv: lightmap atlas coordinates

// Triangle fan of a BSP node's polygon; UVs in texture units (texel coordinate / texture size).
std::vector<Vertex> node_triangles(const Model& m, const Node& n, int usize, int vsize);

}  // namespace ut
