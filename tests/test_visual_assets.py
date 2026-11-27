"""Check the exact SD image formats consumed by the panel and web editor (stdlib only)."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import unittest

SD = Path(__file__).resolve().parents[1] / "sdcard"


def jpeg_header(path):
    data = path.read_bytes()
    if data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
        raise ValueError(f"{path}: incomplete JPEG")
    offset = 2
    while offset + 4 < len(data) and data[offset] == 0xff:
        marker = data[offset + 1]
        size = int.from_bytes(data[offset + 2:offset + 4], "big")
        if size < 2:
            raise ValueError(f"{path}: invalid marker size")
        if marker in {0xc0, 0xc1, 0xc2}:
            depth, height, width, channels = struct.unpack_from(">BHHB", data, offset + 4)
            return marker, depth, width, height, channels
        offset += 2 + size
    raise ValueError(f"{path}: missing JPEG frame header")


class PanelImages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads((SD / "catalog.json").read_text(encoding="utf-8"))

    def check_catalog(self, key, suffix):
        rows = self.catalog[key]
        identifiers = [r["id"] for r in rows]
        self.assertEqual(len(identifiers), len(set(identifiers)))
        self.assertTrue(all(r["name"].strip() for r in rows))
        self.assertTrue(all(re.fullmatch(r"[a-z0-9-]{1,32}", i) for i in identifiers))
        self.assertEqual({p.stem for p in (SD / key).glob(f"*.{suffix}")}, set(identifiers))
        return rows

    def test_wallpapers(self):
        rows = self.check_catalog("backgrounds", "jpg")
        categories = Counter(r["category"] for r in rows)
        self.assertEqual(set(categories), {"landscape", "pattern"})
        self.assertGreaterEqual(categories["landscape"], 32)
        self.assertGreaterEqual(categories["pattern"], 32)
        self.assertEqual(len(set(r["name"] for r in rows)), len(rows))
        digests = set()
        for row in rows:
            name = row["id"]
            with self.subTest(name=name):
                original = SD / "backgrounds" / (name + ".jpg")
                digest = hashlib.sha256(original.read_bytes()).digest()
                self.assertNotIn(digest, digests, "Duplicate full-size artwork")
                digests.add(digest)
                thumb = row["thumbnail"]
                self.assertEqual(thumb, f"thumbs/{name}.jpg")
                for path, size in [(original,720), (SD / thumb,120)]:
                    self.assertEqual(jpeg_header(path), (0xc0, 8, size, size, 3))
        self.assertEqual({r["thumbnail"] for r in rows}, {p.relative_to(SD).as_posix() for p in (SD / "thumbs").glob("*.jpg")})

    def test_icons(self):
        rows = self.check_catalog("icons", "png")
        self.assertTrue({"home", "energy"}.issubset({r["id"] for r in rows}))
        for row in rows:
            with self.subTest(name=row["id"]):
                png = (SD / "icons" / (row["id"] + ".png")).read_bytes()
                self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
                self.assertEqual(png[12:16], b"IHDR")
                self.assertEqual(struct.unpack_from(">IIBBBBB", png, 16), (64, 64, 8, 6, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
