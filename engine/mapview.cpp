// ut_mapview <UT99_DIR> MAP [--shot out.png] [--at x y z yaw pitch]   (yaw, pitch in degrees)
// ut_mapview <UT99_DIR> --check                                     load every map without a window
// Controls: right mouse button + mouse to look, WASD to fly (shift: faster), Esc to quit.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#include "config.h"
#include "packages.h"
#include "render.h"
#include "scene.h"
#include "vfs.h"

using namespace ut;

static const float kRotToRad = 3.14159265f * 2.f / 65536.f;

static int check_all(const std::string& root, const Vfs& vfs) {
    int failed = 0, maps = 0;
    size_t surfaces = 0, missing = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root + "/Maps")) {
        if (entry.path().extension() != ".unr") continue;
        maps++;
        PackageSet set(&vfs);  // fresh per map: keeps memory bounded
        try {
            Scene s = load_scene(set, entry.path().stem().string());
            surfaces += s.surfaces;
            missing += s.missing_textures;
            if (s.missing_textures) std::printf("%s: %zu surfaces without texture\n", entry.path().filename().c_str(), s.missing_textures);
        } catch (const std::exception& ex) {
            failed++;
            std::printf("%s: %s\n", entry.path().filename().c_str(), ex.what());
        }
    }
    std::printf("%d maps, %d errors, %zu surfaces, %zu without texture\n", maps, failed, surfaces, missing);
    return failed ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ut_mapview <UT99_DIR> MAP [--shot out.png] [--at x y z yaw pitch] | ut_mapview <UT99_DIR> --check\n");
        return 2;
    }
    std::string root = argv[1];
    try {
        Vfs vfs(root, read_ini(root + "/System/UnrealTournament.ini"));
        if (!std::strcmp(argv[2], "--check")) return check_all(root, vfs);
        std::string shot;
        Camera cam;
        bool at = false;
        for (int i = 3; i < argc; i++) {
            if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
            else if (!std::strcmp(argv[i], "--at") && i + 5 < argc) {
                cam.pos = {std::strtof(argv[i + 1], nullptr), std::strtof(argv[i + 2], nullptr), std::strtof(argv[i + 3], nullptr)};
                cam.yaw = std::strtof(argv[i + 4], nullptr) * 3.14159265f / 180.f;
                cam.pitch = std::strtof(argv[i + 5], nullptr) * 3.14159265f / 180.f;
                at = true, i += 5;
            }
        }
        PackageSet set(&vfs);
        Scene scene = load_scene(set, argv[2]);
        std::printf("%s: %zu surfaces in %zu batches, %zu without texture, %zu player starts\n", argv[2], scene.surfaces,
                    scene.batches.size(), scene.missing_textures, scene.starts.size());
        if (!at && !scene.starts.empty()) {
            const Spawn& s = scene.starts[0];
            cam.pos = {s.pos.x, s.pos.y, s.pos.z + 30.f};  // shortcut: eye height guessed (collision height 39 - 9)
            cam.yaw = float(s.yaw) * kRotToRad;
            cam.pitch = float(int16_t(s.pitch)) * kRotToRad;
        }

        if (SDL_Init(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        int W = 1280, H = 720;
        SDL_Window* win = SDL_CreateWindow("OpenUT99-Plus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (shot.empty() ? 0 : SDL_WINDOW_HIDDEN));
        if (!win) throw std::runtime_error(SDL_GetError());
        SDL_GLContext ctx = SDL_GL_CreateContext(win);
        if (!ctx) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetSwapInterval(1);
        {
            Renderer renderer(scene);
            if (!shot.empty()) {
                SDL_GL_GetDrawableSize(win, &W, &H);
                renderer.draw(cam, W, H);
                renderer.save_png(shot, W, H);
                std::printf("camera %.0f %.0f %.0f yaw %.1f pitch %.1f\n", double(cam.pos.x), double(cam.pos.y), double(cam.pos.z),
                            double(cam.yaw * 180.f / 3.14159265f), double(cam.pitch * 180.f / 3.14159265f));
            } else {
                bool running = true;
                Uint32 last = SDL_GetTicks();
                while (running) {
                    SDL_Event ev;
                    while (SDL_PollEvent(&ev)) {
                        if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) running = false;
                        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) SDL_SetRelativeMouseMode(SDL_TRUE);
                        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_RIGHT) SDL_SetRelativeMouseMode(SDL_FALSE);
                        if (ev.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode()) {
                            cam.yaw += float(ev.motion.xrel) * 0.003f;
                            cam.pitch = std::fmax(-1.55f, std::fmin(1.55f, cam.pitch - float(ev.motion.yrel) * 0.003f));
                        }
                    }
                    Uint32 now = SDL_GetTicks();
                    float dt = float(now - last) / 1000.f, speed = 600.f;
                    last = now;
                    const Uint8* k = SDL_GetKeyboardState(nullptr);
                    if (k[SDL_SCANCODE_LSHIFT]) speed *= 4;
                    float fx = std::cos(cam.pitch) * std::cos(cam.yaw), fy = std::cos(cam.pitch) * std::sin(cam.yaw), fz = std::sin(cam.pitch);
                    float move = float(k[SDL_SCANCODE_W] - k[SDL_SCANCODE_S]) * speed * dt;
                    float strafe = float(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]) * speed * dt;
                    cam.pos.x += fx * move - std::sin(cam.yaw) * strafe;
                    cam.pos.y += fy * move + std::cos(cam.yaw) * strafe;
                    cam.pos.z += fz * move;
                    SDL_GL_GetDrawableSize(win, &W, &H);
                    renderer.draw(cam, W, H);
                    SDL_GL_SwapWindow(win);
                }
            }
        }
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
