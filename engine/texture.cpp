#include "texture.h"

#include <stdexcept>

namespace ut {

static Image checker() {
    Image img{8, 8, std::vector<uint8_t>(8 * 8 * 4)};
    for (size_t i = 0; i < 64; i++) {
        uint8_t g = ((i % 8) / 4 + (i / 8) / 4) % 2 ? 160 : 96;
        img.rgba[i * 4] = img.rgba[i * 4 + 1] = img.rgba[i * 4 + 2] = g;
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

Image texture_rgba(const Package& p, const Export& tex, bool masked) {
    if (tex.size <= 0) return checker();  // missing texture
    size_t end = 0;
    std::vector<Property> props = read_properties(p, tex, &end);
    int32_t palette = 0, format = 0;
    for (const Property& prop : props) {
        if (prop.name == "Palette") palette = prop_ref(p, prop);
        if (prop.name == "Format") format = prop_byte(p, prop);
    }
    if (format != 0 || palette <= 0 || size_t(palette) > p.exports.size()) return checker();  // not P8 / palette elsewhere
    Reader r(p.data, end);
    if (r.index() < 1) return checker();
    std::vector<uint8_t> pixels = lazy_bytes(r, p.version);
    uint32_t w = r.u32(), h = r.u32();
    if (size_t(w) * h != pixels.size()) return checker();

    const Export& pal = p.exports[size_t(palette - 1)];
    read_properties(p, pal, &end);
    Reader pr(p.data, end);
    int32_t n = pr.index();
    if (n < 1 || n > 256) throw std::runtime_error("bad palette size");
    pr.need(size_t(n) * 4);
    const uint8_t* colours = &p.data[pr.pos];

    Image img{int(w), int(h), std::vector<uint8_t>(pixels.size() * 4)};
    for (size_t i = 0; i < pixels.size(); i++) {
        uint8_t c = pixels[i] < n ? pixels[i] : 0;
        img.rgba[i * 4] = colours[c * 4];
        img.rgba[i * 4 + 1] = colours[c * 4 + 1];
        img.rgba[i * 4 + 2] = colours[c * 4 + 2];
        img.rgba[i * 4 + 3] = masked && c == 0 ? 0 : 255;
    }
    return img;
}

}  // namespace ut
