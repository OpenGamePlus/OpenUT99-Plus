import struct
import unittest

import uobjects
from test_upkg import cindex

# names: 0 None, 1 Health; imports: -1 = class "Thing", -2 = class "Plain", -3 = class "Broken"
PKG = {"names": ["None", "Health"], "exports": [],
       "imports": [{"name": "Thing"}, {"name": "Plain"}, {"name": "Broken"}, {"name": "Unknown"}, {"name": "Short"}]}
PROPS = cindex(1) + bytes([0x22]) + (7).to_bytes(4, "little") + cindex(0)  # Health=7, None


@uobjects.register("Thing")
def _thing(pkg, data, export, pos, props):
    return data[pos], pos + 1  # one byte of class data


@uobjects.register("Broken")
def _broken(pkg, data, export, pos, props):
    raise ValueError("bad")


@uobjects.register("Short")
def _short(pkg, data, export, pos, props):
    return struct.unpack_from("<I", data, len(data)), pos  # reads past the end


def export(cls, tail):
    data = PROPS + tail
    return data, {"class": cls, "offset": 0, "size": len(data), "flags": 0, "name": "x"}


class UobjectsTest(unittest.TestCase):
    def test_exact_leftover_error_noreader(self):
        cases = [(-1, b"\x05", "exact"), (-1, b"\x05\x06", "leftover"), (-2, b"", "exact"),
                 (-3, b"\x01", "error"), (-4, b"\x01", "noreader"), (-5, b"\x01", "error")]
        for cls, tail, want in cases:
            data, e = export(cls, tail)
            self.assertEqual(uobjects.outcome(PKG, data, e), want, (cls, tail))

    def test_read_object_value(self):
        data, e = export(-1, b"\x05")
        obj = uobjects.read_object(PKG, data, e)
        self.assertEqual((obj["value"], obj["end"], obj["props"][0]["value"]), (5, len(data), 7))


if __name__ == "__main__":
    unittest.main()
