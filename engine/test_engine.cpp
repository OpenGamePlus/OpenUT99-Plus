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

int main() {
    test_ini();
    test_vfs();
    test_compact_index();
    test_parse_package();
    test_string_utf16();
    test_lazy_bytes();
    test_properties();
    std::puts("engine tests ok");
}
