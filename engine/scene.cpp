#include "scene.h"

#include <map>
#include <stdexcept>
#include <tuple>

namespace ut {

static const uint32_t kBatchFlags = PF_Masked | PF_Translucent | PF_Modulated | PF_TwoSided;

static void read_rotator(const Package& p, const Property& prop, Spawn& s) {
    Reader r(p.data, prop.value_pos);
    s.pitch = r.i32(), s.yaw = r.i32();
}

Scene load_scene(PackageSet& set, const std::string& map) {
    const Package* pkg = set.get(map);
    if (!pkg) throw std::runtime_error("map not found: " + map);
    const Export* level_exp = nullptr;
    for (const Export& e : pkg->exports)
        if (e.size && e.class_ref && object_name(*pkg, e.class_ref) == "Level") level_exp = &e;
    if (!level_exp) throw std::runtime_error("no Level in " + map);
    Level level = read_level(*pkg, *level_exp);
    Object model_obj = set.resolve(*pkg, level.model);
    if (!model_obj.exp) throw std::runtime_error("no Model in " + map);
    Model m = read_model(*model_obj.package, *model_obj.exp);

    Scene scene;
    std::map<std::tuple<const Export*, uint32_t>, size_t> batch_of;
    for (const Node& n : m.nodes) {
        const Surf& s = m.surfs[size_t(n.surf)];
        if (s.flags & (PF_Invisible | PF_Portal) || n.num_vertices < 3) {
            scene.skipped++;
            continue;
        }
        scene.surfaces++;
        Object tex = set.resolve(*pkg, s.texture);
        uint32_t flags = s.flags & kBatchFlags;
        auto key = std::make_tuple(tex.exp, flags);
        auto it = batch_of.find(key);
        if (it == batch_of.end()) {
            Batch b;
            b.flags = flags;
            if (tex.exp) {
                b.texture = object_name(*pkg, s.texture);
                b.image = texture_rgba(*tex.package, *tex.exp, flags & PF_Masked);
            } else {
                b.texture = "(missing)";
                b.image = texture_rgba(Package{}, Export{}, false);  // falls back to the checker
            }
            it = batch_of.emplace(key, scene.batches.size()).first;
            scene.batches.push_back(std::move(b));
        }
        if (!tex.exp) scene.missing_textures++;
        Batch& b = scene.batches[it->second];
        std::vector<Vertex> tris = node_triangles(m, n, b.image.width, b.image.height);
        b.verts.insert(b.verts.end(), tris.begin(), tris.end());
    }

    for (int32_t ref : level.actors) {
        Object a = set.resolve(*pkg, ref);
        if (!a.exp || a.package != pkg || object_name(*pkg, a.exp->class_ref) != "PlayerStart") continue;
        size_t end = 0;
        Spawn sp{};
        for (const Property& prop : read_properties(*pkg, *a.exp, &end)) {
            if (prop.name == "Location") sp.pos = prop_vector(*pkg, prop);
            if (prop.name == "Rotation") read_rotator(*pkg, prop, sp);
        }
        scene.starts.push_back(sp);
    }
    return scene;
}

}  // namespace ut
