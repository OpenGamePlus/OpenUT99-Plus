#include "png.h"

#include <fstream>
#include <stdexcept>

namespace ut {

static uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c;
}

static void be32(std::vector<uint8_t>& o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s));
}

static void chunk(std::vector<uint8_t>& o, const char* tag, const std::vector<uint8_t>& body) {
    be32(o, uint32_t(body.size()));
    size_t start = o.size();
    o.insert(o.end(), tag, tag + 4);
    o.insert(o.end(), body.begin(), body.end());
    be32(o, crc32(&o[start], o.size() - start) ^ 0xFFFFFFFFu);
}

std::vector<uint8_t> encode_png(int width, int height, const std::vector<uint8_t>& rgba) {
    size_t row = size_t(width) * 4;
    if (rgba.size() != row * size_t(height)) throw std::runtime_error("png: pixel buffer size mismatch");
    std::vector<uint8_t> raw;
    for (int y = 0; y < height; y++) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgba.begin() + long(size_t(y) * row), rgba.begin() + long(size_t(y + 1) * row));
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    for (size_t pos = 0; pos < raw.size() || pos == 0; pos += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);  // stored block, final flag
        z.push_back(uint8_t(n)), z.push_back(uint8_t(n >> 8)), z.push_back(uint8_t(~n)), z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        if (raw.empty()) break;
    }
    be32(z, b << 16 | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(width)), be32(ihdr, uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

void write_png(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> png = encode_png(width, height, rgba);
    std::ofstream f(path, std::ios::binary);
    if (!f.write(reinterpret_cast<const char*>(png.data()), long(png.size()))) throw std::runtime_error("cannot write " + path);
}

}  // namespace ut
