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

// What the keys and mouse say this tick, in the retail default bindings' units (User.ini aliases).
struct PlayerInput {
    float forward = 0;               // aBaseY: +300 forward, -300 back
    float strafe = 0;                // aStrafe: +300 right, -300 left
    float turn = 0, look = 0;        // aBaseX (+-150) / aLookUp (+-100): keyboard turning
    float mouse_x = 0, mouse_y = 0;  // aMouseX / aMouseY: pixels this tick x 6 (Axis aMouseX Speed=6.0)
    bool fire = false, alt_fire = false, duck = false;  // bFire / bAltFire / bDuck while held
    bool jump = false;               // pressed this tick: exec Jump
    int weapon = 0;                  // 1-10 pressed this tick: exec SwitchWeapon(N); 0 none
    std::string command;             // an exec function without arguments to call this tick (e.g. "Suicide")
};
// A fixed 20 s of play for headless checks: fire (starts the match), forward, strafe right, hold fire, Suicide at
// 10 s, fire (respawn), forward. t: match time at the start of the tick.
PlayerInput scripted_input(float t, float dt);

class World {
public:
    World(Vm& vm, PackageSet& packages);  // registers the Actor natives on vm
    // Every actor of the map's Level becomes a live instance; then the game class (LevelInfo.DefaultGameType, else
    // default_game "Package.Class") is spawned as Level.Game (InitGame and play events come in begin_play).
    void load(const std::string& map, const std::string& default_game = "");
    // InitGame(options: the URL's "?Key=Value" part, e.g. ?mutator=Pkg.Class), then PreBeginPlay, BeginPlay,
    // PostBeginPlay, SetInitialState phase by phase.
    void begin_play(const std::string& options = "");
    void tick(float dt);                   // Level.TimeSeconds, Tick events, timers, state code
    Obj* spawn(Obj* cls, Obj* spawner, Obj* owner, const std::string* tag, const Value* loc, const Value* rot);

    std::vector<Obj*> actors;
    Obj* level_info = nullptr;
    Obj* game = nullptr;
    float time = 0;
    std::map<std::string, int> errors;  // script errors (without the function name) -> count; the world goes on
    bool destroyed(Obj* o) const { return deleted_.count(o) > 0; }  // Destroy()ed: still listed, no longer in play
    const Model* model() const { return has_model_ ? &model_ : nullptr; }
    void set_model(Model m) { model_ = std::move(m), has_model_ = true; }  // load() does it; for tests
    // ZoneInfo of the BSP leaf holding p (the LevelInfo for zone 0); also its zone number and leaf if asked.
    Obj* zone_at(Vec3 p, int* number, int* leaf);
    // Region (Zone, iLeaf, ZoneNumber) from Location, ZoneChange when it changes; pawns also FootRegion / HeadRegion.
    void update_region(Obj* o);
    void set_region(Obj* o, const char* var, Vec3 at, const char* event);

    // The local player (world_player.cpp). login: Level.Game's Login + PostLogin, after begin_play; options as in a
    // URL ("?Name=Player?Class=Botpack.TMale1"). Returns the pawn, or nullptr (the reason in errors["login: ..."]).
    Obj* login(const std::string& options, const std::string& player_class);
    Obj* player = nullptr;  // the logged-in pawn; nullptr when none or destroyed
    PlayerInput input;      // read by tick(); the one-tick fields (jump, weapon, mouse) are cleared after
    // The player's PlayerCalcView: camera location and rotation (Unreal units); false without a player.
    bool player_view(Vec3* location, int32_t rotation[3]);

private:
    void guarded(Obj* o, const std::string& event, std::vector<Value> args = {});
    Obj* find_class(const std::string& pkg_dot_class);  // "Package.Class" -> class object, nullptr if none
    void player_tick(float dt);  // input variables, exec commands, PlayerInput, PlayerTick

    void destroy(Obj* o);
    void physics(Obj* o, float dt);
    // Sweeps the actor's collision box (if it collides with the world); *what = what it hit (LevelInfo = the BSP).
    Hit move(Obj* o, Vec3 delta, Obj** what = nullptr);
    // The level BSP and every blocking mover brush; *what = the LevelInfo, a mover or nullptr.
    Hit trace_world(Obj* self, Vec3 a, Vec3 b, Vec3 extent, Obj** what = nullptr);
    // The first actor blocking o's move a-b before h->time; then updates *h (hit, time, normal).
    Obj* blocking_actor(Obj* o, Vec3 a, Vec3 b, Hit* h);
    void bump(Obj* o, Obj* other);  // Bump on both
    Hit trace_brush(Obj* mover, Vec3 a, Vec3 b, Vec3 extent);  // one mover alone (no hit if it does not block)
    void move_brush(Obj* o, float dt);   // PHYS_MovingBrush: InterpolateTo's keyframe glide, carries what stands on it
    void set_base(Obj* o, Obj* base);    // Base, StandingCount, Detach / Attach / BaseChange
    struct Brush {
        Model model;     // the mover's BSP in its local space
        float radius;    // of its points around the local origin, for a quick rejection
    };
    std::map<Obj*, Brush> movers_;
    void add_mover(Obj* o);  // reads its Brush model (throws on bad data)
    const LodMesh::AnimSeq* seq(Obj* o, const std::string& name);  // sequence of the actor's mesh
    // AI (world_ai.cpp): path network from the map's reach specs, routes, latent moves.
    void register_ai();
    bool reachable(Obj* pawn, Vec3 dest);
    // Route from the pawn to the nearest goal: fills Pawn.RouteCache, returns the first step (nullptr: no route).
    // weight(nav, distance) > 0 marks a goal; the best weight wins.
    Obj* route(Obj* pawn, const std::function<float(Obj*, float)>& weight, float* best);
    void walk(Obj* o, float dt);
    void sight(Obj* o, float dt);  // SeePlayer / EnemyNotVisible
    // Closest actor hit by the segment a-b before *t_hit (fraction); updates *t_hit and *normal.
    Obj* trace_actors(Obj* self, Vec3 a, Vec3 b, float* t_hit, Vec3* normal, const std::set<Obj*>* skip = nullptr);
    std::set<std::pair<Obj*, Obj*>> seen_;  // (viewer, seen) last time it looked
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
