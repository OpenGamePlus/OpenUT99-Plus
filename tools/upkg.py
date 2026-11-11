#!/usr/bin/env python3
"""Unreal package (.u .unr .utx .uax .umx .uxx) reader: header, names, imports, exports.

Usage: upkg.py FILE [--names|--imports|--exports]   (summary without a flag)
Spec: docs/formats/package.md
"""
import struct
import sys

TAG = 0x9E2A83C1


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def u32(self):
        (v,) = struct.unpack_from("<I", self.data, self.pos)
        self.pos += 4
        return v

    def i32(self):
        (v,) = struct.unpack_from("<i", self.data, self.pos)
        self.pos += 4
        return v

    def index(self):
        """Compact index: byte 0 = sign(7) more(6) value(5..0); next bytes = more(7) value(6..0)."""
        first = b = self.data[self.pos]
        self.pos += 1
        v, shift, more = b & 0x3F, 6, b & 0x40
        while more:
            if shift > 27:
                raise ValueError("compact index longer than 5 bytes")
            b = self.data[self.pos]
            self.pos += 1
            v |= (b & 0x7F) << shift
            shift, more = shift + 7, b & 0x80
        return -v if first & 0x80 else v

    def string(self):
        """Length-prefixed string (count includes the NUL); a negative count means UTF-16 characters."""
        n = self.index()
        if n < 0:
            s = self.data[self.pos:self.pos - 2 * n]
            self.pos -= 2 * n
            return s.decode("utf-16-le").rstrip("\0")
        s = self.data[self.pos:self.pos + n]
        self.pos += n
        return s.rstrip(b"\0").decode("latin-1")


def lazy_bytes(r, version, item_size=1):
    """Lazy array: [u32 file position past the data, version >= 62], compact item count, items (raw bytes)."""
    skip = r.u32() if version >= 62 else None
    n = r.index() * item_size
    out = r.data[r.pos:r.pos + n]
    r.pos += n
    if skip is not None and skip != r.pos:
        raise ValueError(f"lazy array skip offset {skip} != {r.pos}")
    return out


def parse(data):
    r = Reader(data)
    if r.u32() != TAG:
        raise ValueError("not an Unreal package")
    ver, lic = struct.unpack_from("<HH", data, 4)
    r.pos = 8
    pkg = {"version": ver, "licensee": lic, "flags": r.u32()}
    nc, no, ec, eo, ic, io = (r.u32() for _ in range(6))
    if ver >= 68:
        pkg["guid"] = data[r.pos:r.pos + 16].hex()
        r.pos += 16
        pkg["generations"] = [(r.u32(), r.u32()) for _ in range(r.u32())]  # (exports, names)
    else:
        r.pos += 8  # heritage count, heritage offset
    r.pos = no
    names = []
    for _ in range(nc):
        names.append(r.string() if ver >= 64 else _cstring(r))
        r.u32()  # object flags of the name
    r.pos = io
    imports = [{"class_package": names[r.index()], "class": names[r.index()],
                "package": r.i32(), "name": names[r.index()]} for _ in range(ic)]
    r.pos = eo
    exports = []
    for _ in range(ec):
        e = {"class": r.index(), "super": r.index(), "package": r.i32(),
             "name": names[r.index()], "flags": r.u32(), "size": r.index()}
        e["offset"] = r.index() if e["size"] > 0 else 0
        exports.append(e)
    pkg.update(names=names, imports=imports, exports=exports)
    return pkg


def _cstring(r):
    end = r.data.index(b"\0", r.pos)
    s, r.pos = r.data[r.pos:end].decode("latin-1"), end + 1
    return s


def object_name(pkg, ref):
    """Object reference: 0 none, >0 export ref-1, <0 import -ref-1."""
    if ref == 0:
        return "None"
    return pkg["exports"][ref - 1]["name"] if ref > 0 else pkg["imports"][-ref - 1]["name"]


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    with open(argv[1], "rb") as f:
        pkg = parse(f.read())
    flag = argv[2] if len(argv) > 2 else ""
    if flag == "--names":
        print("\n".join(pkg["names"]))
    elif flag == "--imports":
        for i in pkg["imports"]:
            print(f'{i["class_package"]}.{i["class"]} {i["name"]}')
    elif flag == "--exports":
        for e in pkg["exports"]:
            print(f'{object_name(pkg, e["class"]) if e["class"] else "Class"} {e["name"]} size={e["size"]}')
    else:
        print(f'version {pkg["version"]} licensee {pkg["licensee"]} flags {pkg["flags"]:#x} '
              f'names {len(pkg["names"])} imports {len(pkg["imports"])} exports {len(pkg["exports"])}')


if __name__ == "__main__":
    main(sys.argv)
