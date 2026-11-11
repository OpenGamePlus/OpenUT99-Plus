#!/usr/bin/env python3
"""Script objects: Class, State, Function, Struct, Enum, Const, TextBuffer, property declarations, bytecode.

Usage: uscript.py --tokens DIR    histogram of bytecode tokens over every package under DIR
Spec: docs/formats/script.md
"""
import collections
import glob
import os
import struct
import sys

from upkg import Reader, parse
from uobjects import class_name, register
from uprops import EXTENSIONS, read_properties

FUNC_NET = 0x40
CPF_NET = 0x20
# Property class -> number of object references after the common declaration.
PROPERTY_REFS = {"ByteProperty": 1, "ObjectProperty": 1, "ClassProperty": 2, "StructProperty": 1, "ArrayProperty": 1,
                 "MapProperty": 2, "FixedArrayProperty": 1, "IntProperty": 0, "BoolProperty": 0, "FloatProperty": 0,
                 "NameProperty": 0, "StrProperty": 0, "PointerProperty": 0, "StringProperty": 0}


class _Script:
    """Walks bytecode on disk while counting its in-memory size (object and name references are 4 bytes)."""

    def __init__(self, pkg, data, pos):
        self.pkg, self.r, self.mem = pkg, Reader(data, pos), 0
        self.tokens = collections.Counter()

    def byte(self):
        v = self.r.data[self.r.pos]
        self.r.pos += 1
        self.mem += 1
        return v

    def raw(self, n):
        self.r.pos += n
        self.mem += n

    def u16(self):
        (v,) = struct.unpack_from("<H", self.r.data, self.r.pos)
        self.raw(2)
        return v

    def ref(self):  # object reference or name: compact on disk, 4 bytes in memory
        v = self.r.index()
        self.mem += 4
        return v

    def params(self):
        while self.expr() != 0x16:
            pass

    def expr(self):
        t = self.byte()
        self.tokens[t] += 1
        if t in (0x00, 0x01, 0x02, 0x20, 0x21, 0x29):      # variables, object/name const, native parm
            self.ref()
        elif t in (0x04, 0x0D, 0x0E, 0x2D) or 0x39 <= t < 0x60:  # return, goto label, eat string, bool var, casts
            self.expr()
        elif t == 0x05:                                     # switch: property size, expression
            self.raw(1)
            self.expr()
        elif t == 0x06:                                     # jump
            self.u16()
        elif t in (0x07, 0x09, 0x18):                       # jump-if-not, assert, skip
            self.u16()
            self.expr()
        elif t == 0x0A:                                     # case (0xFFFF = default)
            if self.u16() != 0xFFFF:
                self.expr()
        elif t == 0x0C:                                     # label table, ends with None
            while True:
                name = self.ref()
                self.raw(4)
                if self.pkg["names"][name] == "None":
                    break
        elif t in (0x0F, 0x10, 0x14, 0x1A):                 # let, dyn array element, let bool, array element
            self.expr()
            self.expr()
        elif t == 0x11:                                     # new
            for _ in range(4):
                self.expr()
        elif t in (0x12, 0x19):                             # class context, context
            self.expr()
            self.u16()
            self.raw(1)
            self.expr()
        elif t in (0x13, 0x2E, 0x36):                       # meta cast, dynamic cast, struct member
            self.ref()
            self.expr()
        elif t in (0x1B, 0x38, 0x1C):                       # virtual (name), global (name), final (object) call
            self.ref()
            self.params()
        elif t in (0x1D, 0x1E):                             # int, float const
            self.raw(4)
        elif t == 0x1F:                                     # ANSI string const
            while self.byte():
                pass
        elif t == 0x34:                                     # unicode string const
            while self.u16():
                pass
        elif t in (0x22, 0x23):                             # rotator, vector const
            self.raw(12)
        elif t in (0x24, 0x2C):                             # byte const, int const byte
            self.raw(1)
        elif t == 0x2F:                                     # iterator
            self.expr()
            self.u16()
        elif t in (0x32, 0x33):                             # struct compare
            self.ref()
            self.expr()
            self.expr()
        elif 0x60 <= t < 0x70:                              # extended native: index high bits in token
            self.raw(1)
            self.params()
        elif t >= 0x70:                                     # native function call
            self.params()
        elif t not in (0x08, 0x0B, 0x16, 0x17, 0x25, 0x26, 0x27, 0x28, 0x2A, 0x30, 0x31):
            raise ValueError(f"unknown bytecode token {t:#04x}")
        return t


def decode_script(pkg, data, pos, mem_size):
    """Return (end, token counts) for a script whose in-memory size is mem_size."""
    s = _Script(pkg, data, pos)
    try:
        while s.mem < mem_size:
            s.expr()
    except IndexError:
        raise ValueError("script runs past the data")
    if s.mem != mem_size:
        raise ValueError(f"script decodes to {s.mem} bytes, header says {mem_size}")
    return s.r.pos, s.tokens


def _field(r):
    return {"super": r.index(), "next": r.index()}


def _struct(pkg, data, pos):
    r = Reader(data, pos)
    s = _field(r)
    s.update(script_text=r.index(), children=r.index(), friendly_name=pkg["names"][r.index()],
             line=r.i32(), text_pos=r.i32(), script_size=r.u32())
    end, s["tokens"] = decode_script(pkg, data, r.pos, s["script_size"])
    r.pos = end
    return s, r


@register("Struct")
def read_struct(pkg, data, export, pos, props):
    s, r = _struct(pkg, data, pos)
    return s, r.pos


@register("Function")
def read_function(pkg, data, export, pos, props):
    if pkg["version"] < 64:  # shortcut: older layout has ParmsSize/NumParms/ReturnValueOffset; no retail function uses it
        raise ValueError("function layout before version 64 not implemented")
    s, r = _struct(pkg, data, pos)
    (s["native"],) = struct.unpack_from("<H", data, r.pos)
    s["operator_precedence"] = data[r.pos + 2]
    r.pos += 3
    s["flags"] = r.u32()
    if s["flags"] & FUNC_NET:
        (s["rep_offset"],) = struct.unpack_from("<H", data, r.pos)
        r.pos += 2
    return s, r.pos


def _state(pkg, data, pos):
    s, r = _struct(pkg, data, pos)
    s["probe_mask"], s["ignore_mask"], s["label_table_offset"], s["state_flags"] = struct.unpack_from("<QQHI", data, r.pos)
    r.pos += 22
    return s, r


@register("State")
def read_state(pkg, data, export, pos, props):
    s, r = _state(pkg, data, pos)
    return s, r.pos


@register("Class")
def read_class(pkg, data, export, pos, props):
    """Class objects have no leading tagged properties: struct, state, class fields, then default properties."""
    s, r = _state(pkg, data, pos)
    s["class_flags"] = r.u32()
    s["guid"] = data[r.pos:r.pos + 16].hex()
    r.pos += 16
    s["dependencies"] = [(r.index(), r.u32(), r.u32()) for _ in range(r.index())]  # class, deep, script text CRC
    s["package_imports"] = [r.index() for _ in range(r.index())]
    s["within"] = r.index()
    s["config_name"] = pkg["names"][r.index()]
    limit = export["offset"] + export["size"]
    s["defaults"], end = read_properties(pkg, data, {"offset": r.pos, "size": limit - r.pos, "flags": 0})
    return s, end


def read_property(pkg, data, export, pos, props, refs):
    r = Reader(data, pos)
    d = _field(r)
    d["array_dim"], d["element_size"] = struct.unpack_from("<HH", data, r.pos)
    r.pos += 4
    d["flags"] = r.u32()
    d["category"] = pkg["names"][r.index()]
    if d["flags"] & CPF_NET:
        (d["rep_offset"],) = struct.unpack_from("<H", data, r.pos)
        r.pos += 2
    d["refs"] = [r.index() for _ in range(refs)]  # enum / class / struct / inner, by property type
    return d, r.pos


def _register_properties():
    for cls, refs in PROPERTY_REFS.items():
        def reader(pkg, data, export, pos, props, refs=refs, cls=cls):
            d, end = read_property(pkg, data, export, pos, props, refs)
            if cls in ("FixedArrayProperty", "StringProperty"):  # count / fixed length (unverified: none in retail)
                d["count"] = struct.unpack_from("<I", data, end)[0]
                end += 4
            return d, end
        register(cls)(reader)


_register_properties()


@register("Const")
def read_const(pkg, data, export, pos, props):
    r = Reader(data, pos)
    c = _field(r)
    c["value"] = r.string()
    return c, r.pos


@register("Enum")
def read_enum(pkg, data, export, pos, props):
    r = Reader(data, pos)
    e = _field(r)
    e["names"] = [pkg["names"][r.index()] for _ in range(r.index())]
    return e, r.pos


@register("TextBuffer")
def read_text_buffer(pkg, data, export, pos, props):
    r = Reader(data, pos)
    t = {"pos": r.u32(), "top": r.u32()}
    t["text"] = r.string()
    return t, r.pos


def token_histogram(root):
    total = collections.Counter()
    for ext in EXTENSIONS:
        for path in glob.glob(os.path.join(root, "*", "*." + ext)):
            with open(path, "rb") as f:
                data = f.read()
            pkg = parse(data)
            for e in pkg["exports"]:
                cls = class_name(pkg, e)
                if not e["size"] or cls not in ("Class", "State", "Function", "Struct"):
                    continue
                pos = e["offset"] if cls == "Class" else read_properties(pkg, data, e)[1]
                total.update(_struct(pkg, data, pos)[0]["tokens"])
    return total


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--tokens":
        sys.exit(__doc__)
    for t, n in sorted(token_histogram(sys.argv[2]).items()):
        print(f"{t:#04x} {n}")
