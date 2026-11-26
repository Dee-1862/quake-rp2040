"""Host tests for tools/mkdata.py. Run: python -m unittest discover -s tools/tests"""
import os
import struct
import sys
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import mkdata  # noqa: E402


class ImageFormat(unittest.TestCase):
    def setUp(self):
        self.img = mkdata.build_image([("a", b"hello"), ("bb", b"x" * 33)], label="t", align=16)

    def header(self):
        return struct.unpack("<8I28sI", self.img[:64])

    def test_header_fields(self):
        magic, ver, hsz, n, diro, total, bcrc, align, label, hcrc = self.header()
        self.assertEqual(magic, mkdata.QOGD_MAGIC)
        self.assertEqual((ver, hsz, n, diro, align), (1, 64, 2, 64, 16))
        self.assertEqual(total, len(self.img))
        self.assertEqual(bcrc, zlib.crc32(self.img[64:]) & 0xFFFFFFFF)
        self.assertEqual(hcrc, zlib.crc32(self.img[:60]) & 0xFFFFFFFF)
        self.assertTrue(label.startswith(b"t\x00"))

    def test_directory_points_at_data(self):
        for i, want in enumerate([("a", b"hello"), ("bb", b"x" * 33)]):
            name, off, size = struct.unpack("<24sII", self.img[64 + 32 * i: 96 + 32 * i])
            self.assertEqual(name.rstrip(b"\0").decode(), want[0])
            self.assertEqual(off % 16, 0)
            self.assertEqual(self.img[off:off + size], want[1])

    def test_rejects_bad_names(self):
        with self.assertRaises(ValueError):
            mkdata.build_image([("x" * 25, b"")])
        with self.assertRaises(ValueError):
            mkdata.build_image([("a", b""), ("a", b"")])


class FlashMap(unittest.TestCase):
    """earlier work: the tools read platform/qog_flashmap.h, and the regions do not overlap."""

    def test_offsets(self):
        self.assertEqual(mkdata.XIP_BASE + mkdata.DATA_OFFSET, 0x10100000)
        self.assertEqual(mkdata.DATA_MAX, 0xFF0000 - 0x100000)
        self.assertEqual(mkdata.XIP_BASE + mkdata.MAIN_DATA_OFFSET, 0x10200000)
        self.assertEqual(mkdata.MAIN_DATA_MAX, 0x800000 - 0x200000)

    def test_no_overlap(self):
        app = mkdata.flashmap_define("QOG_APP_OFFSET")
        self.assertGreaterEqual(app, 0x21000)                           # bootloader and metadata below
        self.assertGreater(mkdata.DATA_OFFSET, app)
        self.assertLessEqual(mkdata.DATA_OFFSET + mkdata.DATA_MAX, 0x1000000)
        self.assertLessEqual(mkdata.MAIN_DATA_OFFSET + mkdata.MAIN_DATA_MAX, 0x800000)   # the FAT volume


class Uf2(unittest.TestCase):
    def test_blocks(self):
        data = bytes(range(256)) * 3 + b"z"
        uf2 = mkdata.to_uf2(data, 0x10400000)
        self.assertEqual(len(uf2), 4 * 512)
        for n in range(4):
            b = uf2[n * 512:(n + 1) * 512]
            m0, m1, flags, addr, size, idx, count, fam = struct.unpack("<8I", b[:32])
            self.assertEqual((m0, m1), (mkdata.UF2_MAGIC0, mkdata.UF2_MAGIC1))
            self.assertEqual(addr, 0x10400000 + 256 * n)
            self.assertEqual((size, idx, count, fam), (256, n, 4, mkdata.RP2040_FAMILY))
            self.assertEqual(struct.unpack("<I", b[508:])[0], mkdata.UF2_MAGIC_END)
            self.assertTrue(flags & mkdata.UF2_FLAG_FAMILY)


if __name__ == "__main__":
    unittest.main()
