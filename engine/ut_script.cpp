// ut_script DIR Package.Class.Function [ARG...]   run one UnrealScript function in the VM and print its results
// ut_script DIR --run Package.Class [TICKS]       spawn the class, enter its auto state, tick its state code (0.1 s)
// ut_script DIR --states                          every class of System/*.u: auto state run for 30 ticks, outcomes
// ut_script DIR --map MAP [SECONDS]               load a map's actors, begin play, tick (0.05 s); errors and log ranked
// ut_script DIR --maps [SECONDS]                  the same for every map of Maps/, errors summed over all of them
// Arguments are given per parameter (int, float, bool True/False, string, name); out parameters are printed after.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

#include "collision.h"
#include "config.h"
#include "packages.h"
#include "vfs.h"
#include "vm.h"
#include "world.h"

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

int32_t find_class(const ut::Package& p, const std::string& name) {
    for (size_t i = 0; i < p.exports.size(); i++)
        if (p.exports[i].class_ref == 0 && p.exports[i].size > 0 && p.exports[i].name == name) return int32_t(i + 1);
    return 0;
}

// Spawns cls, enters its auto state and ticks it; returns "" or the error that stopped it.
std::string run_states(ut::Vm& vm, ut::Obj* cls, int ticks, bool* has_auto) {
    try {
        ut::Obj* o = vm.spawn(cls);
        *has_auto = vm.goto_state(o, "Auto");
        for (int i = 0; i < ticks && *has_auto; i++) vm.tick(o, 0.1f);
        return "";
    } catch (const std::exception& e) {
        return e.what();
    }
}

void ranked(const std::map<std::string, int>& counts, int limit) {
    std::multimap<int, std::string> top;
    for (const auto& [e, n] : counts) top.emplace(-n, e);
    int shown = 0;
    for (const auto& [n, e] : top)
        if (shown++ < limit) std::printf("%6d  %s\n", -n, e.c_str());
}

int map(ut::Vm& vm, ut::PackageSet& packages, const std::string& name, float seconds, const std::string& game) {
    ut::World w(vm, packages);
    w.load(name, game);
    size_t loaded = w.actors.size();
    w.begin_play();
    int ticks = 0;
    for (; float(ticks) * 0.05f < seconds; ticks++) w.tick(0.05f);
    std::map<std::string, int> lines;
    for (const std::string& l : vm.log) lines[l]++;
    int errors = 0;
    for (const auto& [e, n] : w.errors) errors += n;
    std::printf("%s: %zu actors loaded, game %s, %zu actors after %d ticks; %d script errors, %zu log lines\n",
                name.c_str(), loaded, vm.name_of(w.game ? w.game->cls : nullptr).c_str(), w.actors.size(), ticks, errors,
                vm.log.size());
    std::printf("errors:\n");
    ranked(w.errors, 20);
    std::printf("log:\n");
    ranked(lines, 15);
    return 0;
}

int maps(ut::PackageSet& packages, const std::string& root, float seconds, const std::string& game) {
    std::map<std::string, int> errors;
    int count = 0, failed = 0, falling = 0, in_solid = 0;
    std::map<std::string, int> solid_classes;
    size_t actors = 0;
    for (const auto& d : std::filesystem::directory_iterator(root + "/Maps")) {
        if (d.path().extension() != ".unr") continue;
        ut::Vm vm(&packages);  // fresh objects per map; packages stay loaded
        for (const char* n : {"Core", "Engine"})
            if (const ut::Package* p = packages.get(n)) vm.bind_natives(*p);
        ut::World w(vm, packages);
        count++;
        try {
            w.load(d.path().stem().string(), game);
            w.begin_play();
            for (int t = 0; float(t) * 0.05f < seconds; t++) w.tick(0.05f);
        } catch (const std::exception& e) {
            failed++;
            errors["map failed: " + std::string(e.what())]++;
        }
        actors += w.actors.size();
        for (const auto& [e, n] : w.errors) errors[e] += n;
        for (ut::Obj* o : w.actors) {
            const ut::Value* mode = vm.prop(o, "Physics");
            const ut::Value* loc = vm.prop(o, "Location");
            const ut::Value* collide = vm.prop(o, "bCollideWorld");
            falling += mode && mode->i == 2;
            if (loc && collide && collide->i && w.model() && ut::point_solid(*w.model(), {loc->v[0], loc->v[1], loc->v[2]})) {
                in_solid++;
                solid_classes[vm.name_of(o->cls)]++;
            }
        }
    }
    std::printf("%d maps, %d failed, %zu actors alive at the end: %d still falling, %d colliding actors inside solid\n",
                count, failed, actors, falling, in_solid);
    if (in_solid) {
        std::printf("inside solid, by class:\n");
        ranked(solid_classes, 8);
    }
    std::printf("errors:\n");
    ranked(errors, 25);
    return 0;
}

int states(ut::Vm& vm, ut::PackageSet& packages, const std::string& root) {
    std::map<std::string, int> errors;  // message without the function name -> count
    int classes = 0, autos = 0, failed = 0;
    for (const auto& d : std::filesystem::directory_iterator(root + "/System")) {
        if (d.path().extension() != ".u") continue;
        const ut::Package* p = packages.get(d.path().stem().string());
        if (!p) continue;
        for (size_t i = 0; i < p->exports.size(); i++) {
            if (p->exports[i].class_ref != 0 || p->exports[i].size == 0) continue;
            classes++;
            bool has_auto = false;
            std::string e = run_states(vm, vm.object(*p, int32_t(i + 1)), 30, &has_auto);
            autos += has_auto;
            if (e.empty()) continue;
            failed++;
            size_t colon = e.find(": ");
            errors[colon == std::string::npos ? e : e.substr(colon + 2)]++;
        }
    }
    std::printf("%d classes, %d with an auto state, %d stopped by an error\n", classes, autos, failed);
    std::multimap<int, std::string> top;
    for (const auto& [e, n] : errors) top.emplace(-n, e);
    int shown = 0;
    for (const auto& [n, e] : top)
        if (shown++ < 25) std::printf("%6d  %s\n", -n, e.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ut_script DIR Package.Class.Function [ARG...]\n");
        return 2;
    }
    try {
        std::string root = argv[1], path = argv[2];
        if (path == "--maps") {
            ut::Vfs vfs(root, ut::read_ini(root + "/System/UnrealTournament.ini"));
            ut::PackageSet packages(&vfs);
            ut::Ini ini = ut::read_ini(root + "/System/UnrealTournament.ini");
            const auto& game = ini["Engine.Engine"]["DefaultGame"];
            return maps(packages, root, argc > 3 ? std::strtof(argv[3], nullptr) : 5.f, game.empty() ? "" : game[0]);
        }
        if (path == "--states" || path == "--run" || path == "--map") {
            ut::Vfs vfs(root, ut::read_ini(root + "/System/UnrealTournament.ini"));
            ut::PackageSet packages(&vfs);
            ut::Vm vm(&packages);
            for (const char* n : {"Core", "Engine"})
                if (const ut::Package* p = packages.get(n)) vm.bind_natives(*p);
            ut::World world(vm, packages);  // Actor natives, an empty level
            if (path == "--states") return states(vm, packages, root);
            if (path == "--map") {
                if (argc < 4) throw std::runtime_error("--map needs a map name");
                ut::Ini ini = ut::read_ini(root + "/System/UnrealTournament.ini");
                const auto& game = ini["Engine.Engine"]["DefaultGame"];
                return map(vm, packages, argv[3], argc > 4 ? std::strtof(argv[4], nullptr) : 5.f, game.empty() ? "" : game[0]);
            }
            std::string target = argc > 3 ? argv[3] : "";
            size_t dot = target.find('.');
            const ut::Package* p = dot == std::string::npos ? nullptr : packages.get(target.substr(0, dot));
            int32_t cls = p ? find_class(*p, target.substr(dot + 1)) : 0;
            if (!cls) throw std::runtime_error("class not found: " + target);
            bool has_auto = false;
            std::string e = run_states(vm, vm.object(*p, cls), argc > 4 ? std::atoi(argv[4]) : 30, &has_auto);
            for (const std::string& line : vm.log) std::printf("%s\n", line.c_str());
            if (!has_auto) std::printf("no auto state\n");
            if (!e.empty()) throw std::runtime_error(e);
            return 0;
        }
        size_t a = path.find('.'), b = path.find('.', a + 1);
        if (a == std::string::npos || b == std::string::npos) throw std::runtime_error("expected Package.Class.Function");
        std::string pkg_name = path.substr(0, a), cls = path.substr(a + 1, b - a - 1), fn = path.substr(b + 1);
        ut::Vfs vfs(root, ut::read_ini(root + "/System/UnrealTournament.ini"));
        ut::PackageSet packages(&vfs);
        ut::Vm vm(&packages);
        for (const char* n : {"Core", "Engine"})
            if (const ut::Package* p = packages.get(n)) vm.bind_natives(*p);
        ut::World world(vm, packages);  // Actor natives; the function's object joins this empty world
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
            ret = vm.call(f, world.spawn(vm.object(*p, cls_ref), nullptr, nullptr, nullptr, nullptr, nullptr), args);
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
