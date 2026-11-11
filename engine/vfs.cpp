#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace ut {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

static std::string first(const Ini& ini, const char* key, const char* fallback) {
    auto s = ini.find("Core.System");
    if (s == ini.end()) return fallback;
    auto k = s->second.find(key);
    return k == s->second.end() || k->second.empty() ? fallback : k->second[0];
}

Vfs::Vfs(const std::string& root, const Ini& ini) {
    fs::path system = fs::path(root) / "System";
    auto s = ini.find("Core.System");
    if (s != ini.end() && s->second.count("Paths")) {
        for (const std::string& pattern : s->second.at("Paths")) {
            fs::path p = (system / pattern).lexically_normal();
            std::string ext = lower(p.extension().string());
            if (!fs::is_directory(p.parent_path())) continue;
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(p.parent_path())) files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            for (const fs::path& f : files)
                if (lower(f.extension().string()) == ext) paths_.emplace(lower(f.stem().string()), f.string());
        }
    }
    fs::path cache = (system / first(ini, "CachePath", "../Cache")).lexically_normal();
    std::string ext = first(ini, "CacheExt", ".uxx");
    if (fs::exists(cache / "cache.ini")) {
        Ini index = read_ini((cache / "cache.ini").string());
        for (const auto& [guid, names] : index["Cache"]) {
            fs::path f = cache / (guid + ext);
            if (!names.empty() && fs::exists(f)) paths_.emplace(lower(fs::path(names[0]).stem().string()), f.string());
        }
    }
}

std::string Vfs::find(const std::string& package) const {
    auto it = paths_.find(lower(package));
    return it == paths_.end() ? "" : it->second;
}

}  // namespace ut
