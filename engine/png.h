// Minimal PNG encoder (RGBA8, uncompressed deflate blocks): screenshots without a zlib dependency.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ut {

std::vector<uint8_t> encode_png(int width, int height, const std::vector<uint8_t>& rgba);
void write_png(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba);  // throws on I/O error

}  // namespace ut
