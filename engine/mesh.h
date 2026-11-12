// LodMesh (vertex-animated meshes). Spec: docs/formats/mesh.md; reference: tools/umesh.py
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "upkg.h"

namespace ut {

Vec3 unpack_vert(uint32_t v);  // X 11, Y 11, Z 10 bits, signed

struct LodMesh {
    std::vector<Vec3> verts;   // every animation frame in turn, each frame_verts long: special verts, then model verts
    int frame_verts = 0, frames = 0;
    int special_verts = 0;
    struct Face { uint16_t wedge[3], material; };
    struct Wedge { uint16_t vertex; uint8_t u, v; };
    struct Material { uint32_t flags; int32_t texture; };
    std::vector<Face> faces;
    std::vector<Wedge> wedges;
    std::vector<Material> materials;
    std::vector<int32_t> textures;  // object refs
    Vec3 scale{}, origin{};
    int32_t rot_origin[3] = {0, 0, 0};  // pitch, yaw, roll
    struct AnimSeq {
        std::string name, group;
        int32_t start = 0, frames = 0;
        float rate = 0;  // frames per second
    };
    std::vector<AnimSeq> seqs;
};

LodMesh read_lod_mesh(const Package& p, const Export& e);  // throws if the export is not a LodMesh layout

}  // namespace ut
