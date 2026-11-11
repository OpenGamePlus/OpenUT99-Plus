import struct
import unittest

from upkg import Reader, TAG, lazy_bytes, name, object_name, parse


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

    def test_lazy_bytes_skip_offset_from_version_62(self):
        payload = b"abc"
        old = cindex(3) + payload
        new = struct.pack("<I", 4 + 1 + 3) + cindex(3) + payload
        for version, data in ((61, old), (62, new), (69, new)):
            r = Reader(data)
            self.assertEqual(lazy_bytes(r, version), payload, version)
            self.assertEqual(r.pos, len(data))
        with self.assertRaises(ValueError):
            lazy_bytes(Reader(struct.pack("<I", 99) + cindex(3) + payload), 69)

    def test_string_negative_length_is_utf16(self):
        r = Reader(cindex(-3) + "hé".encode("utf-16-le") + b"\0\0" + b"x")
        self.assertEqual(r.string(), "hé")
        self.assertEqual(r.pos, 1 + 6)

    def test_truncated_data_raises_value_error(self):
        for read in (lambda: Reader(b"\x00").u32(), lambda: Reader(b"\x40").index(),
                     lambda: Reader(cindex(5) + b"ab").string(),
                     lambda: lazy_bytes(Reader(cindex(10) + b"abc"), 61)):
            with self.assertRaises(ValueError):
                read()

    def test_name_index_checked(self):
        pkg = {"names": ["None", "A"]}
        self.assertEqual(name(pkg, 1), "A")
        for bad in (-1, 2):
            with self.assertRaises(ValueError):
                name(pkg, bad)

    def test_bad_tag(self):
        with self.assertRaises(ValueError):
            parse(b"\0" * 64)


if __name__ == "__main__":
    unittest.main()
