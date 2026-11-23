"""Checks the layout and XML that tools/make_wup.py produces."""
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import make_wup  # noqa: E402


class MakeWupTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp())
        self.rpx = self.tmp / "x.rpx"
        self.rpx.write_bytes(b"\x7fRPX")

    def test_xml_is_well_formed_and_asks_for_codegen(self) -> None:
        for text in (make_wup.app_xml("000500001e64e000"), make_wup.cos_xml(), make_wup.meta_xml("A & B", "P", "000500001e64e000")):
            ET.fromstring(text.encode())
        cos = ET.fromstring(make_wup.cos_xml().encode())
        self.assertEqual(cos.find("codegen_size").text, "01000000")
        self.assertEqual(len(list(cos.find("permissions"))), 19)

    def test_title_tree(self) -> None:
        out = self.tmp / "t"
        make_wup.build_title(self.rpx, out, "Conker", "me", "000500001e64c0ab", ROOT / "wiiu" / "meta")
        for rel in ("code/not64.rpx", "code/app.xml", "code/cos.xml", "meta/meta.xml", "meta/iconTex.tga",
                    "meta/bootTvTex.tga", "meta/bootDrcTex.tga", "content/not64.txt"):
            self.assertTrue((out / rel).is_file(), rel)

    def test_bad_title_id(self) -> None:
        with self.assertRaises(ValueError):
            make_wup.build_title(self.rpx, self.tmp / "t", "n", "p", "nothex", ROOT / "wiiu" / "meta")

    def test_rom_goes_to_sd_not_title(self) -> None:
        rom = self.tmp / "My Game (E).z64"
        rom.write_bytes(b"x" * 16)
        sd = self.tmp / "sd"
        make_wup.write_sd_files(sd, rom, None)
        self.assertEqual((sd / "not64" / "autoboot.txt").read_text().strip(), "roms/My-Game--E-.z64")
        self.assertTrue((sd / "not64" / "roms" / "My-Game--E-.z64").is_file())

    def test_rom_format(self) -> None:
        rom = self.tmp / "a.z64"
        rom.write_bytes(b"\x80\x37\x12\x40" + b"\0" * 8)
        self.assertEqual(make_wup.rom_format(rom), "z64")
        rom.write_bytes(b"nope" + b"\0" * 8)
        self.assertIsNone(make_wup.rom_format(rom))


if __name__ == "__main__":
    unittest.main()
