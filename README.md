# OpenUT99-Plus

Clean-room reimplementation of the Unreal Engine 1 runtime used by Unreal Tournament (1999), built to run the retail game content on modern systems.

Status: research. Every object of the retail install is decoded byte-exactly by the Python readers (phase A). The C++ engine draws every retail map with its textures, baked lightmaps, sky and static meshes (pickups, decorations); nothing moves yet (phase B). See [docs/README.md](docs/README.md).

You need your own copy of Unreal Tournament (tested with the Steam release, OldUnreal patch 469e). This project ships no game content.

## Quick start

```
python3 -m unittest discover -s tools      # tests
python3 tools/check_docs.py                # doc checks
cmake -S . -B build && cmake --build build && ctest --test-dir build
python3 tools/upkg.py <UT99_DIR>/Maps/DM-Deck16][.unr
```

License: [AGPL-3.0](LICENSE), as the rest of the -Plus family, so modified servers also share their source.

Unreal Tournament is a trademark of Epic Games. This project is not affiliated with or endorsed by Epic Games.
