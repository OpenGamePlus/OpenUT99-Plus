#!/usr/bin/env python3
"""UT .ini / .int files and package lookup through [Core.System] Paths and the download cache.

Usage: uini.py --deps DIR     resolve every top-level package imported by every package under DIR
Spec: docs/formats/ini.md
"""
import glob
import os
import sys


def parse(text):
    """{section: {key: [values in file order]}}; ';' lines are comments."""
    ini, section = {}, None
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = ini.setdefault(line[1:-1], {})
        elif section is not None and "=" in line:
            key, value = line.split("=", 1)
            section.setdefault(key, []).append(value)
    return ini


def read(path):
    with open(path, encoding="latin-1") as f:  # all retail .ini / .int files are single-byte text
        return parse(f.read())


def package_paths(root, ini):
    """{lower-case package name: file path}: Paths entries first, then the download cache."""
    system = os.path.join(root, "System")
    found = {}
    for pattern in ini.get("Core.System", {}).get("Paths", []):
        folder, mask = os.path.split(os.path.normpath(os.path.join(system, pattern)))
        ext = os.path.splitext(mask)[1].lower()
        for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
            base, e = os.path.splitext(name)
            if e.lower() == ext:
                found.setdefault(base.lower(), os.path.join(folder, name))
    core = ini.get("Core.System", {})
    cache = os.path.normpath(os.path.join(system, core.get("CachePath", ["../Cache"])[0]))
    ext = core.get("CacheExt", [".uxx"])[0]
    index = os.path.join(cache, "cache.ini")
    if os.path.exists(index):
        for guid, (filename,) in read(index).get("Cache", {}).items():
            path = os.path.join(cache, guid + ext)
            if os.path.exists(path):
                found.setdefault(os.path.splitext(filename)[0].lower(), path)
    return found


def missing_deps(root):
    """(packages checked, {missing package name: count of importers})."""
    from upkg import parse as parse_package
    paths = package_paths(root, read(os.path.join(root, "System", "UnrealTournament.ini")))
    missing, checked = {}, 0
    for path in sorted(set(paths.values())):
        checked += 1
        with open(path, "rb") as f:
            pkg = parse_package(f.read())
        for imp in pkg["imports"]:
            if imp["class"] == "Package" and imp["package"] == 0 and imp["name"].lower() not in paths:
                missing[imp["name"]] = missing.get(imp["name"], 0) + 1
    return checked, missing


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--deps":
        sys.exit(__doc__)
    n, missing = missing_deps(sys.argv[2])
    print(f"{n} packages, {len(missing)} missing dependencies")
    for name, count in sorted(missing.items()):
        print(f"  {name} (imported by {count})")
