// ut_mapview <UT99_DIR> MAP [--shot out.png] [--at x y z yaw pitch]   (yaw, pitch in degrees)
// ut_mapview ... --play [--time SECONDS] [--follow]  runs the match (bots, movers) in the view; with --shot, the
//                                                   picture after SECONDS (default 10), --follow behind the first bot
// ut_mapview ... --player [--mouse-free]  joins the match as a player (implies --play); with --shot, the player plays
//                                       a fixed run (scripted_input) for --time seconds and the shot is its view
// ut_mapview ... --lightmap out.png writes the baked lightmap atlas
// ut_mapview <UT99_DIR> --check                                     load every map without a window
// Controls: hold a mouse button and drag to look (or the arrow keys), WASD to fly (shift: faster), F follows the next
// bot, Esc to quit. As a player: WASD move, mouse (while a button is held; always with --mouse-free) looks, left /
// right button fire / alt fire, arrows turn and look, Space jumps, left Ctrl ducks, 1-0 switch weapons.
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
#include "play_view.h"
#include "png.h"
#include "render.h"
#include "scene.h"
#include "vfs.h"
#include "vm.h"
#include "world.h"

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

// The match: a world running the map's script, and what it looks like each frame.
struct Play {
    Vm vm;
    World world;
    PlayView view;
    Play(PackageSet& set, const std::string& map, const std::string& game) : vm(&set), world(vm, set), view(world, vm, set) {
        for (const char* n : {"Core", "Engine"})
            if (const Package* p = set.get(n)) vm.bind_natives(*p);
        world.load(map, game);
        world.begin_play();
    }
    std::vector<Obj*> pawns() {
        std::vector<Obj*> out;
        for (const Value* p = vm.prop(world.level_info, "PawnList"); p && p->o && out.size() < 64; p = vm.prop(p->o, "nextPawn"))
            out.push_back(p->o);
        return out;
    }
};

// The logged-in player's own view (PlayerCalcView); leaves cam as it is without a player.
static void player_camera(Camera& cam, Play& play) {
    Vec3 at;
    int32_t rot[3];
    if (!play.world.player_view(&at, rot)) return;
    cam.pos = at;
    cam.pitch = float(int16_t(rot[0])) * kRotToRad;
    cam.yaw = float(rot[1]) * kRotToRad;
}

// "health H - frags F - time T" for the window title (no HUD yet).
static std::string player_status(Play& play) {
    Obj* me = play.world.player;
    const Value* health = play.vm.prop(me, "Health");
    const Value* pri = play.vm.prop(me, "PlayerReplicationInfo");
    const Value* gri = play.world.game ? play.vm.prop(play.world.game, "GameReplicationInfo") : nullptr;
    const Value* remaining = gri && gri->o ? play.vm.prop(gri->o, "RemainingTime") : nullptr;
    int t = remaining && remaining->i > 0 ? remaining->i : int(play.world.time);
    return "health " + std::to_string(health ? health->i : 0) + " - frags " +
           std::to_string(int(pri && pri->o ? play.vm.prop(pri->o, "Score")->f : 0.f)) + " - time " + std::to_string(t / 60) +
           ":" + (t % 60 < 10 ? "0" : "") + std::to_string(t % 60);
}

// Camera behind and above the followed pawn, looking the way the camera already looks.
static void follow(Camera& cam, Play& play, Obj* target) {
    const Value* l = play.vm.prop(target, "Location");
    if (!l) return;
    float fx = std::cos(cam.pitch) * std::cos(cam.yaw), fy = std::cos(cam.pitch) * std::sin(cam.yaw), fz = std::sin(cam.pitch);
    cam.pos = {l->v[0] - fx * 160.f, l->v[1] - fy * 160.f, l->v[2] - fz * 160.f + 40.f};
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ut_mapview <UT99_DIR> MAP [--shot out.png] [--at x y z yaw pitch] [--play [--time S] [--follow]] | ut_mapview <UT99_DIR> --check\n");
        return 2;
    }
    std::string root = argv[1];
    try {
        Ini ini = read_ini(root + "/System/UnrealTournament.ini");
        Vfs vfs(root, ini);
        if (!std::strcmp(argv[2], "--check")) return check_all(root, vfs);
        std::string shot, lightmap_png;
        Camera cam;
        bool at = false, play_mode = false, follow_first = false, as_player = false, mouse_free = false;
        float play_time = 10;
        for (int i = 3; i < argc; i++) {
            if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
            else if (!std::strcmp(argv[i], "--lightmap") && i + 1 < argc) lightmap_png = argv[++i];
            else if (!std::strcmp(argv[i], "--play")) play_mode = true;
            else if (!std::strcmp(argv[i], "--follow")) follow_first = true;
            else if (!std::strcmp(argv[i], "--player")) as_player = play_mode = true;
            else if (!std::strcmp(argv[i], "--mouse-free")) mouse_free = true;
            else if (!std::strcmp(argv[i], "--time") && i + 1 < argc) play_time = std::strtof(argv[++i], nullptr);
            else if (!std::strcmp(argv[i], "--at") && i + 5 < argc) {
                cam.pos = {std::strtof(argv[i + 1], nullptr), std::strtof(argv[i + 2], nullptr), std::strtof(argv[i + 3], nullptr)};
                cam.yaw = std::strtof(argv[i + 4], nullptr) * 3.14159265f / 180.f;
                cam.pitch = std::strtof(argv[i + 5], nullptr) * 3.14159265f / 180.f;
                at = true, i += 5;
            }
        }
        PackageSet set(&vfs);
        Scene scene = load_scene(set, argv[2], !play_mode);
        std::printf("%s: %zu surfaces in %zu batches, %zu without texture, %zu mesh actors, %zu player starts\n", argv[2],
                    scene.surfaces, scene.batches.size(), scene.missing_textures, scene.mesh_actors, scene.starts.size());
        if (!lightmap_png.empty()) write_png(lightmap_png, scene.lightmap.width, scene.lightmap.height, scene.lightmap.rgba);
        if (!at && !scene.starts.empty()) {
            const Spawn& s = scene.starts[0];
            cam.pos = {s.pos.x, s.pos.y, s.pos.z + 30.f};  // shortcut: eye height guessed (collision height 39 - 9)
            cam.yaw = float(s.yaw) * kRotToRad;
            cam.pitch = float(int16_t(s.pitch)) * kRotToRad;
        }
        std::unique_ptr<Play> play;
        if (play_mode) {
            const auto& game = ini["Engine.Engine"]["DefaultGame"];
            play = std::make_unique<Play>(set, argv[2], game.empty() ? "" : game[0]);
            if (as_player) {
                const auto& cls = ini["URL"]["Class"];  // the retail default player class
                std::string c = cls.empty() ? "Botpack.TMale1" : cls[0];
                if (!play->world.login("?Name=Player?Class=" + c, c)) {
                    for (const auto& [e, n] : play->world.errors) std::fprintf(stderr, "%s\n", e.c_str());
                    std::fprintf(stderr, "could not join the match: free camera\n");
                }
            }
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
                if (play) {
                    for (float t = 0; t < play_time; t += 0.05f) {
                        if (play->world.player) play->world.input = scripted_input(t, 0.05f);
                        play->world.tick(0.05f);
                    }
                    player_camera(cam, *play);
                    std::vector<Obj*> pawns = play->pawns();
                    if (follow_first && !pawns.empty()) {
                        const Value* rot = play->vm.prop(pawns[0], "Rotation");
                        cam.yaw = rot ? float(rot->r[1]) * kRotToRad : 0, cam.pitch = -0.25f;
                        follow(cam, *play, pawns[0]);
                    }
                    int errors = 0;
                    for (const auto& [e, n] : play->world.errors) errors += n;
                    std::printf("played %.1f s: %zu pawns, %d script errors\n", double(play_time), pawns.size(), errors);
                }
                SDL_GL_GetDrawableSize(win, &W, &H);
                renderer.draw(cam, W, H, play ? play->view.build() : std::vector<LiveBatch>{});
                if (play && play->world.player) renderer.crosshair(W, H);
                if (play) std::printf("%zu live actors drawn\n", play->view.drawn);
                renderer.save_png(shot, W, H);
                std::printf("camera %.0f %.0f %.0f yaw %.1f pitch %.1f\n", double(cam.pos.x), double(cam.pos.y), double(cam.pos.z),
                            double(cam.yaw * 180.f / 3.14159265f), double(cam.pitch * 180.f / 3.14159265f));
            } else {
                bool running = true;
                // Mouse look while a button is held, from plain motion events: relative mode and pointer warping
                // feed back into endless turning under WSLg.
                bool looking = false;
                if (mouse_free && play && play->world.player) SDL_SetRelativeMouseMode(SDL_TRUE), looking = true;
                bool fire = false, alt_fire = false;
                Uint32 last = SDL_GetTicks();
                Obj* followed = nullptr;
                size_t follow_index = 0;
                float title_timer = 0;
                while (running) {
                    SDL_Event ev;
                    while (SDL_PollEvent(&ev)) {
                        if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) running = false;
                        if (ev.type == SDL_MOUSEBUTTONDOWN) looking = true;
                        if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) {
                            bool down = ev.type == SDL_MOUSEBUTTONDOWN;
                            if (ev.button.button == SDL_BUTTON_LEFT) fire = down;
                            if (ev.button.button == SDL_BUTTON_RIGHT) alt_fire = down;
                        }
                        if (ev.type == SDL_MOUSEBUTTONUP || (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST))
                            looking = mouse_free && play && play->world.player, fire = alt_fire = false;
                        if (ev.type == SDL_KEYDOWN && !ev.key.repeat && play && play->world.player) {
                            SDL_Keycode key = ev.key.keysym.sym;
                            if (key == SDLK_SPACE) play->world.input.jump = true;
                            if (key >= SDLK_1 && key <= SDLK_9) play->world.input.weapon = int(key - SDLK_0);
                            if (key == SDLK_0) play->world.input.weapon = 10;
                        }
                        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_f && play) {
                            std::vector<Obj*> pawns = play->pawns();
                            followed = pawns.empty() ? nullptr : pawns[follow_index++ % pawns.size()];
                        }
                        if (ev.type == SDL_MOUSEMOTION && looking && std::abs(ev.motion.xrel) < 200 && std::abs(ev.motion.yrel) < 200 &&
                            play && play->world.player) {  // Axis aMouseX / aMouseY Speed=6.0; SDL's y grows downwards
                            play->world.input.mouse_x += float(ev.motion.xrel) * 6.f;
                            play->world.input.mouse_y -= float(ev.motion.yrel) * 6.f;
                        } else if (ev.type == SDL_MOUSEMOTION && looking && std::abs(ev.motion.xrel) < 200 && std::abs(ev.motion.yrel) < 200) {
                            cam.yaw += float(ev.motion.xrel) * 0.003f;
                            cam.pitch = std::fmax(-1.55f, std::fmin(1.55f, cam.pitch - float(ev.motion.yrel) * 0.003f));
                        }
                    }
                    Uint32 now = SDL_GetTicks();
                    float dt = float(now - last) / 1000.f, speed = 600.f;
                    last = now;
                    const Uint8* k = SDL_GetKeyboardState(nullptr);
                    if (play && play->world.player) {  // the retail default binding speeds (User.ini aliases)
                        PlayerInput& in = play->world.input;
                        in.forward = float(k[SDL_SCANCODE_W] - k[SDL_SCANCODE_S]) * 300.f;
                        in.strafe = float(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]) * 300.f;
                        in.turn = float(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]) * 150.f;
                        in.look = float(k[SDL_SCANCODE_UP] - k[SDL_SCANCODE_DOWN]) * 100.f;
                        in.duck = k[SDL_SCANCODE_LCTRL];
                        in.fire = fire, in.alt_fire = alt_fire;
                        if (in.forward != 0 || in.strafe != 0) followed = nullptr;
                        speed = 0;  // the camera does not fly
                    }
                    if (k[SDL_SCANCODE_LSHIFT]) speed *= 4;
                    if (speed > 0) cam.yaw += float(k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]) * 2.f * dt;  // keyboard look
                    if (speed > 0) cam.pitch = std::fmax(-1.55f, std::fmin(1.55f, cam.pitch + float(k[SDL_SCANCODE_UP] - k[SDL_SCANCODE_DOWN]) * 1.5f * dt));
                    float fx = std::cos(cam.pitch) * std::cos(cam.yaw), fy = std::cos(cam.pitch) * std::sin(cam.yaw), fz = std::sin(cam.pitch);
                    float move = float(k[SDL_SCANCODE_W] - k[SDL_SCANCODE_S]) * speed * dt;
                    float strafe = float(k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]) * speed * dt;
                    if (speed > 0 && (move != 0 || strafe != 0)) followed = nullptr;  // flying again
                    cam.pos.x += fx * move - std::sin(cam.yaw) * strafe;
                    cam.pos.y += fy * move + std::cos(cam.yaw) * strafe;
                    cam.pos.z += fz * move;
                    if (play) {
                        play->world.tick(std::fmin(dt, 0.05f));  // shortcut: a slow frame slows the match down
                        if (followed && play->world.destroyed(followed)) followed = nullptr;
                        if (followed) follow(cam, *play, followed);
                        else player_camera(cam, *play);
                        if ((title_timer -= dt) <= 0) {
                            title_timer = 0.5f;
                            std::string title = std::string("OpenUT99-Plus - ") + argv[2] + " - " +
                                                (play->world.player ? player_status(*play)
                                                                    : std::to_string(int(play->world.time)) + " s, " +
                                                                          std::to_string(play->pawns().size()) + " pawns") +
                                                (followed ? " - following " + play->vm.name_of(followed) : "");
                            SDL_SetWindowTitle(win, title.c_str());
                        }
                    }
                    SDL_GL_GetDrawableSize(win, &W, &H);
                    renderer.draw(cam, W, H, play ? play->view.build() : std::vector<LiveBatch>{});
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
