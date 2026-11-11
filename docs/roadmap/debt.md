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

- World placeholders (2026-11-11), each a `shortcut:` in `engine/world.cpp`: no physics (`SetPhysics` only stores the mode, `Move` never blocks), no collision hash or touching, `FastTrace` always clear, `TraceActors` / `TouchingActors` empty, `Visible*Actors` ignore visibility, animation frames do not advance and `FinishAnim` ends at once, no sound (`PlaySound`, `GetSoundDuration` 0), `ConsoleCommand` returns "", `SaveConfig` is dropped (never write into the install), `.int` lists not read, `describeSpec` empty, `Disable` mutes any event name, `SetOwner` / `SetBase` send no events. The play-start order and spawning the game "already begun" are inferred from the scripts, unverified.

- Engine draws mesh actors full bright, frame 0, no `PrePivot` or environment mapping; DXT / 469 compressed textures show a grey checker.

- Light colour model uncalibrated (falloff curve, no N·L, ×2 texture modulation); zero-brightness radius-255 lights leave areas black (DM-Deck16][ spawn). Needs same-spot screenshots of the original.

- Package and object flag bits not named ([package.md](../formats/package.md)).
- Only patch 469e checked; packages from version 436 installs not yet compared.
