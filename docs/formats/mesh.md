# Meshes: Mesh, LodMesh, SkeletalMesh, Animation

Reader: `tools/umesh.py`. Class data follows the tagged properties ([properties.md](properties.md)).

Verified: all 10 `Mesh`, 668 `LodMesh`, 26 `SkeletalMesh` and 18 `Animation` objects byte-exact (all version 69). The `Soldier` LodMesh, frame 0, exported with `umesh.py ... --obj` and rasterised, shows the player model.

"lazy" = lazy array ([package.md](package.md#lazy-byte-array)) whose count is in items; "array" = compact count + items. Primitive = box (6 × f32 + valid u8) + sphere (4 × f32).

## Mesh (vertex animation)

| Field | Type |
|---|---|
| primitive | 41 bytes |
| verts | lazy × u32: packed X (bits 0–10), Y (11–21), Z (22–31), all signed; `anim_frames` × `frame_verts` |
| tris | lazy × 20 bytes: vertex u16 ×3, (U, V) u8 ×3, poly flags u32, texture index i32 |
| anim seqs | array × (name, group (name), start frame i32, frame count i32, notifys (array × (time f32, function name)), rate f32) |
| connects | lazy × (count i32, offset i32) |
| bounds | primitive |
| vert links | lazy × i32 |
| textures | array × object ref |
| bounding boxes | array × box (25 bytes) |
| bounding spheres | array × 4 f32 |
| frame verts, anim frames | i32 ×2 |
| AND flags, OR flags | u32 ×2 |
| scale, origin | vector ×2 |
| rot origin | rotator (3 × i32) |
| cur poly, cur vertex | i32 ×2 |
| texture LOD | array × f32 |

## LodMesh

Mesh, then:

| Field | Type |
|---|---|
| collapse points, face level | array × u16 each |
| faces | array × (wedge u16 ×3, material index u16) |
| collapse wedges | array × u16 |
| wedges | array × (vertex u16, U u8, V u8) |
| materials | array × (poly flags u32, texture index i32) |
| special faces | array × face |
| model verts, special verts | i32 ×2 |
| mesh scale max, LOD hysteresis, LOD strength | f32 ×3 |
| LOD min verts | i32 |
| LOD morph, LOD Z displace, LOD high quality | f32 ×3 |
| remap anim verts | array × u16 |

In LodMesh, `tris` is empty: geometry is faces → wedges → vertices. Each frame holds `special_verts` vertices first (weapon attachment triangle), then `model_verts`; a wedge's vertex index counts from the first model vertex. Verified visually (without the offset the mesh is scrambled).

World position of a mesh vertex v (as the engine draws it; checked visually on pickups, decorations and the EOL_Statues trophies, including actors and meshes with non-zero roll):

world = Location + R(actor Rotation) · R(RotOrigin)⁻¹ · (Scale × DrawScale ⊙ (v − Origin))

R(pitch, yaw, roll) is the UE rotation (axes X = (cp·cy, cp·sy, sp), Y = (sr·sp·cy − cr·sy, sr·sp·sy + cr·cy, −sr·cp), Z = (−(cr·sp·cy + sr·sy), cy·sr − cr·sp·sy, cr·cp)). Mesh Z scale is typically twice X/Y because Z is packed in 10 bits. Wedge U, V bytes are divided by 256. A material's texture: the actor's `MultiSkins[i]`, else the mesh's texture i, else `Skin`.

## SkeletalMesh

LodMesh, then: ext wedges (array × (vertex u16, flags u16, U f32, V f32)), points (array × vector), ref skeleton (array × bone: name, flags u32, orientation quaternion 4 f32, position vector, length, X/Y/Z size f32, children i32, parent i32), bone weight index (array × 4 u16), bone weights (array × (point u16, weight u16)), local points (array × vector), skeletal depth i32, default animation (ref), weapon bone i32, weapon adjust (coords, 12 f32).

## Animation

Ref bones (array × (name, flags u32, parent i32)), moves (array × (root speed vector, track time f32, start bone i32, flags u32, bone indices (array × i32), tracks (array × track), root track)), anim seqs (as in Mesh). Track: flags u32, key quaternions (array × 4 f32), key positions (array × vector), key times (array × f32).

## Open

- `PrePivot`, environment-mapped meshes (`bMeshEnviroMap`: the Deathmatch trophy draws grey), mesh lighting.
- Bone weight semantics; vertex animation interpolation between frames (engine behaviour).

## Engine

`engine/mesh.*` reads every LodMesh frame (frames follow each other, `frame_verts` each; checked visually: frames 0 and 40 of the player meshes are whole bodies), faces, wedges, materials and the animation sequences (name, group, start frame, frame count, rate); the world uses the sequences for `PlayAnim` / `LoopAnim` timing and `GetAnimGroup`, and `ut_mapview --play` draws actors at frame `start + AnimFrame × frames` (blending to the next frame). Placement: world = Location + PrePivot + R(Rotation) · R(RotOrigin) · ((v − Origin) × Scale × DrawScale); with RotOrigin applied this way the player meshes (RotOrigin yaw 90°, roll −90°) stand up and face their Rotation (the inverse lays them down; checked visually).
