#include "world.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <stdexcept>

#include "level.h"
#include "mesh.h"

namespace ut {

namespace {

std::minstd_rand rng;  // shortcut: not the original generator; replays will differ

float frand() { return float(rng() - rng.min()) / float(rng.max() - rng.min()); }

std::string strip(const std::string& e) {  // "Func: message" -> "message", so errors group across functions
    size_t colon = e.find(": ");
    return colon == std::string::npos ? e : e.substr(colon + 2);
}

}  // namespace

World::World(Vm& vm, PackageSet& packages) : vm_(vm), packages_(packages) {
    register_ai();
    auto set_bool = [&vm](Obj* o, const char* name, const Value* v) {
        if (Value* p = vm.prop(o, name); p && v) p->i = v->i != 0;
    };
    vm.add_native("SetCollision", [set_bool](Obj* self, std::vector<Value*>& a, Value& r) {
        set_bool(self, "bCollideActors", a[0]);
        set_bool(self, "bBlockActors", a[1]);
        set_bool(self, "bBlockPlayers", a[2]);
        (void)r;  // shortcut: no collision hash yet, nothing else changes
    });
    vm.add_native("SetPhysics", [&vm](Obj* self, std::vector<Value*>& a, Value&) {
        if (Value* p = vm.prop(self, "Physics")) p->i = a[0]->i;  // shortcut: no physics runs yet
    });
    vm.add_native("SetTimer", [&vm](Obj* self, std::vector<Value*>& a, Value&) {
        if (Value* p = vm.prop(self, "TimerRate")) p->f = a[0]->f;
        if (Value* p = vm.prop(self, "TimerCounter")) p->f = 0;
        if (Value* p = vm.prop(self, "bTimerLoop")) p->i = a[1]->i != 0;
    });
    vm.add_native("SetLocation", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        Value* p = vm_.prop(self, "Location");
        Value* collide = vm_.prop(self, "bCollideWorld");
        // shortcut: a spot inside solid is refused; the original also nudges actors into free space
        if (!p || (has_model_ && collide && collide->i && point_solid(model_, {a[0]->v[0], a[0]->v[1], a[0]->v[2]}))) return;
        std::copy(a[0]->v, a[0]->v + 3, p->v);
        update_region(self);
        r.i = 1;
    });
    vm.add_native("SetRotation", [&vm](Obj* self, std::vector<Value*>& a, Value& r) {
        if (Value* p = vm.prop(self, "Rotation")) std::copy(a[0]->r, a[0]->r + 3, p->r);
        r.i = 1;
    });
    vm.add_native("PlaySound", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no audio yet
    // Animation (public UE1 documentation, unverified): AnimFrame runs 0..1 over the sequence at
    // AnimRate = Rate * sequence rate / frames; a non-looping animation finishes at AnimLast = 1 - 1 / frames.
    auto play = [this](Obj* self, const Value* name, const Value* rate, bool loop) {
        const LodMesh::AnimSeq* s = seq(self, name->s);
        Value* seqv = vm_.prop(self, "AnimSequence");
        Value* frame = vm_.prop(self, "AnimFrame");
        Value* looping = vm_.prop(self, "bAnimLoop");
        if (!seqv || !frame || !looping) return;
        bool same = loop && looping->i && seqv->s == name->s;  // LoopAnim of the running loop keeps its frame
        seqv->s = name->s;
        if (!same) frame->f = 0;
        looping->i = loop;
        tweens_.erase(self);
        float frames = s && s->frames > 0 ? float(s->frames) : 1.f;
        if (Value* p = vm_.prop(self, "AnimRate")) p->f = s ? (rate ? rate->f : 1.f) * s->rate / frames : 0;
        if (Value* p = vm_.prop(self, "AnimLast")) p->f = 1 - 1 / frames;
        if (Value* p = vm_.prop(self, "bAnimFinished")) p->i = s ? 0 : 1;
    };
    vm.add_native("PlayAnim", [play](Obj* self, std::vector<Value*>& a, Value&) { play(self, a[0], a[1], false); });
    vm.add_native("LoopAnim", [play](Obj* self, std::vector<Value*>& a, Value&) { play(self, a[0], a[1], true); });
    vm.add_native("TweenAnim", [this, play](Obj* self, std::vector<Value*>& a, Value&) {  // shortcut: a pause of Time
        play(self, a[0], nullptr, false);
        if (Value* p = vm_.prop(self, "AnimRate")) p->f = 0;
        if (Value* p = vm_.prop(self, "bAnimFinished")) p->i = 0;
        tweens_[self] = std::max(0.f, a[1]->f);
    });
    vm.add_native("IsAnimating", [this](Obj* self, std::vector<Value*>&, Value& r) { r.i = !anim_done(self); });
    vm.add_native("HasAnim", [this](Obj* self, std::vector<Value*>& a, Value& r) { r.i = seq(self, a[0]->s) != nullptr; });
    vm.add_native("GetAnimGroup", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        const LodMesh::AnimSeq* s = seq(self, a[0]->s);
        r.s = s ? s->group : "None";
    });
    vm.add_native("FinishAnim", [this](Obj* self, std::vector<Value*>&, Value&) {  // latent: until the animation ends
        if (!self) return;
        if (Value* loop = vm_.prop(self, "bAnimLoop")) loop->i = 0;  // a looping animation finishes its round
        self->wait = [this, self] { return anim_done(self); };
    });
    vm.add_native("FinishInterpolation", [](Obj* self, std::vector<Value*>&, Value&) {  // shortcut: no interpolation yet
        if (self) self->sleep = 0;
    });
    vm.add_native("WaitForLanding", [this](Obj* self, std::vector<Value*>&, Value&) {  // latent: until not falling
        if (self) self->wait = [this, self] { return vm_.prop(self, "Physics")->i != 2; };
    });
    // Stats logging (ngStats) writes files: dropped, the engine never writes into the install.
    for (const char* n : {"OpenLog", "CloseLog", "Watermark", "FileLog", "FileFlush", "GetChecksum", "InitialCheck",
                          "ExecuteSilentLogBatcher", "ExecuteWorldLogBatcher", "ExecuteLocalLogBatcher", "BatchLocal",
                          "BrowseRelativeLocalURL", "GetPlayerChecksum", "LogMutator", "GetGMTRef", "GetMapFileName"})
        vm.add_native(n, [](Obj*, std::vector<Value*>&, Value&) {});
    vm.add_native("RotRand", [](Obj*, std::vector<Value*>& a, Value& r) {
        r.r[0] = int32_t(rng() & 0xFFFF), r.r[1] = int32_t(rng() & 0xFFFF);
        r.r[2] = a[0] && a[0]->i ? int32_t(rng() & 0xFFFF) : 0;
    });
    vm.add_native("VRand", [](Obj*, std::vector<Value*>&, Value& r) {
        float z = 2 * frand() - 1, t = 6.2831853f * frand(), s = std::sqrt(1 - z * z);
        r.v[0] = s * std::cos(t), r.v[1] = s * std::sin(t), r.v[2] = z;
    });
    vm.add_native("Destroy", [this](Obj* self, std::vector<Value*>&, Value& r) {
        destroy(self);
        r.i = 1;
    });
    vm.add_native("Spawn", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        r.o = spawn(a[0]->o, self, a[1] ? a[1]->o : nullptr, a[2] ? &a[2]->s : nullptr, a[3], a[4]);
    });
    vm.add_native("Move", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        r.i = !move(self, {a[0]->v[0], a[0]->v[1], a[0]->v[2]}).hit;  // shortcut: no step-up or sliding in Move
    });
    vm.add_native("SetCollisionSize", [&vm](Obj* self, std::vector<Value*>& a, Value& r) {
        if (Value* p = vm.prop(self, "CollisionRadius")) p->f = a[0]->f;  // shortcut: no encroachment test
        if (Value* p = vm.prop(self, "CollisionHeight")) p->f = a[1]->f;
        r.i = 1;
    });
    // FastTrace(vector TraceEnd, optional vector TraceStart): true if the world does not block the line.
    vm.add_native("FastTrace", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        const Value* from = a[1] ? a[1] : vm_.prop(self, "Location");
        r.i = !has_model_ || !from || !trace(model_, {from->v[0], from->v[1], from->v[2]}, {a[0]->v[0], a[0]->v[1], a[0]->v[2]}, {}).hit;
    });
    // Trace(out HitLocation, out HitNormal, TraceEnd, optional TraceStart, optional bTraceActors, optional Extent)
    vm.add_native("Trace", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        const Value* from = a[3] ? a[3] : vm_.prop(self, "Location");
        if (!has_model_ || !from) return;
        Vec3 s0{from->v[0], from->v[1], from->v[2]}, e{a[2]->v[0], a[2]->v[1], a[2]->v[2]};
        Vec3 ext = a[5] ? Vec3{a[5]->v[0], a[5]->v[1], a[5]->v[2]} : Vec3{0, 0, 0};
        Hit h = trace(model_, s0, e, ext);  // shortcut: actors are not traced yet, only the world
        if (!h.hit) return;
        a[0]->v[0] = s0.x + (e.x - s0.x) * h.time, a[0]->v[1] = s0.y + (e.y - s0.y) * h.time, a[0]->v[2] = s0.z + (e.z - s0.z) * h.time;
        a[1]->v[0] = h.normal.x, a[1]->v[1] = h.normal.y, a[1]->v[2] = h.normal.z;
        r.o = level_info;  // the world (BSP) is reported as the LevelInfo
    });
    vm.add_native("describeSpec", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: reach specs not loaded yet
    vm.add_native("SetOwner", [&vm](Obj* self, std::vector<Value*>& a, Value&) {
        if (Value* p = vm.prop(self, "Owner")) p->o = a[0]->o;  // shortcut: GainedChild / LostChild not sent yet
    });
    vm.add_native("MakeNoise", [](Obj*, std::vector<Value*>&, Value&) {});        // shortcut: no AI hearing yet
    vm.add_native("GetSoundDuration", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: 0 s until sounds are read
    vm.add_native("PlayerCanSeeMe", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no player view yet: false
    vm.add_native("GetAddressURL", [](Obj*, std::vector<Value*>&, Value&) {});   // shortcut: no network: ""
    vm.add_native("GetNetworkNumber", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no network: ""
    vm.add_native("AttachDecal", [](Obj*, std::vector<Value*>&, Value&) {});       // shortcut: no decals yet: None
    vm.add_native("SetBase", [&vm](Obj* self, std::vector<Value*>& a, Value&) {
        if (Value* p = vm.prop(self, "Base")) p->o = a[0]->o;  // shortcut: BaseChange events not sent yet
    });
    // Never write into the install (rule: <UT99_DIR> is read-only for us); config writes are dropped.
    vm.add_native("SaveConfig", [](Obj*, std::vector<Value*>&, Value&) {});
    vm.add_native("ConsoleCommand", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no console yet, returns ""
    vm.add_native("GetNextIntDesc", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: .int lists not read yet
    // Pawn list of the level: Level.PawnList -> nextPawn -> ...
    vm.add_native("AddPawn", [this](Obj* self, std::vector<Value*>&, Value&) {
        Value* head = vm_.prop(level_info, "PawnList");
        Value* next = vm_.prop(self, "nextPawn");
        if (!head || !next) return;
        for (Obj* p = head->o; p; p = vm_.prop(p, "nextPawn")->o)
            if (p == self) return;
        next->o = head->o;
        head->o = self;
    });
    vm.add_native("RemovePawn", [this](Obj* self, std::vector<Value*>&, Value&) {
        Value* link = vm_.prop(level_info, "PawnList");
        for (int guard = 0; link && link->o && guard < 100000; guard++) {
            if (link->o == self) {
                link->o = vm_.prop(self, "nextPawn")->o;
                return;
            }
            link = vm_.prop(link->o, "nextPawn");
        }
    });
    // Iterators: fill vm.iterator_items with the actors to visit (a snapshot; shortcut: no visibility or traces).
    auto iterate = [this](std::vector<Value*>& a, auto keep) {
        for (Obj* o : actors)
            if (!deleted_.count(o) && vm_.is_a(o->cls, a[0]->o) && keep(o)) {
                Value v = Value::of(VT::Object);
                v.o = o;
                vm_.iterator_items.push_back(v);
            }
    };
    auto near = [this](Obj* self, std::vector<Value*>& a, Obj* o) {
        const Value* at = a[3] ? a[3] : vm_.prop(self, "Location");
        const Value* loc = vm_.prop(o, "Location");
        if (!at || !loc) return false;
        float d[3] = {loc->v[0] - at->v[0], loc->v[1] - at->v[1], loc->v[2] - at->v[2]};
        return d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= a[2]->f * a[2]->f;
    };
    vm.add_native("AllActors", [this, iterate](Obj*, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return !a[2] || a[2]->s == "None" || vm_.prop(o, "Tag")->s == a[2]->s; });
    });
    vm.add_native("ChildActors", [this, iterate](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return vm_.prop(o, "Owner")->o == self; });
    });
    vm.add_native("BasedActors", [this, iterate](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return vm_.prop(o, "Base")->o == self; });
    });
    vm.add_native("TouchingActors", [this, iterate](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return touching_.count({self, o}) > 0; });
    });
    vm.add_native("TraceActors", [](Obj*, std::vector<Value*>&, Value&) {});     // shortcut: no traces yet
    vm.add_native("RadiusActors", [iterate, near](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return near(self, a, o); });
    });
    vm.add_native("VisibleActors", [this, iterate, near](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return !vm_.prop(o, "bHidden")->i && near(self, a, o); });
    });
    vm.add_native("VisibleCollidingActors", [this, iterate, near](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) { return !vm_.prop(o, "bHidden")->i && vm_.prop(o, "bCollideActors")->i && near(self, a, o); });
    });
    vm.add_native("ZoneActors", [this, iterate](Obj* self, std::vector<Value*>& a, Value&) {
        iterate(a, [&](Obj* o) {
            const Value* region = vm_.prop(o, "Region");
            if (!region || !region->field_names) return false;
            for (size_t k = 0; k < region->fields.size(); k++)
                if ((*region->field_names)[k] == "Zone") return region->fields[k].o == self;
            return false;
        });
    });
}

void World::guarded(Obj* o, const std::string& event, std::vector<Value> args) {
    try {
        vm_.event(o, event, std::move(args));
    } catch (const std::exception& e) {
        errors[strip(e.what())]++;
    }
}

void World::load(const std::string& map, const std::string& default_game) {
    const Package* p = packages_.get(map);
    if (!p) throw std::runtime_error("map not found: " + map);
    const Export* level = nullptr;
    for (const Export& e : p->exports)
        if (e.size && e.class_ref && object_name(*p, e.class_ref) == "Level") level = &e;
    if (!level) throw std::runtime_error("no Level in " + map);
    map_ = p;
    Level lv = read_level(*p, *level);
    if (lv.model > 0 && size_t(lv.model) <= p->exports.size()) {
        model_ = read_model(*p, p->exports[size_t(lv.model - 1)]);
        has_model_ = true;
    }
    for (int32_t ref : lv.actors) {
        if (!ref) continue;
        Obj* o = vm_.object(*p, ref);
        try {
            vm_.instantiate(o);
        } catch (const std::exception& e) {
            errors["load: " + strip(e.what())]++;
            continue;
        }
        if (!level_info) level_info = o;  // the first actor of a level is its LevelInfo
        bool camera = false;  // editor viewpoints: PlayerPawns that would never be ready (unverified removal)
        for (Obj* c = o->cls; c && !camera; c = vm_.super_of(c)) camera = vm_.name_of(c) == "Camera";
        if (camera) continue;
        actors.push_back(o);
    }
    for (Obj* o : actors) {
        if (Value* v = vm_.prop(o, "Level")) v->o = level_info;
        update_region(o);
    }
    for (const Value* n = vm_.prop(level_info, "NavigationPointList"); n && n->o && navs_.size() < 100000;
         n = vm_.prop(n->o, "nextNavigationPoint"))
        navs_.push_back(n->o);
    for (Obj* nav : navs_) {  // NavigationPoint.Paths[16]: indices into the level's reach specs, -1 = none
        const std::vector<Value>* ps = vm_.prop_array(nav, "Paths");
        for (size_t k = 0; ps && k < ps->size(); k++) {
            int32_t i = (*ps)[k].i;
            if (i < 0 || size_t(i) >= lv.reach_specs.size()) continue;
            const ReachSpec& s = lv.reach_specs[size_t(i)];
            if (Obj* to = s.end ? vm_.object(*p, s.end) : nullptr)
                paths_[nav].push_back({to, float(s.distance), s.collision_radius, s.collision_height});
        }
    }
    Obj* cls = nullptr;
    if (const Value* t = vm_.prop(level_info, "DefaultGameType")) cls = t->o;
    size_t dot = default_game.find('.');
    if (!cls && dot != std::string::npos)
        if (const Package* gp = packages_.get(default_game.substr(0, dot)))
            for (size_t i = 0; i < gp->exports.size() && !cls; i++)
                if (gp->exports[i].class_ref == 0 && gp->exports[i].size && gp->exports[i].name == default_game.substr(dot + 1))
                    cls = vm_.object(*gp, int32_t(i + 1));
    if (cls) {
        game = spawn(cls, nullptr, nullptr, nullptr, nullptr, nullptr);  // not begun: no events yet
        if (Value* v = vm_.prop(level_info, "Game")) v->o = game;
    }
    map_actors_ = actors.size();  // the map's actors, then the game
}

void World::begin_play() {
    // Play begins, then InitGame, then each phase over the map's actors and the game; actors spawned from here on
    // get their events at once (unverified order, inferred from what the retail scripts need: the mutator spawned in
    // InitGame must be ready before the map's actors ask it, and Level.Game must be set when the game's
    // PreBeginPlay spawns its GameReplicationInfo).
    begun_ = true;
    if (game) guarded(game, "InitGame", {Value::of(VT::Str), Value::of(VT::Str)});  // Options, out Error
    size_t n = map_actors_;
    for (const char* event : {"PreBeginPlay", "BeginPlay", "PostBeginPlay", "SetInitialState"})
        for (size_t i = 0; i < n; i++)
            if (!deleted_.count(actors[i])) guarded(actors[i], event);
}

Obj* World::spawn(Obj* cls, Obj* spawner, Obj* owner, const std::string* tag, const Value* loc, const Value* rot) {
    if (!cls) return nullptr;
    Obj* o = vm_.spawn(cls);
    if (Value* v = vm_.prop(o, "Level")) v->o = level_info;
    if (Value* v = vm_.prop(o, "Owner")) v->o = owner;  // unverified: an omitted owner stays None
    if (Value* v = vm_.prop(o, "Tag")) v->s = tag && tag->size() && *tag != "None" ? *tag : cls->pkg->exports[size_t(cls->ref - 1)].name;
    const Value* from = spawner ? vm_.prop(spawner, "Location") : nullptr;
    if (Value* v = vm_.prop(o, "Location"); v && (loc || from)) std::copy((loc ? loc : from)->v, (loc ? loc : from)->v + 3, v->v);
    from = spawner ? vm_.prop(spawner, "Rotation") : nullptr;
    if (Value* v = vm_.prop(o, "Rotation"); v && (rot || from)) std::copy((rot ? rot : from)->r, (rot ? rot : from)->r + 3, v->r);
    actors.push_back(o);
    update_region(o);
    if (!begun_) return o;
    // unverified order, from public documentation of the actor lifecycle
    for (const char* event : {"Spawned", "PreBeginPlay", "BeginPlay", "PostBeginPlay", "SetInitialState"})
        if (!deleted_.count(o)) guarded(o, event);
    return deleted_.count(o) ? nullptr : o;
}

void World::touches(Obj* o) {
    // Collision cylinders (bCollideActors) that overlap touch (shortcut: no blocking, Bump or encroachment yet).
    auto f = [this](Obj* x, const char* n) {
        const Value* v = vm_.prop(x, n);
        return v ? v->f : 0.f;
    };
    auto collides = [this](Obj* x) {
        const Value* c = vm_.prop(x, "bCollideActors");
        return c && c->i && !deleted_.count(x);
    };
    auto set_touching = [this](Obj* x, Obj* other, bool add) {  // Actor.Touching[4]
        std::vector<Value>* t = vm_.prop_array(x, "Touching");
        for (size_t k = 0; t && k < t->size(); k++) {
            if (add && !(*t)[k].o) return void((*t)[k].o = other);
            if (!add && (*t)[k].o == other) return void((*t)[k].o = nullptr);
        }
    };
    const Value* lo = vm_.prop(o, "Location");
    if (!lo) return;
    bool mine = collides(o);
    for (Obj* x : actors) {
        if (x == o) continue;
        bool was = touching_.count({o, x}) > 0;
        bool now = false;
        if (mine && collides(x)) {
            const Value* lx = vm_.prop(x, "Location");
            float dx = lx->v[0] - lo->v[0], dy = lx->v[1] - lo->v[1], dz = lx->v[2] - lo->v[2];
            float r = f(o, "CollisionRadius") + f(x, "CollisionRadius"), h = f(o, "CollisionHeight") + f(x, "CollisionHeight");
            now = dx * dx + dy * dy < r * r && std::fabs(dz) < h;
        }
        if (now == was) continue;
        Value vo = Value::of(VT::Object), vx = Value::of(VT::Object);
        vo.o = o, vx.o = x;
        if (now) {
            touching_.insert({o, x}), touching_.insert({x, o});
            set_touching(o, x, true), set_touching(x, o, true);
            guarded(x, "Touch", {vo});
            if (!deleted_.count(o) && !deleted_.count(x)) guarded(o, "Touch", {vx});
        } else {
            touching_.erase({o, x}), touching_.erase({x, o});
            set_touching(o, x, false), set_touching(x, o, false);
            guarded(x, "UnTouch", {vo});
            guarded(o, "UnTouch", {vx});
        }
        if (deleted_.count(o)) return;
    }
}

void World::destroy(Obj* o) {
    if (!o || deleted_.count(o)) return;
    deleted_.insert(o);
    if (Value* v = vm_.prop(o, "bDeleteMe")) v->i = 1;
    guarded(o, "Destroyed");
}

Obj* World::zone_at(Vec3 p, int* number, int* leaf) {
    int32_t n = 0, zone = 0, lf = -1;
    for (int g = 0; has_model_ && n >= 0 && size_t(n) < model_.nodes.size() && g < 4096; g++) {
        const Node& nd = model_.nodes[size_t(n)];
        bool front = nd.plane[0] * p.x + nd.plane[1] * p.y + nd.plane[2] * p.z - nd.plane[3] >= 0;
        zone = nd.zone[front ? 1 : 0];
        lf = nd.leaf[front ? 1 : 0];
        n = front ? nd.front : nd.back;
    }
    if (number) *number = zone;
    if (leaf) *leaf = lf;
    int32_t ref = size_t(zone) < model_.zone_actors.size() ? model_.zone_actors[size_t(zone)] : 0;
    return ref ? vm_.object(*map_, ref) : level_info;
}

void World::update_region(Obj* o) {
    Value* region = vm_.prop(o, "Region");
    Value* loc = vm_.prop(o, "Location");
    if (!region || !loc || !region->field_names) return;
    int number = 0, leaf = -1;
    Obj* zone = zone_at({loc->v[0], loc->v[1], loc->v[2]}, &number, &leaf);
    Obj* old = nullptr;
    for (size_t k = 0; k < region->fields.size(); k++) {  // PointRegion: Zone, iLeaf, ZoneNumber
        const std::string& f = (*region->field_names)[k];
        Value& v = region->fields[k];
        if (f == "Zone") old = v.o, v.o = zone;
        else if (f == "iLeaf") v.i = leaf;
        else if (f == "ZoneNumber") v.i = number & 0xFF;
    }
    if (old != zone && begun_) {
        Value z = Value::of(VT::Object);
        z.o = zone;
        guarded(o, "ZoneChange", {z});  // unverified: the original also tells the zones (ActorEntered / ActorLeaving)
    }
}

Hit World::move(Obj* o, Vec3 d) {
    Value* loc = vm_.prop(o, "Location");
    if (!loc) return {};
    Vec3 from{loc->v[0], loc->v[1], loc->v[2]};
    Value* collide = vm_.prop(o, "bCollideWorld");
    Hit h;
    if (has_model_ && collide && collide->i) {
        Value* r = vm_.prop(o, "CollisionRadius");
        Value* hh = vm_.prop(o, "CollisionHeight");
        Vec3 ext{r ? r->f : 0, r ? r->f : 0, hh ? hh->f : 0};  // shortcut: the collision cylinder as a box
        h = trace(model_, from, {from.x + d.x, from.y + d.y, from.z + d.z}, ext);
        Vec3 to{from.x + d.x * h.time, from.y + d.y * h.time, from.z + d.z * h.time};
        // shortcut: expanded planes (no bevels) can let a box sink at convex edges; the exact point test refuses a
        // move that takes the centre from empty space into solid. Upgrade with bevel planes.
        if (!point_solid(model_, from) && point_solid(model_, to)) {
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            Hit blocked;
            blocked.hit = true, blocked.time = 0;
            blocked.normal = len > 0 ? Vec3{-d.x / len, -d.y / len, -d.z / len} : Vec3{0, 0, 1};
            return blocked;
        }
    }
    loc->v[0] += d.x * h.time, loc->v[1] += d.y * h.time, loc->v[2] += d.z * h.time;
    return h;
}

void World::physics(Obj* o, float dt) {
    // Physics modes (Actor.EPhysics): 1 walking, 2 falling, 4 flying, 5 rotating, 6 projectile.
    Value* mode = vm_.prop(o, "Physics");
    Value* vel = vm_.prop(o, "Velocity");
    Value* is_static = vm_.prop(o, "bStatic");
    if (!mode || !vel || mode->i == 0 || (is_static && is_static->i)) return;  // unverified: static actors never move
    float* v = vel->v;
    auto hit_value = [&](const Hit& h) {
        Value n = Value::of(VT::Vector);
        n.v[0] = h.normal.x, n.v[1] = h.normal.y, n.v[2] = h.normal.z;
        return n;
    };
    auto wall = [&](const Hit& h) {
        Value w = Value::of(VT::Object);
        w.o = level_info;
        guarded(o, "HitWall", {hit_value(h), w});
    };
    switch (mode->i) {
        case 5:  // rotating: RotationRate per second
            if (Value* rot = vm_.prop(o, "Rotation"); Value* rate = vm_.prop(o, "RotationRate"))
                for (int k = 0; k < 3; k++) rot->r[k] += int32_t(float(rate->r[k]) * dt);
            return;
        case 2: {  // falling: zone gravity, land on floors (normal z >= 0.7, unverified), slide off walls
            Value* loc = vm_.prop(o, "Location");
            Obj* zone = loc ? zone_at({loc->v[0], loc->v[1], loc->v[2]}, nullptr, nullptr) : level_info;
            Value* g = vm_.prop(zone, "ZoneGravity");
            Value* term = vm_.prop(zone, "ZoneTerminalVelocity");
            for (int k = 0; k < 3; k++) v[k] += (g ? g->v[k] : (k == 2 ? -950.f : 0.f)) * dt;
            float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (term && term->f > 0 && speed > term->f)
                for (int k = 0; k < 3; k++) v[k] *= term->f / speed;
            Hit h = move(o, {v[0] * dt, v[1] * dt, v[2] * dt});
            if (!h.hit) return;
            if (h.normal.z >= 0.7f) {
                guarded(o, "Landed", {hit_value(h)});
                if (mode->i == 2) {  // Landed did not change physics
                    Value* pawn = vm_.prop(o, "bIsPawn");
                    mode->i = pawn && pawn->i ? 1 : 0;
                    v[2] = 0;
                    if (mode->i == 0) v[0] = v[1] = 0;
                }
            } else {
                wall(h);
                float dot = v[0] * h.normal.x + v[1] * h.normal.y + v[2] * h.normal.z;
                v[0] -= dot * h.normal.x, v[1] -= dot * h.normal.y, v[2] -= dot * h.normal.z;
            }
            return;
        }
        case 6: {  // projectile
            Hit h = move(o, {v[0] * dt, v[1] * dt, v[2] * dt});
            if (h.hit) wall(h);
            return;
        }
        case 4: {  // flying: slide along what it hits
            Hit h = move(o, {v[0] * dt, v[1] * dt, v[2] * dt});
            if (h.hit) {
                wall(h);
                float dot = v[0] * h.normal.x + v[1] * h.normal.y + v[2] * h.normal.z;
                v[0] -= dot * h.normal.x, v[1] -= dot * h.normal.y, v[2] -= dot * h.normal.z;
            }
            return;
        }
        case 1:  // walking (world_ai.cpp)
            walk(o, dt);
            return;
        default: return;  // interpolating, moving brush, spider, trailer...: not yet
    }
}

const LodMesh::AnimSeq* World::seq(Obj* o, const std::string& name) {
    const Value* mesh = vm_.prop(o, "Mesh");
    Obj* m = mesh ? mesh->o : nullptr;
    if (!m || !m->pkg) return nullptr;
    auto it = meshes_.find(m);
    if (it == meshes_.end()) {
        std::unique_ptr<LodMesh> lod;
        if (vm_.name_of(vm_.class_of(m)) == "LodMesh") {
            try {
                lod = std::make_unique<LodMesh>(read_lod_mesh(*m->pkg, m->pkg->exports[size_t(m->ref - 1)]));
            } catch (const std::exception& e) {
                errors["mesh: " + strip(e.what())]++;
            }
        }  // shortcut: vertex-animated Mesh and SkeletalMesh sequences not read yet
        it = meshes_.emplace(m, std::move(lod)).first;
    }
    if (!it->second) return nullptr;
    for (const LodMesh::AnimSeq& s : it->second->seqs)
        if (s.name.size() == name.size() && std::equal(s.name.begin(), s.name.end(), name.begin(),
                                                       [](char x, char y) { return std::tolower(x) == std::tolower(y); }))
            return &s;
    return nullptr;
}

bool World::anim_done(Obj* o) {
    if (tweens_.count(o)) return false;
    const Value* finished = vm_.prop(o, "bAnimFinished");
    const Value* rate = vm_.prop(o, "AnimRate");
    const Value* loop = vm_.prop(o, "bAnimLoop");
    return !finished || finished->i || !rate || rate->f == 0 || (loop && loop->i);
}

void World::animate(Obj* o, float dt) {
    if (auto t = tweens_.find(o); t != tweens_.end()) {
        if ((t->second -= dt) > 0) return;
        tweens_.erase(t);
        if (Value* p = vm_.prop(o, "bAnimFinished")) p->i = 1;
        guarded(o, "AnimEnd");
        return;
    }
    Value* rate = vm_.prop(o, "AnimRate");
    Value* frame = vm_.prop(o, "AnimFrame");
    Value* finished = vm_.prop(o, "bAnimFinished");
    if (!rate || !frame || !finished || rate->f == 0 || finished->i) return;
    frame->f += std::fabs(rate->f) * dt;  // shortcut: negative (velocity-scaled) rates run at their magnitude
    if (vm_.prop(o, "bAnimLoop")->i) {
        if (frame->f >= 1) {
            frame->f -= std::floor(frame->f);
            guarded(o, "AnimEnd");
        }
    } else if (frame->f >= vm_.prop(o, "AnimLast")->f) {
        frame->f = vm_.prop(o, "AnimLast")->f;
        finished->i = 1;
        guarded(o, "AnimEnd");
    }
}

void World::tick(float dt) {
    time += dt;
    if (Value* t = vm_.prop(level_info, "TimeSeconds")) t->f = time;
    Value delta = Value::of(VT::Float);
    delta.f = dt;
    for (size_t i = 0; i < actors.size(); i++) {  // index loop: spawning appends
        Obj* o = actors[i];
        if (deleted_.count(o)) continue;
        guarded(o, "Tick", {delta});
        Value* rate = vm_.prop(o, "TimerRate");
        Value* counter = vm_.prop(o, "TimerCounter");
        if (rate && counter && rate->f > 0 && (counter->f += dt) >= rate->f) {
            Value* loop = vm_.prop(o, "bTimerLoop");
            if (loop && loop->i) counter->f -= rate->f;
            else rate->f = 0, counter->f = 0;  // unverified: what a one-shot timer leaves in TimerCounter
            guarded(o, "Timer");
        }
        if (deleted_.count(o)) continue;
        const Value* mode = vm_.prop(o, "Physics");
        bool moving = mode && mode->i != 0;
        physics(o, dt);
        update_region(o);
        if (moving && !deleted_.count(o)) touches(o);
        animate(o, dt);
        if (deleted_.count(o)) continue;
        try {
            vm_.tick(o, dt);
        } catch (const std::exception& e) {
            errors[strip(e.what())]++;
            o->code_active = false;
        }
    }
    actors.erase(std::remove_if(actors.begin(), actors.end(), [this](Obj* o) { return deleted_.count(o) > 0; }),
                 actors.end());
}

}  // namespace ut
