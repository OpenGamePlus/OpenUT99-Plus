#include "world.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

#include "level.h"

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
    vm.add_native("SetLocation", [&vm](Obj* self, std::vector<Value*>& a, Value& r) {
        if (Value* p = vm.prop(self, "Location")) std::copy(a[0]->v, a[0]->v + 3, p->v);  // shortcut: no collision test
        r.i = 1;
    });
    vm.add_native("SetRotation", [&vm](Obj* self, std::vector<Value*>& a, Value& r) {
        if (Value* p = vm.prop(self, "Rotation")) std::copy(a[0]->r, a[0]->r + 3, p->r);
        r.i = 1;
    });
    vm.add_native("PlaySound", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no audio yet
    // Animation: the natives set the actor's variables; frames do not advance yet (sequence lengths not read).
    auto play = [&vm](Obj* self, const Value* seq, const Value* rate, bool loop) {
        if (Value* p = vm.prop(self, "AnimSequence")) p->s = seq->s;
        if (Value* p = vm.prop(self, "AnimFrame")) p->f = 0;
        if (Value* p = vm.prop(self, "AnimRate")) p->f = rate ? rate->f : 1.f;
        if (Value* p = vm.prop(self, "bAnimLoop")) p->i = loop;
        if (Value* p = vm.prop(self, "bAnimFinished")) p->i = 0;
    };
    vm.add_native("PlayAnim", [play](Obj* self, std::vector<Value*>& a, Value&) { play(self, a[0], a[1], false); });
    vm.add_native("LoopAnim", [play](Obj* self, std::vector<Value*>& a, Value&) { play(self, a[0], a[1], true); });
    vm.add_native("TweenAnim", [play](Obj* self, std::vector<Value*>& a, Value&) { play(self, a[0], nullptr, false); });
    vm.add_native("IsAnimating", [&vm](Obj* self, std::vector<Value*>&, Value& r) {
        Value* rate = vm.prop(self, "AnimRate");
        r.i = rate && rate->f != 0;
    });
    auto latent_next_tick = [](Obj* self, std::vector<Value*>&, Value&) {  // shortcut: animations end at once
        if (self) self->sleep = 0;
    };
    vm.add_native("FinishAnim", latent_next_tick);
    vm.add_native("FinishInterpolation", latent_next_tick);
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
    vm.add_native("Move", [&vm](Obj* self, std::vector<Value*>& a, Value& r) {
        if (Value* p = vm.prop(self, "Location"))
            for (int k = 0; k < 3; k++) p->v[k] += a[0]->v[k];  // shortcut: no collision, never blocked
        r.i = 1;
    });
    vm.add_native("SetCollisionSize", [&vm](Obj* self, std::vector<Value*>& a, Value& r) {
        if (Value* p = vm.prop(self, "CollisionRadius")) p->f = a[0]->f;  // shortcut: no encroachment test
        if (Value* p = vm.prop(self, "CollisionHeight")) p->f = a[1]->f;
        r.i = 1;
    });
    vm.add_native("FastTrace", [](Obj*, std::vector<Value*>&, Value& r) { r.i = 1; });  // shortcut: no BSP trace yet, always clear
    vm.add_native("describeSpec", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: reach specs not loaded yet
    vm.add_native("SetOwner", [&vm](Obj* self, std::vector<Value*>& a, Value&) {
        if (Value* p = vm.prop(self, "Owner")) p->o = a[0]->o;  // shortcut: GainedChild / LostChild not sent yet
    });
    vm.add_native("MakeNoise", [](Obj*, std::vector<Value*>&, Value&) {});        // shortcut: no AI hearing yet
    vm.add_native("GetSoundDuration", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: 0 s until sounds are read
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
    vm.add_native("TouchingActors", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no collision yet
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
    for (int32_t ref : read_level(*p, *level).actors) {
        if (!ref) continue;
        Obj* o = vm_.object(*p, ref);
        try {
            vm_.instantiate(o);
        } catch (const std::exception& e) {
            errors["load: " + strip(e.what())]++;
            continue;
        }
        if (!level_info) level_info = o;  // the first actor of a level is its LevelInfo
        actors.push_back(o);
    }
    for (Obj* o : actors)
        if (Value* v = vm_.prop(o, "Level")) v->o = level_info;
    Obj* cls = nullptr;
    if (const Value* t = vm_.prop(level_info, "DefaultGameType")) cls = t->o;
    size_t dot = default_game.find('.');
    if (!cls && dot != std::string::npos)
        if (const Package* gp = packages_.get(default_game.substr(0, dot)))
            for (size_t i = 0; i < gp->exports.size() && !cls; i++)
                if (gp->exports[i].class_ref == 0 && gp->exports[i].size && gp->exports[i].name == default_game.substr(dot + 1))
                    cls = vm_.object(*gp, int32_t(i + 1));
    map_actors_ = actors.size();
    if (!cls) return;
    begun_ = true;
    game = spawn(cls, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (Value* v = vm_.prop(level_info, "Game")) v->o = game;
    guarded(game, "InitGame", {Value::of(VT::Str), Value::of(VT::Str)});  // Options, out Error
}

void World::begin_play() {
    // Phase by phase over the map's actors; actors spawned meanwhile get their events at once (unverified).
    begun_ = true;
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
    if (!begun_) return o;
    // unverified order, from public documentation of the actor lifecycle
    for (const char* event : {"Spawned", "PreBeginPlay", "BeginPlay", "PostBeginPlay", "SetInitialState"})
        if (!deleted_.count(o)) guarded(o, event);
    return deleted_.count(o) ? nullptr : o;
}

void World::destroy(Obj* o) {
    if (!o || deleted_.count(o)) return;
    deleted_.insert(o);
    if (Value* v = vm_.prop(o, "bDeleteMe")) v->i = 1;
    guarded(o, "Destroyed");
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
