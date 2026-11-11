// A map ready to draw: BSP surfaces batched by texture, player starts. No GL here (used by ut_mapview --check).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "level.h"
#include "packages.h"
#include "texture.h"

namespace ut {

struct Batch {
    std::string texture;  // "Package.Name" for logs
    Image image;
    uint32_t flags = 0;   // poly flags shared by the batch (masked, translucent, modulated, two-sided)
    std::vector<Vertex> verts;  // triangles, UE world coordinates, UVs in texture units
};

struct Spawn {
    Vec3 pos;
    int32_t pitch = 0, yaw = 0;  // UE rotator units, 65536 = full turn
};

struct Scene {
    std::vector<Batch> batches;
    std::vector<Spawn> starts;
    size_t surfaces = 0, skipped = 0, missing_textures = 0;
};

// Throws std::runtime_error if the map or its Level / Model cannot be read.
Scene load_scene(PackageSet& set, const std::string& map);

}  // namespace ut
