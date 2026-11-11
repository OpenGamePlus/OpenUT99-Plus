// Engine unit tests on synthetic data (no game files). Same inputs as tools/test_*.py. Run via ctest.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>

#include "config.h"
#include "level.h"
#include "packages.h"
#include "png.h"
#include "texture.h"
#include "upkg.h"
#include "vfs.h"

using namespace ut;
using Bytes = std::vector<uint8_t>;

template <class F>
static bool throws(F f) {
    try {
        f();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

static Bytes cindex(int32_t v) {  // inverse of Reader::index
    uint32_t a = v < 0 ? uint32_t(-int64_t(v)) : uint32_t(v);
    Bytes out{uint8_t((v < 0 ? 0x80 : 0) | (a & 0x3F))};
    a >>= 6;
    if (a) out[0] |= 0x40;
    while (a) {
        out.push_back(uint8_t(a & 0x7F));
        a >>= 7;
        if (a) out.back() |= 0x80;
    }
    return out;
}

static void put(Bytes& b, const Bytes& more) { b.insert(b.end(), more.begin(), more.end()); }
template <class T>
static void put(Bytes& b, T v) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &v, sizeof(T));
    b.insert(b.end(), raw, raw + sizeof(T));
}

static void test_compact_index() {
    for (int32_t v : {0, 1, -1, 63, 64, -64, 8191, 8192, 1000000, -1000000}) {
        Bytes b = cindex(v);
        Reader r(b);
        assert(r.index() == v);
        assert(r.pos == b.size());
    }
    Bytes overlong{0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    assert(throws([&] { Reader(overlong).index(); }));
}

// Same package as tools/test_upkg.py make_package().
static Bytes make_package() {
    const char* names[] = {"Core", "Class", "Package", "MyActor"};
    Bytes name_table, imports, exports;
    for (const char* n : names) {
        put(name_table, cindex(int32_t(std::strlen(n) + 1)));
        name_table.insert(name_table.end(), n, n + std::strlen(n) + 1);
        put(name_table, uint32_t(0));
    }
    put(imports, cindex(0)); put(imports, cindex(2)); put(imports, int32_t(0)); put(imports, cindex(0));
    put(imports, cindex(0)); put(imports, cindex(1)); put(imports, int32_t(-1)); put(imports, cindex(1));
    put(exports, cindex(-2)); put(exports, cindex(0)); put(exports, int32_t(0)); put(exports, cindex(3));
    put(exports, uint32_t(0)); put(exports, cindex(4)); put(exports, cindex(1000));
    uint32_t no = 36 + 16 + 4 + 8, io = no + uint32_t(name_table.size()), eo = io + uint32_t(imports.size());
    Bytes b;
    put(b, uint32_t(0x9E2A83C1)); put(b, uint16_t(69)); put(b, uint16_t(0)); put(b, uint32_t(1));
    for (uint32_t v : {4u, no, 1u, eo, 2u, io}) put(b, v);
    b.insert(b.end(), 16, 0x11);
    put(b, uint32_t(1)); put(b, uint32_t(1)); put(b, uint32_t(4));
    put(b, name_table); put(b, imports); put(b, exports);
    b.resize(1004);  // the export's data (offset 1000, size 4) must lie inside the file
    return b;
}

static void test_parse_package() {
    Package p = parse_package(make_package());
    assert(p.version == 69);
    assert(p.names.size() == 4 && p.names[3] == "MyActor");
    assert(p.imports.size() == 2 && p.imports[1].name == "Class" && p.imports[1].package == -1);
    assert(p.exports.size() == 1);
    const Export& e = p.exports[0];
    assert(e.name == "MyActor" && e.size == 4 && e.offset == 1000);
    assert(object_name(p, e.class_ref) == "Class");
    Bytes bad(64, 0);
    assert(throws([&] { parse_package(bad); }));
}

static void test_string_utf16() {
    Bytes b = cindex(-3);
    for (uint16_t c : {uint16_t('h'), uint16_t(0xE9), uint16_t(0)}) put(b, c);
    Reader r(b);
    assert(r.string() == "h\xC3\xA9");  // UTF-8
    assert(r.pos == b.size());
}

static void test_lazy_bytes() {
    Bytes payload{'a', 'b', 'c'};
    Bytes old = cindex(3); put(old, payload);
    Bytes neu; put(neu, uint32_t(4 + 1 + 3)); put(neu, cindex(3)); put(neu, payload);
    Reader r61(old), r69(neu);
    assert(lazy_bytes(r61, 61) == payload && r61.pos == old.size());
    assert(lazy_bytes(r69, 69) == payload);
    Bytes bad; put(bad, uint32_t(99)); put(bad, cindex(3)); put(bad, payload);
    assert(throws([&] { Reader r(bad); lazy_bytes(r, 69); }));
}

// Same tags as tools/test_uprops.py (subset): Int, Bool, Struct Vector, Object array index 300, Str, Float.
static void test_properties() {
    Package p;
    p.version = 69;
    p.names = {"None", "Health", "bHidden", "Location", "Vector", "Items", "Message", "Speed"};
    Bytes body = {0xAA, 0xAA, 0xAA, 0xAA};
    put(body, cindex(1)); body.push_back(0x22); put(body, int32_t(100));
    put(body, cindex(2)); body.push_back(0x83);
    put(body, cindex(3)); body.push_back(0x3A); put(body, cindex(4)); put(body, 1.f); put(body, 2.f); put(body, 3.f);
    put(body, cindex(5)); body.push_back(0x85); body.push_back(0x81); body.push_back(0x2C); put(body, cindex(5));
    put(body, cindex(6)); body.push_back(0x5D); body.push_back(5); put(body, cindex(4)); for (char c : std::string("hi!")) body.push_back(uint8_t(c)); body.push_back(0);
    put(body, cindex(7)); body.push_back(0x24); put(body, 1.5f);
    put(body, cindex(0));
    size_t want_end = body.size();
    body.insert(body.end(), {'t', 'a', 'i', 'l'});
    p.data = body;
    Export e{};
    e.offset = 4;
    e.size = int32_t(body.size() - 4);
    size_t end = 0;
    std::vector<Property> props = read_properties(p, e, &end);
    assert(end == want_end);
    assert(props.size() == 6);
    assert(props[0].name == "Health" && props[0].type == PropType::Int && prop_int(p, props[0]) == 100);
    assert(props[1].type == PropType::Bool && props[1].bool_value);
    assert(props[2].struct_name == "Vector" && prop_vector(p, props[2]).y == 2.f);
    assert(props[3].index == 300 && prop_ref(p, props[3]) == 5);
    assert(prop_string(p, props[4]) == "hi!");
    assert(prop_float(p, props[5]) == 1.5f);
    p.data[4 + cindex(1).size()] = 0x72;  // Health: size code 7 (u32 follows) = 100 > export
    assert(throws([&] { read_properties(p, e, &end); }));
}

static const char* kIni =
    "[URL]\nMap=Index.unr\n[Core.System]\nCachePath=../Cache\nCacheExt=.uxx\n"
    "Paths=../System/*.u\nPaths=../Maps/*.unr\n; comment\n[Empty]\n";

static void test_ini() {
    Ini ini = parse_ini(kIni);
    const std::vector<std::string>& paths = ini.at("Core.System").at("Paths");
    assert(paths.size() == 2 && paths[1] == "../Maps/*.unr");
    assert(ini.at("URL").at("Map")[0] == "Index.unr");
    assert(ini.at("Empty").empty());
}

static void test_vfs() {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "ut_test_vfs";
    fs::remove_all(root);
    for (const char* rel : {"System/Engine.u", "Maps/DM-Test.unr", "Cache/ABCD.uxx"}) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel).put('x');
    }
    std::ofstream(root / "Cache" / "cache.ini") << "[Cache]\nABCD=Custom.utx\nMISSING=Gone.u\n";
    Vfs vfs(root.string(), parse_ini(kIni));
    assert(vfs.find("ENGINE") == (root / "System" / "Engine.u").string());
    assert(vfs.find("dm-test") == (root / "Maps" / "DM-Test.unr").string());
    assert(vfs.find("Custom") == (root / "Cache" / "ABCD.uxx").string());
    assert(vfs.find("Gone").empty());
    fs::remove_all(root);
}

// A package with a 2-colour palette and a 2x1 P8 texture referencing it (same layout as tools/test_utex.py).
static void test_texture_rgba() {
    Package p;
    p.version = 69;
    p.names = {"None", "Palette"};
    Bytes d;
    // export 1: Palette — properties: None; data: count 2, colours
    size_t pal_off = d.size();
    put(d, cindex(0)); put(d, cindex(2));
    d.insert(d.end(), {10, 20, 30, 0, 40, 50, 60, 0});
    size_t pal_size = d.size() - pal_off;
    // export 2: Texture — properties: Palette=export 1; one mip 2x1
    size_t tex_off = d.size();
    put(d, cindex(1)); d.push_back(0x05); put(d, cindex(1)); put(d, cindex(0));
    put(d, cindex(1));                                   // mip count
    size_t skip_at = d.size();
    put(d, uint32_t(0)); put(d, cindex(2)); d.push_back(0); d.push_back(1);
    uint32_t skip = uint32_t(d.size());
    std::memcpy(&d[skip_at], &skip, 4);
    put(d, uint32_t(2)); put(d, uint32_t(1)); d.push_back(1); d.push_back(0);
    size_t tex_size = d.size() - tex_off;
    p.data = d;
    p.exports.push_back(Export{-1, 0, 0, "Pal", 0, int32_t(pal_size), int32_t(pal_off)});
    p.exports.push_back(Export{-2, 0, 0, "Tex", 0, int32_t(tex_size), int32_t(tex_off)});
    p.imports.push_back(Import{"Core", "Class", 0, "Palette"});
    p.imports.push_back(Import{"Core", "Class", 0, "Texture"});
    Image img = texture_rgba(p, p.exports[1], false);
    assert(img.width == 2 && img.height == 1);
    assert(img.rgba == (std::vector<uint8_t>{10, 20, 30, 255, 40, 50, 60, 255}));
    Image masked = texture_rgba(p, p.exports[1], true);
    assert(masked.rgba[3] == 0 && masked.rgba[7] == 255);  // index 0 transparent
}

// One square surface (4 points), texture axes 1 texel per unit, pan 0: fan into 2 triangles with UVs in texels / size.
static void test_surface_triangles() {
    Model m;
    m.points = {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}};
    m.vectors = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
    m.verts = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    Surf s{};
    s.base = 0, s.normal = 0, s.texture_u = 1, s.texture_v = 2, s.pan_u = 32;
    m.surfs = {s};
    Node n{};
    n.vert_pool = 0, n.surf = 0, n.num_vertices = 4;
    m.nodes = {n};
    std::vector<Vertex> tris = node_triangles(m, m.nodes[0], 64, 64);
    assert(tris.size() == 6);
    assert(tris[1].x == 64 && tris[1].u == 1.5f && tris[1].v == 0);  // (64 + pan 32) / 64
    assert(tris[2].v == 1.f);
}

// "Map" imports Tex (class Texture) from group Walls of package "Pack"; Pack exports group Walls and Tex inside it,
// plus a decoy Tex in another group.
static void test_resolve_import() {
    Package pack;
    pack.names = {"None", "Walls", "Tex", "Floors"};
    pack.imports = {{"Core", "Class", 0, "Texture"}, {"Core", "Class", 0, "Package"}};
    pack.exports = {{-2, 0, 0, "Floors", 0, 0, 0}, {-1, 0, 1, "Tex", 0, 0, 0},
                    {-2, 0, 0, "Walls", 0, 0, 0}, {-1, 0, 3, "Tex", 0, 0, 0}};
    Package map;
    map.imports = {{"Core", "Package", 0, "Pack"}, {"Core", "Package", -1, "Walls"}, {"Engine", "Texture", -2, "Tex"}};
    PackageSet set(nullptr);
    set.add("Pack", std::move(pack));
    Object o = set.resolve(map, -3);
    assert(o.package && o.exp && o.exp->name == "Tex" && o.exp->package == 3);  // the one inside Walls
    assert(!set.resolve(map, -1).exp);  // a package import is not an object export
}

static void test_png() {
    std::vector<uint8_t> rgba = {255, 0, 0, 255};
    std::vector<uint8_t> png = encode_png(1, 1, rgba);
    assert(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    // IEND chunk: length 0, "IEND", CRC 0xAE426082
    const uint8_t iend[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    assert(std::memcmp(&png[png.size() - 12], iend, 12) == 0);
}

int main() {
    test_resolve_import();
    test_png();
    test_texture_rgba();
    test_surface_triangles();
    test_ini();
    test_vfs();
    test_compact_index();
    test_parse_package();
    test_string_utf16();
    test_lazy_bytes();
    test_properties();
    std::puts("engine tests ok");
}
