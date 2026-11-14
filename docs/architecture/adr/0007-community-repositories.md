# 0007 — Community repositories: which may be studied

- Status: Accepted
- Date: 2026-11-14

## Context

Community projects around UT99 range from format documentation to exported game source, Epic's public C++ source and recompiled binaries. [ADR 0004](0004-clean-room-sources.md) allows public documentation but says nothing about each kind of repository.

## Decision

Allowed (read and cite; copy no code from repositories without a compatible license):

- UT Bible (`web.archive.org/web/20010603192939/http://www.ocrana.de/utbible/`): UnrealScript and engine documentation, for the VM ([ADR 0006](0006-unrealscript-execution.md)).
- `github.com/monteslu/ut99-mapgen` (MIT): builds `.unr` maps headless with the 469 `ucc exec`; a way to make small test maps of our own. Maps it builds stay out of the repo ([ADR 0001](0001-no-game-data.md)).
- `github.com/Sektorka/UTCompress` (no license: read only): the `.uz` compressed package format, for downloads from servers ([ADR 0005](0005-network-compatibility.md)).

Not used:

- `github.com/sp00nznet/ut99-recomp`: a static recompilation of the native binaries, so derived from their disassembly; forbidden by ADR 0004. Do not open it.
- `github.com/FaultyRAM/Ut99PubSrc` (Epic's v432 public C++ source) and repositories whose specs rely on it: not for now; reading it would need a later ADR.
- Exported UnrealScript (`Slipyx/UT99`, `UT-BT/UT99`): redistributed game data; we read the same script from the user's packages.

## Consequences

- A spec that uses an allowed repository cites it as public documentation and still verifies each claim against retail data.
- Other community engines (`daedalus/Ut99py`, `zerth444/ut99-rust`) are not sources: unlicensed and unverified.
