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
| `engine/ut_dump.cpp` | executable: package summary, property counts, dependency check | `upkg`, `config`, `vfs` |
| `tools/compare_engine.py` | runs `ut_dump` against the Python readers on an install | `upkg`, `uprops` (Python) |

## Rules

- Tools: Python stdlib only; a reader never writes into `<UT99_DIR>`.
- Format readers depend only on lower-level readers (every package format sits on `upkg`); no cycles. A class-data reader registers itself with `uobjects.register(<class name>)` and never imports `uobjects` for anything else.
- Engine (when it exists): format modules depend on std only or on other format modules; the game layer never calls OpenGL or SDL; only executables touch SDL.
- No module embeds game data or paths to a real install ([ADR 0001](adr/0001-no-game-data.md)).
