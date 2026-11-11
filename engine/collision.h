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

// trace against a model placed in the world (a mover's brush): world = loc + R * (local - pivot), where the rows of
// axes are R's columns X, Y, Z (unit vectors). The normal comes back in world space. The box keeps its world axes
// (exact for rotations in steps of 90 degrees about Z).
Hit trace_placed(const Model& m, Vec3 loc, Vec3 pivot, const float axes[3][3], Vec3 start, Vec3 end, Vec3 extent);

// Segment a-b against a vertical cylinder (centre c, radius r, half height h). A segment starting inside does not
// hit. On a hit before *t (fraction), sets *t and the surface normal *n and returns true.
bool ray_cylinder(Vec3 a, Vec3 b, Vec3 c, float r, float h, float* t, Vec3* n);

}  // namespace ut
