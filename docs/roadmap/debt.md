# Debt, risks, open questions

## Risks

| Risk | Impact | Note |
|---|---|---|
| Game logic lives in UnrealScript (`.u`) | the engine needs either an UnrealScript VM or native reimplementations of `BotPack` classes | ADR 0006 (proposed: VM) |
| Native-only behaviour (`Engine.dll`, `Render.dll`) | physics, collision, BSP rendering details have no script to read | observe the original ([ADR 0004](../architecture/adr/0004-clean-room-sources.md)) |
| Replication must match the original byte for byte | network phase E may be infeasible | [ADR 0005](../architecture/adr/0005-network-compatibility.md) has a fallback |

## Deferred decisions

- Run UnrealScript bytecode in a VM vs. reimplement the game classes natively: bytecode mapped; [ADR 0006](../architecture/adr/0006-unrealscript-execution.md) proposes a VM, waiting for the maintainer.
- Whether disassembly of native binaries is ever allowed (now forbidden by ADR 0004).
- README translations.

## Known gaps

- Engine draws mesh actors full bright, frame 0, no `PrePivot` or environment mapping; DXT / 469 compressed textures show a grey checker.

- Light colour model uncalibrated (falloff curve, no N·L, ×2 texture modulation); zero-brightness radius-255 lights leave areas black (DM-Deck16][ spawn). Needs same-spot screenshots of the original.

- Package and object flag bits not named ([package.md](../formats/package.md)).
- Only patch 469e checked; packages from version 436 installs not yet compared.
