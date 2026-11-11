// Engine unit tests on synthetic data (no game files). Same inputs as tools/test_*.py. Run via ctest.
#include <cassert>
#include <cmath>
#include <type_traits>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>

#include "collision.h"
#include "config.h"
#include "level.h"
#include "mesh.h"
#include "script.h"
#include "lighting.h"
#include "packages.h"
#include "png.h"
#include "texture.h"
#include "upkg.h"
#include "vfs.h"
#include "vm.h"
#include "world.h"

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
    static_assert(std::is_arithmetic<T>::value, "put: numbers only");
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
static Bytes make_package(int32_t export_offset = 1000) {
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
    put(exports, uint32_t(0)); put(exports, cindex(4)); put(exports, cindex(export_offset));
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
    assert(throws([&] { parse_package(make_package(-1)); }));  // negative offset must not wrap past the check
    Bytes small(4, 0);
    assert(throws([&] { Reader(small, SIZE_MAX).u8(); }));     // position past the end must not wrap in need()
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
    // A class import (class "Class") matches the class object export (class_ref 0).
    Package code;
    code.names = {"None"};
    code.exports = {{0, 0, 0, "HealthVial", 0, 0, 0}};
    set.add("BotPack", std::move(code));
    map.imports.push_back({"Core", "Package", 0, "BotPack"});
    map.imports.push_back({"Core", "Class", -4, "HealthVial"});
    Object cls = set.resolve(map, -5);
    assert(cls.exp && cls.exp->name == "HealthVial" && cls.exp->class_ref == 0);
}

static void test_png() {
    std::vector<uint8_t> rgba = {255, 0, 0, 255};
    std::vector<uint8_t> png = encode_png(1, 1, rgba);
    assert(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    // IEND chunk: length 0, "IEND", CRC 0xAE426082
    const uint8_t iend[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    assert(std::memcmp(&png[png.size() - 12], iend, 12) == 0);
}

// Floor surface on z = 0 with texture U = X, V = Y; lightmap 3x2 luxels, 16 units apart, starting at (10, 20).
static Model lit_floor() {
    Model m;
    m.points = {{0, 0, 0}};
    m.vectors = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
    Surf s{};
    s.base = 0, s.normal = 0, s.texture_u = 1, s.texture_v = 2, s.light_map = 0;
    m.surfs = {s};
    LightMap lm{};
    lm.pan = {10, 20, 0};
    lm.uclamp = 3, lm.vclamp = 2, lm.uscale = 16, lm.vscale = 16, lm.light_actors = 0;
    m.lightmaps = {lm};
    m.lights = {-1, -2, 0};  // two lights, then the terminator
    // light 0: rows of ceil(3/8) = 1 byte; row 0 bits 0 and 2 lit, row 1 bit 1 lit. light 1: all dark.
    m.light_bits = {0x05, 0x02, 0x00, 0x00};
    return m;
}

static void test_luxels() {
    Model m = lit_floor();
    Vec3 p = luxel_position(m, m.surfs[0], m.lightmaps[0], 2, 1);
    assert(p.x == 10 + 32 && p.y == 20 + 16 && p.z == 0);
    assert(lightmap_lights(m, m.lightmaps[0]) == 2);
    assert(shadow_bit(m, m.lightmaps[0], 0, 0, 0) && !shadow_bit(m, m.lightmaps[0], 0, 1, 0));
    assert(shadow_bit(m, m.lightmaps[0], 0, 2, 0) && shadow_bit(m, m.lightmaps[0], 0, 1, 1));
    assert(!shadow_bit(m, m.lightmaps[0], 1, 0, 0));
    m.lightmaps[0].light_actors = -1;  // retail: no lights for this lightmap
    assert(lightmap_lights(m, m.lightmaps[0]) == 0);
}

static void test_validate_model() {
    validate_model(lit_floor());  // valid
    auto bad = [](void (*mutate)(Model&)) {
        Model m = lit_floor();
        mutate(m);
        return throws([&] { validate_model(m); });
    };
    assert(bad([](Model& m) { m.lightmaps[0].uclamp = -1; }));
    assert(bad([](Model& m) { m.lightmaps[0].vclamp = 1 << 20; }));
    assert(bad([](Model& m) { m.lightmaps[0].light_actors = 99; }));
    assert(bad([](Model& m) { m.lightmaps[0].data_offset = 3; }));  // 2 lights x 2 rows x 1 byte do not fit after offset 3
    assert(bad([](Model& m) { m.lightmaps[0].uscale = 0; }));
    assert(bad([](Model& m) { m.surfs[0].normal = 7; }));
    assert(bad([](Model& m) { m.surfs[0].light_map = 5; }));
    {  // 8 lights, 2 luxels wide: 8 x 1 byte per row x 2 rows = 16 bytes (not 8 x 9 / 8 x 2 = 18)
        Model m = lit_floor();
        m.lightmaps[0].uclamp = 2;
        m.lights = {-1, -2, -3, -4, -5, -6, -7, -8, 0};
        m.light_bits.assign(16, 0);
        validate_model(m);
    }
}

static void test_light_colour() {
    Vec3 white = light_colour(0, 255, 255);  // saturation 255 = no colour
    assert(white.x == 1.f && white.y == 1.f && white.z == 1.f);
    Vec3 red = light_colour(0, 0, 255);
    assert(red.x == 1.f && red.y == 0.f && red.z == 0.f);
}

static void test_atlas() {
    Atlas a(16);
    int x0, y0, x1, y1;
    assert(a.place(10, 4, &x0, &y0) && a.place(10, 4, &x1, &y1));
    assert(x0 == 0 && y0 == 0 && y1 > y0);  // second does not fit on the first shelf (10 + 10 > 16)
    assert(!a.place(17, 1, &x1, &y1));
    assert(!a.place(-1, 1, &x1, &y1) && !a.place(1, 0, &x1, &y1));  // never moves the shelf backwards
}

// Same script as tools/test_uscript.py: Health = 5; Tick(1); native 112(); return; label table.
static Bytes script_bytes() {
    Bytes b = {0x0F, 0x01};
    put(b, cindex(-3)); b.push_back(0x1D); put(b, int32_t(5));
    b.push_back(0x1B); put(b, cindex(1)); b.push_back(0x26); b.push_back(0x16);
    b.push_back(0x70); b.push_back(0x16);
    b.push_back(0x04); b.push_back(0x0B);
    b.push_back(0x0C); put(b, cindex(3)); put(b, uint32_t(0)); put(b, cindex(0)); put(b, uint32_t(0xFFFF));
    return b;
}

static void test_skip_script() {
    Package p;
    p.names = {"None", "Tick", "Health", "Label1"};
    p.data = script_bytes();
    assert(skip_script(p, 0, 11 + 7 + 2 + 2 + 17) == p.data.size());
    Bytes mem;
    skip_script(p, 0, 11 + 7 + 2 + 2 + 17, &mem);
    assert(mem.size() == 11 + 7 + 2 + 2 + 17 && mem[2] == 0xFD && mem[5] == 0xFF);  // ref -3 as a 4-byte int
    assert(throws([&] { skip_script(p, 0, 3); }));  // size does not land on a token boundary
    p.data = {0x03};
    assert(throws([&] { skip_script(p, 0, 1); }));  // unknown token
    p.data.assign(300, 0x04);  // 300 nested returns: deeper than any real script, must not recurse unbounded
    p.data.push_back(0x0B);
    assert(throws([&] { skip_script(p, 0, 301); }));
}

// Class object: struct header + script, state fields, class fields, defaults (Health=100).
static void test_read_class() {
    Package p;
    p.version = 69;
    p.names = {"None", "Tick", "Health", "Label1"};
    Bytes d;
    put(d, cindex(-1)); put(d, cindex(0)); put(d, cindex(0)); put(d, cindex(0)); put(d, cindex(1));  // super .. friendly name
    put(d, int32_t(0)); put(d, int32_t(0)); put(d, uint32_t(11 + 7 + 2 + 2 + 17)); put(d, script_bytes());
    put(d, uint64_t(0)); put(d, uint64_t(0)); put(d, uint16_t(0xFFFF)); put(d, uint32_t(0));     // state
    put(d, uint32_t(0)); d.insert(d.end(), 16, 0);                                                 // flags, guid
    put(d, cindex(0)); put(d, cindex(0)); put(d, cindex(0)); put(d, cindex(0));                    // deps, imports, within, config
    put(d, cindex(2)); d.push_back(0x22); put(d, int32_t(100)); put(d, cindex(0));                 // defaults
    p.data = d;
    Export e{0, 0, 0, "MyClass", 0, int32_t(d.size()), 0};
    ClassInfo c = read_class(p, e);
    assert(c.super_ref == -1 && c.defaults.size() == 1 && prop_int(p, c.defaults[0]) == 100);
}

// Package with two script functions and the native declarations they call (exports are 1-based refs):
// 1 class T; 2 Mul(A, B, optional out C) int: C = A * B; return C; 3-6 its properties;
// 7 Multiply_IntInt (native 144) + 8-10; 11 OrOr_BoolBool (native 132) + 12-14; 15 Or() bool + 16.
// Or returns false || (true || native 0xF0()): the unbound native must be skipped by the short circuit.
// 17 instance variable int X of T (default 5); 18 Get(): X = 3; return X * default.X; 20 Null(): return None.X * 4.
// 22 latent native Sleep(float) (256); 24 auto state S: Get() returns 99; code Begin: X = 7; Sleep(1); X = 9; stop.
// 27 Call(): return Get(), a virtual call that sees the state.
// 29 iterator native Three(out int V) (300); 31 Sum(): S = 1; ForEach Three(V) S = S * V; return S.
// 35-37 TimerRate, TimerCounter, bTimerLoop of T; 38 Timer(): X = X * 2.
// 39-47 Location, Velocity, Acceleration, Physics, bCollideActors, bCollideWorld, CollisionRadius, CollisionHeight,
// GroundSpeed of T; 48 Touch(Other): X = X * 2.
static Package vm_package() {
    Package p;
    p.version = 69;
    p.names = {"None", "T", "Mul", "A", "B", "C", "ReturnValue", "Multiply_IntInt", "Function", "IntProperty",
               "Core", "Class", "OrOr_BoolBool", "BoolProperty", "Or", "X", "Get", "Null",
               "Sleep", "Seconds", "FloatProperty", "S", "Begin", "State", "Call",
               "Three", "Sum", "V", "TimerRate", "TimerCounter", "bTimerLoop", "Timer",
               "Location", "Velocity", "Acceleration", "Physics", "bCollideActors", "bCollideWorld", "CollisionRadius",
               "CollisionHeight", "GroundSpeed", "Touch", "Other", "Vector", "Struct", "ByteProperty", "StructProperty",
               "ObjectProperty"};
    p.imports = {{"Core", "Class", 0, "Function"}, {"Core", "Class", 0, "IntProperty"}, {"Core", "Class", 0, "BoolProperty"},
                 {"Core", "Class", 0, "FloatProperty"}, {"Core", "Class", 0, "State"}, {"Core", "Class", 0, "ByteProperty"},
                 {"Core", "Class", 0, "StructProperty"}, {"Core", "Class", 0, "ObjectProperty"}, {"Core", "Struct", 0, "Vector"}};
    auto add = [&](int32_t cls, int32_t outer, const char* name, const Bytes& d) {
        p.exports.push_back({cls, 0, outer, name, 0, int32_t(d.size()), int32_t(p.data.size())});
        put(p.data, d);
    };
    auto prop = [](int32_t next, uint32_t flags) {
        Bytes b = cindex(0);  // no tagged properties
        put(b, cindex(0)); put(b, cindex(next)); put(b, uint16_t(1)); put(b, uint16_t(4)); put(b, flags); put(b, cindex(0));
        return b;
    };
    auto func = [](int32_t children, const Bytes& code, uint32_t mem, uint16_t native, uint32_t flags) {
        Bytes b = cindex(0);
        for (int i = 0; i < 3; i++) put(b, cindex(0));  // super, next, script text
        put(b, cindex(children)); put(b, cindex(0)); put(b, int32_t(0)); put(b, int32_t(0)); put(b, mem); put(b, code);
        put(b, native); b.push_back(0); put(b, flags);
        return b;
    };
    Bytes cls;
    for (int i = 0; i < 5; i++) put(cls, cindex(0));                          // super .. friendly name
    put(cls, int32_t(0)); put(cls, int32_t(0)); put(cls, uint32_t(0));        // line, text pos, no script
    put(cls, uint64_t(0)); put(cls, uint64_t(0)); put(cls, uint16_t(0xFFFF)); put(cls, uint32_t(0));  // state
    put(cls, uint32_t(0)); cls.insert(cls.end(), 16, 0);                      // flags, guid
    for (int i = 0; i < 4; i++) put(cls, cindex(0));                          // deps, imports, within, config
    put(cls, cindex(15)); cls.push_back(0x22); put(cls, int32_t(5)); put(cls, cindex(0));  // defaults: X=5
    add(0, 0, "T", cls);
    Bytes mul = {0x0F, 0x00};
    put(mul, cindex(5)); mul.push_back(0x90);
    mul.push_back(0x00); put(mul, cindex(3)); mul.push_back(0x00); put(mul, cindex(4)); mul.push_back(0x16);
    mul.push_back(0x04); mul.push_back(0x00); put(mul, cindex(5));
    add(-1, 1, "Mul", func(3, mul, 24, 0, 0));
    add(-2, 2, "A", prop(4, kParm));
    add(-2, 2, "B", prop(5, kParm));
    add(-2, 2, "C", prop(6, kParm | kOptionalParm | kOutParm));
    add(-2, 2, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    add(-1, 1, "Multiply_IntInt", func(8, {}, 0, 144, kFuncNative | 1));
    add(-2, 7, "A", prop(9, kParm));
    add(-2, 7, "B", prop(10, kParm));
    add(-2, 7, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    add(-1, 1, "OrOr_BoolBool", func(12, {}, 0, 132, kFuncNative | 1));
    add(-3, 11, "A", prop(13, kParm));
    add(-3, 11, "B", prop(14, kParm | kSkipParm));
    add(-3, 11, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    Bytes orc = {0x04, 0x84, 0x28, 0x18, 9, 0, 0x84, 0x27, 0x18, 3, 0, 0xF0, 0x16, 0x16, 0x16};
    add(-1, 1, "Or", func(16, orc, 15, 0, 0));
    add(-3, 15, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    add(-2, 1, "X", prop(0, 0));
    Bytes get = {0x0F, 0x01};
    put(get, cindex(17)); get.push_back(0x1D); put(get, int32_t(3));
    get.push_back(0x04); get.push_back(0x90); get.push_back(0x01); put(get, cindex(17));
    get.push_back(0x02); put(get, cindex(17)); get.push_back(0x16);
    add(-1, 1, "Get", func(19, get, 24, 0, 0));
    add(-2, 18, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    Bytes null = {0x04, 0x90, 0x19, 0x2A, 5, 0, 4, 0x01};
    put(null, cindex(17)); null.push_back(0x2C); null.push_back(4); null.push_back(0x16);
    add(-1, 1, "Null", func(21, null, 15, 0, 0));
    add(-2, 20, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    add(-1, 1, "Sleep", func(23, {}, 0, 256, kFuncNative | kFuncLatent | 1));
    add(-4, 22, "Seconds", prop(0, kParm));
    Bytes code = {0x0F, 0x01};
    put(code, cindex(17)); code.push_back(0x1D); put(code, int32_t(7));             // 0: X = 7
    code.push_back(0x61); code.push_back(0x00); code.push_back(0x1E); put(code, 1.0f); code.push_back(0x16);  // 11: Sleep(1)
    code.push_back(0x0F); code.push_back(0x01); put(code, cindex(17)); code.push_back(0x1D); put(code, int32_t(9));  // 19
    code.push_back(0x08); code.push_back(0x0C);                                        // 30: stop; 31: label table
    put(code, cindex(22)); put(code, uint32_t(0)); put(code, cindex(0)); put(code, uint32_t(0xFFFF));
    Bytes st = cindex(0);
    for (int i = 0; i < 5; i++) put(st, cindex(0));
    put(st, int32_t(0)); put(st, int32_t(0)); put(st, uint32_t(48)); put(st, code);
    put(st, uint64_t(0)); put(st, uint64_t(0)); put(st, uint16_t(32)); put(st, uint32_t(kStateAuto));
    add(-5, 1, "S", st);
    Bytes get99 = {0x04, 0x1D};
    put(get99, int32_t(99));
    add(-1, 24, "Get", func(26, get99, 6, 0, 0));
    add(-2, 25, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    Bytes call = {0x04, 0x1B};
    put(call, cindex(16)); call.push_back(0x16);
    add(-1, 1, "Call", func(28, call, 7, 0, 0));
    add(-2, 27, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    add(-1, 1, "Three", func(30, {}, 0, 300, kFuncNative | kFuncIterator | 1));
    add(-2, 29, "V", prop(0, kParm | kOutParm));
    Bytes sum = {0x0F, 0x00};
    put(sum, cindex(32)); sum.push_back(0x26);                                                  // 0: S = 1
    sum.push_back(0x2F); sum.push_back(0x61); sum.push_back(0x2C); sum.push_back(0x00);         // 7: ForEach Three(V)
    put(sum, cindex(33)); sum.push_back(0x16); put(sum, uint16_t(37));
    sum.push_back(0x0F); sum.push_back(0x00); put(sum, cindex(32)); sum.push_back(0x90);       // 18: S = S * V
    sum.push_back(0x00); put(sum, cindex(32)); sum.push_back(0x00); put(sum, cindex(33)); sum.push_back(0x16);
    sum.push_back(0x31); sum.push_back(0x30);                                                   // 36: next; 37: pop
    sum.push_back(0x04); sum.push_back(0x00); put(sum, cindex(32));                             // 38: return S
    add(-1, 1, "Sum", func(32, sum, 44, 0, 0));
    add(-2, 31, "S", prop(33, 0));
    add(-2, 31, "V", prop(34, 0));
    add(-2, 31, "ReturnValue", prop(0, kParm | kOutParm | kReturnParm));
    add(-4, 1, "TimerRate", prop(0, 0));
    add(-4, 1, "TimerCounter", prop(0, 0));
    add(-3, 1, "bTimerLoop", prop(0, 0));
    Bytes timer = {0x0F, 0x01};
    put(timer, cindex(17)); timer.push_back(0x90); timer.push_back(0x01); put(timer, cindex(17));
    timer.push_back(0x2C); timer.push_back(2); timer.push_back(0x16);
    add(-1, 1, "Timer", func(0, timer, 15, 0, 0));
    for (const char* v : {"Location", "Velocity", "Acceleration"}) {
        Bytes b = prop(0, 0);
        put(b, cindex(-9));  // StructProperty of struct Vector
        add(-7, 1, v, b);
    }
    add(-6, 1, "Physics", prop(0, 0));
    add(-3, 1, "bCollideActors", prop(0, 0));
    add(-3, 1, "bCollideWorld", prop(0, 0));
    add(-4, 1, "CollisionRadius", prop(0, 0));
    add(-4, 1, "CollisionHeight", prop(0, 0));
    add(-4, 1, "GroundSpeed", prop(0, 0));
    add(-1, 1, "Touch", func(49, timer, 15, 0, 0));
    add(-8, 48, "Other", prop(0, kParm));
    return p;
}

static void test_vm() {
    Package p = vm_package();
    Vm vm(nullptr);
    vm.bind_natives(p);
    const Function& mul = vm.function(p, 2);
    assert(mul.locals.size() == 4 && mul.locals[2].type == VT::Int && mul.code.size() == 24);
    std::vector<Value> args = {Value::of(VT::Int), Value::of(VT::Int), Value::of(VT::Int)};
    args[0].i = 6, args[1].i = -7;
    Value r = vm.call(mul, nullptr, args);
    assert(r.t == VT::Int && r.i == -42 && args[2].i == -42);  // return value and out parameter
    std::vector<Value> none;
    Value b = vm.call(vm.function(p, 15), nullptr, none);
    assert(b.t == VT::Bool && b.i == 1);
    Obj* t = vm.object(p, 1);
    Obj* self = vm.spawn(t);
    assert(vm.call(vm.function(p, 18), self, none).i == 15);              // instance X = 3 times default X = 5
    assert(self->vars[vm.object(p, 17)][0].i == 3 && vm.defaults(t)->vars[vm.object(p, 17)][0].i == 5);
    Value z = vm.call(vm.function(p, 20), self, none);
    assert(z.t == VT::Int && z.i == 0 && vm.log.back().find("accessed None") != std::string::npos);
    const Function& call = vm.function(p, 27);
    assert(vm.call(call, self, none).i == 15);                        // no state: the class's Get
    assert(vm.goto_state(self, "Auto") && self->state == vm.object(p, 24));
    assert(vm.call(call, self, none).i == 99);                        // the state's Get overrides it
    Value& x = self->vars[vm.object(p, 17)][0];
    vm.tick(self, 0.1f);
    assert(x.i == 7 && self->code_active);                             // asleep after X = 7
    vm.tick(self, 0.5f);
    assert(x.i == 7);
    vm.tick(self, 0.6f);
    assert(x.i == 9 && !self->code_active);                            // woke after 1 s, ran to stop
    assert(!vm.goto_state(self, "Missing") && vm.goto_state(self, "None") && !self->state);
    assert(vm.call(call, self, none).i == 15);
    vm.add_native("Three", [&vm](Obj*, std::vector<Value*>&, Value&) {
        for (int k = 1; k <= 3; k++) {
            Value v = Value::of(VT::Int);
            v.i = k;
            vm.iterator_items.push_back(v);
        }
    });
    assert(vm.call(vm.function(p, 31), self, none).i == 6);  // 1 * 1 * 2 * 3: every item visited once
    PackageSet set(nullptr);
    World w(vm, set);
    Obj* a = w.spawn(t, nullptr, nullptr, nullptr, nullptr, nullptr);
    Value& ax = a->vars[vm.object(p, 17)][0];
    vm.prop(a, "TimerRate")->f = 0.25f;
    vm.prop(a, "bTimerLoop")->i = 1;
    for (int k = 0; k < 3; k++) w.tick(0.1f);
    assert(ax.i == 10);  // fired once at 0.3 s
    w.tick(0.2f);
    assert(ax.i == 20);  // looping: counter 0.05 + 0.2 reached the rate again
    vm.prop(a, "bTimerLoop")->i = 0;
    for (int k = 0; k < 6; k++) w.tick(0.1f);
    assert(ax.i == 40 && vm.prop(a, "TimerRate")->f == 0);  // one-shot: fired once, then off
    assert(w.errors.empty());
    // A walks along the floor of the two-node BSP (floor z = 0, wall x = 100), touches B, stops at the wall.
    Model room;
    Node floor{}, wall{};
    floor.plane[2] = 1, floor.back = -1, floor.front = 1;
    wall.plane[0] = -1, wall.plane[3] = -100, wall.back = -1, wall.front = -1;
    room.nodes = {floor, wall};
    w.set_model(room);
    Obj* walker = w.spawn(t, nullptr, nullptr, nullptr, nullptr, nullptr);
    Obj* other = w.spawn(t, nullptr, nullptr, nullptr, nullptr, nullptr);
    for (Obj* o : {walker, other}) {
        vm.prop(o, "bCollideActors")->i = 1;
        vm.prop(o, "CollisionRadius")->f = 20, vm.prop(o, "CollisionHeight")->f = 20;
        vm.prop(o, "Location")->v[2] = 20;
    }
    vm.prop(other, "Location")->v[0] = 60;
    vm.prop(walker, "Physics")->i = 1, vm.prop(walker, "bCollideWorld")->i = 1;
    vm.prop(walker, "Acceleration")->v[0] = 1, vm.prop(walker, "GroundSpeed")->f = 400;
    for (int k = 0; k < 20; k++) w.tick(0.05f);
    assert(vm.prop(walker, "Location")->t == VT::Vector && vm.prop(walker, "Physics")->t == VT::Byte);
    const float* at = vm.prop(walker, "Location")->v;
    assert(walker->vars[vm.object(p, 17)][0].i == 10 && other->vars[vm.object(p, 17)][0].i == 10);  // both touched once
    assert(at[0] > 79 && at[0] < 80.5f && std::fabs(at[2] - 20) < 0.5f && vm.prop(walker, "Physics")->i == 1);
    assert(w.errors.empty());
    p.data[size_t(p.exports[14].offset) + 18 + 9] = 4;  // inner skip too long: runs past the code, must throw
    Vm vm2(nullptr);
    vm2.bind_natives(p);
    assert(throws([&] { vm2.call(vm2.function(p, 15), nullptr, none); }));
}

// Two-node BSP: floor z = 0 (solid below), its front holds a wall x = 100 facing -x (solid beyond).
static void test_collision() {
    Model m;
    Node floor{}, wall{};
    floor.plane[2] = 1, floor.back = -1, floor.front = 1;
    wall.plane[0] = -1, wall.plane[3] = -100, wall.back = -1, wall.front = -1;
    m.nodes = {floor, wall};
    assert(!point_solid(m, {0, 0, 50}) && point_solid(m, {0, 0, -1}) && point_solid(m, {150, 0, 50}));
    Hit h = trace(m, {0, 0, 100}, {0, 0, -100}, {0, 0, 0});
    assert(h.hit && std::fabs(h.time - 0.5f) < 0.01f && h.normal.z == 1);
    h = trace(m, {0, 0, 100}, {0, 0, -100}, {10, 10, 10});  // a box stops when its bottom reaches the floor
    assert(h.hit && std::fabs(h.time - 0.45f) < 0.01f);
    h = trace(m, {0, 0, 50}, {200, 0, 50}, {0, 0, 0});
    assert(h.hit && std::fabs(h.time - 0.5f) < 0.01f && h.normal.x == -1);
    assert(!trace(m, {0, 0, 50}, {90, 0, 60}, {0, 0, 0}).hit);
    h = trace(m, {0, 0, -5}, {0, 0, 50}, {0, 0, 0});  // starts in the floor: may move out, not a hit
    assert(h.start_solid && !h.hit);
    h = trace(m, {0, 0, 5}, {0, 0, 50}, {10, 10, 10});  // box overlapping the floor moves up freely
    assert(h.start_solid && !h.hit);
    assert(trace(m, {0, 0, 5}, {0, 0, -50}, {10, 10, 10}).time < 0.05f);  // but cannot sink further
    h = trace(m, {95, 0, 60}, {95, 0, 0}, {10, 10, 10});  // box overlapping the wall slides down along it to the floor
    assert(h.hit && h.normal.z == 1 && std::fabs(h.time - 50.f / 60.f) < 0.01f);
    floor.flags = 1;  // not CSG: crossing it keeps empty space
    m.nodes[0] = floor;
    assert(!point_solid(m, {0, 0, -1}));
}

static void test_unpack_vert() {
    uint32_t v = uint32_t(-1024 & 0x7FF) | uint32_t(1023 & 0x7FF) << 11 | uint32_t(-512 & 0x3FF) << 22;
    Vec3 p = unpack_vert(v);
    assert(p.x == -1024 && p.y == 1023 && p.z == -512);
}

int main() {
    test_collision();
    test_vm();
    test_skip_script();
    test_read_class();
    test_unpack_vert();
    test_luxels();
    test_validate_model();
    test_light_colour();
    test_atlas();
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
