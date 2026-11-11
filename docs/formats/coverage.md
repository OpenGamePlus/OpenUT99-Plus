# Format coverage

| Format | Spec | Reader | State |
|---|---|---|---|
| Package container (header, names, imports, exports) | [package.md](package.md) | `tools/upkg.py` | verified on all retail packages |
| Tagged properties, state frame (generic object data) | [properties.md](properties.md) | `tools/uprops.py` | verified on all 273,585 non-class exports |
| `Texture`, `Palette` (4,815 textures and 2,820 palettes in `.utx` alone; maps and code packages hold more) | — | — | open |
| `Sound` (1,083 in `.uax` alone; maps and code packages hold more) | — | — | open |
| `Music` (`.umx`, 30 tracker modules) | — | — | open |
| `Level`, `Model` (BSP), `Polys`, actors (`.unr`) | — | — | open |
| `Mesh` / `LodMesh` (vertex animation), `SkeletalMesh` | — | — | open |
| `Class`, `State`, `Function`, bytecode (`.u`) | — | — | open |
| `Font` | — | — | open |
| `.ini` / `.int` configuration and localisation | — | — | open |
| Network protocol | — | — | open (roadmap E) |
