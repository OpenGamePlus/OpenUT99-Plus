#!/usr/bin/env python3
"""Sound and Music class data: a format name and the embedded file.

Usage: usound.py FILE OBJECT --out out/x.wav    write the embedded file
Spec: docs/formats/sound.md
"""
import sys

from upkg import Reader, lazy_bytes, parse
from uobjects import read_object, register


@register("Sound", "Music")
def read_sound(pkg, data, export, pos, props):
    r = Reader(data, pos)
    fmt = pkg["names"][r.index()]
    blob = lazy_bytes(r, pkg["version"])
    return {"format": fmt, "data": blob}, r.pos


def signature(blob):
    """Container type by magic bytes."""
    if blob[:4] == b"RIFF" and blob[8:12] == b"WAVE":
        return "wav"
    if blob[:4] == b"IMPM":
        return "it"
    if blob[44:48] == b"SCRM":
        return "s3m"
    if blob[:17] == b"Extended Module: ":
        return "xm"
    if blob[1080:1084] in (b"M.K.", b"M!K!", b"4CHN", b"6CHN", b"8CHN", b"FLT4", b"FLT8"):
        return "mod"
    return "unknown"


def main(argv):
    if len(argv) != 5 or argv[3] != "--out":
        sys.exit(__doc__)
    with open(argv[1], "rb") as f:
        data = f.read()
    pkg = parse(data)
    e = next(e for e in pkg["exports"] if e["name"] == argv[2] and e["size"])
    with open(argv[4], "wb") as f:
        f.write(read_object(pkg, data, e)["value"]["data"])


if __name__ == "__main__":
    main(sys.argv)
