// What a running world looks like this frame: its live mesh actors (animated) and movers, as world-space triangles
// for the renderer. No GL here.
#pragma once
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "mesh.h"
#include "scene.h"
#include "world.h"

namespace ut {

class PlayView {
public:
    PlayView(World& world, Vm& vm, PackageSet& set) : world_(world), vm_(vm), set_(set) {}
    // Actors with DrawType DT_Mesh (a LodMesh, frame from AnimSequence / AnimFrame) or DT_Brush (movers), not hidden.
    const std::vector<LiveBatch>& build();
    size_t drawn = 0;  // actors in the last build

private:
    const LodMesh* mesh(const Obj* m);
    const Image* image(const Package* p, const Export* e, bool masked);  // nullptr export: the checker
    void add_mesh(Obj* actor, const LodMesh& m, const Obj* mesh_obj);
    void add_brush(Obj* mover);
    size_t batch(const Image* image, uint32_t flags);  // index into out_

    World& world_;
    Vm& vm_;
    PackageSet& set_;
    std::map<const Obj*, std::unique_ptr<LodMesh>> meshes_;  // nullptr: not a readable LodMesh
    std::map<std::tuple<const Export*, bool>, std::unique_ptr<Image>> images_;
    struct BrushPart {
        const Image* image;
        uint32_t flags;
        std::vector<Vertex> local;  // brush space
    };
    std::map<const Obj*, std::vector<BrushPart>> brushes_;  // by Brush model object
    std::map<std::pair<const Image*, uint32_t>, size_t> slots_;
    std::vector<LiveBatch> out_;
};

}  // namespace ut
