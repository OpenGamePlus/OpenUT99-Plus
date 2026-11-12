#include "play_view.h"

#include <algorithm>
#include <cmath>

#include "texture.h"

namespace ut {

namespace {

const int kDrawMesh = 2, kDrawBrush = 3;  // Actor.EDrawType: DT_Mesh, DT_Brush

struct Frame {  // a rotation's axes; local -> world is x * X + y * Y + z * Z
    float x[3], y[3], z[3];
    explicit Frame(const int32_t rot[3]) { rotation_axes(rot, x, y, z); }
    Vec3 apply(Vec3 v) const {
        return {v.x * x[0] + v.y * y[0] + v.z * z[0], v.x * x[1] + v.y * y[1] + v.z * z[1], v.x * x[2] + v.y * y[2] + v.z * z[2]};
    }
};

Vec3 vec_of(const Value* v) { return v ? Vec3{v->v[0], v->v[1], v->v[2]} : Vec3{0, 0, 0}; }

const Export* export_of(const Obj* o) {
    if (!o || !o->pkg || o->ref <= 0 || size_t(o->ref) > o->pkg->exports.size()) return nullptr;
    return &o->pkg->exports[size_t(o->ref - 1)];
}

}  // namespace

const LodMesh* PlayView::mesh(const Obj* m) {
    auto it = meshes_.find(m);
    if (it == meshes_.end()) {
        std::unique_ptr<LodMesh> lod;
        const Export* e = export_of(m);
        if (e && iequal(vm_.name_of(vm_.class_of(const_cast<Obj*>(m))), "LodMesh")) {
            try {
                lod = std::make_unique<LodMesh>(read_lod_mesh(*m->pkg, *e));
            } catch (const std::exception&) {
            }  // unreadable: not drawn
        }
        it = meshes_.emplace(m, std::move(lod)).first;
    }
    return it->second.get();
}

const Image* PlayView::image(const Package* p, const Export* e, bool masked) {
    auto key = std::make_tuple(e, masked);
    auto it = images_.find(key);
    if (it == images_.end()) {
        Image img;
        try {
            img = p && e ? texture_rgba(*p, *e, masked) : texture_rgba(Package{}, Export{}, false);
        } catch (const std::exception&) {
            img = texture_rgba(Package{}, Export{}, false);  // the checker
        }
        it = images_.emplace(key, std::make_unique<Image>(std::move(img))).first;
    }
    return it->second.get();
}

size_t PlayView::batch(const Image* img, uint32_t flags) {
    auto key = std::make_pair(img, flags);
    auto it = slots_.find(key);
    if (it == slots_.end()) {
        it = slots_.emplace(key, out_.size()).first;
        out_.push_back({img, flags, {}});
    }
    return it->second;
}

const std::vector<LiveBatch>& PlayView::build() {
    out_.clear(), slots_.clear();
    drawn = 0;
    for (Obj* a : world_.actors) {
        if (world_.destroyed(a)) continue;
        const Value* type = vm_.prop(a, "DrawType");
        const Value* hidden = vm_.prop(a, "bHidden");
        if (!type || (hidden && hidden->i)) continue;
        if (type->i == kDrawMesh) {
            const Value* m = vm_.prop(a, "Mesh");
            if (const LodMesh* lod = m && m->o ? mesh(m->o) : nullptr; lod && lod->frame_verts) add_mesh(a, *lod, m->o), drawn++;
        } else if (type->i == kDrawBrush) {
            add_brush(a);
            drawn++;
        }
    }
    return out_;
}

void PlayView::add_mesh(Obj* a, const LodMesh& m, const Obj* mesh_obj) {
    // Animation frame: AnimFrame is the fraction of AnimSequence played (negative while tweening: its first frame).
    int f0 = 0, f1 = 0;
    float t = 0;
    const Value* seq_name = vm_.prop(a, "AnimSequence");
    const Value* anim_frame = vm_.prop(a, "AnimFrame");
    const Value* loop = vm_.prop(a, "bAnimLoop");
    for (const LodMesh::AnimSeq& s : m.seqs) {
        if (!seq_name || !iequal(s.name, seq_name->s) || s.frames <= 0) continue;
        float pos = std::clamp(anim_frame ? anim_frame->f : 0.f, 0.f, 1.f) * float(s.frames);
        int i = std::min(int(pos), s.frames - 1);
        int next = loop && loop->i ? (i + 1) % s.frames : std::min(i + 1, s.frames - 1);
        f0 = s.start + i, f1 = s.start + next, t = pos - float(i);
        break;
    }
    if (f0 < 0 || f0 >= m.frames || f1 < 0 || f1 >= m.frames) f0 = f1 = 0, t = 0;

    Vec3 loc = vec_of(vm_.prop(a, "Location")), pre = vec_of(vm_.prop(a, "PrePivot"));
    loc = {loc.x + pre.x, loc.y + pre.y, loc.z + pre.z};
    const Value* rot = vm_.prop(a, "Rotation");
    int32_t zero[3]{};
    Frame actor(rot ? rot->r : zero), origin(m.rot_origin);
    const Value* ds = vm_.prop(a, "DrawScale");
    float scale = ds ? ds->f : 1.f;
    auto world = [&](size_t vi) {
        const Vec3& p = m.verts[size_t(f0) * size_t(m.frame_verts) + vi];
        const Vec3& q = m.verts[size_t(f1) * size_t(m.frame_verts) + vi];
        Vec3 v{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t};
        Vec3 s{(v.x - m.origin.x) * m.scale.x * scale, (v.y - m.origin.y) * m.scale.y * scale, (v.z - m.origin.z) * m.scale.z * scale};
        Vec3 w = actor.apply(origin.apply(s));  // RotOrigin, then the actor's rotation (same as scene.cpp)
        return Vec3{loc.x + w.x, loc.y + w.y, loc.z + w.z};
    };
    std::vector<Value>* multi = vm_.prop_array(a, "MultiSkins");
    const Value* skin = vm_.prop(a, "Skin");
    std::map<size_t, size_t> slot_batch;  // material slot -> batch index
    for (const LodMesh::Face& f : m.faces) {
        if (f.material >= m.materials.size()) continue;
        const LodMesh::Material& mat = m.materials[f.material];
        size_t slot = size_t(std::max(mat.texture, 0));
        auto bs = slot_batch.find(slot);
        if (bs == slot_batch.end()) {
            // Texture of material slot i: MultiSkins[i], else the mesh's own texture i, else Skin.
            const Package* tp = nullptr;
            const Export* te = nullptr;
            if (multi && slot < multi->size() && export_of((*multi)[slot].o)) tp = (*multi)[slot].o->pkg, te = export_of((*multi)[slot].o);
            if (!te && slot < m.textures.size()) {
                Object o = set_.resolve(*mesh_obj->pkg, m.textures[slot]);
                tp = o.package, te = o.exp && o.exp->size ? o.exp : nullptr;
            }
            if (!te && skin && export_of(skin->o)) tp = skin->o->pkg, te = export_of(skin->o);
            uint32_t flags = (mat.flags & (PF_Masked | PF_Translucent | PF_Modulated)) | PF_TwoSided;
            bs = slot_batch.emplace(slot, batch(image(tp, te, flags & PF_Masked), flags)).first;
        }
        for (int k = 0; k < 3; k++) {
            const LodMesh::Wedge& w = m.wedges[f.wedge[k]];
            Vec3 p = world(size_t(w.vertex) + size_t(m.special_verts));
            out_[bs->second].verts.push_back({p.x, p.y, p.z, float(w.u) / 256.f, float(w.v) / 256.f, 0, 0});
        }
    }
}

void PlayView::add_brush(Obj* mover) {
    const Value* brush = vm_.prop(mover, "Brush");
    const Export* e = brush ? export_of(brush->o) : nullptr;
    if (!e) return;
    auto it = brushes_.find(brush->o);
    if (it == brushes_.end()) {
        std::vector<BrushPart> parts;
        try {
            const Package& p = *brush->o->pkg;
            Model m = read_model(p, *e);
            std::map<std::pair<const Image*, uint32_t>, size_t> part_of;
            for (const Node& n : m.nodes) {
                if (n.surf < 0 || size_t(n.surf) >= m.surfs.size() || n.num_vertices < 3) continue;
                const Surf& s = m.surfs[size_t(n.surf)];
                if (s.flags & (PF_Invisible | PF_Portal)) continue;
                Object tex = set_.resolve(p, s.texture);
                uint32_t flags = s.flags & (PF_Masked | PF_Translucent | PF_Modulated | PF_TwoSided);
                const Image* img = image(tex.package, tex.exp && tex.exp->size ? tex.exp : nullptr, flags & PF_Masked);
                auto key = std::make_pair(img, flags);
                auto pi = part_of.find(key);
                if (pi == part_of.end()) pi = part_of.emplace(key, parts.size()).first, parts.push_back({img, flags, {}});
                std::vector<Vertex> tris = node_triangles(m, n, img->width, img->height);
                parts[pi->second].local.insert(parts[pi->second].local.end(), tris.begin(), tris.end());
            }
        } catch (const std::exception&) {
            parts.clear();  // unreadable brush: not drawn
        }
        it = brushes_.emplace(brush->o, std::move(parts)).first;
    }
    // world = Location + R * (local - PrePivot), as for collision (world_mover.cpp)
    Vec3 loc = vec_of(vm_.prop(mover, "Location")), pivot = vec_of(vm_.prop(mover, "PrePivot"));
    const Value* rot = vm_.prop(mover, "Rotation");
    int32_t zero[3]{};
    Frame r(rot ? rot->r : zero);
    for (const BrushPart& part : it->second) {
        std::vector<Vertex>& out = out_[batch(part.image, part.flags)].verts;
        for (Vertex v : part.local) {
            Vec3 w = r.apply({v.x - pivot.x, v.y - pivot.y, v.z - pivot.z});
            v.x = loc.x + w.x, v.y = loc.y + w.y, v.z = loc.z + w.z;
            out.push_back(v);
        }
    }
}

}  // namespace ut
