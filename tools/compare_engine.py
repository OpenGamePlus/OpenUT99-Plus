#!/usr/bin/env python3
"""Compare the C++ package reader with the Python reference on every package of an install.

Usage: compare_engine.py DIR UT_DUMP    (exit 1 on any difference)
"""
import glob
import os
import subprocess
import sys

from upkg import parse
from uprops import EXTENSIONS, read_properties


def python_lines(data):
    pkg = parse(data)
    summary = (f'version {pkg["version"]} licensee {pkg["licensee"]} flags {pkg["flags"]:#x} '
               f'names {len(pkg["names"])} imports {len(pkg["imports"])} exports {len(pkg["exports"])}')
    props = []
    for i, e in enumerate(pkg["exports"]):
        if e["size"] and e["class"]:
            p, end = read_properties(pkg, data, e)
            props.append(f"{i} {len(p)} {end}")
    return summary, props


def main(root, exe):
    files = differ = 0
    for ext in EXTENSIONS:
        for path in sorted(glob.glob(os.path.join(root, "*", "*." + ext))):
            files += 1
            with open(path, "rb") as f:
                summary, props = python_lines(f.read())
            cpp_summary = subprocess.run([exe, path], capture_output=True, text=True).stdout.strip()
            cpp_props = subprocess.run([exe, path, "--props"], capture_output=True, text=True).stdout.split("\n")[:-1]
            if cpp_summary != summary or cpp_props != props:
                differ += 1
                print(f"DIFF {os.path.basename(path)}")
    print(f"{files} packages, {differ} differ")
    return differ


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(1 if main(sys.argv[1], sys.argv[2]) else 0)
