import struct
import unittest

from test_upkg import cindex
from usound import read_sound, signature

PKG = {"version": 69, "names": ["None", "WAV", "it"]}


class UsoundTest(unittest.TestCase):
    def test_sound_format_and_data(self):
        blob = b"RIFF\x04\x00\x00\x00WAVE"
        data = cindex(1) + struct.pack("<I", 1 + 4 + 1 + len(blob)) + cindex(len(blob)) + blob
        snd, end = read_sound(PKG, data, {}, 0, [])
        self.assertEqual((snd["format"], snd["data"], end), ("WAV", blob, len(data)))

    def test_music_v61_without_skip_offset(self):
        blob = b"IMPMsong"
        data = cindex(2) + cindex(len(blob)) + blob
        snd, end = read_sound({**PKG, "version": 61}, data, {}, 0, [])
        self.assertEqual((snd["format"], end), ("it", len(data)))

    def test_signature(self):
        self.assertEqual(signature(b"RIFF\0\0\0\0WAVEfmt "), "wav")
        self.assertEqual(signature(b"IMPMxxxx"), "it")
        self.assertEqual(signature(b"x" * 44 + b"SCRM"), "s3m")
        self.assertEqual(signature(b"Extended Module: abc"), "xm")
        self.assertEqual(signature(b"x" * 1080 + b"M.K."), "mod")
        self.assertEqual(signature(b"????"), "unknown")


if __name__ == "__main__":
    unittest.main()
