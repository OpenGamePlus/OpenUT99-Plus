# Format coverage

| Format | Spec | Reader | State |
|---|---|---|---|
| Dispatcher, per-class sweep | — | `tools/uobjects.py` | progress meter |
| Package container (header, names, imports, exports) | [package.md](package.md) | `tools/upkg.py` | verified on all retail packages |
| Tagged properties, state frame (generic object data) | [properties.md](properties.md) | `tools/uprops.py` | verified on all 273,585 non-class exports |
| `Texture`, `Palette`, `ScriptedTexture` | [texture.md](texture.md) | `tools/utex.py` | exact on all 16,427; `CompFormat` 7/12 not identified |
| `FireTexture`, `WetTexture`, `WaveTexture`, `IceTexture` | [texture.md](texture.md) | `tools/utex.py` | exact; animation rules open |
| `Sound` (2,881, all PCM WAV) | [sound.md](sound.md) | `tools/usound.py` | exact |
| `Music` (33: 30 IT, 2 S3M, 1 XM) | [sound.md](sound.md) | `tools/usound.py` | exact |
| `Level`, `Model` (BSP), `Polys`, actors (`.unr`) | — | — | open |
| `Mesh` / `LodMesh` (vertex animation), `SkeletalMesh` | — | — | open |
| `Class`, `State`, `Function`, bytecode (`.u`) | — | — | open |
| `Font` | [font.md](font.md) | `tools/ufont.py` | exact |
| `.ini` / `.int` configuration and localisation | — | — | open |
| Network protocol | — | — | open (roadmap E) |
