# Palette and Texture

Reader: `tools/utex.py` (registered for `Palette`, `Texture`, `ScriptedTexture`). Class data follows the tagged properties ([properties.md](properties.md)).

Verified (`uobjects.py --sweep <UT99_DIR>`): every `Palette` (6,621), `Texture` (9,792) and `ScriptedTexture` (14) in the retail install is consumed byte-exactly. PNG previews of P8 textures (versions 61 and 69, world texture and player skin) look right.

## Palette

| Field | Type |
|---|---|
| count | compact index (always 256 in retail) |
| colours | count × (R, G, B, A u8) |

Alpha is 0 in some palettes and 255 in others; P8 textures are drawn opaque in previews (unverified for masked textures, where index 0 is transparent per public docs).

## Texture

| Field | Type | Note |
|---|---|---|
| mips | mip array | always |
| comp mips | mip array | only when property `bHasComp` is true (469 packages) |

Mip array: compact-index count, then per mip:

| Field | Type | Note |
|---|---|---|
| skip offset | u32 | version ≥ 63: absolute file position just past the pixel data; checked by the reader. Absent in version 61 (no retail texture is version 62) |
| data size | compact index | |
| data | bytes | |
| USize, VSize | u32 ×2 | width, height |
| UBits, VBits | u8 ×2 | log2 of width, height |

## Pixel formats seen

| Property | Value | Bytes per pixel | Count (mip 0) | Meaning |
|---|---|---|---|---|
| `Format` | 0 (default) | 1 | 9,791 | P8: palette index into the `Palette` property's object |
| `Format` | 0 | 0 | 14 | no pixel data (scripted / placeholder) |
| `Format` | 3 | 0.5 | 1 | DXT1 by size (unverified) |
| `CompFormat` | 3 | 0.5 | 127 | DXT1 by size (unverified) |
| `CompFormat` | 12, 7 | 1 | 398, 2 | a 16-bytes-per-4×4-block format (469 extension); not identified |

## Open

- Identify `CompFormat` 7 and 12; decode DXT1 (Python previews are P8 only).
- Procedural subclasses (`FireTexture`, `WetTexture`, `WaveTexture`, `IceTexture`): base mips + own data, see coverage.
