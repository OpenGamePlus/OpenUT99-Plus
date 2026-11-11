# 0003 — Engine stack: C++17, CMake, SDL2, OpenGL 3.3

- Status: Accepted
- Date: 2026-11-10

## Context

The engine must run on modern desktop systems and be buildable by contributors without proprietary SDKs. OpenBF1942-Plus uses the same stack, so knowledge and code patterns carry over.

## Decision

C++17, CMake, SDL2 (window, input, audio device), OpenGL 3.3 core. Unreal Engine 1 collision is BSP and cylinder based, so no general physics library is planned.

## Consequences

- The engine does not exist yet; this ADR fixes the stack before the first line.
- A physics library, if ever needed, requires a new ADR.
