# Boundaries

## Modules

| Module | Role | May use |
|---|---|---|
| `tools/upkg.py` | Unreal package reader: header, names, imports, exports | std only |
| `tools/check_docs.py` | documentation checker | std only |

The engine (`engine/`, C++) does not exist yet; its modules get rows here as they are added.

## Rules

- Tools: Python stdlib only; a reader never writes into `<UT99_DIR>`.
- Format readers depend only on lower-level readers (every package format sits on `upkg`); no cycles.
- Engine (when it exists): format modules depend on std only or on other format modules; the game layer never calls OpenGL or SDL; only executables touch SDL.
- No module embeds game data or paths to a real install ([ADR 0001](adr/0001-no-game-data.md)).
