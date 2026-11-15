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
    // Climb and drop limits measured (probe OU-Reach, JumpZ 325, CollisionHeight 39): climbs up to 29 (a step) and
    // drops up to 24 without bCanJump; with it, climbs up to 52 and drops up to 118. shortcut: otherwise the box (raised
    // by a step) fits along the straight line with floor under it every 48 units; the original simulates the walk.
    Vec3 from = vec_of(vm_.prop(pawn, "Location"));
    const Value* jump = vm_.prop(pawn, "bCanJump");
    bool can_jump = jump && jump->i;
    float climb_max = can_jump ? 52.5f : 29.5f, drop_max = can_jump ? 118.5f : 24.5f;
    if (dest.z - from.z > climb_max || from.z - dest.z > drop_max) return false;
    if (!has_model_ && movers_.empty()) return true;
    const Value* rv = vm_.prop(pawn, "CollisionRadius");
    const Value* hv = vm_.prop(pawn, "CollisionHeight");
    float r = rv ? rv->f * 0.8f : 0, h = hv ? std::max(0.f, hv->f - 25) : 0;
    // Going down, it walks off at its own height and falls (a straight line would clip the edge it walks off).
    Vec3 corner{dest.x, dest.y, std::max(from.z, dest.z) + 25};
    if (trace_world(pawn, {from.x, from.y, from.z + 25}, corner, {r, r, h}).hit ||
        trace_world(pawn, corner, {dest.x, dest.y, dest.z + 25}, {r, r, h}).hit)
        return false;
    if (!vm_.prop(pawn, "Physics") || vm_.prop(pawn, "Physics")->i != 1) return true;  // flyers and swimmers
    float len = dist2d(from, dest), feet = hv ? hv->f : 0;
    for (float s = 48; s < len; s += 48) {
        float k = s / len;
        Vec3 p{from.x + (dest.x - from.x) * k, from.y + (dest.y - from.y) * k, from.z + (dest.z - from.z) * k};
        if (!trace_world(pawn, p, {p.x, p.y, p.z - feet - drop_max}, {0, 0, 0}).hit) return false;  // a pit (lifts are floor)
    }
    return true;
}

// Ray against the collision cylinders of actors that can be hit (bCollideActors and bProjTarget or bBlockActors).
Obj* World::trace_actors(Obj* self, Vec3 a, Vec3 b, float* t_hit, Vec3* normal, const std::set<Obj*>* skip) {
    Obj* best = nullptr;
    for (Obj* o : actors) {
        if (o == self || deleted_.count(o) || (skip && skip->count(o)) || movers_.count(o)) continue;
        const Value* c = vm_.prop(o, "bCollideActors");
        const Value* pt = vm_.prop(o, "bProjTarget");
        const Value* ba = vm_.prop(o, "bBlockActors");
        if (!c || !c->i || !((pt && pt->i) || (ba && ba->i))) continue;
        Vec3 p = vec_of(vm_.prop(o, "Location"));
        if (ray_cylinder(a, b, p, vm_.prop(o, "CollisionRadius")->f, vm_.prop(o, "CollisionHeight")->f, t_hit, normal))
            best = o;
    }
    return best;
}

void World::sight(Obj* o, float dt) {
    // Every 0.2 s a pawn looks for players: within SightRadius, inside PeripheralVision (cosine from its rotation)
    // and in line of sight from its eyes -> SeePlayer(Other); a visible Enemy lost from sight -> EnemyNotVisible.
    Value* counter = vm_.prop(o, "SightCounter");
    if (!counter || (counter->f -= dt) > 0) return;
    counter->f += 0.2f;
    Vec3 eye = vec_of(vm_.prop(o, "Location"));
    if (const Value* e = vm_.prop(o, "EyeHeight")) eye.z += e->f;
    const Value* rot = vm_.prop(o, "Rotation");
    float yaw = rot ? float(rot->r[1]) * 3.14159265f / 32768 : 0, pitch = rot ? float(rot->r[0]) * 3.14159265f / 32768 : 0;
    Vec3 dir{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    float radius = vm_.prop(o, "SightRadius")->f, cone = vm_.prop(o, "PeripheralVision")->f;
    auto visible = [&](Obj* other, bool cone_check) {
        Vec3 p = vec_of(vm_.prop(other, "Location"));
        float dd = dist(eye, p);
        if (dd > radius || dd < 1) return false;
        Vec3 to{(p.x - eye.x) / dd, (p.y - eye.y) / dd, (p.z - eye.z) / dd};
        if (cone_check && to.x * dir.x + to.y * dir.y + to.z * dir.z < cone) return false;
        return !has_model_ || !trace(model_, eye, p, {0, 0, 0}).hit;
    };
    Value* enemy = vm_.prop(o, "Enemy");
    if (enemy && enemy->o && !deleted_.count(enemy->o) && seen_.count({o, enemy->o}) && !visible(enemy->o, false)) {
        seen_.erase({o, enemy->o});
        guarded(o, "EnemyNotVisible");
    }
    for (const Value* p = vm_.prop(level_info, "PawnList"); p && p->o; p = vm_.prop(p->o, "nextPawn")) {
        Obj* other = p->o;
        const Value* player = vm_.prop(other, "bIsPlayer");
        const Value* health = vm_.prop(other, "Health");
        if (other == o || deleted_.count(other) || !player || !player->i || !health || health->i <= 0) continue;
        if (!visible(other, true)) continue;
        seen_.insert({o, other});
        Value v = Value::of(VT::Object);
        v.o = other;
        guarded(o, "SeePlayer", {v});
        if (deleted_.count(o)) return;
    }
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
    // Navigation points with bSpecialCost (JumpSpot, TranslocStart / Dest, PainPath) add what their SpecialCost(Seeker)
    // event answers: a JumpSpot the pawn cannot use (no translocator, no jump boots) costs it 10,000,000 in its script.
    std::map<Obj*, float> specials;
    auto special_cost = [&](Obj* to) {
        auto it = specials.find(to);
        if (it != specials.end()) return it->second;
        float cost = 0;
        const Value* flag = vm_.prop(to, "bSpecialCost");
        if (flag && flag->i) {
            Value seeker = Value::of(VT::Object), ret;
            seeker.o = pawn;
            try {
                if (vm_.event(to, "SpecialCost", {seeker}, &ret)) cost = float(std::max(0, ret.i));
            } catch (const std::exception& e) {
                errors[e.what()]++;
            }
        }
        return specials[to] = cost;
    };
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
            float nc = c + e.distance + special_cost(e.to);
            auto it = d.find(e.to);
            if (it == d.end() || nc < it->second) d[e.to] = nc, prev[e.to] = n, open.push({nc, e.to});
        }
    }
    if (!goal) return nullptr;
    std::vector<Obj*> path;
    for (Obj* n = goal; n && path.size() < 10000; n = prev.count(n) ? prev[n] : nullptr) path.push_back(n);
    std::reverse(path.begin(), path.end());
    // Already standing on the first node: the next one is the step (else the pawn "arrives" there forever).
    Vec3 first = vec_of(vm_.prop(path.front(), "Location"));
    const Value* nr = vm_.prop(path.front(), "CollisionRadius");
    if (path.size() > 1 && dist2d(at, first) < radius + (nr ? nr->f : 0) + 8 && std::fabs(at.z - first.z) < height + 40)
        path.erase(path.begin());
    if (std::vector<Value>* cache = vm_.prop_array(pawn, "RouteCache"))
        for (size_t k = 0; k < cache->size(); k++) (*cache)[k].o = k < path.size() ? path[k] : nullptr;
    // The step may redirect the pawn (a lift's center sends it to wait at the exit, to a trigger...). Actor's default
    // SpecialHandling answers None: keep the step then (unverified: the original may treat None as "no way").
    const Value* special = vm_.prop(pawn, "bCanDoSpecial");
    if (special && special->i) {
        Value other = Value::of(VT::Object), ret;
        other.o = pawn;
        try {
            if (vm_.event(path.front(), "SpecialHandling", {other}, &ret) && ret.o && !deleted_.count(ret.o)) return ret.o;
        } catch (const std::exception& e) {
            errors[e.what()]++;
        }
    }
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
        const Value* human = vm_.prop(o, "Player");  // a player's script sets its own rotation (strafing keeps it)
        if (Value* rot = vm_.prop(o, "Rotation"); rot && !(human && human->o)) rot->r[1] = yaw_to({0, 0, 0}, acc);
    } else {
        float k = std::max(0.f, 1 - 8 * dt);  // shortcut: friction, unverified
        v[0] *= k, v[1] *= k;
    }
    v[2] = 0;
    Vec3 delta{v[0] * dt, v[1] * dt, 0};
    Obj* blocker = nullptr;
    Hit h = move(o, delta, &blocker);
    if (h.hit && blocker && blocker != level_info) {  // walking into an actor, door or lift: Bump both
        bump(o, blocker);
        // shortcut: HitWall only for movers (bots open doors there); BSP walls would cancel moves our coarse steering
        // still slides along. Upgrade with the original step-up failure rule once steering follows paths closely.
        Value b = Value::of(VT::Object), n = Value::of(VT::Vector);
        b.o = blocker;
        n.v[0] = h.normal.x, n.v[1] = h.normal.y, n.v[2] = h.normal.z;
        if (movers_.count(blocker) && !deleted_.count(o)) guarded(o, "HitWall", {n, b});
    }
    if (h.hit && h.normal.z >= 0.263f && h.normal.z < 0.7f) {
        // A slope too steep to walk: the original turns to falling and slides up until gravity brings it back. It
        // does so up to 74.5 deg (normal z 0.267) and treats 75 deg (0.259) and steeper as a wall (probe OU-Entry,
        // script.md). shortcut: velocity projected on the slope; the original's start speed differs (script.md).
        float dot = v[0] * h.normal.x + v[1] * h.normal.y + v[2] * h.normal.z;
        for (int k = 0; k < 3; k++) v[k] -= dot * (&h.normal.x)[k];
        vm_.prop(o, "Physics")->i = 2;
        set_base(o, nullptr);
        guarded(o, "Falling");
        return;
    }
    for (int k = 0; k < 2 && h.hit && h.normal.z >= 0.7f; k++) {  // a ramp: go on along its surface
        Vec3 rest{delta.x * (1 - h.time), delta.y * (1 - h.time), 0};
        float dot = rest.x * h.normal.x + rest.y * h.normal.y;
        delta = {rest.x - dot * h.normal.x, rest.y - dot * h.normal.y, -dot * h.normal.z};
        h = move(o, delta);
    }
    if (h.hit) {  // blocked (a wall, or a ramp foot the slide could not climb): step up 25 (from the hover below: climbs 29 as measured), else slide
        Vec3 rest{delta.x * (1 - h.time), delta.y * (1 - h.time), 0};
        Value saved = *loc;
        move(o, {0, 0, 25});
        Hit fwd = move(o, rest);
        Hit settle = move(o, {0, 0, -25});
        // A step only counts onto walkable floor: stepping onto a steep face would climb walls 25 at a time (the
        // original stops at 75 deg and steeper: probe OU-Entry).
        bool onto_steep = settle.hit && settle.normal.z < 0.7f;
        if (fwd.time < 0.1f || onto_steep) {
            *loc = saved;
            float dot = rest.x * h.normal.x + rest.y * h.normal.y;
            move(o, {rest.x - dot * h.normal.x, rest.y - dot * h.normal.y, 0});
            // The original sends HitWall for walls too (probe OU-Ledge); bots answer with PickWallAdjust, which may jump.
            // It does not jump by itself, even with bCanJump and the goal on the ledge.
            if (!blocker || blocker == level_info) {
                Value b = Value::of(VT::Object), n = Value::of(VT::Vector);
                b.o = level_info;
                n.v[0] = h.normal.x, n.v[1] = h.normal.y, n.v[2] = h.normal.z;
                guarded(o, "HitWall", {n, b});
                if (deleted_.count(o)) return;
                if (vm_.prop(o, "Physics")->i == 2) {  // jumped (PickWallAdjust): from the step's height, falling the
                    move(o, {0, 0, 25});                // rest of the tick, as measured (probe OU-Ledge)
                    physics(o, dt * (1 - h.time));
                }
                if (vm_.prop(o, "Physics")->i != 1) return;
            }
        }
    }
    Obj* floor = nullptr;
    Vec3 before = vec_of(loc);
    Hit down = move(o, {0, 0, -27}, &floor);  // stay on the floor
    const Value* old_base = vm_.prop(o, "Base");
    if (down.hit && floor == level_info && old_base && movers_.count(old_base->o)) {
        // Riding a lift with an edge inside a wall: the brush it stands on stays its base, or the lift would
        // encroach on it and stop.
        const Value* r = vm_.prop(o, "CollisionRadius");
        const Value* hh = vm_.prop(o, "CollisionHeight");
        Vec3 ext{r ? r->f : 0, r ? r->f : 0, hh ? hh->f : 0};
        if (trace_brush(old_base->o, before, {before.x, before.y, before.z - 27}, ext).hit) floor = old_base->o;
    }
    if (!down.hit) {
        loc->v[2] += 27;  // nothing below: undo the probe and fall
        vm_.prop(o, "Physics")->i = 2;
        set_base(o, nullptr);
        guarded(o, "Falling");
    } else {
        set_base(o, floor);  // the LevelInfo on BSP floors (unverified), else the mover it stands on
        // Measured (script.md, probe OU-Steps): a walking pawn rests 4.8 above the floor, so a 25 step up from
        // there climbs 29, not 30, as the original does. Only over walkable floor: lifting off a steep face the
        // probe touched first would climb it 4.8 a tick (the original stays put at a 75 deg face: OU-Entry).
        if (down.normal.z >= 0.7f) move(o, {0, 0, 4.8f});
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
        Vec3 last = vec_of(vm_.prop(self, "Location"));
        float last_time = start;
        auto steer = [this, self, dest, target, radius, height, start, timeout, last, last_time]() mutable {
            Vec3 at = vec_of(vm_.prop(self, "Location")), to = dest();
            if (time - last_time >= 1) {  // no progress for a second: give up so the AI picks another way
                if (dist(at, last) < 16) timeout = 0;
                last = at, last_time = time;
            }
            if (Value* d = vm_.prop(self, "Destination")) d->v[0] = to.x, d->v[1] = to.y, d->v[2] = to.z;
            Value* acc = vm_.prop(self, "Acceleration");
            // A target that collides (a pickup) is reached when the cylinders touch; others (path nodes) loosely.
            const Value* tc = target ? vm_.prop(target, "bCollideActors") : nullptr;
            bool touchable = tc && tc->i;
            bool reached = dist2d(at, to) < radius(self) + radius(target) + (touchable ? 0 : 8) &&
                           std::fabs(at.z - to.z) < height(self) + height(target) + (touchable ? 0 : 40);
            if (reached || time - start > timeout || deleted_.count(self)) {
                if (acc) acc->v[0] = acc->v[1] = acc->v[2] = 0;
                return true;
            }
            // A falling pawn keeps the Acceleration it had (air control from its walk, none from a move begun in the
            // air: probes OU-Ledge, OU-Air).
            const Value* phys = vm_.prop(self, "Physics");
            if (acc && !(phys && phys->i == 2)) acc->v[0] = to.x - at.x, acc->v[1] = to.y - at.y, acc->v[2] = 0;
            return false;
        };
        if (steer()) self->sleep = 0;  // already there: still a latent call, it ends on the next tick
        else self->wait = steer;
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
    vm.add_native("StopWaiting", [](Obj* self, std::vector<Value*>&, Value&) {  // ends a latent Sleep
        if (self && self->sleep > 0) self->sleep = 0;
    });
    // PickWallAdjust: measured (probe OU-Ledge) for a walking pawn blocked by a ledge: up to 77 high True, falling, with
    // Velocity GroundSpeed toward Destination and JumpZ up; 78 and up False; False while falling. The limit is the same
    // for JumpZ 250, 325 and 400 (at 250 it jumps ledges it cannot clear) and for CollisionHeight 39 and 50: we test the
    // box raised to 77.5 above the floor. Then a sideways adjust (below).
    vm.add_native("PickWallAdjust", [this](Obj* self, std::vector<Value*>&, Value& r) {
        Value* mode = vm_.prop(self, "Physics");
        const Value* can = vm_.prop(self, "bCanJump");
        const Value* jz = vm_.prop(self, "JumpZ");
        const Value* gs = vm_.prop(self, "GroundSpeed");
        const Value* rv = vm_.prop(self, "CollisionRadius");
        const Value* hv = vm_.prop(self, "CollisionHeight");
        if (!mode || mode->i != 1 || !rv || !hv) return;
        Vec3 at = vec_of(vm_.prop(self, "Location"));
        Value* dest = vm_.prop(self, "Destination");
        Vec3 to = vec_of(dest);
        float dx = to.x - at.x, dy = to.y - at.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1) return;
        dx /= len, dy /= len;
        float reach = rv->f * 2;
        Vec3 ext{rv->f, rv->f, hv->f};
        auto clear = [&](Vec3 from, Vec3 side) {  // the box moves there, then a reach toward Destination
            Vec3 p{from.x + side.x, from.y + side.y, from.z + side.z};
            return !trace_world(self, from, p, ext).hit && !trace_world(self, p, {p.x + dx * reach, p.y + dy * reach, p.z}, ext).hit;
        };
        if (can && can->i && jz && jz->f > 0 && clear(at, {0, 0, 72.7f})) {  // from the hover: box bottom 77.5 above the floor
            float speed = gs ? gs->f : 400;
            Value* v = vm_.prop(self, "Velocity");
            v->v[0] = dx * speed, v->v[1] = dy * speed, v->v[2] = jz->f;
            mode->i = 2;
            set_base(self, nullptr);
            r.i = 1;
            return;
        }
        // Sideways (probe OU-Wall, walls centred or off to one side): left of the way to Destination first, then right.
        // Destination becomes 2 * radius + 16 to that side (50 at radius 17, 66 at 25) and 25 up. It fits if the box
        // clears a distance found between 40 and 41 at radius 17 (walls 46 wide passed, 48 not) and between 58 and 60 at
        // radius 25 (66 passed, 70 not). shortcut: that distance is a line fitted through both, 2.25 * radius + 1.75.
        float check = 2.25f * rv->f + 1.75f, aside = 2 * rv->f + 16;
        for (float side : {1.f, -1.f}) {
            Vec3 l{dy * side, -dx * side, 0};  // facing +x, left is -y
            if (!clear(at, {l.x * check, l.y * check, 0})) continue;
            dest->v[0] = at.x + l.x * aside, dest->v[1] = at.y + l.y * aside, dest->v[2] = at.z + 25;
            r.i = 1;
            return;
        }
    });
    vm.add_native("EAdjustJump", [this](Obj* self, std::vector<Value*>&, Value& r) {  // shortcut: straight up jump
        const Value* v = vm_.prop(self, "Velocity");
        const Value* jz = vm_.prop(self, "JumpZ");
        r.v[0] = v ? v->v[0] : 0, r.v[1] = v ? v->v[1] : 0, r.v[2] = jz ? jz->f : 325;
    });
    vm.add_native("PickTarget", [](Obj*, std::vector<Value*>&, Value&) {});     // shortcut: no aiming help yet
    vm.add_native("PickAnyTarget", [](Obj*, std::vector<Value*>&, Value&) {});  // shortcut: no aiming help yet
}

}  // namespace ut
