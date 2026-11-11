# ARCHITECTURE_NOTES

Normative summary. Details: [boundaries.md](../architecture/boundaries.md), [ADRs](../architecture/adr/README.md).

## Must

- Read all content from `<UT99_DIR>` at runtime ([0001](../architecture/adr/0001-no-game-data.md)).
- Write a Python reader with a synthetic test and a spec before porting a format to C++ ([0002](../architecture/adr/0002-python-tools-cpp-engine.md)).
- Use C++17, CMake, SDL2, OpenGL 3.3 for the engine ([0003](../architecture/adr/0003-engine-stack.md)).
- State how each format claim was verified ([0004](../architecture/adr/0004-clean-room-sources.md)).
- Model the game layer on the packages' actors and classes, so replication can match the original ([0005](../architecture/adr/0005-network-compatibility.md)).

## Must not

- Commit packages, extracted assets, UnrealScript taken from packages, recordings or binaries.
- Disassemble or decompile native binaries.
- Add pip dependencies to `tools/`.

## Irreversible

- No game data in git history: a slip needs a history rewrite, not a revert.
