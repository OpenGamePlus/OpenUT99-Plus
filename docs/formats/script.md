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
| State | Struct + probe mask u64, ignore mask u64, label table offset u16, state flags u32 |
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

Engine: `engine/vm.*` ([ADR 0006](../architecture/adr/0006-unrealscript-execution.md)). A function is loaded into its in-memory form (references and names widened to 4-byte ints), so jump, case and skip offsets index it directly. All 20,052 retail functions load; 662 of their 50,236 parameters and locals have types the VM does not hold yet (structs other than Vector / Rotator, dynamic arrays).

| Behaviour | Evidence |
|---|---|
| Function `children` lists parameters in declaration order, then the return value, then locals. Flags: 0x80 parameter, 0x10 optional, 0x100 out, 0x200 skip, 0x400 return value | retail declarations, e.g. `Core.Object.AddAdd_PreInt`, `Engine.TestInfo.SubTestOptionalOut` |
| Native index → function: each native declaration in Core / Engine carries its index; its export name (`Add_IntInt`, `InStr`) names the operation, so natives are implemented by name | 152 indexed natives in Core |
| `0x18` skip (right operand of `&&`, `\|\|`, parameters flagged skip): the u16 counts the operand **and** the closing `0x16`; when the left side decides, execution resumes after the call | retail `TestInfo.TestContinueWhile`: size 10 = 9-byte operand + `0x16` |
| `0x05` switch evaluates the cases in turn (`0x0A`: u16 offset of the next case, `0xFFFF` = default) and resumes at the first match; a case reached by falling through skips its value | `TestInfo.TestSwitch` logs its success |
| Omitted optional parameters are `0x0B` (or missing before `0x16`) and start as zero | `TestInfo.TestOptionalOut` logs its success |
| Conversions `0x39`–`0x59` (e.g. `0x53` int → string, `0x55` float → string) | public UE1 documentation; `0x55` with 6 decimals and the others' text formats are **unverified** |

Retail checks (`build/ut_script <UT99_DIR> Package.Class.Function [args]`): `Engine.Console.FormatFloat 1.2345` → `"1.23"`, `Botpack.TournamentScoreBoard.TwoDigitString 5` → `"05"`, and `Engine.TestInfo` `TestContinueWhile`, `TestContinueDoUntil`, `TestContinueFor`, `TestSwitch`, `TestOptionalOut` run to the end with every `assert` passing and the log their source text predicts.

Not yet: objects (`self`, instance and default variables, contexts), states and labels, iterators, `new`, casts, struct member assignment, arrays.

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
