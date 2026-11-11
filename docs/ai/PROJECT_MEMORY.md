# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

## 2026-11-10 — mesh actors

- The viewer draws actors with LodMeshes: properties come from the actor, then its class defaults up the super chain (C++ class reader with the bytecode walker; class defaults and meshes match Python on all packages). Pickups, decorations and the EOL_Statues trophies stand upright and textured.
- Fixed on the way: class imports must match class objects (export class 0). Mesh space needs RotOrigin inverted; with it, raw Y-up player meshes and roll-rotated trophies come out upright.

## 2026-11-10 — lightmaps and sky

- Lightmap layout pinned with statistics: luxels sample the polygon in texture space from `pan` (U min − 0.1) by U/V scale; shadow bits LSB first; set bits lie within 25 × (LightRadius + 1) of their light. Baked into one atlas per map; CTF-Face shows its star-field sky through FakeBackdrop surfaces drawn depth-only after a pass from the SkyZoneInfo.
- Light intensity is a guess: with N·L the median lit luxel was 0.18 (too dark), without it 0.34; kept without, flagged in debt. Deck16][ spawn is lit only by brightness-0 lights and stays black: compare with the original.

## 2026-11-10 — first level viewer

- `ut_mapview`: C++ Model reader (0 differences vs Python on all maps), cross-package texture resolution, P8 textures, BSP surfaces batched per texture, OpenGL 3.3. All 83 maps load; the only untextured surfaces (88) have texture `None` in the data. Shots of Deck16][, Face and Morpheus look right, text unmirrored.
- Level models carry vert entries beyond the points array; no node uses them, so validation checks only node-referenced verts.
- Not yet: lightmaps (full bright), sky, actors, back-face culling, DXT / 469 compressed textures (checker).

## 2026-11-10 — configuration and package lookup

- `tools/uini.py` and `engine/config.*` / `engine/vfs.*`: `.ini` sections with repeated keys; packages found through `[Core.System] Paths`, then the download cache (`cache.ini` maps GUID file names to package names). Every top-level import of every retail package resolves, in Python and C++.

## 2026-11-10 — C++ engine started

- `CMakeLists.txt`, `engine/upkg.*`: package and tagged-property reader in C++17, built with `-Wall -Wextra -Wconversion`, no warnings; unit tests reuse the Python synthetic inputs.
- `tools/compare_engine.py` runs `ut_dump` against the Python readers: 0 differences on all 396 packages. The C++ loader also rejects exports outside the file (the Python one does not).

## 2026-11-10 — UnrealScript execution proposal

- ADR 0006 (Proposed): run the bytecode in a VM. Gameplay and UI packages have zero native functions (≈ 7,300 functions, 1.3 MB of bytecode); the 383 natives live in Core, Engine and the network / web / demo packages. Native reimplementation was weighed and rejected in the proposal (ADR 0004 forbids line-by-line translation; networking and mods need the original logic). The maintainer decides.

## 2026-11-10 — meshes; phase A exit check met

- `tools/umesh.py`: Mesh, LodMesh, SkeletalMesh, Animation byte-exact; the sweep now reports all 276,885 retail exports exact, with no class left without a reader.
- Byte-exact was not enough: the first Soldier render was scrambled. Each LodMesh frame stores the special (weapon) vertices first, so wedge indices are offset by `special_verts`; with that the model renders right.

## 2026-11-10 — level geometry

- `tools/umodel.py`: Polys, Model (BSP nodes, surfaces, verts, zones, lightmaps, bounds, leaves) and Level (actor list, URL, reach specs) byte-exact on the first hypothesis from public UE1 docs.
- Cross-checks on 83 maps: all surface/node indices in range; each lightmap is one shadow bitmap per light (1 bit per luxel, rows rounded to bytes) and all 155,353 sizes match. Light colour is computed at run time from light actors: engine behaviour, open.

## 2026-11-10 — script objects and bytecode

- `tools/uscript.py`: classes, states, functions, structs, enums, consts, text buffers and property declarations, all byte-exact. Class objects have no leading property list; their defaults come last as a tagged list.
- Skipping bytecode is impossible: the header's script size counts memory (4-byte references), disk uses compact indices. The decoder walks every expression; all 1.58 M tokens of the install land exactly on the declared size. 9,156 functions in `System/*.u`, 383 native.
- Strings with a negative length are UTF-16 (third-party cache packages).

## 2026-11-10 — fonts

- `tools/ufont.py`: pages of glyph rectangles plus characters-per-page; 469 (version 69) adds a code-point remap table and a flag. All 78 fonts byte-exact.

## 2026-11-10 — sound and music

- `tools/usound.py`: Sound and Music are a format name plus a complete embedded file; all 2,914 byte-exact. 2,881 PCM WAVs (mostly mono, 11–22 kHz), music is 30 IT, 2 S3M and 1 XM: `.umx` are tracker modules, as suspected.
- The bulk-data skip offset starts at version 62, not 63 (a v62 music file has it); one helper, `upkg.lazy_bytes`, now serves textures and sounds.

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
