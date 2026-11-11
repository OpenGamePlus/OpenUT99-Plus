import struct
import unittest

from test_upkg import cindex
import umodel

PKG = {"version": 69, "names": ["None", "Item"]}
BOX = struct.pack("<6fB", 0, 0, 0, 1, 1, 1, 1)


def vec(*v):
    return struct.pack("<3f", *v)


class UmodelTest(unittest.TestCase):
    def test_polys(self):
        poly = cindex(3) + vec(0, 0, 0) + vec(0, 0, 1) + vec(1, 0, 0) + vec(0, 1, 0)
        poly += vec(0, 0, 0) + vec(1, 0, 0) + vec(0, 1, 0)
        poly += struct.pack("<I", 0x80) + cindex(-1) + cindex(-2) + cindex(1) + cindex(0) + cindex(-1) + struct.pack("<hh", 4, -4)
        data = struct.pack("<II", 1, 1) + poly
        polys, end = umodel.read_polys(PKG, data, {}, 0, [])
        self.assertEqual(end, len(data))
        p = polys[0]
        self.assertEqual((len(p["vertices"]), p["normal"], p["flags"], p["texture"], p["pan"]), (3, (0.0, 0.0, 1.0), 0x80, -2, (4, -4)))

    def test_model(self):
        node = struct.pack("<4fQB", 0, 0, 1, 0, 3, 0) + b"".join(cindex(v) for v in (0, 0, -1, -1, 0, 0, 0)) + bytes([1, 2, 3]) + struct.pack("<ii", 0, -1)
        surf = cindex(-5) + struct.pack("<I", 0x400) + b"".join(cindex(v) for v in (0, 0, 1, 2, 0, -1)) + struct.pack("<hh", 0, 0) + cindex(0)
        lmap = struct.pack("<i", 0) + vec(0, 0, 0) + cindex(8) + cindex(4) + struct.pack("<ffi", 32.0, 32.0, 0)
        data = BOX + struct.pack("<4f", 0, 0, 0, 10)
        data += cindex(3) + vec(1, 0, 0) + vec(0, 1, 0) + vec(0, 0, 1)    # vectors
        data += cindex(1) + vec(5, 6, 7)                                   # points
        data += cindex(1) + node + cindex(1) + surf                        # nodes, surfs
        data += cindex(1) + cindex(0) + cindex(0)                          # verts
        data += struct.pack("<ii", 0, 1) + cindex(0) + struct.pack("<QQ", 1, 1)  # shared sides, zones
        data += cindex(-7)                                                 # polys
        data += cindex(1) + lmap + cindex(3) + b"\x01\x02\x03"             # lightmaps, light bits
        data += cindex(1) + BOX + cindex(1) + struct.pack("<i", 9)         # bounds, leaf hulls
        data += cindex(1) + cindex(0) + cindex(0) + cindex(0) + struct.pack("<Q", 0)  # leaves
        data += cindex(1) + cindex(-3) + struct.pack("<II", 1, 1)          # lights, root outside, linked
        m, end = umodel.read_model(PKG, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual(m["points"], [(5.0, 6.0, 7.0)])
        self.assertEqual(m["nodes"][0]["num_vertices"], 3)
        self.assertEqual((m["surfs"][0]["texture"], m["surfs"][0]["flags"], m["surfs"][0]["texture_v"]), (-5, 0x400, 2))
        self.assertEqual((m["lightmaps"][0]["uclamp"], m["light_bits"]), (8, b"\x01\x02\x03"))
        self.assertEqual(m["polys"], -7)

    def test_level(self):
        data = struct.pack("<II", 2, 2) + cindex(-1) + cindex(0)
        data += b"".join(cindex(len(s) + 1) + s + b"\0" for s in (b"unreal", b"", b"DM-Test", b""))
        data += cindex(1) + cindex(5) + b"Game\0" + struct.pack("<ii", 7777, 1) + cindex(-2)
        data += cindex(1) + struct.pack("<i", 100) + cindex(-3) + cindex(-4) + struct.pack("<iii", 17, 39, 1) + b"\x00"
        data += struct.pack("<f", 0) + cindex(0) + cindex(0) * 16 + cindex(0)
        lv, end = umodel.read_level(PKG, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual((lv["actors"], lv["url"]["map"], lv["url"]["options"], lv["model"]), ([-1, 0], "DM-Test", ["Game"], -2))
        self.assertEqual(lv["reach_specs"][0]["distance"], 100)


if __name__ == "__main__":
    unittest.main()
