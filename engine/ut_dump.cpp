// ut_dump FILE            summary line, same text as tools/upkg.py
// ut_dump FILE --props    per non-class export: "<index> <property count> <end offset>", compared by tools/compare_engine.py
// ut_dump FILE --model    per Model export: "<index> nodes surfs verts points vectors lightmaps light_bits lights end"
// ut_dump FILE --classes  per class export: "<index> <default property count>"; per LodMesh: "<index> m <frame verts> <faces> <wedges>"
// ut_dump --deps DIR       resolve every top-level package import through the Vfs (same report as tools/uini.py --deps)
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <set>

#include "config.h"
#include "level.h"
#include "mesh.h"
#include "script.h"
#include "upkg.h"
#include "vfs.h"

static int deps(const std::string& root) {
    ut::Vfs vfs(root, ut::read_ini(root + "/System/UnrealTournament.ini"));
    std::set<std::string> files;
    for (const auto& kv : vfs.packages()) files.insert(kv.second);
    std::map<std::string, int> missing;
    for (const std::string& f : files)
        for (const ut::Import& im : ut::load_package(f).imports)
            if (im.class_name == "Package" && im.package == 0 && vfs.find(im.name).empty()) missing[im.name]++;
    std::printf("%zu packages, %zu missing dependencies\n", files.size(), missing.size());
    for (const auto& [name, n] : missing) std::printf("  %s (imported by %d)\n", name.c_str(), n);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--deps") == 0) {
        try {
            return deps(argv[2]);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "%s\n", ex.what());
            return 1;
        }
    }
    if (argc < 2 || (argc == 3 && std::strcmp(argv[2], "--props") != 0 && std::strcmp(argv[2], "--model") != 0 && std::strcmp(argv[2], "--classes") != 0) || argc > 3) {
        std::fprintf(stderr, "usage: ut_dump FILE [--props|--model] | ut_dump --deps DIR\n");
        return 2;
    }
    try {
        ut::Package p = ut::load_package(argv[1]);
        if (argc == 2) {
            std::printf("version %d licensee %d flags %#x names %zu imports %zu exports %zu\n", p.version, p.licensee,
                        p.flags, p.names.size(), p.imports.size(), p.exports.size());
            return 0;
        }
        if (!std::strcmp(argv[2], "--classes")) {
            for (size_t i = 0; i < p.exports.size(); i++) {
                const ut::Export& e = p.exports[i];
                if (e.size == 0) continue;
                if (e.class_ref == 0) std::printf("%zu %zu\n", i, ut::read_class(p, e).defaults.size());
                else if (ut::object_name(p, e.class_ref) == "LodMesh") {
                    ut::LodMesh m = ut::read_lod_mesh(p, e);
                    std::printf("%zu m %zu %zu %zu\n", i, size_t(m.frame_verts), m.faces.size(), m.wedges.size());
                }
            }
            return 0;
        }
        if (!std::strcmp(argv[2], "--model")) {
            for (size_t i = 0; i < p.exports.size(); i++) {
                const ut::Export& e = p.exports[i];
                if (e.size == 0 || e.class_ref == 0 || ut::object_name(p, e.class_ref) != "Model") continue;
                ut::Model m = ut::read_model(p, e);
                std::printf("%zu %zu %zu %zu %zu %zu %zu %zu %zu %zu\n", i, m.nodes.size(), m.surfs.size(), m.verts.size(),
                            m.points.size(), m.vectors.size(), m.lightmaps.size(), m.light_bits.size(), m.lights.size(),
                            ut::model_end(p, e));
            }
            return 0;
        }
        for (size_t i = 0; i < p.exports.size(); i++) {
            const ut::Export& e = p.exports[i];
            if (e.size == 0 || e.class_ref == 0) continue;
            size_t end = 0;
            size_t n = ut::read_properties(p, e, &end).size();
            std::printf("%zu %zu %zu\n", i, n, end);
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "%s: %s\n", argv[1], ex.what());
        return 1;
    }
}
