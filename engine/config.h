// UT .ini / .int files. Spec: docs/formats/ini.md; reference: tools/uini.py
#pragma once
#include <map>
#include <string>
#include <vector>

namespace ut {

// section -> key -> values in file order (keys like Paths repeat).
using Ini = std::map<std::string, std::map<std::string, std::vector<std::string>>>;

Ini parse_ini(const std::string& text);
Ini read_ini(const std::string& path);  // throws std::runtime_error if the file cannot be opened

}  // namespace ut
