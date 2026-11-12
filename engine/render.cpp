#include "render.h"

#include <epoxy/gl.h>

#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

#include "png.h"

namespace ut {

static const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec2 luv;
uniform mat4 mvp;
out vec2 v_uv;
out vec2 v_luv;
void main() { v_uv = uv; v_luv = luv; gl_Position = mvp * vec4(pos, 1.0); })";

static const char* kFrag = R"(#version 330 core
in vec2 v_uv;
in vec2 v_luv;
uniform sampler2D tex;
uniform sampler2D lightmap;
uniform bool masked;
uniform bool lit;
out vec4 colour;
void main() {
    vec4 c = texture(tex, v_uv);
    if (masked && c.a < 0.5) discard;
    vec3 light = lit ? 2.0 * texture(lightmap, v_luv).rgb : vec3(1.0);
    colour = vec4(c.rgb * light, 1.0);
})";

struct GpuBatch {
    GLuint vao = 0, vbo = 0, tex = 0;
    GLsizei count = 0;
    uint32_t flags = 0;
};

struct Renderer::Impl {
    GLuint prog = 0, lightmap = 0;
    GLint mvp = -1, masked = -1, lit = -1;
    std::vector<GpuBatch> batches;
    bool has_sky = false;
    Vec3 sky_pos{};
    GLuint live_vao = 0, live_vbo = 0;
    std::map<const Image*, GLuint> live_tex;  // textures of live batches, uploaded on first use
    void pass(const Camera& cam, int w, int h, bool backdrop_depth_only, const std::vector<LiveBatch>* live);
    GLuint texture_of(const Image* image);
};

static void vertex_layout() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(5 * sizeof(float)));
}

static GLuint upload_texture(const Image& image) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return t;
}

GLuint Renderer::Impl::texture_of(const Image* image) {
    auto it = live_tex.find(image);
    if (it == live_tex.end()) it = live_tex.emplace(image, upload_texture(*image)).first;
    return it->second;
}

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        throw std::runtime_error(std::string("shader: ") + log);
    }
    return s;
}

Renderer::Renderer(const Scene& scene) : p_(new Impl) {
    p_->prog = glCreateProgram();
    glAttachShader(p_->prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(p_->prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glLinkProgram(p_->prog);
    p_->mvp = glGetUniformLocation(p_->prog, "mvp");
    p_->masked = glGetUniformLocation(p_->prog, "masked");
    p_->lit = glGetUniformLocation(p_->prog, "lit");
    glUseProgram(p_->prog);
    glUniform1i(glGetUniformLocation(p_->prog, "tex"), 0);
    glUniform1i(glGetUniformLocation(p_->prog, "lightmap"), 1);
    p_->has_sky = scene.has_sky, p_->sky_pos = scene.sky_pos;
    glGenTextures(1, &p_->lightmap);
    glBindTexture(GL_TEXTURE_2D, p_->lightmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, scene.lightmap.width, scene.lightmap.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 scene.lightmap.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (const Batch& b : scene.batches) {
        GpuBatch g;
        g.flags = b.flags;
        g.count = GLsizei(b.verts.size());
        glGenVertexArrays(1, &g.vao);
        glBindVertexArray(g.vao);
        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.verts.size() * sizeof(Vertex)), b.verts.data(), GL_STATIC_DRAW);
        vertex_layout();
        g.tex = upload_texture(b.image);
        p_->batches.push_back(g);
    }
    glGenVertexArrays(1, &p_->live_vao);
    glBindVertexArray(p_->live_vao);
    glGenBuffers(1, &p_->live_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p_->live_vbo);
    vertex_layout();
}

Renderer::~Renderer() {
    for (GpuBatch& g : p_->batches) {
        glDeleteTextures(1, &g.tex);
        glDeleteBuffers(1, &g.vbo);
        glDeleteVertexArrays(1, &g.vao);
    }
    for (auto& [image, t] : p_->live_tex) glDeleteTextures(1, &t);
    glDeleteBuffers(1, &p_->live_vbo);
    glDeleteVertexArrays(1, &p_->live_vao);
    glDeleteTextures(1, &p_->lightmap);
    glDeleteProgram(p_->prog);
}

// Column-major 4x4. UE world (X forward, Y right, Z up, left-handed) → GL eye space (right-handed, -Z forward).
static void view_projection(const Camera& c, float aspect, float out[16]) {
    float cy = std::cos(c.yaw), sy = std::sin(c.yaw), cp = std::cos(c.pitch), sp = std::sin(c.pitch);
    float f[3] = {cp * cy, cp * sy, sp};       // forward
    float r[3] = {-sy, cy, 0};                 // right (UE +Y at yaw 0)
    float u[3] = {-sp * cy, -sp * sy, cp};     // up
    float t = 1.f / std::tan(c.fov * 3.14159265f / 360.f), n = 4.f, far = 65536.f;
    float px = t, py = t * aspect, a = (far + n) / (n - far), b = 2 * far * n / (n - far);
    // eye = (dot(r, p - pos), dot(u, p - pos), -dot(f, p - pos))
    float d[3] = {c.pos.x, c.pos.y, c.pos.z};
    auto dot = [&](const float* v) { return v[0] * d[0] + v[1] * d[1] + v[2] * d[2]; };
    float m[16] = {px * r[0], py * u[0], -a * f[0], f[0],
                   px * r[1], py * u[1], -a * f[1], f[1],
                   px * r[2], py * u[2], -a * f[2], f[2],
                   -px * dot(r), -py * dot(u), a * dot(f) + b, -dot(f)};
    for (int i = 0; i < 16; i++) out[i] = m[i];
}

void Renderer::draw(const Camera& cam, int w, int h, const std::vector<LiveBatch>& live) {
    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (p_->has_sky) {  // the sky zone seen from its SkyZoneInfo, behind everything
        Camera sky = cam;
        sky.pos = p_->sky_pos;
        p_->pass(sky, w, h, true, nullptr);
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    p_->pass(cam, w, h, true, &live);
}

// backdrop_depth_only: FakeBackdrop surfaces write depth but no colour, so the sky drawn before shows through them.
void Renderer::Impl::pass(const Camera& cam, int w, int h, bool backdrop_depth_only, const std::vector<LiveBatch>* live) {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);  // shortcut: BSP winding not checked yet, draw both sides
    glUseProgram(prog);
    float m[16];
    view_projection(cam, float(w) / float(h), m);
    glUniformMatrix4fv(mvp, 1, GL_FALSE, m);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, lightmap);
    glActiveTexture(GL_TEXTURE0);
    for (int pass = 0; pass < 2; pass++) {  // 0: opaque and masked, 1: translucent / modulated
        if (pass == 1) {
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
        }
        for (const GpuBatch& g : batches) {
            bool blended = g.flags & (PF_Translucent | PF_Modulated);
            if (blended != (pass == 1)) continue;
            if (g.flags & PF_Modulated) glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
            else glBlendFunc(GL_ONE, GL_ONE);
            bool backdrop = g.flags & PF_FakeBackdrop;
            if (backdrop && backdrop_depth_only && has_sky) glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glUniform1i(masked, g.flags & PF_Masked ? 1 : 0);
            glUniform1i(lit, blended ? 0 : 1);
            glBindTexture(GL_TEXTURE_2D, g.tex);
            glBindVertexArray(g.vao);
            glDrawArrays(GL_TRIANGLES, 0, g.count);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        for (size_t i = 0; live && i < live->size(); i++) {  // shortcut: one upload per batch per frame, full bright
            const LiveBatch& b = (*live)[i];
            bool blended = b.flags & (PF_Translucent | PF_Modulated);
            if (blended != (pass == 1) || b.verts.empty() || !b.image) continue;
            if (b.flags & PF_Modulated) glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
            else glBlendFunc(GL_ONE, GL_ONE);
            glUniform1i(masked, b.flags & PF_Masked ? 1 : 0);
            glUniform1i(lit, 0);
            glBindTexture(GL_TEXTURE_2D, texture_of(b.image));
            glBindVertexArray(live_vao);
            glBindBuffer(GL_ARRAY_BUFFER, live_vbo);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.verts.size() * sizeof(Vertex)), b.verts.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, GLsizei(b.verts.size()));
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::save_png(const std::string& path, int w, int h) const {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4), flipped(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    size_t row = size_t(w) * 4;
    for (size_t y = 0; y < size_t(h); y++)
        std::copy(px.begin() + long((size_t(h) - 1 - y) * row), px.begin() + long((size_t(h) - y) * row), flipped.begin() + long(y * row));
    write_png(path, w, h, flipped);
}

}  // namespace ut
