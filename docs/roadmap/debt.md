# Debt, risks, open questions

## Risks

| Risk | Impact | Note |
|---|---|---|
| Game logic lives in UnrealScript (`.u`) | the engine needs either an UnrealScript VM or native reimplementations of `Botpack` classes | deferred decision below |
| Native-only behaviour (`Engine.dll`, `Render.dll`) | physics, collision, BSP rendering details have no script to read | observe the original ([ADR 0004](../architecture/adr/0004-clean-room-sources.md)) |
| Replication must match the original byte for byte | network phase E may be infeasible | [ADR 0005](../architecture/adr/0005-network-compatibility.md) has a fallback |

## Deferred decisions

- Run UnrealScript bytecode in a VM vs. reimplement the game classes natively. Decide during phase A, after the bytecode is mapped.
- Whether disassembly of native binaries is ever allowed (now forbidden by ADR 0004).
- README translations.

## Known gaps

- Package and object flag bits not named ([package.md](../formats/package.md)).
- Only patch 469e checked; packages from version 436 installs not yet compared.
