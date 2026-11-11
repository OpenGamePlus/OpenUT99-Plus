// World: pawn walking and the AI natives (paths, routes, latent moves). Behaviour from the native declarations in
// Engine.Pawn and public documentation; every rule here is unverified against the original (see debt.md).
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <set>

#include "world.h"

namespace ut {

namespace {

std::minstd_rand ai_rng;  // shortcut: not the original generator

Vec3 vec_of(const Value* v) { return v ? Vec3{v->v[0], v->v[1], v->v[2]} : Vec3{0, 0, 0}; }

float dist(Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

float dist2d(Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

int32_t yaw_to(Vec3 from, Vec3 to) {
    return int32_t(std::atan2(to.y - from.y, to.x - from.x) * 32768.f / 3.14159265f) & 0xFFFF;
}

}  // namespace

bool World::reachable(Obj* pawn, Vec3 dest) {
    // shortcut: a clear line and no climb higher than a step + jump (120, unverified); the original walks the path
    Vec3 from = vec_of(vm_.prop(pawn, "Location"));
    if (dest.z - from.z > 120) return false;
    return !has_model_ || !trace(model_, from, dest, {0, 0, 0}).hit;
}

Obj* World::route(Obj* pawn, const std::function<float(Obj*, float)>& weight, float* best) {
    Vec3 at = vec_of(vm_.prop(pawn, "Location"));
    const Value* rv = vm_.prop(pawn, "CollisionRadius");
    const Value* hv = vm_.prop(pawn, "CollisionHeight");
    float radius = rv ? rv->f : 0, height = hv ? hv->f : 0;
    std::map<Obj*, float> d;
    std::map<Obj*, Obj*> prev;
    using Item = std::pair<float, Obj*>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (Obj* n : navs_) {  // entry points: navigation points the pawn can walk to directly
        Vec3 p = vec_of(vm_.prop(n, "Location"));
        float s = dist(at, p);
        if (s < 1200 && reachable(pawn, p)) d[n] = s, open.push({s, n});
    }
    Obj* goal = nullptr;
    std::set<Obj*> done;
    while (!open.empty()) {
        auto [c, n] = open.top();
        open.pop();
        if (!done.insert(n).second) continue;
        float w = weight(n, c);
        if (w > *best) *best = w, goal = n;
        for (const Edge& e : paths_[n]) {
            if (e.radius > 0 && (float(e.radius) < radius || float(e.height) < height)) continue;  // too small
            float nc = c + e.distance;
            auto it = d.find(e.to);
            if (it == d.end() || nc < it->second) d[e.to] = nc, prev[e.to] = n, open.push({nc, e.to});
        }
    }
    if (!goal) return nullptr;
    std::vector<Obj*> path;
    for (Obj* n = goal; n && path.size() < 10000; n = prev.count(n) ? prev[n] : nullptr) path.push_back(n);
    std::reverse(path.begin(), path.end());
    if (std::vector<Value>* cache = vm_.prop_array(pawn, "RouteCache"))
        for (size_t k = 0; k < cache->size(); k++) (*cache)[k].o = k < path.size() ? path[k] : nullptr;
    return path.front();
}

void World::walk(Obj* o, float dt) {
    Value* collide = vm_.prop(o, "bCollideWorld");
    Value* loc = vm_.prop(o, "Location");
    Value* vel = vm_.prop(o, "Velocity");
    if (!has_model_ || !collide || !collide->i || !loc || !vel) return;  // no floor to find (e.g. flag decorations)
    float* v = vel->v;
    Vec3 acc = vec_of(vm_.prop(o, "Acceleration"));
    float alen = std::sqrt(acc.x * acc.x + acc.y * acc.y);
    if (alen > 0) {  // shortcut: full ground speed at once (no AccelRate), facing the way it walks
        const Value* gs = vm_.prop(o, "GroundSpeed");
        float speed = (gs ? gs->f : 400) * (speed_.count(o) ? speed_[o] : 1.f);
        v[0] = acc.x / alen * speed, v[1] = acc.y / alen * speed;
        if (Value* rot = vm_.prop(o, "Rotation")) rot->r[1] = yaw_to({0, 0, 0}, acc);
    } else {
        float k = std::max(0.f, 1 - 8 * dt);  // shortcut: friction, unverified
        v[0] *= k, v[1] *= k;
    }
    v[2] = 0;
    Vec3 delta{v[0] * dt, v[1] * dt, 0};
    Hit h = move(o, delta);
    if (h.hit && h.normal.z < 0.7f) {  // blocked: step up (25, unverified), else slide along the wall
        Vec3 rest{delta.x * (1 - h.time), delta.y * (1 - h.time), 0};
        Value saved = *loc;
        move(o, {0, 0, 25});
        Hit fwd = move(o, rest);
        move(o, {0, 0, -25});
        if (fwd.time < 0.1f) {
            *loc = saved;
            float dot = rest.x * h.normal.x + rest.y * h.normal.y;
            move(o, {rest.x - dot * h.normal.x, rest.y - dot * h.normal.y, 0});
        }
    }
    Hit down = move(o, {0, 0, -27});  // stay on the floor
    if (!down.hit) {
        loc->v[2] += 27;  // nothing below: undo the probe and fall
        vm_.prop(o, "Physics")->i = 2;
        guarded(o, "Falling");
    }
}

void World::register_ai() {
    Vm& vm = vm_;
    auto loc = [&vm](Obj* o) { return vec_of(o ? vm.prop(o, "Location") : nullptr); };
    auto radius = [&vm](Obj* o) {
        const Value* r = o ? vm.prop(o, "CollisionRadius") : nullptr;
        return r ? r->f : 0.f;
    };
    auto height = [&vm](Obj* o) {
        const Value* h = o ? vm.prop(o, "CollisionHeight") : nullptr;
        return h ? h->f : 0.f;
    };
    vm.add_native("actorReachable", [this, loc](Obj* self, std::vector<Value*>& a, Value& r) {
        r.i = a[0]->o && reachable(self, loc(a[0]->o));
    });
    vm.add_native("pointReachable", [this](Obj* self, std::vector<Value*>& a, Value& r) { r.i = reachable(self, vec_of(a[0])); });
    vm.add_native("LineOfSightTo", [this, loc](Obj* self, std::vector<Value*>& a, Value& r) {
        r.i = a[0]->o && (!has_model_ || !trace(model_, loc(self), loc(a[0]->o), {0, 0, 0}).hit);
    });
    vm.add_native("CanSee", [this, loc](Obj* self, std::vector<Value*>& a, Value& r) {  // shortcut: no view cone
        r.i = a[0]->o && (!has_model_ || !trace(model_, loc(self), loc(a[0]->o), {0, 0, 0}).hit);
    });
    // FindPathToward(actor, optional bSinglePath, optional bClearPaths) / FindPathTo(vector, ...): the next step.
    auto path_to = [this](Obj* self, Vec3 goal) -> Obj* {
        if (reachable(self, goal)) return nullptr;  // caller returns the target itself
        std::set<Obj*> ends;  // navigation points from which the goal is in sight
        for (Obj* n : navs_) {
            Vec3 p = vec_of(vm_.prop(n, "Location"));
            if (dist(p, goal) < 1000 && (!has_model_ || !trace(model_, p, goal, {0, 0, 0}).hit)) ends.insert(n);
        }
        float best = 0;
        return route(self, [&](Obj* n, float d) {
            return ends.count(n) ? 1 / (1 + d + dist(vec_of(vm_.prop(n, "Location")), goal)) : 0.f;
        }, &best);
    };
    vm.add_native("FindPathToward", [this, path_to, loc](Obj* self, std::vector<Value*>& a, Value& r) {
        if (!a[0]->o) return;
        r.o = reachable(self, loc(a[0]->o)) ? a[0]->o : path_to(self, loc(a[0]->o));
    });
    vm.add_native("FindPathTo", [this, path_to](Obj* self, std::vector<Value*>& a, Value& r) {
        r.o = reachable(self, vec_of(a[0])) ? nullptr : path_to(self, vec_of(a[0]));
    });
    vm.add_native("FindRandomDest", [this](Obj* self, std::vector<Value*>&, Value& r) {
        float best = 0;
        Obj* dest = nullptr;
        route(self, [&](Obj* n, float d) {
            float w = d > 0 ? float(ai_rng() % 1000 + 1) : 0.f;
            if (w > best) dest = n;
            return w;
        }, &best);
        r.o = dest;
    });
    // FindBestInventoryPath(out float MinWeight, bool bPredictRespawns): the step toward the item with the best
    // BotDesireability / path distance above MinWeight (InventorySpot.markedItem).
    vm.add_native("FindBestInventoryPath", [this](Obj* self, std::vector<Value*>& a, Value& r) {
        float best = a[0]->f;
        bool predict = a[1]->i != 0;
        r.o = route(self, [&](Obj* n, float d) {
            const Value* item = vm_.prop(n, "markedItem");
            if (!item || !item->o || deleted_.count(item->o)) return 0.f;
            const Value* hidden = vm_.prop(item->o, "bHidden");
            if (hidden && hidden->i && !predict) return 0.f;
            Value pawn = Value::of(VT::Object), desire;
            pawn.o = self;
            try {
                if (!vm_.event(item->o, "BotDesireability", {pawn}, &desire)) return 0.f;
            } catch (const std::exception& e) {
                errors[e.what()]++;
                return 0.f;
            }
            return desire.f / std::max(d, 1.f);
        }, &best);
        if (r.o) a[0]->f = best;
    });
    // Latent moves: steer each tick until the destination is reached or the move times out.
    auto move_to = [this, radius, height](Obj* self, std::function<Vec3()> dest, Obj* target, const Value* speed) {
        speed_[self] = speed && speed->f > 0 ? speed->f : 1.f;
        float start = time;
        Vec3 d0 = dest();
        const Value* gs = vm_.prop(self, "GroundSpeed");
        float timeout = 1 + dist(vec_of(vm_.prop(self, "Location")), d0) / std::max(50.f, gs ? gs->f : 400.f) * 2;
        auto steer = [this, self, dest, target, radius, height, start, timeout]() {
            Vec3 at = vec_of(vm_.prop(self, "Location")), to = dest();
            if (Value* d = vm_.prop(self, "Destination")) d->v[0] = to.x, d->v[1] = to.y, d->v[2] = to.z;
            Value* acc = vm_.prop(self, "Acceleration");
            bool reached = dist2d(at, to) < radius(self) + radius(target) + 8 &&
                           std::fabs(at.z - to.z) < height(self) + height(target) + 40;
            if (reached || time - start > timeout || deleted_.count(self)) {
                if (acc) acc->v[0] = acc->v[1] = acc->v[2] = 0;
                return true;
            }
            if (acc) acc->v[0] = to.x - at.x, acc->v[1] = to.y - at.y, acc->v[2] = 0;
            return false;
        };
        steer();
        self->wait = steer;
    };
    vm.add_native("MoveTo", [move_to](Obj* self, std::vector<Value*>& a, Value&) {
        Vec3 d = vec_of(a[0]);
        move_to(self, [d] { return d; }, nullptr, a[1]);
    });
    vm.add_native("MoveToward", [this, move_to, loc](Obj* self, std::vector<Value*>& a, Value&) {
        Obj* t = a[0]->o;
        if (Value* m = vm_.prop(self, "MoveTarget")) m->o = t;
        if (!t) return;
        move_to(self, [loc, t] { return loc(t); }, t, a[1]);
    });
    vm.add_native("StrafeTo", [move_to](Obj* self, std::vector<Value*>& a, Value&) {
        Vec3 d = vec_of(a[0]);
        move_to(self, [d] { return d; }, nullptr, nullptr);
    });
    vm.add_native("StrafeFacing", [move_to](Obj* self, std::vector<Value*>& a, Value&) {
        Vec3 d = vec_of(a[0]);
        move_to(self, [d] { return d; }, nullptr, nullptr);
    });
    auto turn = [this](Obj* self, Vec3 to) {  // shortcut: turns at once
        Value* rot = vm_.prop(self, "Rotation");
        if (rot) rot->r[1] = yaw_to(vec_of(vm_.prop(self, "Location")), to);
        if (Value* f = vm_.prop(self, "Focus")) f->v[0] = to.x, f->v[1] = to.y, f->v[2] = to.z;
        self->sleep = 0;
    };
    vm.add_native("TurnTo", [turn](Obj* self, std::vector<Value*>& a, Value&) { turn(self, vec_of(a[0])); });
    vm.add_native("TurnToward", [turn, loc](Obj* self, std::vector<Value*>& a, Value&) {
        if (a[0]->o) turn(self, loc(a[0]->o));
    });
    vm.add_native("ClearPaths", [](Obj*, std::vector<Value*>&, Value&) {});
    vm.add_native("PickWallAdjust", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: never finds one
    vm.add_native("EAdjustJump", [this](Obj* self, std::vector<Value*>&, Value& r) {  // shortcut: straight up jump
        const Value* v = vm_.prop(self, "Velocity");
        const Value* jz = vm_.prop(self, "JumpZ");
        r.v[0] = v ? v->v[0] : 0, r.v[1] = v ? v->v[1] : 0, r.v[2] = jz ? jz->f : 325;
    });
    vm.add_native("PickTarget", [](Obj*, std::vector<Value*>&, Value&) {});     // shortcut: no aiming help yet
    vm.add_native("PickAnyTarget", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no aiming help yet
}

}  // namespace ut
