#include "config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ut {

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

Ini parse_ini(const std::string& text) {
    Ini ini;
    std::map<std::string, std::vector<std::string>>* section = nullptr;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = &ini[line.substr(1, line.size() - 2)];
        } else if (section) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) (*section)[line.substr(0, eq)].push_back(line.substr(eq + 1));
        }
    }
    return ini;
}

Ini read_ini(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_ini(ss.str());
}

}  // namespace ut
