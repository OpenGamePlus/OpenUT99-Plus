// Actor world: a map's actors as live VM objects, play start, ticking, timers, Actor natives (ADR 0006).
// Spec: docs/formats/script.md (execution); behaviour marked unverified there until compared with the original.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "collision.h"
#include "level.h"
#include "mesh.h"
#include "packages.h"
#include "vm.h"

namespace ut {

class World {
public:
    World(Vm& vm, PackageSet& packages);  // registers the Actor natives on vm
    // Every actor of the map's Level becomes a live instance; then the game class (LevelInfo.DefaultGameType, else
    // default_game "Package.Class") is spawned as Level.Game (InitGame and play events come in begin_play).
    void load(const std::string& map, const std::string& default_game = "");
    void begin_play();  // InitGame, then PreBeginPlay, BeginPlay, PostBeginPlay, SetInitialState phase by phase
    void tick(float dt);                   // Level.TimeSeconds, Tick events, timers, state code
    Obj* spawn(Obj* cls, Obj* spawner, Obj* owner, const std::string* tag, const Value* loc, const Value* rot);

    std::vector<Obj*> actors;
    Obj* level_info = nullptr;
    Obj* game = nullptr;
    float time = 0;
    std::map<std::string, int> errors;  // script errors (without the function name) -> count; the world goes on
    const Model* model() const { return has_model_ ? &model_ : nullptr; }
    void set_model(Model m) { model_ = std::move(m), has_model_ = true; }  // load() does it; for tests
    // ZoneInfo of the BSP leaf holding p (the LevelInfo for zone 0); also its zone number and leaf if asked.
    Obj* zone_at(Vec3 p, int* number, int* leaf);
    void update_region(Obj* o);  // Region (Zone, iLeaf, ZoneNumber) from Location; ZoneChange when it changes

private:
    void guarded(Obj* o, const std::string& event, std::vector<Value> args = {});
    void destroy(Obj* o);
    void physics(Obj* o, float dt);
    Hit move(Obj* o, Vec3 delta);  // sweeps the actor's collision box (if it collides with the world)
    const LodMesh::AnimSeq* seq(Obj* o, const std::string& name);  // sequence of the actor's mesh
    // AI (world_ai.cpp): path network from the map's reach specs, routes, latent moves.
    void register_ai();
    bool reachable(Obj* pawn, Vec3 dest);
    // Route from the pawn to the nearest goal: fills Pawn.RouteCache, returns the first step (nullptr: no route).
    // weight(nav, distance) > 0 marks a goal; the best weight wins.
    Obj* route(Obj* pawn, const std::function<float(Obj*, float)>& weight, float* best);
    void walk(Obj* o, float dt);
    struct Edge {
        Obj* to;
        float distance;
        int radius, height;
    };
    std::map<Obj*, std::vector<Edge>> paths_;
    std::map<Obj*, float> speed_;  // MoveTo speed factor of a pawn
    void touches(Obj* o);  // Touch / UnTouch of a moving actor against the actors it overlaps
    std::set<std::pair<Obj*, Obj*>> touching_;  // both orders
    std::vector<Obj*> navs_;
    bool anim_done(Obj* o);
    void animate(Obj* o, float dt);  // advances AnimFrame, AnimEnd when a sequence (or a loop round) ends
    std::map<const Obj*, std::unique_ptr<LodMesh>> meshes_;  // by mesh object; nullptr if not a readable LodMesh
    std::map<Obj*, float> tweens_;  // TweenAnim time left

    const Package* map_ = nullptr;
    Model model_;
    bool has_model_ = false;

    Vm& vm_;
    PackageSet& packages_;
    std::set<Obj*> deleted_;
    bool begun_ = false;    // false while the map's actors load: they get their play events in begin_play
    size_t map_actors_ = 0;  // the actors loaded from the map, first in `actors`
};

}  // namespace ut
