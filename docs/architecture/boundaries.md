# Boundaries

## Modules

| Module | Role | May use |
|---|---|---|
| `tools/upkg.py` | Unreal package reader: header, names, imports, exports | std only |
| `tools/uprops.py` | tagged properties, state frame | `upkg` |
| `tools/uobjects.py` | dispatcher: properties then class data, per-class sweep | `upkg`, `uprops`, reader modules (`tools/u*.py`, loaded by name) |
| `tools/utex.py` | Palette, Texture mips, PNG preview | `upkg`, `uobjects` |
| `tools/usound.py` | Sound, Music: embedded WAV / tracker files | `upkg`, `uobjects` |
| `tools/ufont.py` | Font pages and glyphs | `upkg`, `uobjects` |
| `tools/uscript.py` | script objects, property declarations, bytecode walker | `upkg`, `uprops`, `uobjects` |
| `tools/umodel.py` | Polys, Model (BSP), Level | `upkg`, `uobjects` |
| `tools/umesh.py` | Mesh, LodMesh, SkeletalMesh, Animation, OBJ export | `upkg`, `uobjects` |
| `tools/uini.py` | `.ini` parsing, package lookup, dependency check | `upkg` |
| `tools/check_docs.py` | documentation checker | std only |

| `engine/upkg.*` (library `ut`) | packages, compact index, strings, lazy arrays, tagged properties (port of `upkg.py` + `uprops.py`) | std only |
| `engine/config.*` | `.ini` parsing | std only |
| `engine/vfs.*` | package name → file (Paths, cache) | `config` |
| `engine/packages.*` | loaded packages, cross-package object references | `upkg`, `vfs` |
| `engine/texture.*` | Palette + P8 Texture → RGBA | `upkg` |
| `engine/level.*` | Model (BSP), Level, surface triangles | `upkg` |
| `engine/png.*` | PNG encoder | std only |
| `engine/script.*` | bytecode walker, class objects (defaults) | `upkg` |
| `engine/mesh.*` | LodMesh | `upkg` |
| `engine/vm.*` | UnrealScript VM: functions in memory form, interpreter, natives by declared name | `upkg`, `script`, `packages` |
| `engine/collision.*` | BSP point solidity, swept boxes | `level` |
| `engine/world.*` | map actors as VM instances, play start, ticking, timers, physics, Actor natives | `vm`, `level`, `collision`, `packages` |
| `engine/lighting.*` | luxel positions, shadow bits, light colours, atlas packer | `level` |
| `engine/scene.*` | map → texture batches, baked lightmap atlas, sky position, mesh actors (property lookup through class defaults), player starts (no GL) | `packages`, `level`, `texture`, `lighting`, `script`, `mesh` |
| `engine/render.*` (library `ut_render`) | OpenGL 3.3 renderer, screenshots | `scene`, `png`, epoxy |
| `engine/mapview.cpp` | executable `ut_mapview`: window, input, `--shot`, `--check` | `render`, `scene`, `vfs`, SDL2 |
| `engine/ut_dump.cpp` | executable: package summary, property counts, model, class-default and mesh counts, dependency check | `upkg`, `level`, `script`, `mesh`, `config`, `vfs` |
| `engine/ut_script.cpp` | executable: run a script function, a class's auto state, or maps; sweeps | `vm`, `world`, `config`, `vfs` |
| `tools/compare_engine.py` | runs `ut_dump` against the Python readers on an install | `upkg`, `uprops` (Python) |

## Rules

- Tools: Python stdlib only; a reader never writes into `<UT99_DIR>`.
- Format readers depend only on lower-level readers (every package format sits on `upkg`); no cycles. A class-data reader registers itself with `uobjects.register(<class name>)`; besides that it may import `uobjects.read_object` only for its command-line entry point.
- Engine: format modules depend on std only or on other format modules; nothing below `render` calls OpenGL; only executables touch SDL.
- No module embeds game data or paths to a real install ([ADR 0001](adr/0001-no-game-data.md)).
