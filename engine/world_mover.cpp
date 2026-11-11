// Movers (moving brushes): brush collision, keyframe interpolation, actors standing on them (ADR 0006).
// Spec: docs/formats/script.md (World); behaviour marked unverified there until compared with the original.
#include <algorithm>
#include <cmath>

#include "world.h"

namespace ut {

namespace {

Vec3 vec_of(const Value* v) { return v ? Vec3{v->v[0], v->v[1], v->v[2]} : Vec3{0, 0, 0}; }

float length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// Distance from c to the segment a-b.
float segment_distance(Vec3 a, Vec3 b, Vec3 c) {
    Vec3 d{b.x - a.x, b.y - a.y, b.z - a.z}, e{c.x - a.x, c.y - a.y, c.z - a.z};
    float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    float t = len2 > 0 ? std::clamp((e.x * d.x + e.y * d.y + e.z * d.z) / len2, 0.f, 1.f) : 0.f;
    float px = e.x - d.x * t, py = e.y - d.y * t, pz = e.z - d.z * t;
    return std::sqrt(px * px + py * py + pz * pz);
}

}  // namespace

void World::add_mover(Obj* o) {
    const Value* brush = vm_.prop(o, "Brush");
    if (!brush || !brush->o || !brush->o->pkg || brush->o->ref <= 0) return;
    const Package& p = *brush->o->pkg;
    if (size_t(brush->o->ref) > p.exports.size()) return;
    Brush b{read_model(p, p.exports[size_t(brush->o->ref - 1)]), 0};
    if (b.model.nodes.empty()) return;
    for (const Vec3& q : b.model.points) b.radius = std::max(b.radius, std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z));
    movers_[o] = std::move(b);
}

Hit World::trace_brush(Obj* m, Vec3 a, Vec3 b, Vec3 ext) {
    auto it = movers_.find(m);
    if (it == movers_.end() || deleted_.count(m)) return {};
    const Value* c = vm_.prop(m, "bCollideActors");
    const Value* bp = vm_.prop(m, "bBlockPlayers");
    const Value* ba = vm_.prop(m, "bBlockActors");
    // shortcut: a mover blocking players or actors blocks every trace, players and others alike
    if (!c || !c->i || !((bp && bp->i) || (ba && ba->i))) return {};
    Vec3 loc = vec_of(vm_.prop(m, "Location")), pivot = vec_of(vm_.prop(m, "PrePivot"));
    if (segment_distance(a, b, loc) > it->second.radius + length(pivot) + length(ext) + 1) return {};
    const Value* rot = vm_.prop(m, "Rotation");
    int32_t r0[3]{};
    float axes[3][3];
    rotation_axes(rot ? rot->r : r0, axes[0], axes[1], axes[2]);
    // shortcut: the collision box keeps its world axes inside a rotated brush (exact for yaw steps of 90 degrees)
    return trace_placed(it->second.model, loc, pivot, axes, a, b, ext);
}

Hit World::trace_world(Obj* self, Vec3 a, Vec3 b, Vec3 ext, Obj** what) {
    Hit best = has_model_ ? trace(model_, a, b, ext) : Hit{};
    if (what) *what = best.hit ? level_info : nullptr;
    for (auto& entry : movers_) {
        if (entry.first == self) continue;
        Hit h = trace_brush(entry.first, a, b, ext);
        if (h.hit && (!best.hit || h.time < best.time)) {
            best = h;
            if (what) *what = entry.first;
        }
    }
    return best;
}

void World::set_base(Obj* o, Obj* base) {
    Value* v = vm_.prop(o, "Base");
    if (!v || v->o == base) return;
    Obj* old = v->o;
    v->o = base;
    Value other = Value::of(VT::Object);
    other.o = o;
    if (old) {
        if (Value* n = vm_.prop(old, "StandingCount"); n && n->i > 0) n->i--;
        guarded(old, "Detach", {other});
    }
    if (base) {
        if (Value* n = vm_.prop(base, "StandingCount")) n->i = std::min(n->i + 1, 255);
        guarded(base, "Attach", {other});
    }
    guarded(o, "BaseChange");
}

void World::move_brush(Obj* o, float dt) {
    // Mover.InterpolateTo set OldPos / OldRot, PhysAlpha 0, PhysRate 1 / seconds, bInterpolating; the brush glides
    // from there to BasePos + KeyPos[KeyNum] (and BaseRot + KeyRot[KeyNum]); at the end InterpolateEnd chains keys.
    Value* interp = vm_.prop(o, "bInterpolating");
    Value* alpha = vm_.prop(o, "PhysAlpha");
    const Value* rate = vm_.prop(o, "PhysRate");
    std::vector<Value>* keys = vm_.prop_array(o, "KeyPos");
    std::vector<Value>* key_rots = vm_.prop_array(o, "KeyRot");
    Value* loc = vm_.prop(o, "Location");
    Value* rot = vm_.prop(o, "Rotation");
    const Value* key = vm_.prop(o, "KeyNum");
    if (!interp || !interp->i || !alpha || !rate || !keys || !key_rots || !loc || !rot || !key) return;
    size_t k = std::min(size_t(key->i), keys->size() - 1);
    float old_alpha = alpha->f;
    alpha->f = std::min(1.f, alpha->f + dt * rate->f);
    float t = alpha->f;
    const Value* glide = vm_.prop(o, "MoverGlideType");
    if (glide && glide->i == 1) t = t * t * (3 - 2 * t);  // MV_GlideByTime: smooth start and stop (unverified curve)
    Vec3 base = vec_of(vm_.prop(o, "BasePos")), from = vec_of(vm_.prop(o, "OldPos"));
    const float* kp = (*keys)[k].v;
    Vec3 to{base.x + kp[0], base.y + kp[1], base.z + kp[2]};
    Vec3 at{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t};
    Vec3 delta{at.x - loc->v[0], at.y - loc->v[1], at.z - loc->v[2]};
    const Value* base_rot = vm_.prop(o, "BaseRot");
    const Value* old_rot = vm_.prop(o, "OldRot");
    int32_t new_rot[3];
    for (int c = 0; c < 3; c++) {
        int64_t goal = int64_t(base_rot ? base_rot->r[c] : 0) + (*key_rots)[k].r[c];
        int64_t start = old_rot ? old_rot->r[c] : rot->r[c];
        new_rot[c] = int32_t(start + int64_t(std::lround(double(goal - start) * t)));
    }
    // Encroachment: a blocking actor the brush would overlap (not one standing on it) gets EncroachingOn; true
    // stops the move this tick (the script then stops or returns the group).
    Value saved_loc = *loc, saved_rot = *rot;
    loc->v[0] = at.x, loc->v[1] = at.y, loc->v[2] = at.z;
    for (int c = 0; c < 3; c++) rot->r[c] = new_rot[c];
    Vec3 pivot = vec_of(vm_.prop(o, "PrePivot"));
    float axes[3][3];
    rotation_axes(new_rot, axes[0], axes[1], axes[2]);
    for (size_t i = 0; i < actors.size(); i++) {
        Obj* x = actors[i];
        if (x == o || deleted_.count(x) || movers_.count(x)) continue;
        const Value* xb = vm_.prop(x, "Base");
        const Value* xc = vm_.prop(x, "bCollideActors");
        const Value* bp = vm_.prop(x, "bBlockPlayers");
        const Value* ba = vm_.prop(x, "bBlockActors");
        if ((xb && xb->o == o) || !xc || !xc->i || !((bp && bp->i) || (ba && ba->i))) continue;
        const Value* xr = vm_.prop(x, "CollisionRadius");
        const Value* xh = vm_.prop(x, "CollisionHeight");
        Vec3 p = vec_of(vm_.prop(x, "Location")), ext{xr ? xr->f : 0, xr ? xr->f : 0, xh ? xh->f : 0};
        const Brush& brush = movers_[o];
        if (length({p.x - at.x, p.y - at.y, p.z - at.z}) > brush.radius + length(pivot) + length(ext) + 1) continue;
        // a zero-length sweep reports a box that overlaps the brush as start_solid
        bool overlaps = trace_placed(brush.model, at, pivot, axes, p, p, ext).start_solid;
        if (!overlaps) continue;
        Value other = Value::of(VT::Object), ret = Value::of(VT::Bool);
        other.o = x;
        try {
            vm_.event(o, "EncroachingOn", {other}, &ret);
        } catch (const std::exception& e) {
            errors[e.what()]++;
        }
        if (ret.i) {
            *loc = saved_loc, *rot = saved_rot, alpha->f = old_alpha;
            return;
        }
    }
    // Actors standing on the brush ride along. shortcut: translation only, a rotating brush does not turn them
    for (Obj* x : actors) {
        const Value* xb = vm_.prop(x, "Base");
        if (x == o || deleted_.count(x) || !xb || xb->o != o) continue;
        if (Value* xl = vm_.prop(x, "Location")) xl->v[0] += delta.x, xl->v[1] += delta.y, xl->v[2] += delta.z;
    }
    if (alpha->f >= 1) {
        interp->i = 0;
        guarded(o, "InterpolateEnd", {Value::of(VT::Object)});
    }
}

}  // namespace ut
