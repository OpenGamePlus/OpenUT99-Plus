// Palette and Texture to RGBA. Spec: docs/formats/texture.md; reference: tools/utex.py
#pragma once
#include <cstdint>
#include <vector>

#include "upkg.h"

namespace ut {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// Mip 0 of a texture export. P8 only (shortcut: other formats return an 8x8 grey checker, upgrade when
// DXT/469 formats are decoded). masked: palette index 0 is transparent (PF_Masked surfaces).
Image texture_rgba(const Package& p, const Export& tex, bool masked);

}  // namespace ut
