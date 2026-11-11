import struct
import unittest

from upkg import Reader, TAG, object_name, parse


def cindex(v):
    """Encode a compact index (inverse of Reader.index)."""
    a = abs(v)
    out = [(0x80 if v < 0 else 0) | (a & 0x3F)]
    a >>= 6
    if a:
        out[0] |= 0x40
    while a:
        out.append(a & 0x7F)
        a >>= 7
        if a:
            out[-1] |= 0x80
    return bytes(out)


def make_package():
    names = [b"Core", b"Class", b"Package", b"MyActor"]
    name_table = b"".join(cindex(len(n) + 1) + n + b"\0" + struct.pack("<I", 0) for n in names)
    imports = cindex(0) + cindex(2) + struct.pack("<i", 0) + cindex(0)            # Core.Package Core
    imports += cindex(0) + cindex(1) + struct.pack("<i", -1) + cindex(1)          # Core.Class Class
    exports = cindex(-2) + cindex(0) + struct.pack("<i", 0) + cindex(3) + struct.pack("<I", 0) + cindex(4) + cindex(1000)
    header_len = 36 + 16 + 4 + 8
    no = header_len
    io = no + len(name_table)
    eo = io + len(imports)
    head = struct.pack("<IHHI6I", TAG, 69, 0, 1, len(names), no, 1, eo, 2, io)
    head += b"\x11" * 16 + struct.pack("<III", 1, 1, len(names))
    return head + name_table + imports + exports


class UpkgTest(unittest.TestCase):
    def test_compact_index_roundtrip(self):
        for v in [0, 1, -1, 63, 64, -64, 8191, 8192, 1_000_000, -1_000_000]:
            self.assertEqual(Reader(cindex(v)).index(), v, v)

    def test_parse_synthetic_package(self):
        pkg = parse(make_package())
        self.assertEqual(pkg["version"], 69)
        self.assertEqual(pkg["names"], ["Core", "Class", "Package", "MyActor"])
        self.assertEqual(pkg["generations"], [(1, 4)])
        self.assertEqual([i["name"] for i in pkg["imports"]], ["Core", "Class"])
        e = pkg["exports"][0]
        self.assertEqual((e["name"], e["size"], e["offset"]), ("MyActor", 4, 1000))
        self.assertEqual(object_name(pkg, e["class"]), "Class")

    def test_old_version_names_and_heritage(self):
        # version 61: NUL-terminated names, heritage count/offset instead of GUID + generations
        names = b"None\0" + struct.pack("<I", 0) + b"Tex\0" + struct.pack("<I", 0)
        head = struct.pack("<IHHI6I", TAG, 61, 0, 0, 2, 44, 0, 44 + len(names), 0, 44 + len(names))
        pkg = parse(head + struct.pack("<II", 0, 0) + names)
        self.assertEqual(pkg["names"], ["None", "Tex"])
        self.assertNotIn("generations", pkg)

    def test_overlong_index(self):
        with self.assertRaises(ValueError):
            Reader(b"\x40" + b"\xff" * 5).index()

    def test_bad_tag(self):
        with self.assertRaises(ValueError):
            parse(b"\0" * 64)


if __name__ == "__main__":
    unittest.main()
