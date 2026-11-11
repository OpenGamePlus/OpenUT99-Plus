#!/usr/bin/env python3
"""Tagged properties: the generic start of every exported object's data.

Usage: uprops.py FILE OBJECT     properties of one export
       uprops.py --sweep DIR     parse every export of every package under DIR, print counts
Spec: docs/formats/properties.md
"""
import collections
import glob
import os
import struct
import sys

from upkg import Reader, object_name, parse
from upkg import name as name_of

HAS_STACK = 0x02000000  # object flag: a state frame precedes the properties
TYPES = {1: "Byte", 2: "Int", 3: "Bool", 4: "Float", 5: "Object", 6: "Name", 7: "String", 8: "Class",
         9: "Array", 10: "Struct", 11: "Vector", 12: "Rotator", 13: "Str", 14: "Map", 15: "FixedArray"}
SIZES = {0: 1, 1: 2, 2: 4, 3: 12, 4: 16}
EXTENSIONS = ("u", "unr", "utx", "uax", "umx", "uxx")


def _struct(name, r, end):
    if name in ("Vector", "Rotator"):
        return struct.unpack_from("<3f" if name == "Vector" else "<3i", r.data, r.pos)
    if name == "Color":
        return tuple(r.data[r.pos:r.pos + 4])  # R, G, B, A
    if name == "Scale":
        x, y, z, sheer_rate = struct.unpack_from("<4f", r.data, r.pos)
        return {"Scale": (x, y, z), "SheerRate": sheer_rate, "SheerAxis": r.data[r.pos + 16]}
    if name == "PointRegion":
        return {"Zone": r.index(), "iLeaf": r.i32(), "ZoneNumber": r.data[r.pos]}
    if name == "Coords":
        v = struct.unpack_from("<12f", r.data, r.pos)
        return {"Origin": v[0:3], "XAxis": v[3:6], "YAxis": v[6:9], "ZAxis": v[9:12]}
    return r.data[r.pos:end]  # shortcut: other structs stay raw bytes, decode when a reader needs them


def _value(t, r, size, struct_name, info):
    end = r.pos + size
    if t == 1:
        return r.data[r.pos]
    if t == 2:
        return r.i32()
    if t == 3:
        return bool(info & 0x80)
    if t == 4:
        return struct.unpack_from("<f", r.data, r.pos)[0]
    if t in (5, 6, 8):
        return r.index()  # object reference / name index
    if t == 13:
        return r.string()
    if t == 10:
        return _struct(struct_name, r, end)
    return r.data[r.pos:end]


def read_properties(pkg, data, export):
    """Return (properties, end): a list of dicts and the offset just past the closing None tag."""
    r = Reader(data, export["offset"])
    limit = export["offset"] + export["size"]
    if export["flags"] & HAS_STACK:
        node = r.index()
        r.index()  # state node
        r.pos += 8 + 4  # probe mask u64, latent action u32
        if node:
            r.index()  # bytecode offset
    props = []
    while True:
        name = name_of(pkg, r.index())
        if name == "None":
            return props, r.pos
        info = data[r.pos]
        r.pos += 1
        t, code = info & 0x0F, (info >> 4) & 0x07
        struct_name = name_of(pkg, r.index()) if t == 10 else None
        if code < 5:
            size = SIZES[code]
        else:
            fmt = {5: "<B", 6: "<H", 7: "<I"}[code]
            (size,) = struct.unpack_from(fmt, data, r.pos)
            r.pos += struct.calcsize(fmt)
        index = 0
        if info & 0x80 and t != 3:  # bool keeps its value in bit 7, so it has no array index
            b = data[r.pos]
            r.pos += 1
            if b & 0xC0 == 0xC0:
                index = ((b & 0x3F) << 24) | (data[r.pos] << 16) | (data[r.pos + 1] << 8) | data[r.pos + 2]
                r.pos += 3
            elif b & 0x80:
                index = ((b & 0x7F) << 8) | data[r.pos]
                r.pos += 1
            else:
                index = b
        if t == 3:
            size = 0
        start = r.pos
        if start + size > limit:
            raise ValueError(f"property {name} runs past the export")
        value = _value(t, Reader(data, start), size, struct_name, info)
        r.pos = start + size
        props.append({"name": name, "type": TYPES.get(t, t), "index": index, "value": value,
                      **({"struct": struct_name} if struct_name else {})})


def sweep(root):
    """Parse every non-class export; count per class whether data ends at the None tag."""
    exact, tail, files = collections.Counter(), collections.Counter(), 0
    for ext in EXTENSIONS:
        for path in glob.glob(os.path.join(root, "*", "*." + ext)):
            files += 1
            with open(path, "rb") as f:
                data = f.read()
            pkg = parse(data)
            for e in pkg["exports"]:
                if e["size"] == 0 or e["class"] == 0:  # class objects: struct data comes first
                    continue
                _, end = read_properties(pkg, data, e)
                (exact if end == e["offset"] + e["size"] else tail)[object_name(pkg, e["class"])] += 1
    return files, exact, tail


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    if argv[1] == "--sweep":
        files, exact, tail = sweep(argv[2])
        print(f"{files} packages, {sum(exact.values()) + sum(tail.values())} exports parsed")
        print(f"end at None: {sum(exact.values())} in {len(exact)} classes; "
              f"class data follows: {sum(tail.values())} in {len(tail)} classes")
        both = sorted(set(exact) & set(tail))
        print("classes with both:", ", ".join(both) or "none")
        print("with class data:", ", ".join(c for c, _ in tail.most_common()))
        return
    with open(argv[1], "rb") as f:
        data = f.read()
    pkg = parse(data)
    for e in pkg["exports"]:
        if e["name"] == argv[2] and e["size"]:
            props, end = read_properties(pkg, data, e)
            for p in props:
                v = p["value"]
                if p["type"] == "Name":
                    v = pkg["names"][v]
                elif p["type"] in ("Object", "Class"):
                    v = object_name(pkg, v)
                print(f'{p["name"]}[{p["index"]}] {p.get("struct", p["type"])} = {v}')
            print(f'-- {e["offset"] + e["size"] - end} bytes of class data follow')
            return
    sys.exit(f"no export named {argv[2]}")


if __name__ == "__main__":
    main(sys.argv)
