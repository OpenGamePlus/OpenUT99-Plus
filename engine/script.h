// Script objects: bytecode walker and class objects (defaults). Spec: docs/formats/script.md; reference: tools/uscript.py
#pragma once
#include <cstdint>
#include <vector>

#include "upkg.h"

namespace ut {

// Walks bytecode at pos until its in-memory size reaches mem_size; returns the end offset. Throws on unknown tokens
// or if the size does not land on a token boundary.
size_t skip_script(const Package& p, size_t pos, uint32_t mem_size);

struct ClassInfo {
    int32_t super_ref = 0;  // object ref of the parent class
    std::vector<Property> defaults;
};

ClassInfo read_class(const Package& p, const Export& cls);  // export with class_ref 0

}  // namespace ut
