# Font

Reader: `tools/ufont.py`. Class data follows the tagged properties (none in retail fonts).

Verified: all 78 retail `Font` objects (versions 68 and 69) consumed byte-exactly.

## Layout

| Field | Type | Note |
|---|---|---|
| page count | compact index | |
| pages | per page: texture (compact-index object reference), char count (compact index), chars × 4 i32 (StartU, StartV, USize, VSize: glyph rectangle in the page texture) | |
| characters per page | u32 | 64 in classic fonts, 2048 in 469 unicode fonts |
| remap count | compact index | version ≥ 69 only |
| remap | count × (u16 code point, u16 glyph index) | version ≥ 69 only |
| remapped | u32 | version ≥ 69 only; 1 in fonts with a remap table, 0 otherwise (name and meaning unverified) |

A glyph for code point `c` without remap: page `c / chars_per_page`, char `c % chars_per_page` (public docs; unverified).
