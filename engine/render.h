// OpenGL 3.3 renderer for the map viewer. Needs a current GL context.
#pragma once
#include <memory>
#include <string>

#include "scene.h"

namespace ut {

struct Camera {
    Vec3 pos;
    float yaw = 0, pitch = 0;  // radians; yaw from +X towards +Y, pitch up
    float fov = 90;            // horizontal, degrees (UT default)
};

class Renderer {
public:
    explicit Renderer(const Scene& scene);  // uploads all batches
    ~Renderer();
    void draw(const Camera& cam, int w, int h);
    void save_png(const std::string& path, int w, int h) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace ut
