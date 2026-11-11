# SYSTEM_SUMMARY

Last updated: 2026-11-10. Phase: A (package loading): container and tagged properties done.

## Tree

```
AGENTS.md            agent entry point
LICENSE              AGPL-3.0
README.md
docs/                specs, ADRs, roadmap, AI layer (index: docs/README.md)
tools/
  upkg.py            Unreal package reader (header, names, imports, exports)
  uprops.py          tagged properties (generic object data)
  check_docs.py      documentation checker
  test_*.py          unit tests on synthetic data
```

## State per subsystem

| Subsystem | State |
|---|---|
| Package container | done, verified on all 396 retail packages |
| Tagged properties | done, verified on all 273,585 non-class exports |
| Class-specific data (textures, sounds, levels, meshes, classes) | not started |
| Engine (C++) | not started |
| Network | not started |

## Commands

```
python3 -m unittest discover -s tools          # all tests
python3 tools/check_docs.py                    # doc checks
python3 tools/upkg.py FILE [--names|--imports|--exports]
python3 tools/uprops.py FILE OBJECT              # properties of one export
python3 tools/uprops.py --sweep <UT99_DIR>       # parse every export, counts per class
```

## Contracts

- `upkg.parse(bytes) -> dict`: `version`, `licensee`, `flags`, `guid` / `generations` (version ≥ 68), `names` (list of str), `imports` (`class_package`, `class`, `package`, `name`), `exports` (`class`, `super`, `package`, `name`, `flags`, `size`, `offset`). Raises `ValueError` on a bad tag.
- `upkg.object_name(pkg, ref)`: name of an object reference (0, export, import).
- `uprops.read_properties(pkg, data, export) -> (props, end)`: `props` is a list of `name`, `type`, `index` (array index), `value`, `struct` (struct tags only); `end` is the offset after the `None` tag, where class data starts. Raises `ValueError` if a tag runs past the export.
