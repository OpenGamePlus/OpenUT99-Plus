import struct
import unittest

from test_upkg import cindex
import umesh

PKG = {"version": 69, "names": ["None", "All", "Fire", "Bone"]}
PRIM = struct.pack("<6fB4f", 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1)


def lazy(count, payload, base):
    head = cindex(count)
    return struct.pack("<I", base + 4 + len(head) + len(payload)) + head + payload


def pack_vert(x, y, z):
    return struct.pack("<I", (x & 0x7FF) | ((y & 0x7FF) << 11) | ((z & 0x3FF) << 22))


def mesh_body(base=0):
    data = PRIM
    data += lazy(2, pack_vert(1, -2, 3) + pack_vert(-1024, 1023, -512), base + len(data))       # verts: 1 frame x 2
    data += lazy(0, b"", base + len(data))                                                    # tris
    data += cindex(1) + cindex(2) + cindex(0) + struct.pack("<ii", 0, 1) + cindex(1) + struct.pack("<f", 0.5) + cindex(2) + struct.pack("<f", 30.0)  # seq Fire
    data += lazy(0, b"", base + len(data))                                                    # connects
    data += PRIM
    data += lazy(0, b"", base + len(data))                                                    # vert links
    data += cindex(1) + cindex(-9)                                                            # textures
    data += cindex(1) + PRIM[:25] + cindex(1) + PRIM[25:]                                     # bounding boxes, spheres
    data += struct.pack("<iiII", 2, 1, 0, 0) + struct.pack("<3f", 0.1, 0.1, 0.2) + struct.pack("<3f", 0, 0, 0) + struct.pack("<3i", 0, 16384, 0)
    data += struct.pack("<ii", 0, 0) + cindex(1) + struct.pack("<f", 1.0)                     # cur poly/vertex, texture LOD
    return data


def lod_tail():
    data = cindex(2) + struct.pack("<2H", 0, 0) + cindex(1) + struct.pack("<H", 1)            # collapse points, face level
    data += cindex(1) + struct.pack("<4H", 0, 1, 0, 0)                                         # faces
    data += cindex(0) + cindex(2) + struct.pack("<HBB", 0, 10, 20) + struct.pack("<HBB", 1, 30, 40)  # collapse wedges, wedges
    data += cindex(1) + struct.pack("<Ii", 0, 0) + cindex(0)                                   # materials, special faces
    data += struct.pack("<iifffifff", 2, 0, 0.1, 0, 1, 10, 0.3, 0, 0) + cindex(0)                  # LOD parameters, remap
    return data


class UmeshTest(unittest.TestCase):
    def test_unpack_vert(self):
        v = struct.unpack("<I", pack_vert(-1024, 1023, -512))[0]
        self.assertEqual(umesh.unpack_vert(v), (-1024, 1023, -512))

    def test_mesh(self):
        data = mesh_body()
        m, end = umesh.read_mesh(PKG, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual(m["verts"], [(1, -2, 3), (-1024, 1023, -512)])
        self.assertEqual(m["anim_seqs"][0], {"name": "Fire", "group": "None", "start": 0, "frames": 1,
                                              "notifys": [(0.5, "Fire")], "rate": 30.0})
        self.assertEqual((m["frame_verts"], m["anim_frames"], m["textures"]), (2, 1, [-9]))
        self.assertEqual(m["rot_origin"], (0, 16384, 0))

    def test_lod_mesh(self):
        data = mesh_body() + lod_tail()
        m, end = umesh.read_lod_mesh(PKG, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual(m["wedges"], [(0, 10, 20), (1, 30, 40)])
        self.assertEqual(m["faces"], [(0, 1, 0, 0)])
        self.assertEqual((m["model_verts"], m["lod_min_verts"]), (2, 10))

    def test_lod_mesh_obj(self):
        m, _ = umesh.read_lod_mesh(PKG, mesh_body() + lod_tail(), {}, 0, [])
        self.assertEqual(umesh.lod_mesh_obj(m), "v 1 -2 3\nv -1024 1023 -512\nf 1 2 1\n")

    def test_lod_mesh_obj_skips_special_verts(self):
        m = {"frame_verts": 3, "special_verts": 1, "verts": [(9, 9, 9), (1, 0, 0), (0, 1, 0)],
             "wedges": [(0, 0, 0), (1, 0, 0)], "faces": [(0, 1, 0, 0)]}
        self.assertTrue(umesh.lod_mesh_obj(m).endswith("f 2 3 2\n"))

    def test_skeletal_mesh_and_animation(self):
        bone = cindex(3) + struct.pack("<I", 0) + struct.pack("<4f3f4f", 0, 0, 0, 1, 1, 2, 3, 0, 0, 0, 0) + struct.pack("<ii", 0, 0)
        skel = mesh_body() + lod_tail() + cindex(0) + cindex(1) + struct.pack("<3f", 1, 2, 3) + cindex(1) + bone
        skel += cindex(0) + cindex(0) + cindex(0) + struct.pack("<i", 1) + cindex(0) + struct.pack("<i", -1) + struct.pack("<12f", *([0] * 12))
        m, end = umesh.read_skeletal_mesh(PKG, skel, {}, 0, [])
        self.assertEqual(end, len(skel))
        self.assertEqual((m["points"], m["ref_skeleton"][0]["name"], m["ref_skeleton"][0]["position"]), ([(1.0, 2.0, 3.0)], "Bone", (1.0, 2.0, 3.0)))
        track = struct.pack("<I", 0) + cindex(1) + struct.pack("<4f", 0, 0, 0, 1) + cindex(1) + struct.pack("<3f", 0, 0, 0) + cindex(1) + struct.pack("<f", 0)
        anim = cindex(1) + cindex(3) + struct.pack("<Ii", 0, -1)
        anim += cindex(1) + struct.pack("<3ffii", 0, 0, 0, 1.0, 0, 0) + cindex(1) + struct.pack("<i", 0) + cindex(1) + track + track
        anim += cindex(0)
        a, end = umesh.read_animation(PKG, anim, {}, 0, [])
        self.assertEqual(end, len(anim))
        self.assertEqual(a["moves"][0]["tracks"][0]["key_quat"], [(0.0, 0.0, 0.0, 1.0)])


if __name__ == "__main__":
    unittest.main()
