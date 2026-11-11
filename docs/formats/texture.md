# Palette and Texture

Reader: `tools/utex.py` (registered for `Palette`, `Texture`, `ScriptedTexture`, `FireTexture`, `WetTexture`, `WaveTexture`, `IceTexture`). Class data follows the tagged properties ([properties.md](properties.md)).

Verified (`uobjects.py --sweep <UT99_DIR>`): every `Palette` (6,621), `Texture` (9,792) and `ScriptedTexture` (14), `FireTexture` (191), `WetTexture` (99), `IceTexture` (17) and `WaveTexture` (7) in the retail install is consumed byte-exactly. PNG previews of P8 textures (versions 61 and 69, world texture and player skin) look right.

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
| data | lazy byte array ([package.md](package.md#lazy-byte-array)) | pixels |
| USize, VSize | u32 ×2 | width, height |
| UBits, VBits | u8 ×2 | log2 of width, height |

## Procedural textures

`WetTexture`, `WaveTexture`, `IceTexture`: the Texture data only; their parameters (source texture, wave settings) are tagged properties.

`FireTexture`: Texture data, then a spark list: compact-index count (equals property `NumSparks`), then 8 bytes per spark:

| Byte | Field (names from public UE1 docs, unverified) |
|---|---|
| 0 | type (spark kind) |
| 1 | heat |
| 2, 3 | X, Y in texture pixels |
| 4–7 | per-type arguments (ByteA–ByteD) |

The animation itself (how sparks and heat evolve per frame) is native-only behaviour, not in the data.

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
- Procedural animation rules (fire, water, wave, ice), needed for rendering them animated.
