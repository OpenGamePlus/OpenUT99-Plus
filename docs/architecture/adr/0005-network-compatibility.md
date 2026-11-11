# 0005 — Interoperate with the original game over the network

- Status: Accepted
- Date: 2026-11-10

## Context

The goal is to play with original UT clients and servers (versions 436 and 469), not only between copies of this engine. Unreal replication sends actor properties and function calls declared in UnrealScript `replication` blocks, so a compatible implementation must reproduce the original actor classes and their replicated state closely.

## Decision

- Network compatibility with original clients and servers is a long-term goal (roadmap phase E).
- The protocol is studied from traffic recorded on the maintainer's machine between original programs, for interoperability only; findings go to a network spec under `docs/formats/` with a decoder and tests on synthetic packets. No captured sessions, keys or binaries are committed ([ADR 0001](0001-no-game-data.md)).
- The game layer is organised around actors and classes as the packages define them, so replication can follow the original's object model.

## Consequences

- Package and class loading (roadmap phase A) is a prerequisite for networking, not just for rendering.
- If the protocol proves infeasible, the engine keeps its own protocol and this ADR is superseded.
