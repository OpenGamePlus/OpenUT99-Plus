#!/usr/bin/env python3
"""Object dispatcher: tagged properties, then the class-specific data of native classes.

Usage: uobjects.py FILE OBJECT     decode one export
       uobjects.py --sweep DIR     classify every export of every package under DIR
Readers live in their own modules and register by native class name.
"""
import collections
import glob
import os
import struct
import sys

from upkg import object_name, parse
from uprops import EXTENSIONS, read_properties

READERS = {}  # class name -> reader(pkg, data, export, pos) -> (value, end)
OUTCOMES = ("exact", "leftover", "error", "noreader")


def register(*class_names):
    def wrap(fn):
        for c in class_names:
            READERS[c] = fn
        return fn
    return wrap


def class_name(pkg, export):
    return object_name(pkg, export["class"]) if export["class"] else "Class"


def read_object(pkg, data, export):
    """Return {"props", "value", "end"}; value is None when the class has no reader."""
    cls = class_name(pkg, export)
    props, end = ([], export["offset"]) if cls == "Class" else read_properties(pkg, data, export)
    value = None
    limit = export["offset"] + export["size"]
    if end < limit and cls in READERS:
        value, end = READERS[cls](pkg, data, export, end)
    return {"props": props, "value": value, "end": end}


def outcome(pkg, data, export):
    try:
        end = read_object(pkg, data, export)["end"]
    except (ValueError, IndexError, KeyError, struct.error):
        return "error"
    limit = export["offset"] + export["size"]
    if end == limit:
        return "exact"
    if class_name(pkg, export) not in READERS:
        return "noreader"
    return "leftover" if end < limit else "error"


def _load_readers():
    """Import every reader module next to this file so its @register calls run."""
    here = os.path.dirname(os.path.abspath(__file__))
    for path in sorted(glob.glob(os.path.join(here, "u*.py"))):
        mod = os.path.basename(path)[:-3]
        if mod not in ("upkg", "uprops", "uobjects"):
            __import__(mod)


def sweep(root):
    """Per class name, a Counter of outcomes over all non-empty exports."""
    _load_readers()
    result = collections.defaultdict(collections.Counter)
    for ext in EXTENSIONS:
        for path in glob.glob(os.path.join(root, "*", "*." + ext)):
            with open(path, "rb") as f:
                data = f.read()
            pkg = parse(data)
            for e in pkg["exports"]:
                if e["size"]:
                    result[class_name(pkg, e)][outcome(pkg, data, e)] += 1
    return result


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    if argv[1] == "--sweep":
        result = sweep(argv[2])
        total = collections.Counter()
        for c in result.values():
            total.update(c)
        print("total " + " ".join(f"{o}={total[o]}" for o in OUTCOMES))
        for cls, c in sorted(result.items(), key=lambda kv: -sum(kv[1].values())):
            if c["exact"] != sum(c.values()):
                print(f"{cls}: " + " ".join(f"{o}={c[o]}" for o in OUTCOMES if c[o]))
        return
    _load_readers()
    with open(argv[1], "rb") as f:
        data = f.read()
    pkg = parse(data)
    for e in pkg["exports"]:
        if e["name"] == argv[2] and e["size"]:
            obj = read_object(pkg, data, e)
            print(f'{class_name(pkg, e)} {e["name"]}: {len(obj["props"])} properties, '
                  f'{e["offset"] + e["size"] - obj["end"]} bytes left')
            print(obj["value"])
            return
    sys.exit(f"no export named {argv[2]}")


if __name__ == "__main__":
    main(sys.argv)
