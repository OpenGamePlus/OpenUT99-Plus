# SYSTEM_SUMMARY

Last updated: 2026-11-10. Phase: A (package loading), container only.

## Tree

```
AGENTS.md            agent entry point
LICENSE              AGPL-3.0
README.md
docs/                specs, ADRs, roadmap, AI layer (index: docs/README.md)
tools/
  upkg.py            Unreal package reader (header, names, imports, exports)
  check_docs.py      documentation checker
  test_*.py          unit tests on synthetic data
```

## State per subsystem

| Subsystem | State |
|---|---|
| Package container | done, verified on all 396 retail packages |
| Object data (properties, textures, sounds, levels, meshes, classes) | not started |
| Engine (C++) | not started |
| Network | not started |

## Commands

```
python3 -m unittest discover -s tools          # all tests
python3 tools/check_docs.py                    # doc checks
python3 tools/upkg.py FILE [--names|--imports|--exports]
```

## Contracts

- `upkg.parse(bytes) -> dict`: `version`, `licensee`, `flags`, `guid` / `generations` (version ≥ 68), `names` (list of str), `imports` (`class_package`, `class`, `package`, `name`), `exports` (`class`, `super`, `package`, `name`, `flags`, `size`, `offset`). Raises `ValueError` on a bad tag.
- `upkg.object_name(pkg, ref)`: name of an object reference (0, export, import).
