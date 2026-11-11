// ut_script DIR Package.Class.Function [ARG...]   run one UnrealScript function in the VM and print its results
// Arguments are given per parameter (int, float, bool True/False, string, name); out parameters are printed after.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

#include "config.h"
#include "packages.h"
#include "vfs.h"
#include "vm.h"

namespace {

std::string show(const ut::Value& v) {
    switch (v.t) {
        case ut::VT::Float: return std::to_string(v.f);
        case ut::VT::Bool: return v.i ? "True" : "False";
        case ut::VT::Str: return "\"" + v.s + "\"";
        case ut::VT::Name: return "'" + v.s + "'";
        case ut::VT::Vector: return std::to_string(v.v[0]) + "," + std::to_string(v.v[1]) + "," + std::to_string(v.v[2]);
        case ut::VT::Rotator: return std::to_string(v.r[0]) + "," + std::to_string(v.r[1]) + "," + std::to_string(v.r[2]);
        case ut::VT::None: return "(none)";
        default: return std::to_string(v.i);
    }
}

ut::Value parse(const ut::Local& l, const std::string& s) {
    ut::Value v = ut::Value::of(l.type);
    if (l.type == ut::VT::Float) v.f = std::strtof(s.c_str(), nullptr);
    else if (l.type == ut::VT::Bool) v.i = s == "True" || s == "true" || s == "1";
    else if (l.type == ut::VT::Str || l.type == ut::VT::Name) v.s = s;
    else v.i = int32_t(std::strtol(s.c_str(), nullptr, 10));
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ut_script DIR Package.Class.Function [ARG...]\n");
        return 2;
    }
    try {
        std::string root = argv[1], path = argv[2];
        size_t a = path.find('.'), b = path.find('.', a + 1);
        if (a == std::string::npos || b == std::string::npos) throw std::runtime_error("expected Package.Class.Function");
        std::string pkg_name = path.substr(0, a), cls = path.substr(a + 1, b - a - 1), fn = path.substr(b + 1);
        ut::Vfs vfs(root, ut::read_ini(root + "/System/UnrealTournament.ini"));
        ut::PackageSet packages(&vfs);
        ut::Vm vm(&packages);
        for (const char* n : {"Core", "Engine"})
            if (const ut::Package* p = packages.get(n)) vm.bind_natives(*p);
        const ut::Package* p = packages.get(pkg_name);
        if (!p) throw std::runtime_error("package not found: " + pkg_name);
        int32_t cls_ref = 0, fn_ref = 0;
        for (size_t i = 0; i < p->exports.size(); i++)
            if (p->exports[i].class_ref == 0 && p->exports[i].name == cls) cls_ref = int32_t(i + 1);
        for (size_t i = 0; i < p->exports.size() && cls_ref; i++)
            if (p->exports[i].package == cls_ref && p->exports[i].name == fn) fn_ref = int32_t(i + 1);
        if (!fn_ref) throw std::runtime_error("function not found: " + path);
        const ut::Function& f = vm.function(*p, fn_ref);
        std::vector<ut::Value> args;
        int next = 3;
        for (const ut::Local& l : f.locals)
            if ((l.flags & ut::kParm) && !(l.flags & ut::kReturnParm))
                args.push_back(next < argc ? parse(l, argv[next++]) : ut::Value::of(l.type));
        ut::Value ret;
        std::string error;
        try {
            ret = vm.call(f, vm.spawn(vm.object(*p, cls_ref)), args);
        } catch (const std::exception& e) {
            error = e.what();
        }
        for (const std::string& line : vm.log) std::printf("%s\n", line.c_str());
        if (!error.empty()) throw std::runtime_error(error);
        if (ret.t != ut::VT::None) std::printf("return %s\n", show(ret).c_str());
        size_t k = 0;
        for (const ut::Local& l : f.locals)
            if ((l.flags & ut::kParm) && !(l.flags & ut::kReturnParm)) {
                if (l.flags & ut::kOutParm) std::printf("out %s = %s\n", l.name.c_str(), show(args[k]).c_str());
                k++;
            }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ut_script: %s\n", e.what());
        return 1;
    }
}
