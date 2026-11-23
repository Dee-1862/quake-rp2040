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


def fake_map(planes=b"p" * 40, extra=b"lump"):
    """A BSP header (version 29) with a plane lump and one other lump."""
    hdr = bytearray(124)
    struct.pack_into("<i", hdr, 0, 29)
    struct.pack_into("<ii", hdr, 4 + 8 * 1, 124, len(planes))
    struct.pack_into("<ii", hdr, 4 + 8 * 8, 124 + len(planes), len(extra))
    return bytes(hdr) + planes + extra


class Stubs(unittest.TestCase):
    def test_fnv_matches_engine_constants(self):
        self.assertEqual(mkboard.fnv(b""), 0x811C9DC5)
        self.assertEqual(mkboard.fnv(b"a"), 0xE40C292C)   # published FNV-1a 32 value

    def test_map_hash_reads_header_and_planes_only(self):
        a, b = fake_map(extra=b"AAAA"), fake_map(extra=b"BBBB")
        self.assertEqual(mkboard.file_hash(a), mkboard.file_hash(b))
        self.assertNotEqual(mkboard.file_hash(a), mkboard.file_hash(fake_map(planes=b"q" * 40)))

    def test_stub_layout(self):
        m = fake_map()
        st = mkboard.make_stub(m)
        self.assertEqual(len(st), 132)
        self.assertEqual(struct.unpack_from("<I", st, 0)[0], mkboard.QOGB_STUB_IDENT)
        self.assertEqual(st[4:124], m[4:124])
        self.assertEqual(struct.unpack_from("<II", st, 124), (len(m), mkboard.file_hash(m)))

    def test_maps_last_keeps_other_files_in_place(self):
        ents = mkpak.maps_last([("maps/a.bsp", fake_map()), ("x.wav", b"w" * 7), ("maps/b.bsp", fake_map()),
                                ("progs/p.mdl", b"m" * 21)])
        self.assertEqual([n for n, _ in ents], ["x.wav", "progs/p.mdl", "maps/a.bsp", "maps/b.bsp"])
        pak = mkpak.build_pak(ents)
        new = mkboard.stub_pak(pak)
        self.assertEqual(mkboard.pc_layout(new), len(new))
        old = {n: o for n, o, _ in mkboard.pak_entries(pak)}
        for n, o, d in mkboard.pak_entries(new):
            if mkpak.is_map(n):
                self.assertEqual(len(d), 132)
            else:
                self.assertEqual(o, old[n])

    def test_stub_pak_refuses_maps_first(self):
        pak = mkpak.build_pak([("maps/a.bsp", fake_map()), ("x.wav", b"w" * 7)])
        with self.assertRaises(SystemExit):
            mkboard.stub_pak(pak)


if __name__ == "__main__":
    unittest.main()
