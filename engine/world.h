// Actor world: a map's actors as live VM objects, play start, ticking, timers, Actor natives (ADR 0006).
// Spec: docs/formats/script.md (execution); behaviour marked unverified there until compared with the original.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "packages.h"
#include "vm.h"

namespace ut {

class World {
public:
    World(Vm& vm, PackageSet& packages);  // registers the Actor natives on vm
    // Every actor of the map's Level becomes a live instance; then the game class (LevelInfo.DefaultGameType, else
    // default_game "Package.Class") is spawned as Level.Game and gets InitGame. The game and what it spawns get
    // their play events at once (unverified; needed so mutators are ready before the map's actors ask them).
    void load(const std::string& map, const std::string& default_game = "");
    void begin_play();                     // PreBeginPlay, BeginPlay, PostBeginPlay, SetInitialState, phase by phase
    void tick(float dt);                   // Level.TimeSeconds, Tick events, timers, state code
    Obj* spawn(Obj* cls, Obj* spawner, Obj* owner, const std::string* tag, const Value* loc, const Value* rot);

    std::vector<Obj*> actors;
    Obj* level_info = nullptr;
    Obj* game = nullptr;
    float time = 0;
    std::map<std::string, int> errors;  // script errors (without the function name) -> count; the world goes on

private:
    void guarded(Obj* o, const std::string& event, std::vector<Value> args = {});
    void destroy(Obj* o);

    Vm& vm_;
    PackageSet& packages_;
    std::set<Obj*> deleted_;
    bool begun_ = false;    // false while the map's actors load: they get their play events in begin_play
    size_t map_actors_ = 0;  // the actors loaded from the map, first in `actors`
};

}  // namespace ut
