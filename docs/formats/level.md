# Level geometry: Polys, Model, Level

Reader: `tools/umodel.py`. Class data follows the tagged properties ([properties.md](properties.md)).

Verified (`uobjects.py --sweep <UT99_DIR>`): every `Polys` (36,489), `Model` (36,489) and `Level` (87) byte-exact. On the 83 retail maps: every surface's point / vector / lightmap index is in range, every node's surface and vertex pool are in range, and every lightmap's shadow-bit block has exactly the size the layout below predicts (155,353 lightmaps).

Vectors are 3 × f32; a box is min vector, max vector, valid u8 (25 bytes). "ref" = compact-index object reference; "idx" = compact index into an array of the same Model.

## Polys (editor polygons; brushes and the level's source geometry)

`num` u32, `max` u32, then `num` polygons:

| Field | Type |
|---|---|
| vertex count | compact index |
| base, normal, texture U, texture V | vectors |
| vertices | count × vector |
| poly flags | u32 |
| actor, texture | ref |
| item name | name |
| link, brush poly | compact index |
| pan U, pan V | i16 ×2 |

## Model (BSP)

| Field | Type |
|---|---|
| bounding box | box |
| bounding sphere | 4 × f32 |
| vectors | compact count × vector (normals, texture axes) |
| points | compact count × vector (positions) |
| nodes | compact count × node |
| surfs | compact count × surface |
| verts | compact count × (point idx, side idx) |
| shared sides | i32 |
| zones | i32 count × (zone actor ref, connectivity u64, visibility u64) |
| polys | ref (the `Polys` object) |
| lightmaps | compact count × lightmap |
| light bits | compact count × u8 |
| bounds | compact count × box |
| leaf hulls | compact count × i32 |
| leaves | compact count × (zone idx, permeating idx, volumetric idx, visible zones u64) |
| lights | compact count × ref (light actors; lists end at a 0 entry) |
| root outside, linked | u32 ×2 |

Node (36 bytes + 7 compact indices): plane (4 × f32: normal, distance), zone mask u64, node flags u8, vert pool, surf, back, front, plane index, collision bound, render bound (compact), zone u8 ×2 (back, front), vertex count u8, leaf i32 ×2. A node's polygon is `verts[vert_pool .. vert_pool + vertex count]`.

Surface: texture ref, poly flags u32, base (point idx), normal, texture U, texture V (vector idx), light map (idx, −1 = none), brush poly (compact), pan U, pan V (i16), actor ref.

Lightmap: data offset i32 (into light bits), pan vector, U clamp, V clamp (compact), U scale, V scale (f32), first light i32 (index into `lights`). Its data is, for each light in `lights[first light ..]` up to the 0 entry, a shadow bitmap of V clamp rows × ceil(U clamp / 8) bytes, one bit per luxel. The final light colour is computed at run time from the light actors' properties (brightness, hue, radius, effect): engine behaviour, not data.

## Lightmap coordinates and shadow bits (verified)

- Luxel (i, j) of a lightmap sits at texture coordinates U = pan.x + i × U scale, V = pan.y + j × V scale (texel units, measured from the surface base with its texture axes, without the surface pan). `pan` is the polygon's minimum (U, V) minus 0.1, and (U max − U min) / U scale + 1 = U clamp: luxels sample the polygon's corners and edges.
- Shadow bits are LSB first within each byte. On DM-Deck16][, 99.99 % of set bits face their light (N · L > 0) and 99.85 % lie within 25 × (LightRadius + 1) units of it; MSB-first order or a smaller radius fits worse.

## Lighting model used by the engine (unverified)

Engine behaviour, not data; chosen to look plausible, to be calibrated against the original: luxel colour = zone ambient (`AmbientBrightness`, `AmbientHue`, `AmbientSaturation` of the zone's ZoneInfo) + Σ over lights whose bit is set of colour(LightHue, LightSaturation, LightBrightness) × (1 − (d / (25 × (LightRadius + 1)))²), no N · L term; the texture is multiplied by 2 × luxel colour. Missing light properties take Engine.Light defaults (brightness 64, saturation 255 = white, radius 64). Some maps rely on lights with `LightBrightness 0` and radius 255 (DM-Deck16][ spawn corridor), which this model leaves black.

## Sky

`FakeBackdrop` surfaces are windows into the sky zone: the engine first draws the level seen from the `SkyZoneInfo` actor (same view direction), then draws the level normally with backdrop surfaces writing depth only. Verified visually on CTF-Face (star field and nebula behind the towers).

## Drawing a surface

Texture coordinates of a point P on a node's polygon, in texels: U = (P − base) · texture U + pan U, V = (P − base) · texture V + pan V; divide by the texture's size for 0..1 coordinates. Verified visually on three maps (`ut_mapview --shot`): walls and floors line up, text on posters reads correctly (UE world is left-handed: X forward, Y right, Z up).

## Poly flags seen on surfaces (83 maps)

Names from public UE1 documentation, unverified against behaviour.

| Bit | Name | Surfaces |
|---|---|---|
| 0 | Invisible | 3,333 |
| 1 | Masked | 1,159 |
| 2 | Translucent | 1,828 |
| 3 | NotSolid | 6,452 |
| 5 | Semisolid | 63,468 |
| 6 | Modulated | 488 |
| 7 | FakeBackdrop | 1,183 |
| 8 | TwoSided | 3,884 |
| 9, 10 | AutoUPan, AutoVPan | 226, 274 |
| 11 | NoSmooth | 71 |
| 12, 13 | BigWavy, SmallWavy | 6, 49 |
| 15 | LowShadowDetail | 12,501 |
| 18 | DirtyShadows | 24 |
| 19 | BrightCorners | 13,114 |
| 20 | SpecialLit | 1,983 |
| 21 | Gouraud | 261 |
| 22 | Unlit | 6,641 |
| 23 | HighShadowDetail | 8,382 |
| 25 | (unnamed) | 4 |
| 26 | Portal | 1,106 |
| 27 | Mirrored | 8 |
| 29 | (unnamed) | 1 |

## Level

| Field | Type |
|---|---|
| actors | num u32, max u32, num × ref (0 = deleted slot) |
| URL | protocol, host, map, portal (strings), options (compact count × string), port i32, valid i32 |
| model | ref |
| reach specs | compact count × (distance i32, start ref, end ref, collision radius i32, collision height i32, reach flags i32, pruned u8) |
| approx time | f32 |
| first deleted | ref |
| text blocks | 16 × ref |
| travel info | compact count × (string, string) |

## Open

- Calibrating the light colour model against the original; light effects (pulse, flicker, spot cones), dynamic lights, zone fog, sky zone rotation.
- Meaning of node flags and reach flags bits.
