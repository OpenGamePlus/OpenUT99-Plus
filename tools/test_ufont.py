import struct
import unittest

from test_upkg import cindex
from ufont import read_font


def pages(n_pages, chars):
    data = cindex(n_pages)
    for i in range(n_pages):
        data += cindex(-(i + 1)) + cindex(chars)
        data += b"".join(struct.pack("<4i", c, 0, 8, 12) for c in range(chars))
    return data


class UfontTest(unittest.TestCase):
    def test_v68_pages_and_chars_per_page(self):
        data = pages(2, 3) + struct.pack("<I", 64)
        font, end = read_font({"version": 68}, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual(font["pages"][1]["texture"], -2)
        self.assertEqual(font["pages"][0]["chars"][2], (2, 0, 8, 12))
        self.assertEqual(font["chars_per_page"], 64)
        self.assertNotIn("remap", font)

    def test_v69_remap_table(self):
        data = pages(1, 1) + struct.pack("<I", 2048) + cindex(2) + struct.pack("<4H", 65, 1, 0x416, 2) + struct.pack("<I", 1)
        font, end = read_font({"version": 69}, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual(font["remap"], {65: 1, 0x416: 2})
        self.assertEqual(font["remapped"], 1)


if __name__ == "__main__":
    unittest.main()
