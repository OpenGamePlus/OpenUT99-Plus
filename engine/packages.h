// Loaded packages and cross-package object references. Spec: docs/formats/package.md (object references)
#pragma once
#include <map>
#include <string>

#include "upkg.h"
#include "vfs.h"

namespace ut {

struct Object {
    const Package* package = nullptr;
    const Export* exp = nullptr;
};

class PackageSet {
public:
    explicit PackageSet(const Vfs* vfs) : vfs_(vfs) {}
    void add(const std::string& name, Package p);  // for tests and preloaded maps
    const Package* get(const std::string& name);  // loads through the Vfs on first use; nullptr if not found
    // Export behind an object reference of package `from`; imports are matched by name, class and group path.
    Object resolve(const Package& from, int32_t ref);

private:
    const Vfs* vfs_;
    std::map<std::string, Package> loaded_;  // lower-case name
};

}  // namespace ut
