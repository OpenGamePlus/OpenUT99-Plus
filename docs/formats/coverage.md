# Format coverage

Phase A status: `uobjects.py --sweep <UT99_DIR>` reports every one of the 276,885 non-empty exports of the retail install as exact (0 leftover, 0 error, 0 without reader).

| Format | Spec | Reader | State |
|---|---|---|---|
| Dispatcher, per-class sweep | — | `tools/uobjects.py` | progress meter |
| Package container (header, names, imports, exports) | [package.md](package.md) | `tools/upkg.py` | verified on all retail packages |
| Tagged properties, state frame (generic object data) | [properties.md](properties.md) | `tools/uprops.py` | verified on all 273,585 non-class exports |
| `Texture`, `Palette`, `ScriptedTexture` | [texture.md](texture.md) | `tools/utex.py` | exact on all 16,427; `CompFormat` 7/12 not identified |
| `FireTexture`, `WetTexture`, `WaveTexture`, `IceTexture` | [texture.md](texture.md) | `tools/utex.py` | exact; animation rules open |
| `Sound` (2,881, all PCM WAV) | [sound.md](sound.md) | `tools/usound.py` | exact |
| `Music` (33: 30 IT, 2 S3M, 1 XM) | [sound.md](sound.md) | `tools/usound.py` | exact |
| `Level`, `Model` (BSP, lightmaps, zones), `Polys`; actors are tagged properties | [level.md](level.md) | `tools/umodel.py` | exact; light colour model open |
| `Mesh`, `LodMesh` (vertex animation), `SkeletalMesh`, `Animation` | [mesh.md](mesh.md) | `tools/umesh.py` | exact; LodMesh frame checked visually |
| `Class`, `State`, `Function`, `Struct`, `Enum`, `Const`, `TextBuffer`, `*Property`, bytecode | [script.md](script.md) | `tools/uscript.py` | exact on all; token semantics from public docs |
| `Font` | [font.md](font.md) | `tools/ufont.py` | exact |
| `.ini` / `.int` configuration, package lookup | [ini.md](ini.md) | `tools/uini.py`, `engine/config.*`, `engine/vfs.*` | all dependencies resolve |
| Network protocol | — | — | open (roadmap E) |
