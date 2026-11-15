# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

## 2026-11-15 — routes pay SpecialCost: bots stop planning through jump spots

- Lifts: the map's lift edges (flag 0x20, distance 500) were in our graph, but `JumpSpot` edges looked as cheap. Over the 26 DM maps with a `LiftCenter`, 1,534 routes went through jump spots that need a translocator. Routes now add the `SpecialCost(Seeker)` of nodes with `bSpecialCost` (`JumpSpot` answers 100,000,000 to a bot that cannot use it). Jump-spot routes 1,534 → 63, lifts moved 38 → 49, rides 28 → 31.
- `--maps 60`: 8,541 units per bot (8,250), 128 deaths, no script error. DM-Agony and DM-Peak: their lifts start in `StandOpenTimed` and are in the graph, but no route needed them in 120 s (not a fault). No route test (needs a script class with `SpecialCost`).

## 2026-11-14 — pointReachable limits measured

- `calibrate.py reach` (`OU-Reach`, `ProbeReacher`): the original's `pointReachable` reaches climbs up to 29 and drops up to 24 without `bCanJump`, and climbs up to 52 and drops up to 118 with it. Ours had a 120 climb and a 400 drop. Our straight-line test also clipped the edge on drops, so a drop now walks off at its own height first. Ours matches all eight lanes.
- `--maps 60`: 8,250 units per bot (8,368), 135 deaths, bots still falling 33 (45), no script error. Bots play with `bCanJump` on and `JumpZ` 357.5. The limits are the same at `JumpZ` 250, 325 and 400 (twelve lanes, ours matches). Not measured: other collision sizes.

## 2026-11-14 — PickWallAdjust with other pawn sizes

- Probe tags take a collision size (`CRR_HH`). The ledge limit stays 77 with `CollisionHeight` 50, so it is a fixed height above the floor, not 2 × height; the engine now lifts the box a fixed 72.7 above the hover. The sidestep does follow the radius: the goal goes 2 × radius + 16 aside (50 at 17, 66 at 25). The clearance is 40–41 at 17 and 58–60 at 25, fitted as 2.25 × radius + 1.75. Ours matches all 7 wall and 6 ledge lanes.
- Both fit (a block 40 high): the original jumps, as ours does. `PickWallAdjust` is now measured in every case we can build; what remains is how bots use it (state code), not the native.

## 2026-11-14 — the ledge jump limit does not depend on JumpZ

- `calibrate.py ledge` lanes now carry `JumpZ` (`UpNNNJMMM`). With JumpZ 250, 325 and 400, the original's `PickWallAdjust` jumps at ledges up to 77 and not at 78; at 250 it jumps at ledges it cannot clear. Our apex-based guess was wrong (it predicted 50 at 250 and 101 at 400). Now a fixed box lift (2 × `CollisionHeight` − 0.5 from the floor); ours matches all 11 lanes.
- Sideways adjust (`calibrate.py wall`, `OU-Wall`): left first, then right; it fits if the box clears 40 to that side (walls 46 wide pass, 48 not), and `Destination` becomes 50 aside and 25 up. Modelled; ours matches all five lanes. `--maps 60`: 8,368 units per bot, 140 deaths, no script error (neutral). Next: whether both limits follow `CollisionHeight` / radius.

## 2026-11-14 — mesh skins: empty slots, environment maps, Style

- Found while taking the README shots. CTF-Face's asteroids drew flat grey: `boulder1M` / `boulder2M` point their material at an empty slot, and they now take the mesh's first texture (unverified). `UDamage` uses `bMeshEnviroMap` with `Texture`, now drawn with a static environment map. Actor `Style` (masked / translucent / modulated) was ignored, so translucent effects drew black (DM-Liandri's teleporter); it now adds to the mesh flags. Fixed in both the static scene and the play view.
- Open: AS-Frigate's black patches against the sky are not meshes and not the sky pass; they look like unlit surfaces. Next: pictures of the original (CTF-Face's asteroids, AS-Frigate) to settle both.

## 2026-11-14 — README in the family format, with screenshots

- The README follows OpenBF1942-Plus: banner, badges, six languages (en, es, pt, fr, de, ru), screenshots, what works, how to try, roadmap, transparency, notice. Shots taken with `ut_mapview --shot` (`--play --follow` for bots): DM-Curse][, CTF-Coret, DM-Morpheus.
- ADR 0001 gains the family's exception: a few screenshots of our engine drawing retail maps, credited to Epic Games for identification only. Left out: CTF-Face and AS-Frigate shots, which show an untextured sky mesh and black holes.

## 2026-11-14 — falling bots: jumping ledges as the original does

- Of the bots "still falling" at the end of a sweep, most were simply mid-jump at the snapshot (none stayed in the air 3 s on the maps checked). Real defect: bots hopping in place against a ledge (DM-Codex). Our walk physics jumped by itself straight up, and our falling projected away the forward speed.
- `calibrate.py ledge` (`OU-Ledge`, `UpNNN` tags raise the goal): the original never jumps by itself. Walls send `HitWall`, and `PickWallAdjust` jumps (400 toward the goal, `JumpZ` up) for ledges up to 77, from the step-up height. Falling moves by the tick's mean velocity, and air control applies the `Acceleration` the walk left (102/s at 0.05 × 2048). Modelled. Ours matches the jump split, and the earlier calibrations are unchanged.
- `--maps 60`: still falling 56 actors (74), bots 41; 8,392 units per bot (8,310), 126 deaths, no script error; inside solid 3 dropped items. Next: the `JumpZ` scaling of the ledge limit, `PickWallAdjust`'s sideways adjust.

## 2026-11-14 — walking into steep faces: the original's wall limit; three of our bugs

- `calibrate.py entry` (`OU-Entry`, `WalkNNN` tags set `GroundSpeed`): the original switches to falling against slopes up to about 75° and treats steeper faces as walls (the limit moves between runs); its slide start velocity is not along the slope and fits no simple rule (not modelled).
- The probe exposed three bugs of ours: the 4.8 hover was applied over a steep face the floor probe touched (climbing 75–80° faces 4.8 a tick), a step could end on a steep face, and a pawn wedged between floor and face fell forever. Fixed (hover and steps only onto walkable floor, landing when the slide meets a floor, faces under normal z 0.263 are walls); synthetic 75° test.
- `--maps 60`: bots still falling 49 (59), 8,310 units per bot, no script error. Next: the remaining falling bots, case by case.

## 2026-11-14 — sliding on steep slopes measured and modelled

- `calibrate.py slide` (`OU-Slide`, `ProbeDropper` dropped on 46–80° slopes): the original slides with 1 / (1 + nz²) of gravity's pull along the slope (fit within 0.003 at five angles, mechanism unknown). The engine applies it; ours matches within 0.3 %. Synthetic test on a 60° slope.
- Still apart: walking into a too-steep slope, the original starts its slide at 347 along it (ours 278), so it gets to 171 where ours gets to 100. Simple rules tried do not fit.
- `--maps 60`: 8,294 units per bot (8,348), 141 deaths, bots still falling 59 (same), inside solid only the two map-data items, no script error. Next: the walk-to-slide switch, with entry speeds varied.

## 2026-11-14 — air control measured: none from latent moves

- `calibrate.py air` (`OU-Air`, `ProbeFaller`; the mutator reads `AirNN` PathNode tags as AirControl): a falling non-player pawn doing `MoveTo` gets no horizontal acceleration in the original, for AirControl 0 to 1; ours matches, gravity 950 in both.
- So the steep-slope gap is the slide itself: the original's slide starts at 347 along the slope and slows by 414 units/s² (ours 278 and 683). Next: a slide calibration (pawns dropped on slopes of several angles, with and without speed) to find the rule.

## 2026-11-14 — leaf hulls in the box trace; sliding on steep slopes

- The Model's `leaf hulls` decoded (level.md): per solid cell, its planes and bounding box. The box trace checks a reached solid leaf against its hull (the box sides act as bevels): on the probe's ramps ours now matches the original to 0.2 at the crest (it rose 30 too high). C++ vs Python 0 differ; ASan/UBSan clean on all maps and two 20 s matches.
- Walking into a slope too steep to walk turns to falling along it, and falling slides the rest of the tick, as the probe showed. Ours reaches 81 up a 46° slope, the original about 165: falling has no air control yet.
- `--maps 60`: 8,348 units per bot (5,328), 139 deaths (97), no bot inside solid (shell cases and a dropped minigun are), bots still falling 59 (50), no script error. Next: measure air control with the probe.

## 2026-11-14 — our VM runs the same probe

- `ut_script --map MAP?mutator=Pkg.Class`: the URL options reach `InitGame`, so the retail game loads the mutator; `UTVM_LOG=1` prints every log line; spawned instances are numbered per class (ProbePawn0...). `calibrate.py --ours build/ut_script` reports both engines side by side.
- Steps: identical. Ramps: past a crest ours rises ~30 too high (no bevel planes in the box trace) and ours climbs 45.7–46° slopes the original cannot.
- Next: bevel planes in the box trace, checked with `calibrate.py ramps --ours`.

## 2026-11-14 — walking hover from the probe; floor limit verified

- Walking pawns now hover 4.8 above the floor (measured) and step 25 from there: a synthetic test climbs 29, not 30, as the original. `--maps 60`: 5,328 units per bot (5,374), 97 deaths (88), bots still falling 50 (68), no script error.
- `calibrate.py ramps` (true wedge brushes, `--angles`): the original walks 45.5° (normal z 0.7009), not 45.7° (0.6984), so the 0.7 floor limit holds. On steeper slopes it still gets about 120 units up, not compared yet.
- Next: our VM running the same probe (mutator from the URL), so both engines answer each calibration; then slopes too steep to walk, falling bots.

## 2026-11-14 — the probe measures the original

- [ADR 0008](../architecture/adr/0008-probe-the-original.md): `tools/probe/OUProbe` (our UnrealScript: a mutator spawning a `ProbePawn` at each PathNode, logging its state each tick) runs on the original's Linux dedicated server; `calibrate.py` compiles it, builds the map with ut99-mapgen, runs the server and reads the log.
- First numbers (`OU-Steps`): a walking pawn hovers 4.8 above the floor and climbs steps up to 29, not 30 (we use 25 on the floor). Pawns driven by `Acceleration` in `Tick` do not move; the probe walks with `MoveTo`.
- Snags: the 469f-rc5 Linux `ucc make` recurses forever into `Classes/./` (compiled with a 469e System instead); a pawn spawned by a mutator needs `bGameRelevant` or the game discards it; killing the server with SIGKILL loses its log. Next: our engine with the hover and step, then ramps.

## 2026-11-14 — ut99-mapgen set up; the install is 469f-rc5

- ut99-mapgen ([ADR 0007](../architecture/adr/0007-community-repositories.md)) runs outside the repo on a copy of the install with the OldUnreal Linux patch; its self test passes (13/13). It builds our own calibration maps with the retail `ucc`; maps stay out of the repo.
- Matching the patch found the install is 469f-rc5, not 469e: `Manifest.ini` says 469e, but the packages are byte-identical to the v469f-rc5 Linux release. Docs that called it 469e corrected (ADR 0006 and older stages keep the old name).
- ADR 0007 corrected the same day: the UT Bible is a players' guide, not engine documentation. Next: a calibration map (steps, ramps, ledges) observed in the original and in our engine.

## 2026-11-14 — blocking between actors

- `World::move` sweeps the moving centre against the cylinders of actors that block it (grown by both sizes); both must block the other's kind (`bBlockPlayers` toward a `bIsPlayer` pawn, else `bBlockActors`; inferred from retail defaults, unverified), so pawns block pawns and projectiles still touch them. A blocked move sends `Bump` both ways, falling and flying included (`HitWall` stays for BSP and movers).
- `--maps 60`: 5,374 units per bot (5,267 before), 88 deaths (51), no script error; bots still falling unchanged (68 vs 63, an older problem). Costs ≈ 20 % of a match's time (every move scans every actor).
- The sweep now names the map of an actor inside solid: a CTF-Gauntlet bot was pushed into a wall by the lift it rode (riders carried without collision, known). Next: bots stepping around a blocking pawn, or the falling bots.

## 2026-11-14 — community repositories reviewed

- [ADR 0007](../architecture/adr/0007-community-repositories.md): UT Bible, ut99-mapgen (own test maps) and UTCompress (`.uz`) allowed as public documentation; ut99-recomp forbidden (from disassembly); Epic's Ut99PubSrc not used for now; exported script repositories redundant.

## 2026-11-11 — the match in the viewer

- `ut_mapview MAP --play`: the world runs in the window; `engine/play_view.*` draws live actors each frame (LodMesh actors at their animation frame, blended, and mover brushes); drag to look, F follows a bot; `--play --shot --time S --follow` for checks. The LodMesh reader now keeps every frame.
- Watching it found what the meters missed: player meshes lay down (RotOrigin was applied inverted; fixed in both views); bots fell into Deck16's slime and Morpheus' void and never died: `FootZoneChange` ran after the region was updated (the script compares the old zone, so `PainTime` never started), `PainTimer` and the zones' `ActorEntered` / `ActorLeaving` were never sent.
- Relative mouse mode and pointer warping turn the camera endlessly under WSLg: drag-to-look instead. Next: bots stuck in corners (seen in the viewer), blocking between actors.

## 2026-11-11 — movers: lifts and doors

- `engine/world_mover.cpp`: mover brushes (their own BSP, placed by Location, Rotation and PrePivot, checked on lift shafts) block traces and moves; `PHYS_MovingBrush` glides between keyframes for the retail `Mover` script; bases (`Attach` / `Detach`, riders carried), `EncroachingOn`, `Bump` + `HitWall` on movers, routes calling `SpecialHandling`. A DM-Deck16][ bot waits, rides a lift up and walks on; `--maps 60`: 4,649 units per bot (was 4,084), 47 deaths, no script error.
- Found on the way: `reachable` saw a raised lift as a 500-unit pit; tags compared with case (`Trigger` events missed their movers); `touches` iterated `actors` while `Touch` spawned (a crash only the chained 60 s sweep hit; found with gdb).
- Still: routes rarely choose lifts (24 of 151 DM movers moved in 60 s), `JumpSpot`s, items above jump height. Next: blocking between actors, or the player and the renderer joining the world.

## 2026-11-11 — navigation quality

- Bots travel 2x farther (`--maps 60`: 4,084 units per bot, was 1,968; 28 deaths, was 13; no script error, ASan clean): routes skip the node the bot stands on, moves give up after a second without progress, walking slides up ramps and jumps ledges, pickups need a real touch, `LatentFloat` follows `Sleep` (at 0, bots camped at every item).
- Found with `--maps` "still falling, by class": comparing the destination with the bot's feet made it jump at every node (nodes sit at centre height); compare centres. Fixed `LodMesh` class-name case (animation sequences were never found).
- Still stopping bots: lifts (movers not simulated), `JumpSpot`s, teleporters reached by lift, ledges higher than a jump (DM-Codex, bot hopping in place). Next: movers.

## 2026-11-11 — combat: bots see, fight, die and respawn

- Sight (`SeePlayer` / `EnemyNotVisible` every 0.2 s), traces through actor cylinders (`Trace`, `TraceActors` with several out values per `ForEach` item), foot / head regions: bots attack, fire, damage, kill, score and respawn on the retail scripts. `--maps 60`: 83 maps, no script error, 13 deaths among 289 bots (they still get stuck too often to meet much).
- Found on the way: Object's native `Class` / `Name` must be filled in (`Item.Class == Class` decides every pickup); a trace starting inside a cylinder (a weapon in its owner) must not hit it; `UTVM_TRACE` call tracing and "accessed None (Member)" made each cause visible in minutes.
- Next: navigation quality (bots stuck), then blocking between actors and projectile weapons checked in play.

## 2026-11-11 — bots in the match

- Bots now join and roam: reach specs read, path network and Dijkstra routes (`FindPathToward`, `FindBestInventoryPath` with the items' own `BotDesireability`), latent `MoveTo` / `MoveToward` / `WaitForLanding` / `FinishAnim`, walking with step-up, touching (pickups), mesh animation sequences, actor regions. `--maps 20`: 289 bots over 83 maps, no script error.
- What the retail scripts demanded: editor `Camera` actors must go (they block the match start as never-ready players); the game must be `Level.Game` before its `PreBeginPlay` (else its GameReplicationInfo destroys itself); `Texture(...)` casts need the class of package objects; struct inheritance (`Plane extends Vector`).
- Next: combat (aiming, weapons firing, damage), blocking between actors, better reachability so bots stop getting stuck.

## 2026-11-11 — BSP collision and physics

- `engine/collision.*`: point solidity and swept boxes on the level BSP; `World` runs falling, projectile, flying, rotating and a minimal walking, and `Trace`, `FastTrace`, `Move`, `SetLocation` use it. Sweep: of 904 actors falling at map start, 3 are still airborne after 5 s (Assault projectiles in flight).
- Solidity rule found on data: front is empty; back is empty only from empty space through a node with flag 0x1 (not CSG); 1,283 / 1,285 player starts pass. Fixed on the way: a box that starts overlapping solid (pickups resting on floors, boxes near walls) must be able to move; only entering through a plane is a hit.
- Next: actor collision (touch, bump, encroach) and pawns: player starts, bots walking (`MoveTo`, `WaitForLanding`).

## 2026-11-11 — actor world: retail maps run their script

- `engine/world.*`: map actors become VM instances, the game class (`DeathMatchPlus`) is spawned as `Level.Game` with `InitGame`, play events, ticks, timers, `ForEach` iterators, pawn list, spawn / destroy, rotator math, `DynamicLoadObject`. `ut_script --maps 5`: all 83 maps run 5 s with no script error.
- Learned: without a GameInfo every actor destroys itself in `PreBeginPlay` (`Level.Game.IsRelevant`); what the game spawns during `InitGame` must begin play at once or `DMMutator.MyGame` is None for every actor. Classes like `Core.Class` exist only in C++ (no export). `ForEach` ends at its `0x30`; `continue` = `0x31` + jump.
- Next: physics and collision against the BSP (falling, walking, traces), then pawns and bots (`MoveTo`, `WaitForLanding`), the remaining stops in `--states`.

## 2026-11-11 — VM states

- States in the VM: dispatch through the current state and its parents, labels, `GotoState` (EndState / BeginState, `'Auto'`), latent `Sleep`, `tick`. `ut_script --states` is the phase C meter: all 1,604 classes enter their auto state (233 have one); the 346 that stop do so only on engine natives (`SetCollision`, `SetPhysics`, animation, timers).
- Learned on retail data: the label table offset in the State header points at the first entry, one byte after the `0x0C` token (all 996 states); a State's `super` is its parent state; flag 0x2 = auto, function flag 0x8 = latent.
- Next: an actor world (location, rotation, collision flags, physics none / falling), timers and the animation natives, driven by the sweep's ranking.

## 2026-11-11 — VM objects

- Objects in the VM: one canonical `Obj` per package object (imports resolved), instances copy their class defaults (super's, then the tagged list; all 1,604 retail classes build), variables are keyed by their property declaration. Contexts (`a.b`), class contexts, structs, static arrays and casts work; 13 `TestInfo` self-tests now pass, `PostBeginPlay` runs to its end.
- Learned: in a context only the variable or call uses the context object; arguments and array indices use the caller's self. A None context skips its expression and yields zero. Static arrays reach 16,384 elements. Exports named `None` with size 0 and class 0 are placeholders, not classes.
- Next: states and labels, then the first engine natives (spawn, timers) to run an actor.

## 2026-11-11 — UnrealScript VM: first retail functions run

- `engine/vm.*` + `ut_script`: functions are loaded in their in-memory form (the bytecode walker now emits it), natives are bound by the export name of their Core declaration (`Add_IntInt`), not by hard-coded indices. `Console.FormatFloat`, `TwoDigitString` and Epic's own `TestInfo` self-tests (loops with `continue`, `switch`, optional out parameters) run with every `assert` passing.
- Learned on retail data: the `0x18` skip size covers the operand plus the closing `0x16`.
- Next: objects (self, instance / default variables), then states.

## 2026-11-11 — ADR 0006 accepted; repository published

- The maintainer accepted ADR 0006: the engine runs the retail UnrealScript bytecode in a VM and implements only the native functions in C++. Phase C starts with the VM.
- Repository public at `github.com/OpenGamePlus/OpenUT99-Plus`; the overnight branch was fast-forwarded into `main`.

## 2026-11-11 — review fixes: untrusted package data

- A fresh review plus an ASan/UBSan build found memory errors on retail and crafted data: lightmaps with `first light = −1` read before the light list (retail, DM-Morpheus), negative export offsets wrapped past the bounds check, lightmap fields and surface normals were unvalidated, bytecode recursion was unbounded, and a dangling property pointer in the actor lookup. All fixed with tests; `validate_model` now guards every index the engine follows.
- Python readers now raise `ValueError` for truncated data and bad name indices (shared helpers in `upkg.py`). Deferred minor findings are listed in debt.md.

## 2026-11-10 — mesh actors

- The viewer draws actors with LodMeshes: properties come from the actor, then its class defaults up the super chain (C++ class reader with the bytecode walker; class defaults and meshes match Python on all packages). Pickups, decorations and the EOL_Statues trophies stand upright and textured.
- Fixed on the way: class imports must match class objects (export class 0). Mesh space needs RotOrigin inverted; with it, raw Y-up player meshes and roll-rotated trophies come out upright.

## 2026-11-10 — lightmaps and sky

- Lightmap layout pinned with statistics: luxels sample the polygon in texture space from `pan` (U min − 0.1) by U/V scale; shadow bits LSB first; set bits lie within 25 × (LightRadius + 1) of their light. Baked into one atlas per map; CTF-Face shows its star-field sky through FakeBackdrop surfaces drawn depth-only after a pass from the SkyZoneInfo.
- Light intensity is a guess: with N·L the median lit luxel was 0.18 (too dark), without it 0.34; kept without, flagged in debt. Deck16][ spawn is lit only by brightness-0 lights and stays black: compare with the original.

## 2026-11-10 — first level viewer

- `ut_mapview`: C++ Model reader (0 differences vs Python on all maps), cross-package texture resolution, P8 textures, BSP surfaces batched per texture, OpenGL 3.3. All 83 maps load; the only untextured surfaces (88) have texture `None` in the data. Shots of Deck16][, Face and Morpheus look right, text unmirrored.
- Level models carry vert entries beyond the points array; no node uses them, so validation checks only node-referenced verts.
- Not yet: lightmaps (full bright), sky, actors, back-face culling, DXT / 469 compressed textures (checker).

## 2026-11-10 — configuration and package lookup

- `tools/uini.py` and `engine/config.*` / `engine/vfs.*`: `.ini` sections with repeated keys; packages found through `[Core.System] Paths`, then the download cache (`cache.ini` maps GUID file names to package names). Every top-level import of every retail package resolves, in Python and C++.

## 2026-11-10 — C++ engine started

- `CMakeLists.txt`, `engine/upkg.*`: package and tagged-property reader in C++17, built with `-Wall -Wextra -Wconversion`, no warnings; unit tests reuse the Python synthetic inputs.
- `tools/compare_engine.py` runs `ut_dump` against the Python readers: 0 differences on all 396 packages. The C++ loader also rejects exports outside the file (the Python one does not).

## 2026-11-10 — UnrealScript execution proposal

- ADR 0006 (Proposed): run the bytecode in a VM. Gameplay and UI packages have zero native functions (≈ 7,300 functions, 1.3 MB of bytecode); the 383 natives live in Core, Engine and the network / web / demo packages. Native reimplementation was weighed and rejected in the proposal (ADR 0004 forbids line-by-line translation; networking and mods need the original logic). The maintainer decides.

## 2026-11-10 — meshes; phase A exit check met

- `tools/umesh.py`: Mesh, LodMesh, SkeletalMesh, Animation byte-exact; the sweep now reports all 276,885 retail exports exact, with no class left without a reader.
- Byte-exact was not enough: the first Soldier render was scrambled. Each LodMesh frame stores the special (weapon) vertices first, so wedge indices are offset by `special_verts`; with that the model renders right.

## 2026-11-10 — level geometry

- `tools/umodel.py`: Polys, Model (BSP nodes, surfaces, verts, zones, lightmaps, bounds, leaves) and Level (actor list, URL, reach specs) byte-exact on the first hypothesis from public UE1 docs.
- Cross-checks on 83 maps: all surface/node indices in range; each lightmap is one shadow bitmap per light (1 bit per luxel, rows rounded to bytes) and all 155,353 sizes match. Light colour is computed at run time from light actors: engine behaviour, open.

## 2026-11-10 — script objects and bytecode

- `tools/uscript.py`: classes, states, functions, structs, enums, consts, text buffers and property declarations, all byte-exact. Class objects have no leading property list; their defaults come last as a tagged list.
- Skipping bytecode is impossible: the header's script size counts memory (4-byte references), disk uses compact indices. The decoder walks every expression; all 1.58 M tokens of the install land exactly on the declared size. 9,156 functions in `System/*.u`, 383 native.
- Strings with a negative length are UTF-16 (third-party cache packages).

## 2026-11-10 — fonts

- `tools/ufont.py`: pages of glyph rectangles plus characters-per-page; 469 (version 69) adds a code-point remap table and a flag. All 78 fonts byte-exact.

## 2026-11-10 — sound and music

- `tools/usound.py`: Sound and Music are a format name plus a complete embedded file; all 2,914 byte-exact. 2,881 PCM WAVs (mostly mono, 11–22 kHz), music is 30 IT, 2 S3M and 1 XM: `.umx` are tracker modules, as suspected.
- The bulk-data skip offset starts at version 62, not 63 (a v62 music file has it); one helper, `upkg.lazy_bytes`, now serves textures and sounds.

## 2026-11-10 — procedural textures

- Wet, Wave and Ice textures carry nothing beyond the Texture data; FireTexture adds a spark list (count + 8 bytes each). All 314 byte-exact. How they animate is engine behaviour, still open.

## 2026-11-10 — textures and palettes

- `tools/utex.py`: all 16,427 Palette / Texture / ScriptedTexture objects byte-exact. Mips carry a u32 skip offset from version 63 (absent in 61); 469 packages add a second, compressed mip array when `bHasComp` is set. P8 PNG previews look right.
- Readers now receive the object's properties (`bHasComp` decides the layout). `CompFormat` 7 and 12 (1 byte per pixel) are not identified; DXT1 not decoded in Python.

## 2026-11-10 — object dispatcher

- `tools/uobjects.py`: native class readers register by class name; `--sweep` classifies every export as exact / leftover / error / noreader. Baseline: 83,444 exact, 193,441 without reader (34 native classes + 3,300 class objects). This is the phase A progress meter.

## 2026-11-10 — tagged properties decoded

- `tools/uprops.py`: state frame, tags and 8 property types, with known structs (Vector, Rotator, Scale, PointRegion, Color, Coords) decoded. All 273,585 non-class exports parse; no class mixes "ends at None" with "class data follows", so the reader gives the exact start of the native data.
- Rejected: decoding each value by its own type width; values are read from a slice of `size` bytes, so an unknown struct can't throw the rest of the list out of step.
- Next: class data, starting with `Texture`/`Palette` or `Class`/bytecode (needed for the VM vs. native decision).

## 2026-11-10 — project started, package container decoded

- Docs set up on the OpenBF1942-Plus model (AGENTS.md, AI layer, ADRs 0001–0005, checker). Decided: clean-room engine running retail content, Python stdlib tools + C++17/SDL2/OpenGL 3.3, network compatibility with original 436/469 as a long-term goal.
- `tools/upkg.py` reads header, names, imports and exports; all 396 packages of the 469e install parse (versions 61–69), every export inside its file.
- License AGPL-3.0, same as the sibling projects. Open questions in [debt.md](../roadmap/debt.md).
