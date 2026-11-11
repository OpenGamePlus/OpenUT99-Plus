# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

## 2026-11-10 — procedural textures

- Wet, Wave and Ice textures carry nothing beyond the Texture data; FireTexture adds a spark list (count + 8 bytes each). All 314 byte-exact. How they animate is engine behaviour, still open.

## 2026-11-10 — textures and palettes

- `tools/utex.py`: all 16,427 Palette / Texture / ScriptedTexture objects byte-exact. Mips carry a u32 skip offset from version 63 (absent in 61); 469 packages add a second, compressed mip array when `bHasComp` is set. P8 PNG previews look right.
- Readers now receive the object's properties (`bHasComp` decides the layout). `CompFormat` 7 and 12 (1 byte per pixel) are not identified; DXT1 not decoded in Python.

## 2026-11-10 — object dispatcher

- `tools/uobjects.py`: native class readers register by class name; `--sweep` classifies every export as exact / leftover / error / noreader. Baseline: 83,444 exact, 193,441 without reader (34 native classes + 3,300 class objects). This is the phase A progress meter.

## 2026-11-10 — tagged properties decoded

- `tools/uprops.py`: state frame, tags and 8 property types, with known structs (Vector, Rotator, Scale, PointRegion, Color, Coords) decoded. All 273,585 non-class exports parse; no class mixes "ends at None" with "class data follows", so the reader gives the exact start of the native data.
- Rejected: decoding each value by its own type width; values are read from a slice of `size` bytes, so an unknown struct can't throw the rest of the list out of step.
- Next: class data, starting with `Texture`/`Palette` or `Class`/bytecode (needed for the VM vs. native decision).

## 2026-11-10 — project started, package container decoded

- Docs set up on the OpenBF1942-Plus model (AGENTS.md, AI layer, ADRs 0001–0005, checker). Decided: clean-room engine running retail content, Python stdlib tools + C++17/SDL2/OpenGL 3.3, network compatibility with original 436/469 as a long-term goal.
- `tools/upkg.py` reads header, names, imports and exports; all 396 packages of the 469e install parse (versions 61–69), every export inside its file.
- License AGPL-3.0, same as the sibling projects. Open questions in [debt.md](../roadmap/debt.md).
