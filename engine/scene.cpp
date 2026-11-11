#include "scene.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace ut {

static const uint32_t kBatchFlags = PF_Masked | PF_Translucent | PF_Modulated | PF_TwoSided | PF_FakeBackdrop;

static void read_rotator(const Package& p, const Property& prop, Spawn& s) {
    Reader r(p.data, prop.value_pos);
    s.pitch = r.i32(), s.yaw = r.i32();
}

struct LightInfo {
    Vec3 pos, colour;
    int radius = 64;
    bool on = false;
};

// Light defaults of Engine.Light (shortcut: actors of other light classes use them too, until class defaults are read).
static LightInfo light_info(const Package& p, const Export& e) {
    LightInfo l;
    int brightness = 64, hue = 0, saturation = 255, type = 1;
    size_t end = 0;
    for (const Property& prop : read_properties(p, e, &end)) {
        if (prop.name == "Location") l.pos = prop_vector(p, prop);
        else if (prop.name == "LightBrightness") brightness = prop_byte(p, prop);
        else if (prop.name == "LightHue") hue = prop_byte(p, prop);
        else if (prop.name == "LightSaturation") saturation = prop_byte(p, prop);
        else if (prop.name == "LightRadius") l.radius = prop_byte(p, prop);
        else if (prop.name == "LightType") type = prop_byte(p, prop);
    }
    l.colour = light_colour(hue, saturation, brightness);
    l.on = type != 0;
    return l;
}

static Vec3 zone_ambient(const Package& p, const Export& e) {
    int brightness = 0, hue = 0, saturation = 255;  // Engine.ZoneInfo defaults
    size_t end = 0;
    for (const Property& prop : read_properties(p, e, &end)) {
        if (prop.name == "AmbientBrightness") brightness = prop_byte(p, prop);
        else if (prop.name == "AmbientHue") hue = prop_byte(p, prop);
        else if (prop.name == "AmbientSaturation") saturation = prop_byte(p, prop);
    }
    return light_colour(hue, saturation, brightness);
}

static float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct Placed { int x = 0, y = 0; };

// Fills the atlas region for one lightmap: zone ambient + every listed light whose shadow bit is set.
static void bake(const Model& m, const Surf& s, const LightMap& lm, const std::vector<LightInfo>& lights, Vec3 ambient,
                 Image& atlas, Placed at) {
    int count = lightmap_lights(m, lm);
    for (int j = 0; j < lm.vclamp; j++)
        for (int i = 0; i < lm.uclamp; i++) {
            Vec3 p = luxel_position(m, s, lm, i, j), c = ambient;
            for (int k = 0; k < count; k++) {
                const LightInfo& l = lights[size_t(lm.light_actors + k)];
                if (!l.on || !shadow_bit(m, lm, k, i, j)) continue;
                Vec3 d{l.pos.x - p.x, l.pos.y - p.y, l.pos.z - p.z};
                // shortcut: no incidence (N·L) term; chosen because it gives plausible brightness, uncalibrated
                float w = light_falloff(std::sqrt(dot3(d, d)), l.radius);
                c.x += l.colour.x * w, c.y += l.colour.y * w, c.z += l.colour.z * w;
            }
            uint8_t* px = &atlas.rgba[(size_t(at.y + j) * size_t(atlas.width) + size_t(at.x + i)) * 4];
            px[0] = uint8_t(std::min(c.x, 1.f) * 255.f), px[1] = uint8_t(std::min(c.y, 1.f) * 255.f);
            px[2] = uint8_t(std::min(c.z, 1.f) * 255.f), px[3] = 255;
        }
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
    // Lights in model order (Model::lights holds actor refs; 0 entries end each lightmap's list).
    std::vector<LightInfo> lights;
    for (int32_t ref : m.lights) {
        Object a = set.resolve(*pkg, ref);
        lights.push_back(a.exp && a.exp->size ? light_info(*a.package, *a.exp) : LightInfo{});
    }
    std::vector<Vec3> ambient;
    for (int32_t ref : m.zone_actors) {
        Object z = set.resolve(*pkg, ref);
        ambient.push_back(z.exp && z.exp->size ? zone_ambient(*z.package, *z.exp) : Vec3{0, 0, 0});
    }
    // Place every lightmap in the atlas; texel (0, 0) is reserved full bright (0.5, doubled in the shader).
    const int kAtlasWidth = 2048;
    Atlas atlas(kAtlasWidth);
    std::vector<Placed> placed(m.lightmaps.size());
    int fx, fy;
    atlas.place(1, 1, &fx, &fy);
    for (size_t i = 0; i < m.lightmaps.size(); i++)
        if (!atlas.place(m.lightmaps[i].uclamp, m.lightmaps[i].vclamp, &placed[i].x, &placed[i].y))
            throw std::runtime_error("lightmap wider than the atlas");
    scene.lightmap = Image{kAtlasWidth, std::max(1, atlas.height()), {}};
    scene.lightmap.rgba.assign(size_t(scene.lightmap.width) * size_t(scene.lightmap.height) * 4, 0);
    for (int c = 0; c < 4; c++) scene.lightmap.rgba[size_t(c)] = c == 3 ? 255 : 128;
    std::vector<bool> baked(m.lightmaps.size());
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
        float aw = float(scene.lightmap.width), ah = float(scene.lightmap.height);
        if (s.light_map >= 0 && size_t(s.light_map) < m.lightmaps.size() && !(s.flags & PF_Unlit)) {
            const LightMap& lm = m.lightmaps[size_t(s.light_map)];
            Placed at = placed[size_t(s.light_map)];
            if (!baked[size_t(s.light_map)]) {
                uint8_t zone = n.zone[1] ? n.zone[1] : n.zone[0];
                bake(m, s, lm, lights, zone < ambient.size() ? ambient[zone] : Vec3{0, 0, 0}, scene.lightmap, at);
                baked[size_t(s.light_map)] = true;
            }
            const Vec3 &base = m.points[size_t(s.base)], &tu = m.vectors[size_t(s.texture_u)], &tv = m.vectors[size_t(s.texture_v)];
            for (Vertex& v : tris) {
                Vec3 d{v.x - base.x, v.y - base.y, v.z - base.z};
                v.lu = (float(at.x) + (dot3(d, tu) - lm.pan.x) / lm.uscale + 0.5f) / aw;
                v.lv = (float(at.y) + (dot3(d, tv) - lm.pan.y) / lm.vscale + 0.5f) / ah;
            }
        } else {
            for (Vertex& v : tris) v.lu = 0.5f / aw, v.lv = 0.5f / ah;  // full bright texel
        }
        b.verts.insert(b.verts.end(), tris.begin(), tris.end());
    }

    for (int32_t ref : level.actors) {
        Object a = set.resolve(*pkg, ref);
        if (!a.exp || a.package != pkg || !a.exp->size) continue;
        std::string cls = object_name(*pkg, a.exp->class_ref);
        if (!scene.has_sky && cls.find("Sky") != std::string::npos) {  // shortcut: SkyZoneInfo and subclasses by name
            size_t end = 0;
            for (const Property& prop : read_properties(*pkg, *a.exp, &end))
                if (prop.name == "Location") scene.sky_pos = prop_vector(*pkg, prop), scene.has_sky = true;
        }
        if (cls != "PlayerStart") continue;
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
