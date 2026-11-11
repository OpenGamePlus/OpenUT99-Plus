# Tagged properties

Reader: `tools/uprops.py`. The data of every exported object, except class objects (export class 0), starts with an optional state frame and a list of tagged properties ending with a tag named `None`. Class-specific data follows for native types.

Verified (`uprops.py --sweep <UT99_DIR>`): all 273,585 non-class exports of the 396 retail packages parse with no tag running past its export. No class mixes the two cases:

- 83,444 objects in 243 classes end exactly at the `None` tag (actors, `Package`, ...).
- 190,141 objects in 34 classes carry class data after it: `Model`, `Polys`, `Level`, `Texture` and its subclasses, `Palette`, `Font`, `Sound`, `Music`, `Mesh`, `LodMesh`, `SkeletalMesh`, `Animation`, and the script types (`Function`, `State`, `Struct`, `Enum`, `Const`, `TextBuffer`, `*Property`).

## State frame

Present when object flag `0x02000000` (has stack) is set; 101,193 retail objects have it.

| Field | Type |
|---|---|
| node | compact index (object reference) |
| state node | compact index (object reference) |
| probe mask | u64 |
| latent action | u32 |
| offset | compact index, only if node ≠ 0 |

## Tag

| Field | Type | Note |
|---|---|---|
| name | compact index into names | `None` ends the list |
| info | u8 | bits 0–3 type, bits 4–6 size code, bit 7 array flag (bool: the value) |
| struct name | compact index | type 10 only |
| size | — / u8 / u16 / u32 | size code 0–4 → 1, 2, 4, 12, 16 bytes; 5 → u8 follows, 6 → u16, 7 → u32 |
| array index | 1, 2 or 4 bytes | only if bit 7 set and type ≠ bool. Byte 0 `0xxxxxxx` → 7 bits; `10xxxxxx` + 1 byte → 14 bits; `11xxxxxx` + 3 bytes → 30 bits |
| value | `size` bytes | bool: no bytes |

## Types

Seen in retail: Byte (1), Int (2), Bool (3), Float (4), Object (5), Name (6), Struct (10), Str (13). Never seen: String (7), Class (8; class references are presumably written as Object), Array (9), Vector (11), Rotator (12), Map (14), FixedArray (15).

| Type | Value |
|---|---|
| Byte | u8 |
| Int | i32 |
| Float | f32 |
| Object, Name | compact index (object reference / name index) |
| Str | compact-index length (including NUL), then bytes |
| Struct | binary struct body, no inner tags |

## Structs seen

| Struct | Size | Layout |
|---|---|---|
| `Vector` | 12 | X, Y, Z f32 |
| `Rotator` | 12 | Pitch, Yaw, Roll i32 (65536 = full turn) |
| `Scale` | 17 | Scale Vector, SheerRate f32, SheerAxis u8 |
| `PointRegion` | 6–7 | Zone (compact index object ref), iLeaf i32, ZoneNumber u8 |
| `Color` | 4 | R, G, B, A u8 |
| `Coords` | 48 | Origin, XAxis, YAxis, ZAxis Vectors |
| `ADrop` | 8 | unverified; kept as raw bytes |

## Unverified / open

- The probe-mask width (u64) and the 65536-per-turn rotator unit are taken from what parses and public docs, not from behaviour.
- Field order and names inside `Scale`, `PointRegion`, `Coords` come from public UE1 docs; values look right on retail maps (actor locations, zones).
