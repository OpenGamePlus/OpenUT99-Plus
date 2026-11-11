// Static lighting from BSP lightmaps: luxel positions, shadow bits, light colours, atlas packing.
// Spec: docs/formats/level.md (lightmaps); the colour model is engine behaviour, unverified against the original.
#pragma once
#include <cstdint>
#include <vector>

#include "level.h"

namespace ut {

// World position of luxel (i, j): texture coordinates (pan.x + i * uscale, pan.y + j * vscale) on the surface plane.
Vec3 luxel_position(const Model& m, const Surf& s, const LightMap& lm, int i, int j);
int lightmap_lights(const Model& m, const LightMap& lm);  // lights listed for the lightmap (up to the 0 entry)
// Whether light number `light` of the lightmap reaches luxel (i, j): one bit per luxel, rows padded to bytes, LSB first.
bool shadow_bit(const Model& m, const LightMap& lm, int light, int i, int j);
// UE1 light colour: hue 0..255, saturation 255 = white, brightness 0..255 → RGB 0..1.
Vec3 light_colour(int hue, int saturation, int brightness);
// Light falloff at distance d for LightRadius r (world radius 25 * (r + 1); shortcut: curve and scale unverified).
float light_falloff(float d, int radius);

// Shelf packer for lightmaps into one texture of fixed width.
class Atlas {
public:
    explicit Atlas(int width) : width_(width) {}
    bool place(int w, int h, int* x, int* y);  // false if wider than the atlas
    int width() const { return width_; }
    int height() const { return shelf_y_ + shelf_h_; }

private:
    int width_, shelf_x_ = 0, shelf_y_ = 0, shelf_h_ = 0;
};

}  // namespace ut
