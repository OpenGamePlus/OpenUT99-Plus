# Script objects and bytecode

Reader: `tools/uscript.py`. Covers `Class`, `State`, `Function`, `Struct`, `Enum`, `Const`, `TextBuffer` and every `*Property` declaration.

Verified (`uobjects.py --sweep <UT99_DIR>`): all of them byte-exact across the retail install, including 3,300 class objects, 20,052 functions and every function's bytecode (decoded token by token to exactly its declared in-memory size). Token meanings follow public UE1 documentation; byte-exact decoding of all 1.58 million tokens is the evidence that the operand layout below is right, not that each token's semantics are.

Strings anywhere in packages: compact-index length including the NUL; a **negative** length means UTF-16LE characters (seen in third-party cache packages).

## Common prefixes

Non-class script objects start with the tagged property list (normally just `None`), then the fields below. Class objects (export class 0) start directly with the Struct fields.

| Part | Fields |
|---|---|
| Field | super (object ref), next (object ref) |
| Struct | Field + script text (object ref, a `TextBuffer`), children (object ref, first member), friendly name (name), line i32, text position i32, script size u32 (**in-memory** bytes), bytecode |
| State | Struct + probe mask u64, ignore mask u64, label table offset u16 (in-memory offset of the first label entry, just after the `0x0C` token; `0xFFFF` = none), state flags u32 (`0x2` = auto) |
| Class | State + class flags u32, GUID (16 bytes), dependencies (compact count × (class ref, deep u32, script text CRC u32)), package imports (compact count × object ref), within (object ref), config name (name), default properties (tagged list) |

## Other script objects

| Class | Layout after the property list |
|---|---|
| `Function` | Struct + native index u16, operator precedence u8, function flags u32, rep offset u16 if flags & `0x40` (net). Version 64+ only; no retail function is older |
| `Const` | Field + value (string) |
| `Enum` | Field + compact count × name |
| `TextBuffer` | pos u32, top u32, text (string) |
| `*Property` | Field + array dim u16, element size u16, property flags u32, category (name), rep offset u16 if flags & `0x20` (net), then type refs: Byte → enum; Object → class; Class → class, meta class; Struct → struct; Array → inner property; Map → key, value; FixedArray → inner + count u32 (unverified: none in retail) |

## Bytecode

The header's script size counts memory, where object references and names take 4 bytes; on disk they are compact indices, so the reader must decode every expression and count memory size until it matches. Operands per token:

| Token | Name | Operands |
|---|---|---|
| 0x00, 0x01, 0x02 | local / instance / default variable | object ref |
| 0x04 | return | expr |
| 0x05 | switch | u8 size, expr |
| 0x06 | jump | u16 |
| 0x07 | jump if not | u16, expr |
| 0x08 | stop | — |
| 0x09 | assert | u16 line, expr |
| 0x0A | case | u16 next (0xFFFF = default), expr unless default |
| 0x0B | nothing | — |
| 0x0C | label table | (name, u32 offset)… until name `None` |
| 0x0D, 0x0E | goto label, eat string | expr |
| 0x0F, 0x14 | let, let bool | expr, expr |
| 0x10, 0x1A | dyn array element, array element | expr, expr |
| 0x11 | new | 4 exprs |
| 0x12, 0x19 | class context, context | expr, u16 skip, u8 size, expr |
| 0x13, 0x2E | meta cast, dynamic cast | class ref, expr |
| 0x16 | end of function parameters | — |
| 0x17 | self | — |
| 0x18 | skip | u16, expr |
| 0x1B, 0x38 | virtual call, global call | name, params… 0x16 |
| 0x1C | final call | function ref, params… 0x16 |
| 0x1D, 0x1E | int, float const | 4 bytes |
| 0x1F | string const | NUL-terminated bytes |
| 0x20, 0x21 | object, name const | object ref / name |
| 0x22, 0x23 | rotator, vector const | 12 bytes |
| 0x24, 0x2C | byte const, int const byte | u8 |
| 0x25–0x28, 0x2A | 0, 1, true, false, none | — |
| 0x29 | native parm | object ref |
| 0x2D | bool variable | expr |
| 0x2F | iterator | expr, u16 |
| 0x30, 0x31 | iterator pop / next | — |
| 0x32, 0x33 | struct ==, != | struct ref, expr, expr |
| 0x34 | unicode string const | u16 chars until 0 |
| 0x36 | struct member | property ref, expr |
| 0x39–0x5F | primitive conversions | expr |
| 0x60–0x6F | extended native call | u8 (index = (token − 0x60) × 256 + u8), params… 0x16 |
| 0x70–0xFF | native call (index = token) | params… 0x16 |

## Execution (VM)

Engine: `engine/vm.*` ([ADR 0006](../architecture/adr/0006-unrealscript-execution.md)). A function is loaded into its in-memory form (references and names widened to 4-byte ints), so jump, case and skip offsets index it directly. All 20,052 retail functions and 996 states load; 27 of the 50,236 parameters and locals have types the VM does not hold yet (dynamic arrays, maps).

| Behaviour | Evidence |
|---|---|
| Function `children` lists parameters in declaration order, then the return value, then locals. Flags: 0x80 parameter, 0x10 optional, 0x100 out, 0x200 skip, 0x400 return value | retail declarations, e.g. `Core.Object.AddAdd_PreInt`, `Engine.TestInfo.SubTestOptionalOut` |
| Native index → function: each native declaration in Core / Engine carries its index; its export name (`Add_IntInt`, `InStr`) names the operation, so natives are implemented by name | 152 indexed natives in Core |
| `0x18` skip (right operand of `&&`, `\|\|`, parameters flagged skip): the u16 counts the operand **and** the closing `0x16`; when the left side decides, execution resumes after the call | retail `TestInfo.TestContinueWhile`: size 10 = 9-byte operand + `0x16` |
| `0x05` switch evaluates the cases in turn (`0x0A`: u16 offset of the next case, `0xFFFF` = default) and resumes at the first match; a case reached by falling through skips its value | `TestInfo.TestSwitch` logs its success |
| Omitted optional parameters are `0x0B` (or missing before `0x16`) and start as zero | `TestInfo.TestOptionalOut` logs its success |
| Objects: instance (`0x01`) and default (`0x02`) variables name the property declaration; a class's defaults are its super's plus its own tagged list (all 1,604 retail classes build them) | `TestInfo.TestStatic` asserts `default.xnum == 777` |
| Context `0x19` / class context `0x12`: object expr, u16 skip, u8 size, expr. The inner variable or call uses the context object; arguments and array indices are evaluated with the caller's self. A None context skips u16 bytes (counted after the size byte) and yields zero | `TestInfo.TestNullContext`; synthetic test |
| Array element `0x1A`: index first, then the array (any variable, also inside a context) | retail `TestInfo.TestX` bytes; `TestInfo.temp` |
| Static array dimension is a u16; retail goes up to 16,384 (`udemo.DemoPlaybackSpec.HideActors`) | sweep over all property declarations |
| States: a State's `super` is its parent state (`extends`, or the same-named state of the super class); flag `0x2` marks `auto state`. Calls (`0x1B`) look in the current state and its parents first, then the class chain; `0x38` (global) skips the state | retail `TestInfo` states (`CCAA extends AA` → super `AA`); source of `Fan2`, `Chest` (`auto state`) |
| Label table entries: name (u32 in memory) + code offset u32, ended by a name whose text is `None` (packages hold several `None` names) | all 996 retail states: every table offset follows a `0x0C` |
| `Actor.LatentFloat` holds what is left of a latent `Sleep`: bots read `markedItem.LatentFloat` to decide whether to camp for a respawning item (at 0, every sleeping item looks about to respawn) | `Bot` state `Roaming` source |
| Function flag `0x8` = latent (`Sleep`, `FinishAnim`, `MoveTo`...): state code stops at the call and continues after it on a later tick | Engine native declarations |
| `ForEach` `0x2F`: iterator native call (flag `0x4`, e.g. `AllActors` 0x405), u16 offset of the loop's `0x30` pop. `0x31` returns to the body while items remain, else falls through; `continue` is `0x31` + a jump to the `0x30` | retail `TestInfo.TestContinueForEach` bytes; its log matches its source |
| Object's native variables `Class` and `Name` are kept by the engine: scripts compare `Item.Class == Class` (inventory pickup queries); left at None, every pickup query answers "already have it" | bots picked up nothing until they were filled |
| `ForEach` iterator natives may write several out parameters per item (`TraceActors`: actor, hit location, hit normal) | `Pawn.TraceShot` source |
| Classes defined only in C++ (`Core.Class`, `Engine.Sound`) are imported but have no export anywhere | map sweep: 292 such references |
| `GotoState`: `EndState` in the old state, `BeginState` in the new one, code from the label (default `Begin`) of the state or its nearest parent state with labels; `'Auto'` picks the auto state | public documentation; order **unverified** |
| Conversions `0x39`–`0x59` (e.g. `0x53` int → string, `0x55` float → string) | public UE1 documentation; `0x55` with 6 decimals and the others' text formats are **unverified** |

Retail checks (`build/ut_script <UT99_DIR> Package.Class.Function [args]`): `Engine.Console.FormatFloat 1.2345` → `"1.23"`, `Botpack.TournamentScoreBoard.TwoDigitString 5` → `"05"`, and `Engine.TestInfo` `TestContinueWhile`, `TestContinueDoUntil`, `TestContinueFor`, `TestSwitch`, `TestOptionalOut` run to the end with every `assert` passing and the log their source text predicts; so do `TestStatic`, `TestQ`, `TestStructBools`, `TestX`, `TestNullContext`, `TestLimitor` and `PostBeginPlay`. `TestInfo.Tick` runs until `GetPropertyText` (not implemented).

State sweep (`ut_script <UT99_DIR> --states`): all 1,604 classes spawn; 453 enter an auto state; none stops on an error.

## World (map play)

Engine: `engine/world.*`, `engine/world_ai.cpp`, `engine/world_mover.cpp`. A map's actors become instances (class defaults, then the actor's own tagged properties); `Level` points at the first actor, the LevelInfo; editor `Camera` actors are dropped (they are PlayerPawns that never get ready, and would hold the match start). The game class (`LevelInfo.DefaultGameType`, else `[Engine.Engine] DefaultGame`) is spawned without events and set as `Level.Game`; then play begins, the game gets `InitGame` (what it spawns from here on gets its play events at once), then the map's actors and the game get `PreBeginPlay`, `BeginPlay`, `PostBeginPlay`, `SetInitialState`, phase by phase. Each tick: `Level.TimeSeconds`, `Tick`, timers, physics, `Region` (zone from the BSP leaf, `ZoneChange`; pawns also `FootRegion` / `HeadRegion`), animation, touching, sight (`SeePlayer` / `EnemyNotVisible` every 0.2 s: `SightRadius`, `PeripheralVision`, line of sight), state code. This order is **unverified**; the evidence is that the retail scripts then run cleanly: `DMMutator` needs its `PostBeginPlay` before the map's actors call `CheckReplacement`, and the `GameReplicationInfo` spawned in `GameInfo.PreBeginPlay` destroys itself unless `Level.Game` is already set.

Map sweep (`ut_script <UT99_DIR> --maps 20`): all 83 maps load, start `DeathMatchPlus`, add their bots, and run 20 s with no script error. 289 bots spawn, roam the path network (reach specs), pick up items by touching them, fall off ledges and land; they travel 1,764 units each on average (often stuck, see debt). No bot ends inside solid except one that spawns there (CTF-Coret's misplaced start). Physics: falling (zone gravity, terminal velocity, `Landed` on a normal with z ≥ 0.7, `HitWall` and slide otherwise), projectile, flying, rotating, a minimal walking; `Trace`, `FastTrace`, `Move`, `SetLocation` use the BSP ([level.md](level.md#collision-engine)). Combat runs on the retail scripts: bots see each other, attack, fire (instant-hit weapons through `TraceShot` → `TraceActors`, a ray against collision cylinders), take damage, die, score and respawn. Animation follows the mesh's sequences (`AnimRate` = rate × sequence rate / frames, finish at `AnimLast` = 1 − 1/frames, `AnimEnd`; public documentation, unverified). Movers run on the retail `Mover` script: the engine's part is `PHYS_MovingBrush` (9): `InterpolateTo` sets `OldPos`, `PhysAlpha` 0, `PhysRate` = 1 / seconds and `bInterpolating`; each tick the brush goes from `OldPos` toward `BasePos + KeyPos[KeyNum]` (rotation likewise), linearly or, for `MV_GlideByTime`, along 3t² − 2t³ (**unverified** curve); at the end `bInterpolating` clears and `InterpolateEnd` chains the keys; `FinishInterpolation` waits for it. Brushes block traces and moves ([level.md](level.md#collision-engine)). Walking sets `Base` to the mover stood on, else the LevelInfo on BSP floors (the bot script tests `Base == Level`), None when falling, with `Detach` / `Attach` / `BaseChange` and `StandingCount`; actors standing on a moving brush ride along; a blocking actor the brush would overlap gets `EncroachingOn` (true stops it). Walking into a mover sends `Bump` both ways and `HitWall` to the walker, where bots ask `HandleDoor`. Routes call the next step's `SpecialHandling(Pawn)` (`bCanDoSpecial`): `LiftCenter` sends a bot to wait at its exit, the lift's `StandOpenTimed` opens when the bot stands on it, `FinishNotify` → `StopWaiting` sends it on. Retail check: in DM-Deck16][ a bot placed at the bottom exit waits, rides the lift 532 units up and walks off at the top. Still placeholders (see [debt.md](../roadmap/debt.md)): blocking between actors, combat aiming, sound.

Not yet: `new`, dynamic arrays, binary struct defaults (other than Vector / Rotator), state `ignores`, engine natives (actors, spawning, timers).

## Numbers (retail `System/*.u`)

| Measure | Value |
|---|---|
| Classes | 1,604 |
| Functions | 9,156 (383 flagged native, 241 with a native index) |
| Bytecode | 1,533,034 bytes in memory |
| Tokens decoded (all packages) | 1,577,527, 202 distinct values; 162,601 native calls ≥ 0x70, 20,511 extended native calls |

## Open

- Semantics of each native function beyond the operators, string and math functions implemented in `engine/vm.cpp`.
- Flag bit names (function, property, class, state flags).
