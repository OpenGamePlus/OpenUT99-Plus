# 0001 — Read assets from the user's install, never redistribute

- Status: Accepted
- Date: 2026-11-10

## Context

Unreal Tournament (1999) content — maps, textures, sounds, music, meshes and the UnrealScript game code in `.u` packages — is copyrighted by Epic Games. The engine runtime can be reimplemented; the content cannot be shipped.

## Decision

The project ships only original code and documentation. All game data is read at runtime from a retail install (`<UT99_DIR>`), following the OpenMW / OpenRA model.

## Consequences

- No packages, extracted files, decompiled or exported UnrealScript, or executables in the repo; `out/` is git-ignored.
- Tests use synthetic data; checks against retail data run locally only.
- Format docs describe structure, not content; at most a short excerpt where a field needs an example.
