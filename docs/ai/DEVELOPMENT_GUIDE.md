# DEVELOPMENT_GUIDE

## Workflow for a new format

1. Survey: dump samples to `out/` (git-ignored) or a scratch dir, inventory structure.
2. Write the reader in `tools/u<format>.py` (registered with `uobjects.register`) with a synthetic-data test in `tools/test_<format>.py`.
3. Verify locally against every matching object in `<UT99_DIR>`; record the counts in the spec.
4. Write `docs/formats/<format>.md`; mark anything not verified; update [coverage.md](../formats/coverage.md).
5. Update SYSTEM_SUMMARY and add a PROJECT_MEMORY stage.
6. C++ port (when the engine needs it): module in `engine/`, tests in `engine/test_engine.cpp` on the same synthetic data as the Python test, a comparison against the Python reader on every retail package, row in boundaries.

## Quality bar

- Tests, engine build without warnings, engine tests and doc checks from the [SYSTEM_SUMMARY commands](SYSTEM_SUMMARY.md#commands) pass.
- An engine reader change: `compare_engine.py` still reports 0 differences, and the sanitizer build passes on all maps: `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" && cmake --build build-asan && build-asan/test_engine && build-asan/ut_mapview <UT99_DIR> --check` (slow: tens of minutes).
- Package data is untrusted: C++ readers validate every file-provided index or size before using it; Python readers raise `ValueError` (the `Reader`, `lazy_bytes` and `name` helpers in `upkg.py` do it for you).
- A reader change still parses every retail package: `python3 tools/uprops.py --sweep <UT99_DIR>` reports the same counts as the specs, "classes with both: none"; `python3 tools/uobjects.py --sweep <UT99_DIR>` shows no `leftover` or `error` for a class marked exact in coverage.

## Debugging script

- `UTVM_TRACE=Fire,TakeDamage build/ut_script <UT99_DIR> --map MAP 60` logs every call of those script functions with object and state; the `--map` report ranks errors and log lines and lists the pawns (state, weapon, enemy, score).
- "accessed None (Member)" warnings name the member reached through a None context: usually something the engine should have filled in.
- Watch it: `build/ut_mapview <UT99_DIR> MAP --play` (F follows a bot). Meters count; the view shows what they miss (bots lying down, stuck in corners, swimming forever in slime). `--play --time 20 --follow --shot out/x.png` saves the picture for a check without a window.

## Calibration maps (ut99-mapgen)

Our own small maps built with the retail `ucc`, to observe the original ([ADR 0007](../architecture/adr/0007-community-repositories.md)). Everything lives outside the repo; built maps are never committed.

1. Clone `github.com/monteslu/ut99-mapgen`; copy `<UT99_DIR>` to its `ut/` (it writes into `Maps/`).
2. Extract the OldUnreal Linux release that matches the install's packages (now v469f-rc5, see [install.md](../formats/install.md)) over `ut/`; `chmod +x ut/System/ucc-bin* ut/System/ut-bin*`. Do not fetch the SDK (engine headers, [ADR 0007](../architecture/adr/0007-community-repositories.md)).
3. In the copy's `System/UnrealTournament.ini`: `ViewportManager=SDLDrv.SDLClient` (the Windows ini names `WinDrv`, which the Linux `ucc` cannot load).
4. `python3 tools/selftest.py` in the mapgen clone: 13/13.
5. For the probe: `make-469e/` in the mapgen clone, a copy of the install's `System/` with the v469e Linux release extracted over it (binaries land in `System64/`). The 469f-rc5 Linux `ucc make` recurses forever into `Classes/./` (seen under gdb), so the probe is compiled there; the package compiled against 469e loads in the 469f-rc5 server. `calibrate.py` writes `System64/Probe.ini` (only Core, Engine, OUProbe) and the package lands in `~/.utpg/System/`.

## Commits

- Imperative subject, body says why. One topic per commit; code and the docs it affects together.

## Doc-sync

| When you change | Update |
|---|---|
| A format reader | its `docs/formats/*.md`, coverage, SYSTEM_SUMMARY contracts |
| Files or folders in the tree | SYSTEM_SUMMARY tree |
| A roadmap phase's state | SYSTEM_SUMMARY, README |
| A hard rule or stack choice | new ADR, ARCHITECTURE_NOTES, AGENTS.md |
| A module or its dependencies | [boundaries.md](../architecture/boundaries.md), SYSTEM_SUMMARY tree |
| A risk or open question | `docs/roadmap/debt.md` |

## Memory entry template

```
## YYYY-MM-DD — topic
- what changed and why it mattered
- what was rejected
- what is left open
```
