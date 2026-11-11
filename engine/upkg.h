// Unreal packages and tagged properties. Specs: docs/formats/package.md, docs/formats/properties.md;
// reference: tools/upkg.py, tools/uprops.py
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ut {

// Little-endian reader over a byte buffer. Throws std::runtime_error on any read past the end.
struct Reader {
    const std::vector<uint8_t>& data;
    size_t pos = 0;
    explicit Reader(const std::vector<uint8_t>& d, size_t p = 0) : data(d), pos(p) {}
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    int32_t i32() { return int32_t(u32()); }
    float f32();
    int32_t index();     // compact index
    std::string string();  // length-prefixed; negative length = UTF-16, returned as UTF-8
    void need(size_t n) const;
};

struct Import {
    std::string class_package, class_name;
    int32_t package;
    std::string name;
};

struct Export {
    int32_t class_ref, super_ref, package;
    std::string name;
    uint32_t flags;
    int32_t size, offset;
};

struct Package {
    int version = 0, licensee = 0;
    uint32_t flags = 0;
    std::vector<std::string> names;
    std::vector<Import> imports;
    std::vector<Export> exports;
    std::vector<uint8_t> data;
};

Package parse_package(std::vector<uint8_t> data);
Package load_package(const std::string& path);
// 0 = "None", > 0 export ref - 1, < 0 import -ref - 1.
std::string object_name(const Package& p, int32_t ref);
// Lazy array: [u32 skip offset, version >= 62], compact item count, items. Returns the raw bytes.
std::vector<uint8_t> lazy_bytes(Reader& r, int version, size_t item_size = 1);

enum class PropType : uint8_t { Byte = 1, Int, Bool, Float, Object, Name, String, Class, Array, Struct, Vector, Rotator, Str, Map, FixedArray };

struct Property {
    std::string name;
    PropType type;
    int32_t index = 0;        // array index
    std::string struct_name;  // Struct only
    size_t value_pos = 0, size = 0;
    bool bool_value = false;
};

struct Vec3 { float x, y, z; };

// Tagged properties at the start of an export's data (after the state frame if any). *end = offset after "None".
std::vector<Property> read_properties(const Package& p, const Export& e, size_t* end);
int32_t prop_int(const Package& p, const Property& prop);   // Int
float prop_float(const Package& p, const Property& prop);   // Float
int32_t prop_ref(const Package& p, const Property& prop);   // Object, Name (compact index)
uint8_t prop_byte(const Package& p, const Property& prop);  // Byte
std::string prop_string(const Package& p, const Property& prop);  // Str
Vec3 prop_vector(const Package& p, const Property& prop);   // Struct Vector, Scale (first 12 bytes)

}  // namespace ut
