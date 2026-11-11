import os
import tempfile
import unittest

import uini

INI = """[URL]
Map=Index.unr
[Core.System]
CachePath=../Cache
CacheExt=.uxx
Paths=../System/*.u
Paths=../Maps/*.unr
; comment
[Empty]
"""


class UiniTest(unittest.TestCase):
    def test_parse_keeps_repeated_keys_in_order(self):
        ini = uini.parse(INI)
        self.assertEqual(ini["Core.System"]["Paths"], ["../System/*.u", "../Maps/*.unr"])
        self.assertEqual(ini["URL"]["Map"], ["Index.unr"])
        self.assertEqual(ini["Empty"], {})

    def test_package_paths_from_paths_and_cache(self):
        root = tempfile.mkdtemp()
        for rel in ("System/Engine.u", "Maps/DM-Test.unr", "Cache/ABCD.uxx"):
            os.makedirs(os.path.join(root, os.path.dirname(rel)), exist_ok=True)
            open(os.path.join(root, rel), "w").close()
        with open(os.path.join(root, "Cache", "cache.ini"), "w") as f:
            f.write("[Cache]\nABCD=Custom.utx\nMISSING=Gone.u\n")
        paths = uini.package_paths(root, uini.parse(INI))
        self.assertEqual(paths["engine"], os.path.join(root, "System", "Engine.u"))
        self.assertEqual(paths["dm-test"], os.path.join(root, "Maps", "DM-Test.unr"))
        self.assertEqual(paths["custom"], os.path.join(root, "Cache", "ABCD.uxx"))
        self.assertNotIn("gone", paths)


if __name__ == "__main__":
    unittest.main()
