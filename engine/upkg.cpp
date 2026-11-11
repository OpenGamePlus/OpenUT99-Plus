#include "upkg.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ut {

void Reader::need(size_t n) const {
    if (pos > data.size() || n > data.size() - pos) throw std::runtime_error("read past end of data");
}

uint8_t Reader::u8() {
    need(1);
    return data[pos++];
}

uint16_t Reader::u16() {
    need(2);
    uint16_t v = uint16_t(data[pos] | data[pos + 1] << 8);
    pos += 2;
    return v;
}

uint32_t Reader::u32() {
    need(4);
    uint32_t v;
    std::memcpy(&v, &data[pos], 4);
    pos += 4;
    return v;
}

float Reader::f32() {
    uint32_t v = u32();
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

int32_t Reader::index() {
    uint8_t first = u8(), b = first;
    uint32_t v = b & 0x3F;
    int shift = 6;
    bool more = b & 0x40;
    while (more) {
        if (shift > 27) throw std::runtime_error("compact index longer than 5 bytes");
        b = u8();
        v |= uint32_t(b & 0x7F) << shift;
        shift += 7;
        more = b & 0x80;
    }
    return first & 0x80 ? -int32_t(v) : int32_t(v);
}

static void utf8_append(std::string& s, uint32_t c) {
    if (c < 0x80) {
        s += char(c);
    } else if (c < 0x800) {
        s += char(0xC0 | (c >> 6));
        s += char(0x80 | (c & 0x3F));
    } else {
        s += char(0xE0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 0x3F));
        s += char(0x80 | (c & 0x3F));
    }
}

std::string Reader::string() {
    int32_t n = index();
    std::string s;
    if (n < 0) {  // UTF-16LE characters (shortcut: surrogate pairs kept as two 3-byte sequences; none seen in retail)
        for (int32_t i = 0; i < -n; i++) {
            uint16_t c = u16();
            if (c) utf8_append(s, c);
        }
        return s;
    }
    need(size_t(n));
    for (int32_t i = 0; i < n; i++) {  // Latin-1 → UTF-8, as tools/upkg.py decodes latin-1
        uint8_t c = data[pos + size_t(i)];
        if (c) utf8_append(s, c);
    }
    pos += size_t(n);
    return s;
}

static std::string cstring(Reader& r) {
    std::string s;
    for (uint8_t c; (c = r.u8()) != 0;) utf8_append(s, c);
    return s;
}

static const std::string& name_at(const Package& p, int32_t i) {
    if (i < 0 || size_t(i) >= p.names.size()) throw std::runtime_error("name index out of range");
    return p.names[size_t(i)];
}

Package parse_package(std::vector<uint8_t> data) {
    Package p;
    p.data = std::move(data);
    Reader r(p.data);
    if (r.u32() != 0x9E2A83C1) throw std::runtime_error("not an Unreal package");
    p.version = r.u16();
    p.licensee = r.u16();
    p.flags = r.u32();
    uint32_t nc = r.u32(), no = r.u32(), ec = r.u32(), eo = r.u32(), ic = r.u32(), io = r.u32();
    r.pos = no;
    for (uint32_t i = 0; i < nc; i++) {
        p.names.push_back(p.version >= 64 ? r.string() : cstring(r));
        r.u32();  // object flags
    }
    r.pos = io;
    for (uint32_t i = 0; i < ic; i++) {
        Import im;
        im.class_package = name_at(p, r.index());
        im.class_name = name_at(p, r.index());
        im.package = r.i32();
        im.name = name_at(p, r.index());
        p.imports.push_back(std::move(im));
    }
    r.pos = eo;
    for (uint32_t i = 0; i < ec; i++) {
        Export e;
        e.class_ref = r.index();
        e.super_ref = r.index();
        e.package = r.i32();
        e.name = name_at(p, r.index());
        e.flags = r.u32();
        e.size = r.index();
        e.offset = e.size > 0 ? r.index() : 0;
        if (e.size < 0 || e.offset < 0 || size_t(e.size) > p.data.size() || size_t(e.offset) > p.data.size() - size_t(e.size))
            throw std::runtime_error("export outside file");
        p.exports.push_back(std::move(e));
    }
    return p;
}

Package load_package(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    return parse_package(std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {}));
}

std::string object_name(const Package& p, int32_t ref) {
    if (ref == 0) return "None";
    if (ref > 0) {
        if (size_t(ref) > p.exports.size()) throw std::runtime_error("export ref out of range");
        return p.exports[size_t(ref - 1)].name;
    }
    if (size_t(-ref) > p.imports.size()) throw std::runtime_error("import ref out of range");
    return p.imports[size_t(-ref - 1)].name;
}

std::vector<uint8_t> lazy_bytes(Reader& r, int version, size_t item_size) {
    uint32_t skip = version >= 62 ? r.u32() : 0;
    int32_t n = r.index();
    if (n < 0) throw std::runtime_error("negative lazy array count");
    size_t bytes = size_t(n) * item_size;
    r.need(bytes);
    std::vector<uint8_t> out(r.data.begin() + long(r.pos), r.data.begin() + long(r.pos + bytes));
    r.pos += bytes;
    if (version >= 62 && skip != r.pos) throw std::runtime_error("lazy array skip offset mismatch");
    return out;
}

static const size_t kSizes[] = {1, 2, 4, 12, 16};

std::vector<Property> read_properties(const Package& p, const Export& e, size_t* end) {
    Reader r(p.data, size_t(e.offset));
    size_t limit = size_t(e.offset) + size_t(e.size);
    if (e.flags & 0x02000000) {  // state frame
        int32_t node = r.index();
        r.index();
        r.pos += 8 + 4;
        if (node) r.index();
    }
    std::vector<Property> props;
    for (;;) {
        const std::string& name = name_at(p, r.index());
        if (name == "None") break;
        Property prop;
        prop.name = name;
        uint8_t info = r.u8();
        prop.type = PropType(info & 0x0F);
        int code = (info >> 4) & 7;
        if (prop.type == PropType::Struct) prop.struct_name = name_at(p, r.index());
        size_t size = code < 5 ? kSizes[code] : code == 5 ? r.u8() : code == 6 ? r.u16() : r.u32();
        if ((info & 0x80) && prop.type != PropType::Bool) {
            uint8_t b = r.u8();
            if ((b & 0xC0) == 0xC0) {
                uint32_t v = uint32_t(b & 0x3F) << 24;
                v |= uint32_t(r.u8()) << 16;
                v |= uint32_t(r.u8()) << 8;
                v |= r.u8();
                prop.index = int32_t(v);
            } else if (b & 0x80) {
                prop.index = int32_t(uint32_t(b & 0x7F) << 8 | r.u8());
            } else {
                prop.index = b;
            }
        }
        if (prop.type == PropType::Bool) {
            size = 0;
            prop.bool_value = info & 0x80;
        }
        if (r.pos + size > limit) throw std::runtime_error("property " + name + " runs past the export");
        prop.value_pos = r.pos;
        prop.size = size;
        r.pos += size;
        props.push_back(std::move(prop));
    }
    *end = r.pos;
    return props;
}

int32_t prop_int(const Package& p, const Property& prop) { return Reader(p.data, prop.value_pos).i32(); }
float prop_float(const Package& p, const Property& prop) { return Reader(p.data, prop.value_pos).f32(); }
int32_t prop_ref(const Package& p, const Property& prop) { return Reader(p.data, prop.value_pos).index(); }
uint8_t prop_byte(const Package& p, const Property& prop) { return Reader(p.data, prop.value_pos).u8(); }
std::string prop_string(const Package& p, const Property& prop) { return Reader(p.data, prop.value_pos).string(); }

Vec3 prop_vector(const Package& p, const Property& prop) {
    Reader r(p.data, prop.value_pos);
    float x = r.f32(), y = r.f32();
    return {x, y, r.f32()};
}

}  // namespace ut
