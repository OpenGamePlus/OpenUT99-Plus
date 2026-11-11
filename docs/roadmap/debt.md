# Debt, risks, open questions

## Risks

| Risk | Impact | Note |
|---|---|---|
| Game logic lives in UnrealScript (`.u`) | the engine needs either an UnrealScript VM or native reimplementations of `BotPack` classes | VM chosen ([ADR 0006](../architecture/adr/0006-unrealscript-execution.md)) |
| Native-only behaviour (`Engine.dll`, `Render.dll`) | physics, collision, BSP rendering details have no script to read | observe the original ([ADR 0004](../architecture/adr/0004-clean-room-sources.md)) |
| Replication must match the original byte for byte | network phase E may be infeasible | [ADR 0005](../architecture/adr/0005-network-compatibility.md) has a fallback |

## Deferred decisions

- Whether disassembly of native binaries is ever allowed (now forbidden by ADR 0004).
- README translations.

## Known gaps

- Review minors (2026-11-11): C++ `read_properties` does not stop tag headers at the export limit (wrong output, no UB); `prop_*` accessors ignore the property's type and size; a corrupt dependency aborts a whole map load and failed loads are retried; `compare_engine.py` stops on a Python exception and ignores `ut_dump` exit codes; Python format readers that index bytes or call `struct` directly can still raise `IndexError` / `struct.error` (the sweep counts them as errors).

- VM shortcuts (unverified against the original): integer and float division by zero give 0, float → int out of range gives 0, `~=` on floats uses 1e-4, `Caps` is ASCII only, `Rand` / `FRand` use a different generator; text formats of conversions to string; out-of-bounds array indices are clamped with a warning; instances have no numbered names; struct defaults other than Vector / Rotator and struct inheritance are not read; state `ignores` (ignore mask) not applied; after `GotoState` from state code the new code starts on the next tick.

- World placeholders (2026-11-11), each a `shortcut:` in `engine/world.cpp`: physics is minimal (walking reaches ground speed at once, step 25 and floor 0.7 unverified, `Move` does not slide, collision cylinders are swept as boxes without bevel planes, so moves that put the centre into solid are refused; static actors never move), touching only (no blocking, `Bump` or encroachment), `Trace` reports only the world, `TraceActors` / `TouchingActors` empty, `Visible*Actors` ignore visibility, `TweenAnim` is a pause, negative (velocity-scaled) anim rates run at their magnitude, Mesh / SkeletalMesh sequences not read, no sound (`PlaySound`, `GetSoundDuration` 0), `ConsoleCommand` returns "", `SaveConfig` is dropped (never write into the install), `.int` lists not read, `describeSpec` empty, `Disable` mutes any event name, `SetOwner` / `SetBase` send no events. The play-start order and spawning the game "already begun" are inferred from the scripts, unverified.

- AI placeholders (2026-11-11, `engine/world_ai.cpp`): "reachable" is a clear line and a climb under 120 units; routes start from navigation points within 1,200 units in sight; `FindRandomDest` picks at random; `CanSee` has no view cone (sight events do use `PeripheralVision`); traces through actors ignore their extent; swimming moves like flying; moves end within the two collision radii + 8 or on a timeout; turning is instant; `PickTarget` / `PickAnyTarget` / `PickWallAdjust` find nothing. Bots still stop at obstacles their straight line misses, less since reachability samples the floor and moves give up after a second without progress. Editor cameras are dropped at load (inferred).

- Engine draws mesh actors full bright, frame 0, no `PrePivot` or environment mapping; DXT / 469 compressed textures show a grey checker.

- Light colour model uncalibrated (falloff curve, no N·L, ×2 texture modulation); zero-brightness radius-255 lights leave areas black (DM-Deck16][ spawn). Needs same-spot screenshots of the original.

- Package and object flag bits not named ([package.md](../formats/package.md)).
- Only patch 469e checked; packages from version 436 installs not yet compared.
