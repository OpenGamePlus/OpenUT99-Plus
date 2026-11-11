# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

## 2026-11-10 — project started, package container decoded

- Docs set up on the OpenBF1942-Plus model (AGENTS.md, AI layer, ADRs 0001–0005, checker). Decided: clean-room engine running retail content, Python stdlib tools + C++17/SDL2/OpenGL 3.3, network compatibility with original 436/469 as a long-term goal.
- `tools/upkg.py` reads header, names, imports and exports; all 396 packages of the 469e install parse (versions 61–69), every export inside its file.
- License AGPL-3.0, same as the sibling projects. Open questions in [debt.md](../roadmap/debt.md).
