# 0004 — Clean-room: what may be studied and how

- Status: Accepted
- Date: 2026-11-10

## Context

Unlike most games, UT `.u` packages carry the UnrealScript source text and bytecode of the game logic. The native engine (`Core.dll`, `Engine.dll`, `Render.dll`, ...) is closed. Public community documentation of the package format and of UnrealScript exists.

## Decision

Allowed sources, in order of preference:

1. Retail data in `<UT99_DIR>`, read with our own tools: structure, values, and the game's UnrealScript (read to understand behaviour; never copied into the repo).
2. Behaviour of the original game observed on the maintainer's machine (screenshots, logs, network traffic between original programs).
3. Public documentation of formats and protocols; cite it in the spec.

Disassembly or decompilation of the native binaries is not allowed until a later ADR decides otherwise.

## Consequences

- Engine code is written from specs, never translated line by line from UnrealScript.
- Every spec says how each claim was verified (file counts, observed behaviour) or marks it unverified.
