// Package lookup: [Core.System] Paths, then the download cache (cache.ini). Reference: tools/uini.py package_paths
#pragma once
#include <map>
#include <string>

#include "config.h"

namespace ut {

class Vfs {
public:
    Vfs(const std::string& root, const Ini& ini);  // root = install directory (holds System/)
    std::string find(const std::string& package) const;  // case-insensitive; "" if not found
    const std::map<std::string, std::string>& packages() const { return paths_; }

private:
    std::map<std::string, std::string> paths_;  // lower-case name -> path
};

}  // namespace ut
