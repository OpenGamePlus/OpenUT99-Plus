// ut_dump FILE            summary line, same text as tools/upkg.py
// ut_dump FILE --props    per non-class export: "<index> <property count> <end offset>", compared by tools/compare_engine.py
#include <cstdio>
#include <cstring>
#include <exception>

#include "upkg.h"

int main(int argc, char** argv) {
    if (argc < 2 || (argc == 3 && std::strcmp(argv[2], "--props") != 0) || argc > 3) {
        std::fprintf(stderr, "usage: ut_dump FILE [--props]\n");
        return 2;
    }
    try {
        ut::Package p = ut::load_package(argv[1]);
        if (argc == 2) {
            std::printf("version %d licensee %d flags %#x names %zu imports %zu exports %zu\n", p.version, p.licensee,
                        p.flags, p.names.size(), p.imports.size(), p.exports.size());
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
