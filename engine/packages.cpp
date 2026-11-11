#include "packages.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace ut {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

void PackageSet::add(const std::string& name, Package p) { loaded_[lower(name)] = std::move(p); }

const Package* PackageSet::get(const std::string& name) {
    std::string key = lower(name);
    auto it = loaded_.find(key);
    if (it != loaded_.end()) return &it->second;
    std::string path = vfs_ ? vfs_->find(name) : "";
    if (path.empty()) return nullptr;
    return &(loaded_[key] = load_package(path));
}

// Names of the outer chain of an export, innermost first.
static std::vector<std::string> export_groups(const Package& p, const Export& e) {
    std::vector<std::string> g;
    for (int32_t o = e.package; o > 0 && size_t(o) <= p.exports.size() && g.size() < 16; o = p.exports[size_t(o - 1)].package)
        g.push_back(lower(p.exports[size_t(o - 1)].name));
    return g;
}

Object PackageSet::resolve(const Package& from, int32_t ref) {
    if (ref > 0) return size_t(ref) <= from.exports.size() ? Object{&from, &from.exports[size_t(ref - 1)]} : Object{};
    if (ref == 0 || size_t(-ref) > from.imports.size()) return {};
    const Import& im = from.imports[size_t(-ref - 1)];
    std::vector<std::string> groups;  // innermost first
    std::string top;
    for (int32_t o = im.package; o != 0;) {
        if (o > 0 || size_t(-o) > from.imports.size() || groups.size() > 16) return {};
        const Import& outer = from.imports[size_t(-o - 1)];
        if (outer.package == 0) {
            top = outer.name;
            break;
        }
        groups.push_back(lower(outer.name));
        o = outer.package;
    }
    if (top.empty()) return {};  // the import is itself a top-level package
    const Package* p = get(top);
    if (!p) return {};
    std::string name = lower(im.name), cls = lower(im.class_name);
    const Export* fallback = nullptr;
    for (const Export& e : p->exports) {
        if (lower(e.name) != name || e.class_ref == 0 || lower(object_name(*p, e.class_ref)) != cls) continue;
        if (export_groups(*p, e) == groups) return {p, &e};
        if (!fallback) fallback = &e;
    }
    return fallback ? Object{p, fallback} : Object{};  // shortcut: group mismatch falls back to the first name+class match
}

}  // namespace ut
