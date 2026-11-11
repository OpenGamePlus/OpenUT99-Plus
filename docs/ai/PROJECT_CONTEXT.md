# PROJECT_CONTEXT

## Identity

Clean-room reimplementation of the Unreal Engine 1 runtime as used by Unreal Tournament (1999). Part of the "-Plus" family (OpenBF1942-Plus, OpenCoD2-Plus, OpenMOHAA-Plus): keep an old game playable and moddable on modern hardware.

## Goals

- Run retail UT content (maps, packages, mods) on a new engine.
- Document every file format so others can build on it.
- Play with original UT clients and servers ([ADR 0005](../architecture/adr/0005-network-compatibility.md)); order in [roadmap.md](../roadmap/roadmap.md).

## Non-goals

- Shipping, recreating or replacing game content.
- Patching or wrapping the original executable; the OldUnreal 469 patch already maintains it.

## Anti-goals

- Any game data in the repository ([ADR 0001](../architecture/adr/0001-no-game-data.md)).
- Code translated from the game's UnrealScript or from the native binaries ([ADR 0004](../architecture/adr/0004-clean-room-sources.md)).
- Format claims not checked against retail data.

## Stack

| Part | Choice |
|---|---|
| Research tools | Python 3, stdlib only |
| Engine | C++17 / CMake / SDL2 / OpenGL 3.3 ([ADR 0003](../architecture/adr/0003-engine-stack.md)) |
| Platform | developed on Linux (WSL); target: modern desktop OSes |

## Constraints

- Input data: a retail install at `<UT99_DIR>`, Steam release with OldUnreal patch 469e (`System/Manifest.ini`).
