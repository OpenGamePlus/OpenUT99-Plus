# 0008 — Measure the original with our own probe scripts

- Status: Accepted
- Date: 2026-11-14

## Context

Physics, collision and event order are native code ([ADR 0004](0004-clean-room-sources.md) forbids reading it), so many engine rules are marked unverified. The OldUnreal Linux patch runs a dedicated server headless (`ucc server`), and ut99-mapgen ([ADR 0007](0007-community-repositories.md)) builds maps with the retail `ucc`. Watching the original by playing is slow and imprecise.

## Decision

- We write small UnrealScript probe classes of our own (`tools/probe/OUProbe/`): they spawn test actors, drive them and log what the original engine does (location, velocity, physics, events). They are compiled with the retail `ucc make` and run on calibration maps we build with ut99-mapgen. This is observed behaviour (ADR 0004, source 2).
- Probe sources are ours and live in the repo; nothing in them is copied from the game's scripts. Compiled probe packages, calibration maps and server logs stay outside the repo ([ADR 0001](0001-no-game-data.md)).
- The runner (`tools/probe/calibrate.py`) is Python standard library; it imports ut99-mapgen from its own checkout when asked to build a map. Neither is needed to build or test the engine ([ADR 0002](0002-python-tools-cpp-engine.md) still holds for the project's tools).

## Consequences

- A rule measured this way cites the probe and map in its spec and loses its "unverified" mark.
- The same probe package can later run in our VM, so both engines answer the same question.
