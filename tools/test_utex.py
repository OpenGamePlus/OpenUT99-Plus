import struct
import unittest

from test_upkg import cindex
from utex import read_fire_texture, read_palette, read_texture, rgba


def mip(width, height, version, base, fill=1):
    pixels = bytes([fill]) * (width * height)
    body = cindex(len(pixels)) + pixels + struct.pack("<IIBB", width, height, width.bit_length() - 1, height.bit_length() - 1)
    if version >= 63:  # u32 file position just after the pixel data
        body = struct.pack("<I", base + 4 + len(cindex(len(pixels))) + len(pixels)) + body
    return body


def texture_data(version, sizes=((4, 2), (2, 1))):
    data = cindex(len(sizes))
    for w, h in sizes:
        data += mip(w, h, version, len(data))
    return data


class UtexTest(unittest.TestCase):
    def test_palette(self):
        data = cindex(256) + bytes(range(256)) * 4
        colors, end = read_palette({}, data, {}, 0, [])
        self.assertEqual(len(colors), 256)
        self.assertEqual(colors[1], (4, 5, 6, 7))
        self.assertEqual(end, len(data))

    def test_texture_mips_v69(self):
        data = texture_data(69)
        tex, end = read_texture({"version": 69}, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual([(m["width"], m["height"], m["ubits"]) for m in tex["mips"]], [(4, 2, 2), (2, 1, 1)])
        self.assertEqual(tex["mips"][0]["data"], b"\x01" * 8)

    def test_compressed_mips_follow_when_bHasComp(self):
        data = texture_data(69)
        comp = texture_data(69, sizes=((4, 4),))
        # the compressed array's skip offsets are file positions: shift them by the first array's length
        comp = cindex(1) + struct.pack("<I", struct.unpack_from("<I", comp, 1)[0] + len(data)) + comp[5:]
        props = [{"name": "bHasComp", "value": True}]
        tex, end = read_texture({"version": 69}, data + comp, {}, 0, props)
        self.assertEqual((end, len(tex["mips"]), len(tex["comp_mips"])), (len(data + comp), 2, 1))

    def test_texture_mips_v61_has_no_skip_offset(self):
        data = texture_data(61)
        tex, end = read_texture({"version": 61}, data, {}, 0, [])
        self.assertEqual((end, len(tex["mips"])), (len(data), 2))

    def test_bad_skip_offset_raises(self):
        data = bytearray(texture_data(69))
        data[1] ^= 0xFF  # corrupt first mip's skip offset
        with self.assertRaises(ValueError):
            read_texture({"version": 69}, bytes(data), {}, 0, [])

    def test_fire_texture_sparks_follow_mips(self):
        base = texture_data(69)
        sparks = cindex(2) + bytes([5, 255, 10, 20, 1, 2, 3, 4]) + bytes([2, 52, 24, 127, 0, 0, 0, 10])
        tex, end = read_fire_texture({"version": 69}, base + sparks, {}, 0, [])
        self.assertEqual(end, len(base + sparks))
        self.assertEqual(tex["sparks"][0], {"type": 5, "heat": 255, "x": 10, "y": 20, "args": (1, 2, 3, 4)})
        self.assertEqual(len(tex["sparks"]), 2)

    def test_rgba_from_palette(self):
        palette = [(i, i, i, 255) for i in range(256)]
        self.assertEqual(rgba(b"\x00\x02", palette), bytes([0, 0, 0, 255, 2, 2, 2, 255]))


if __name__ == "__main__":
    unittest.main()
