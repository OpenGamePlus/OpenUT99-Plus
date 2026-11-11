// Collision against the level BSP: point solidity and swept boxes. Spec: docs/formats/level.md (collision)
#pragma once
#include "level.h"

namespace ut {

struct Hit {
    bool hit = false;
    bool start_solid = false;  // the start overlaps solid; the box may still move out of it (only entering hits)
    float time = 1;            // fraction of the move done before the hit (pulled back slightly)
    Vec3 normal{0, 0, 0};      // of the surface hit, pointing out of the solid
};

// Empty space: a node's front side is empty; its back side is empty only if the walk was in empty space and the
// node is not CSG (node flag 0x1). Checked on retail maps: 1,283 of 1,285 player starts are empty with solid below.
bool point_solid(const Model& m, Vec3 p);

// Sweeps an axis-aligned box of half size extent (0 = a line) from start to end.
Hit trace(const Model& m, Vec3 start, Vec3 end, Vec3 extent);

}  // namespace ut
