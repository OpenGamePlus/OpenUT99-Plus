# SYSTEM_SUMMARY

Last updated: 2026-11-10. Phase: A (package loading) complete on retail data: every export byte-exact. B (C++ engine) in progress: textured BSP level viewer, all retail maps load.

## Tree

```
AGENTS.md            agent entry point
LICENSE              AGPL-3.0
README.md
CMakeLists.txt       C++17 engine build (library ut, ut_dump, test_engine)
engine/
  upkg.{h,cpp}       packages + tagged properties (port of tools/upkg.py, uprops.py)
  config.{h,cpp}     .ini parsing
  vfs.{h,cpp}        package lookup (Paths, cache)
  packages.{h,cpp}   loaded packages, cross-package references
  texture.{h,cpp}    Palette + P8 Texture -> RGBA
  level.{h,cpp}      Model (BSP), Level, surface triangles
  script.{h,cpp}     bytecode walker, class defaults
  mesh.{h,cpp}       LodMesh
  lighting.{h,cpp}   lightmap luxels, shadow bits, light colours, atlas
  scene.{h,cpp}      map -> texture batches, lightmap atlas, sky, mesh actors, player starts
  render.{h,cpp}     OpenGL 3.3 renderer (library ut_render)
  png.{h,cpp}        PNG encoder for screenshots
  mapview.cpp        ut_mapview: free-fly viewer, --shot, --check
  ut_dump.cpp        summary / property counts / --deps
  test_engine.cpp    unit tests on synthetic data (ctest)
docs/                specs, ADRs, roadmap, AI layer (index: docs/README.md)
tools/
  upkg.py            Unreal package reader (header, names, imports, exports)
  uprops.py          tagged properties (generic object data)
  utex.py            Palette, Texture (mips, PNG preview)
  usound.py          Sound, Music (embedded WAV / IT / S3M / XM)
  ufont.py           Font (pages, glyph rectangles, 469 remap)
  uscript.py         Class, State, Function, Struct, Enum, Const, TextBuffer, *Property, bytecode
  umodel.py          Polys, Model (BSP, surfaces, lightmaps, zones), Level
  umesh.py           Mesh, LodMesh, SkeletalMesh, Animation (OBJ export)
  uobjects.py        dispatcher + sweep over all exports (exact / leftover / error / noreader)
  uini.py            .ini parsing, package lookup, --deps
  compare_engine.py  C++ vs Python on every package of an install
  check_docs.py      documentation checker
  test_*.py          unit tests on synthetic data
```

## State per subsystem

| Subsystem | State |
|---|---|
| Package container | done, verified on all 396 retail packages |
| Tagged properties | done, verified on all 273,585 non-class exports |
| Palette, Texture, ScriptedTexture | done, byte-exact on all retail objects |
| FireTexture, WetTexture, WaveTexture, IceTexture | done (data); animation not decoded |
| Sound, Music | done, byte-exact |
| Font | done, byte-exact |
| Script objects and bytecode | done, byte-exact (token semantics from public docs) |
| Polys, Model, Level | done, byte-exact; indices and lightmap sizes cross-checked |
| Mesh, LodMesh, SkeletalMesh, Animation | done, byte-exact; LodMesh frame checked visually |
| Engine (C++) | ini + package lookup (0 missing dependencies); package, property, Model, class-default and LodMesh readers match Python on all 396 packages; level viewer: textured BSP, baked lightmaps (colour model uncalibrated), sky zones, mesh actors (frame 0, full bright); all 83 maps load |
| Network | not started |

## Commands

```
python3 -m unittest discover -s tools          # all tests
cmake -S . -B build && cmake --build build && ctest --test-dir build   # engine build + tests
python3 tools/compare_engine.py <UT99_DIR> build/ut_dump      # C++ vs Python, must say "0 differ"
build/ut_dump --deps <UT99_DIR>                 # must say "0 missing dependencies" (same as uini.py --deps)
build/ut_mapview <UT99_DIR> --check             # every map loads: "83 maps, 0 errors"
build/ut_mapview <UT99_DIR> MAP [--shot out/x.png] [--at x y z yaw pitch] [--lightmap out/lm.png]   # RMB + mouse look, WASD fly
python3 tools/check_docs.py                    # doc checks
python3 tools/upkg.py FILE [--names|--imports|--exports]
python3 tools/uprops.py FILE OBJECT              # properties of one export
python3 tools/uprops.py --sweep <UT99_DIR>       # parse every export, counts per class
python3 tools/uobjects.py --sweep <UT99_DIR>     # phase A progress: classes not yet byte-exact
python3 tools/uobjects.py FILE OBJECT            # decode one export
python3 tools/utex.py FILE TEXTURE --png out/x.png   # P8 preview
python3 tools/usound.py FILE OBJECT --out out/x.wav  # embedded file
python3 tools/uscript.py --tokens <UT99_DIR>     # bytecode token histogram
python3 tools/umesh.py FILE LODMESH --obj out/x.obj [FRAME]
```

## Contracts

- `upkg.parse(bytes) -> dict`: `version`, `licensee`, `flags`, `guid` / `generations` (version ≥ 68), `names` (list of str), `imports` (`class_package`, `class`, `package`, `name`), `exports` (`class`, `super`, `package`, `name`, `flags`, `size`, `offset`). Raises `ValueError` on a bad tag.
- `upkg.object_name(pkg, ref)`: name of an object reference (0, export, import).
- `uprops.read_properties(pkg, data, export) -> (props, end)`: `props` is a list of `name`, `type`, `index` (array index), `value`, `struct` (struct tags only); `end` is the offset after the `None` tag, where class data starts. Raises `ValueError` if a tag runs past the export.
- `uobjects.register(*class_names)`: decorator for `reader(pkg, data, export, pos, props) -> (value, end)`, called with `pos` after the `None` tag (class objects: the export offset). `uobjects.read_object(pkg, data, export) -> {"props", "value", "end"}`; `uobjects.outcome(...)` → `exact` | `leftover` | `error` | `noreader`.
- `utex.read_palette(...) -> [(R,G,B,A)]`; `utex.read_texture(...) -> {"mips": [...], "comp_mips"?: [...]}`, mip = `width`, `height`, `ubits`, `vbits`, `data`; `utex.read_mips(pkg, data, pos)`.
- `upkg.lazy_bytes(reader, version) -> bytes`: bulk data with the version ≥ 62 skip offset (checked). `usound.read_sound(...) -> {"format", "data"}`; `usound.signature(bytes) -> "wav" | "it" | "s3m" | "xm" | "mod" | "unknown"`.
- `ufont.read_font(...) -> {"pages": [{"texture", "chars": [(u, v, w, h)]}], "chars_per_page", "remap"?, "remapped"?}`.
- `uscript.decode_script(pkg, data, pos, mem_size) -> (end, Counter of tokens)`; readers for `Class` (with `defaults`), `State`, `Function`, `Struct`, `Enum`, `Const`, `TextBuffer`, every `*Property` (`refs` = type references).
- `umodel.read_model(...)` → `vectors`, `points`, `nodes`, `surfs`, `verts`, `zones`, `lightmaps`, `light_bits`, `lights`, ... (names as in `docs/formats/level.md`); `umodel.read_level(...)` → `actors`, `url`, `model`, `reach_specs`, ...; `umodel.read_polys(...)` → list of polygons.
- `umesh.read_mesh / read_lod_mesh / read_skeletal_mesh / read_animation`; `umesh.unpack_vert(u32) -> (x, y, z)`; `umesh.lod_mesh_obj(mesh, frame) -> str`.
- C++ `ut::load_package(path) -> Package` (`names`, `imports`, `exports`, `data`), `ut::read_properties(pkg, export, &end) -> vector<Property>` (raw `value_pos`/`size`; decode with `prop_int`, `prop_float`, `prop_ref`, `prop_byte`, `prop_string`, `prop_vector`). Throws `std::runtime_error` on bad data; exports outside the file are rejected at load.
- `uini.parse(text) -> {section: {key: [values]}}`, `uini.package_paths(root, ini) -> {lower name: path}`; C++ `ut::parse_ini`, `ut::read_ini`, `ut::Vfs(root, ini).find(name)`.
- C++ `ut::PackageSet(&vfs).resolve(pkg, ref) -> Object{package, exp}` (imports by name + class + group path); `ut::load_scene(set, map) -> Scene{batches, starts}`; `ut::Renderer(scene).draw(cam, w, h)`.
- C++ `ut::skip_script(pkg, pos, mem_size)`, `ut::read_class(pkg, export) -> ClassInfo{super_ref, defaults}`, `ut::read_lod_mesh(pkg, export) -> LodMesh` (frame 0, faces, wedges, materials, textures, scale, origin, rot_origin).
