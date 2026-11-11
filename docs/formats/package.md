# Unreal package (`.u .unr .utx .uax .umx .uxx`)

Reader: `tools/upkg.py`. Little-endian.

Verified: all 396 packages of the retail install parse; every export's data range lies inside its file. Versions seen (licensee always 0):

| Extension | Versions |
|---|---|
| `.u` | 69 |
| `.unr` | 68, 69 |
| `.utx` | 61, 63–66, 68, 69 |
| `.uax` | 61, 63, 65, 66, 68 |
| `.umx` | 62, 64, 66, 68 |
| `.uxx` | 61, 68, 69 |

## Compact index

Variable-length signed integer. Byte 0: bit 7 sign, bit 6 "more", bits 5–0 value. Following bytes: bit 7 "more", bits 6–0 value, least significant group first. At most 5 bytes.

## Header

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | tag `0x9E2A83C1` |
| 4 | u16 | file version |
| 6 | u16 | licensee version |
| 8 | u32 | package flags |
| 12 | u32 ×2 | name count, name table offset |
| 20 | u32 ×2 | export count, export table offset |
| 28 | u32 ×2 | import count, import table offset |
| 36 | — | version ≥ 68: GUID (16 bytes), generation count u32, generations (export count u32, name count u32 each). Version < 68: heritage count u32, heritage offset u32 |

## Strings

Compact-index length including the trailing NUL, then the bytes (Latin-1). A negative length means that many UTF-16LE characters. Reader: `upkg.Reader.string`.

## Name table

Per name: version ≥ 64: compact-index length (including the trailing NUL), then the bytes; version < 64: NUL-terminated string. Then u32 object flags.

## Object references

An i32 or compact index: 0 = none, > 0 = export `ref − 1`, < 0 = import `−ref − 1`.

## Lazy byte array

Bulk data (mip pixels, sound and music files) is stored as:

| Field | Type | Note |
|---|---|---|
| skip offset | u32 | version ≥ 62 only: absolute file position just past the data; the reader checks it |
| size | compact index | |
| data | bytes | |

Verified boundary: version 61 objects have no skip offset, version 62 (music) has it. Reader: `upkg.lazy_bytes`.

## Import table

Per import: class package (name index), class name (name index), package (i32 object reference: the outer), object name (name index).

## Export table

Per export: class (object reference; 0 means the export is itself a class), super (object reference), package (i32 object reference: the outer), object name (name index), object flags u32, serial size (compact index), serial offset (compact index, present only if size > 0).

## Unverified / open

- Meaning of the package and object flag bits.
- The serialised object data (properties, then class-specific data): next step, see [coverage.md](coverage.md).
