import struct
import unittest

from test_upkg import cindex
from uprops import HAS_STACK, read_properties

NAMES = ["None", "Health", "bHidden", "Location", "Vector", "Tag", "Rotation", "Rotator", "Region",
         "PointRegion", "Items", "Message", "Speed"]


def n(name):
    return cindex(NAMES.index(name))


def run(body, flags=0, tail=b""):
    data = b"\xAA" * 4 + body + tail  # 4 junk bytes: offsets must be honoured
    pkg = {"names": NAMES}
    return read_properties(pkg, data, {"offset": 4, "size": len(body) + len(tail), "flags": flags}), len(data) - len(tail)


class UpropsTest(unittest.TestCase):
    def test_scalars_struct_array_and_tail(self):
        body = n("Health") + bytes([0x22]) + struct.pack("<i", 100)               # Int, size 4
        body += n("bHidden") + bytes([0x83])                                       # Bool true, no value bytes
        body += n("Location") + bytes([0x3A]) + n("Vector") + struct.pack("<3f", 1, 2, 3)  # Struct, size 12
        body += n("Rotation") + bytes([0x3A]) + n("Rotator") + struct.pack("<3i", 0, 16384, 0)
        body += n("Region") + bytes([0x5A]) + n("PointRegion") + bytes([6]) + cindex(-3) + struct.pack("<i", 7) + b"\x02"
        body += n("Items") + bytes([0x85]) + bytes([0x81, 0x2C]) + cindex(5)      # Object, size 1, array index 300
        body += n("Message") + bytes([0x5D, 5]) + cindex(4) + b"hi!\0"            # Str, size byte follows
        body += n("Speed") + bytes([0x24]) + struct.pack("<f", 1.5)
        body += n("None")
        (props, end), want_end = run(body, tail=b"classdata")
        self.assertEqual(end, want_end)
        v = {p["name"]: p["value"] for p in props}
        self.assertEqual(v["Health"], 100)
        self.assertIs(v["bHidden"], True)
        self.assertEqual(v["Location"], (1.0, 2.0, 3.0))
        self.assertEqual(v["Rotation"], (0, 16384, 0))
        self.assertEqual(v["Region"], {"Zone": -3, "iLeaf": 7, "ZoneNumber": 2})
        self.assertEqual(v["Message"], "hi!")
        self.assertEqual(v["Speed"], 1.5)
        items = props[5]
        self.assertEqual((items["index"], items["value"], items["type"]), (300, 5, "Object"))

    def test_state_frame_skipped(self):
        frame = cindex(-2) + cindex(-2) + struct.pack("<QI", 0, 0) + cindex(10)  # node set: offset follows
        (props, end), want_end = run(frame + n("Health") + bytes([0x22]) + struct.pack("<i", 5) + n("None"), HAS_STACK)
        self.assertEqual([p["value"] for p in props], [5])
        self.assertEqual(end, want_end)

    def test_overrun_raises(self):
        with self.assertRaises(ValueError):
            run(n("Health") + bytes([0x72]) + struct.pack("<I", 999) + n("None"))


if __name__ == "__main__":
    unittest.main()
