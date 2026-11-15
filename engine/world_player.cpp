// The local player: joining through the game's Login, input into the PlayerPawn script, its view.
// The pawn is moved only by its script (PlayerInput / PlayerTick -> ProcessMove); this file writes the input
// variables the original's input system would and sends the events.
#include "world.h"

namespace ut {

namespace {

Value str(const std::string& s) {
    Value v = Value::of(VT::Str);
    v.s = s;
    return v;
}

Value num(float f) {
    Value v = Value::of(VT::Float);
    v.f = f;
    return v;
}

}  // namespace

PlayerInput scripted_input(float t, float dt) {
    PlayerInput in;
    auto during = [t](float a, float b) { return t >= a && t < b; };
    in.fire = during(1, 1.1f) || during(8, 10) || during(11, 11.1f);
    in.forward = during(1.5f, 6) || t >= 11.5f ? 300.f : 0.f;
    in.strafe = during(6, 8) ? 300.f : 0.f;
    if (during(10, 10 + dt)) in.command = "Suicide";
    return in;
}

Obj* World::login(const std::string& options, const std::string& player_class) {
    Obj* cls = find_class(player_class);
    if (!game || !cls) return errors["login: no game or no class " + player_class]++, nullptr;
    Value spawn_class = Value::of(VT::Object);
    spawn_class.o = cls;
    std::vector<Value> args = {str(""), str(options), str(""), spawn_class};  // Portal, Options, out Error, SpawnClass
    Value ret;
    try {
        vm_.event(game, "Login", args, &ret);
    } catch (const std::exception& e) {
        errors["login: " + std::string(e.what())]++;
    }
    if (!ret.o || deleted_.count(ret.o)) return errors["login: refused: " + args[2].s]++, nullptr;
    player = ret.o;
    if (!find_class("Engine.Player")) errors["login: no Engine.Player class"]++;
    // shortcut: a bare Engine.Player (the original's Viewport subclass is C++ only: no console, no canvas) so scripts
    // see a local player; upgrade when a script needs its Console or the HUD draws.
    if (Obj* vp_cls = find_class("Engine.Player"))
        if (Value* p = vm_.prop(player, "Player")) {
            p->o = vm_.spawn(vp_cls);
            if (Value* a = vm_.prop(p->o, "Actor")) a->o = player;
        }
    // shortcut: Possess (the player takes the pawn: StartWalk) before PostLogin, order unverified.
    guarded(player, "Possess");
    Value np = Value::of(VT::Object);
    np.o = player;
    guarded(game, "PostLogin", {np});
    return player;
}

void World::player_tick(float dt) {
    // shortcut: order unverified (public descriptions of the original's tick: input, PlayerInput, PlayerTick, then
    // the actor's Tick and physics); upgrade with a probe that logs the order.
    auto set = [this](const char* n, float f) {
        if (Value* v = vm_.prop(player, n)) v->f = f;
    };
    auto button = [this](const char* n, bool b) {
        if (Value* v = vm_.prop(player, n)) v->i = b;
    };
    static const char* kAxes[] = {"aForward", "aTurn", "aUp", "aBaseZ"};  // the input system zeroes them each frame
    for (const char* n : kAxes) set(n, 0);
    set("aBaseY", input.forward);
    set("aStrafe", input.strafe);
    set("aBaseX", input.turn);
    set("aLookUp", input.look);
    // UpdateRotation turns by 32 x DeltaTime x aTurn, so the input system hands mouse axes over as a rate.
    // shortcut: the 0.1 scale is tuned by feel (0.32 degrees a pixel at the default sensitivity 3), unverified;
    // upgrade with a measurement of the original's mouse scale.
    float rate = dt > 0 ? 0.1f / dt : 0;
    set("aMouseX", input.mouse_x * rate);
    set("aMouseY", input.mouse_y * rate);
    if (input.duck) set("aUp", -300);  // Duck: Button bDuck | Axis aUp Speed=-300.0
    const Value* was_fire = vm_.prop(player, "bFire");
    const Value* was_alt = vm_.prop(player, "bAltFire");
    bool fire_edge = input.fire && !(was_fire && was_fire->i), alt_edge = input.alt_fire && !(was_alt && was_alt->i);
    button("bFire", input.fire);
    button("bAltFire", input.alt_fire);
    button("bDuck", input.duck);
    if (fire_edge) guarded(player, "Fire", {num(0)});
    if (alt_edge && player && !deleted_.count(player)) guarded(player, "AltFire", {num(0)});
    if (input.jump && player && !deleted_.count(player)) guarded(player, "Jump", {num(0)});
    if (input.weapon && player && !deleted_.count(player)) {
        Value w = Value::of(VT::Byte);
        w.i = input.weapon;
        guarded(player, "SwitchWeapon", {w});
    }
    if (!input.command.empty() && player && !deleted_.count(player)) guarded(player, input.command);
    input.jump = false, input.weapon = 0, input.mouse_x = input.mouse_y = 0, input.command.clear();
    if (!player || deleted_.count(player)) return;
    guarded(player, "PlayerInput", {num(dt)});
    if (!deleted_.count(player)) guarded(player, "PlayerTick", {num(dt)});
}

bool World::player_view(Vec3* location, int32_t rotation[3]) {
    if (player && deleted_.count(player)) player = nullptr;
    if (!player) return false;
    std::vector<Value> args = {Value::of(VT::Object), Value::of(VT::Vector), Value::of(VT::Rotator)};
    args[0].o = player;
    if (const Value* l = vm_.prop(player, "Location")) std::copy(l->v, l->v + 3, args[1].v);
    if (const Value* r = vm_.prop(player, "ViewRotation")) std::copy(r->r, r->r + 3, args[2].r);
    try {
        vm_.event(player, "PlayerCalcView", args);
    } catch (const std::exception& e) {
        errors["PlayerCalcView: " + std::string(e.what())]++;
    }
    *location = {args[1].v[0], args[1].v[1], args[1].v[2]};
    std::copy(args[2].r, args[2].r + 3, rotation);
    return true;
}

}  // namespace ut
