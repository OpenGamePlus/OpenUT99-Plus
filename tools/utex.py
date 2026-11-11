#!/usr/bin/env python3
"""Palette and Texture class data: palette colours and mip maps.

Usage: utex.py FILE TEXTURE --png OUT.png    write mip 0 as PNG (P8 textures)
Spec: docs/formats/texture.md
"""
import struct
import sys
import zlib

from upkg import Reader, parse
from uobjects import read_object, register


@register("Palette")
def read_palette(pkg, data, export, pos, props):
    r = Reader(data, pos)
    n = r.index()
    colors = [tuple(data[r.pos + 4 * i:r.pos + 4 * i + 4]) for i in range(n)]  # R, G, B, A
    return colors, r.pos + 4 * n


def read_mips(pkg, data, pos):
    r = Reader(data, pos)
    mips = []
    for _ in range(r.index()):
        skip = r.u32() if pkg["version"] >= 63 else None
        size = r.index()
        pixels = data[r.pos:r.pos + size]
        r.pos += size
        if skip is not None and skip != r.pos:
            raise ValueError(f"mip skip offset {skip} != {r.pos}")
        w, h = r.u32(), r.u32()
        mips.append({"width": w, "height": h, "ubits": data[r.pos], "vbits": data[r.pos + 1], "data": pixels})
        r.pos += 2
    return mips, r.pos


@register("Texture", "ScriptedTexture", "WetTexture", "WaveTexture", "IceTexture")
def read_texture(pkg, data, export, pos, props):
    mips, end = read_mips(pkg, data, pos)
    tex = {"mips": mips}
    if any(p["name"] == "bHasComp" and p["value"] for p in props):  # 469: compressed copy follows
        tex["comp_mips"], end = read_mips(pkg, data, end)
    return tex, end


@register("FireTexture")
def read_fire_texture(pkg, data, export, pos, props):
    tex, end = read_texture(pkg, data, export, pos, props)
    r = Reader(data, end)
    n = r.index()
    tex["sparks"] = [{"type": data[p], "heat": data[p + 1], "x": data[p + 2], "y": data[p + 3],
                      "args": tuple(data[p + 4:p + 8])} for p in range(r.pos, r.pos + 8 * n, 8)]
    return tex, r.pos + 8 * n


def rgba(pixels, palette):
    return b"".join(bytes(palette[p]) for p in pixels)


def png(width, height, rgba_bytes):
    rows = b"".join(b"\0" + rgba_bytes[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main(argv):
    if len(argv) != 5 or argv[3] != "--png":
        sys.exit(__doc__)
    with open(argv[1], "rb") as f:
        data = f.read()
    pkg = parse(data)
    by_name = {e["name"]: e for e in pkg["exports"] if e["size"]}
    tex = read_object(pkg, data, by_name[argv[2]])
    pal_ref = next(p["value"] for p in tex["props"] if p["name"] == "Palette")
    if pal_ref <= 0:
        sys.exit("palette is imported from another package; not supported yet")
    palette = read_object(pkg, data, pkg["exports"][pal_ref - 1])["value"]
    m = tex["value"]["mips"][0]
    if len(m["data"]) != m["width"] * m["height"]:
        sys.exit("not a P8 texture")
    opaque = [(r, g, b, 255) for r, g, b, _ in palette]  # palette alpha is unused for P8 (unverified)
    with open(argv[4], "wb") as f:
        f.write(png(m["width"], m["height"], rgba(m["data"], opaque)))


if __name__ == "__main__":
    main(sys.argv)
