# 0006 — Execute the game's UnrealScript bytecode in a VM

- Status: Accepted
- Date: 2026-11-10 (accepted 2026-11-11)

## Context

UT's game logic is UnrealScript bytecode inside the retail `.u` packages ([script.md](../../formats/script.md)). Measured on `System/*.u` (469e):

| Package group | Functions | Native functions | Bytecode (memory bytes) |
|---|---|---|---|
| Gameplay: `BotPack`, `UnrealShare`, `UnrealI`, `multimesh`, `relics`, `de`, `SkeletalChars` | 5,041 | 0 | 776,555 |
| UI: `UWindow`, `UMenu`, `UTMenu`, `UBrowser`, `UTBrowser`, `UTServerAdmin` | 2,251 | 0 | 524,599 |
| Engine core: `Core`, `Engine` | 1,446 | 308 | 145,147 |
| Other: `IpDrv`, `IpServer`, `UWeb`, `udemo`, `Editor`, `OldUnreal469c` | 418 | 75 | 86,733 |

241 distinct native function indices are declared (highest 3971). The bytecode decoder already walks all 1.58 million tokens of the install exactly.

Options:

1. **VM**: load classes from the user's packages and run their bytecode; implement in C++ only the native functions (≈ 383) and native classes' behaviour (actors, physics, rendering, networking).
2. **Native reimplementation**: rewrite the game classes (≈ 9,000 functions) in C++.

## Decision

Option 1, a bytecode VM.

- [ADR 0004](0004-clean-room-sources.md) forbids translating UnrealScript line by line; option 2 would mean rewriting 1.3 MB of gameplay and UI logic "from behaviour", the largest and least verifiable path.
- The VM runs the original logic unchanged, which is what network compatibility ([ADR 0005](0005-network-compatibility.md)) needs: replication follows the classes' own `replication` blocks and state.
- Mods and the 120 cached third-party packages are script too; only a VM runs them.
- The work concentrates on a small, testable surface: ≈ 383 native functions, specified one by one from their declarations, the script that calls them, and observed behaviour.

## Consequences

- New engine modules: class/property layout (from the property declarations), object instances with default properties, the VM, native function table.
- The VM is verified against the original by observable behaviour (game state, logs, network traffic), not by reading native binaries.
- Performance: the original ran this bytecode on 1999 hardware; an interpreter is acceptable.
