import struct
import unittest

from test_upkg import cindex
import uscript

NAMES = ["None", "Tick", "Health", "Label1"]
PKG = {"version": 69, "names": NAMES, "imports": [], "exports": []}

# Health = 5; Tick(1); native 112(); return; label table (Label1 @ 0, None)
SCRIPT = (bytes([0x0F, 0x01]) + cindex(-3) + bytes([0x1D]) + struct.pack("<i", 5)        # mem 1+1+4+1+4 = 11
          + bytes([0x1B]) + cindex(1) + bytes([0x26, 0x16])                               # mem 1+4+1+1 = 7
          + bytes([0x70, 0x16])                                                           # mem 2
          + bytes([0x04, 0x0B])                                                           # mem 2
          + bytes([0x0C]) + cindex(3) + struct.pack("<I", 0) + cindex(0) + struct.pack("<I", 0xFFFF))  # mem 1+8+8
SCRIPT_MEM = 11 + 7 + 2 + 2 + 17


def struct_body(script=SCRIPT, mem=SCRIPT_MEM):
    head = cindex(-1) + cindex(0) + cindex(0) + cindex(-2) + cindex(1)  # super, next, text, children, friendly name
    return head + struct.pack("<iiI", 10, 200, mem) + script


class UscriptTest(unittest.TestCase):
    def test_decode_script_counts_memory_size(self):
        end, tokens = uscript.decode_script(PKG, SCRIPT, 0, SCRIPT_MEM)
        self.assertEqual(end, len(SCRIPT))
        self.assertEqual(tokens[0x0F], 1)
        self.assertEqual(tokens[0x70], 1)

    def test_decode_script_unknown_token_raises(self):
        with self.assertRaises(ValueError):
            uscript.decode_script(PKG, bytes([0x03]), 0, 1)

    def test_decode_script_overrun_raises(self):
        with self.assertRaises(ValueError):
            uscript.decode_script(PKG, bytes([0x1D]) + b"\0" * 4, 0, 3)  # IntConst is 5 bytes, size says 3

    def test_function(self):
        data = struct_body() + struct.pack("<HBI", 0, 0, 0x40) + struct.pack("<H", 7)  # FUNC_Net: rep offset
        fn, end = uscript.read_function(PKG, data, {}, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual((fn["line"], fn["script_size"], fn["flags"], fn["rep_offset"]), (10, SCRIPT_MEM, 0x40, 7))

    def test_state(self):
        data = struct_body() + struct.pack("<QQHI", 1, 2, 0xFFFF, 4)
        st, end = uscript.read_state(PKG, data, {}, 0, [])
        self.assertEqual((end, st["probe_mask"], st["state_flags"]), (len(data), 1, 4))

    def test_class_with_default_properties(self):
        data = struct_body() + struct.pack("<QQHI", 0, 0, 0xFFFF, 0)
        data += struct.pack("<I", 0x21) + b"\x11" * 16                               # class flags, guid
        data += cindex(1) + cindex(-1) + struct.pack("<II", 1, 0xABCD)                # dependencies
        data += cindex(1) + cindex(-2)                                                # package imports
        data += cindex(0) + cindex(0)                                                 # within, config name
        data += cindex(2) + bytes([0x22]) + struct.pack("<i", 100) + cindex(0)       # defaults: Health=100
        export = {"offset": 0, "size": len(data), "flags": 0}
        cls, end = uscript.read_class(PKG, data, export, 0, [])
        self.assertEqual(end, len(data))
        self.assertEqual(cls["class_flags"], 0x21)
        self.assertEqual(cls["dependencies"], [(-1, 1, 0xABCD)])
        self.assertEqual(cls["defaults"][0]["value"], 100)

    def test_property_declarations(self):
        head = cindex(0) + cindex(0) + struct.pack("<HHI", 2, 4, 0x20) + cindex(2) + struct.pack("<H", 9)
        decl, end = uscript.read_property(PKG, head + cindex(-4), {}, 0, [], refs=1)
        self.assertEqual((decl["array_dim"], decl["flags"], decl["rep_offset"], decl["refs"], end),
                         (2, 0x20, 9, [-4], len(head) + 1))

    def test_const_enum_textbuffer(self):
        const = cindex(0) + cindex(0) + cindex(3) + b"42\0"
        self.assertEqual(uscript.read_const(PKG, const, {}, 0, [])[0]["value"], "42")
        enum = cindex(0) + cindex(0) + cindex(2) + cindex(1) + cindex(2)
        self.assertEqual(uscript.read_enum(PKG, enum, {}, 0, [])[0]["names"], ["Tick", "Health"])
        text = struct.pack("<II", 1, 2) + cindex(3) + b"hi\0"
        self.assertEqual(uscript.read_text_buffer(PKG, text, {}, 0, [])[0]["text"], "hi")


if __name__ == "__main__":
    unittest.main()
