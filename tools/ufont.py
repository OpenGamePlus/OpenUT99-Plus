"""Font class data: texture pages with glyph rectangles. Spec: docs/formats/font.md"""
import struct

from upkg import Reader
from uobjects import register


@register("Font")
def read_font(pkg, data, export, pos, props):
    r = Reader(data, pos)
    pages = []
    for _ in range(r.index()):
        texture = r.index()  # object reference
        n = r.index()
        chars = [struct.unpack_from("<4i", data, r.pos + 16 * i) for i in range(n)]  # StartU, StartV, USize, VSize
        r.pos += 16 * n
        pages.append({"texture": texture, "chars": chars})
    font = {"pages": pages, "chars_per_page": r.u32()}
    if pkg["version"] >= 69:  # 469: character remap table and its flag
        n = r.index()
        pairs = struct.unpack_from(f"<{2 * n}H", data, r.pos)
        r.pos += 4 * n
        font["remap"] = dict(zip(pairs[0::2], pairs[1::2]))
        font["remapped"] = r.u32()
    return font, r.pos
