"""Host tests for tools/mkboard.py and mkpak's pak merge. Run: python -m unittest discover -s tools/tests"""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import mkboard  # noqa: E402
import mkpak    # noqa: E402


class PcLayout(unittest.TestCase):
    def test_mkpak_output_is_what_the_pc_maps(self):
        pak = mkpak.build_pak([("a.txt", b"x" * 5), ("b.bin", b"y" * 33), ("c", b"")])
        self.assertEqual(mkboard.pc_layout(pak), len(pak))

    def test_moved_file_is_refused(self):
        pak = bytearray(mkpak.build_pak([("a", b"x" * 5), ("b", b"y")]))
        dofs = struct.unpack_from("<i", pak, 4)[0]
        struct.pack_into("<i", pak, dofs + 64 + 56, 40)   # second file not where the PC puts it
        self.assertIsNone(mkboard.pc_layout(bytes(pak)))


class PakMerge(unittest.TestCase):
    def test_later_pak_wins_and_keeps_first_order(self):
        with tempfile.TemporaryDirectory() as d:
            p0, p1 = os.path.join(d, "pak0.pak"), os.path.join(d, "pak1.pak")
            open(p0, "wb").write(mkpak.build_pak([("a", b"0"), ("b", b"0")]))
            open(p1, "wb").write(mkpak.build_pak([("c", b"1"), ("a", b"1")]))
            files, order = mkpak.read_paks([p0, p1])
        self.assertEqual(order, ["a", "b", "c"])
        self.assertEqual(files["a"], b"1")


class BakeHeader(unittest.TestCase):
    def test_header_fields(self):
        img = struct.pack("<7I", mkboard.QOGB_MAGIC, 2, 0, 0x10C00000, 64, 0, 64) + b"\0" * 36
        h = mkboard.bake_header(img)
        self.assertEqual((h[3], h[4]), (0x10C00000, 64))
        self.assertIsNone(mkboard.bake_header(b"PACK" + b"\0" * 60))


if __name__ == "__main__":
    unittest.main()
