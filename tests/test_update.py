"""Exercise the shipped browser/packager and the device's C transaction handlers."""
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UPDATE = ROOT / "components/update"
FIXTURE = ROOT / "tests/update"
SPEC = importlib.util.spec_from_file_location("package_update", ROOT / "tools/package_update.py")
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


class CardUpdate(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.workspace = Path(self.temp.name)
        self.assets = self.workspace / "assets"
        (self.assets / "icons").mkdir(parents=True)
        (self.assets / "icons/a.png").write_bytes(b"A")
        self.gen = hashlib.sha256(PACK.manifest_bytes(PACK.entries(self.assets))).hexdigest()

    def test_firmware_cannot_ship_with_foreign_card_or_board(self):
        image = self.workspace / "image.bin"
        bundle = self.workspace / "release.zld"
        image.write_bytes(b"\xe9" + PACK.firmware_marker("31416", "rev1", self.gen))
        PACK.package(self.assets, image, "31416", "rev1", bundle)
        for sku, silicon in [("31570", "rev1"), ("31416", "rev3")]:
            with self.assertRaises(ValueError):
                PACK.package(self.assets, image, sku, silicon, bundle)
        (self.assets / "icons/a.png").write_bytes(b"altered image")
        with self.assertRaises(ValueError):
            PACK.package(self.assets, image, "31416", "rev1", bundle)

    def test_browser_reads_packager_output_and_rejects_truncation(self):
        image = self.workspace / "image.bin"
        bundle = self.workspace / "release.zld"
        image.write_bytes(b"\xe9" + PACK.firmware_marker("31416", "rev1", self.gen))
        PACK.package(self.assets, image, "31416", "rev1", bundle)
        subprocess.run(["node", str(FIXTURE / "browser.cjs"), str(UPDATE / "update.html"), str(bundle)], check=True)

    def compile_transaction(self, assets):
        # Transport stubs and an in-memory flash partition wrap the same C handlers
        # used by ESP-IDF. Card operations act on real files in the working dir.
        for header in re.findall(r'^#include "([^"]+)"', (UPDATE / "zld_update.c").read_text(), re.M):
            if header not in ("zld_update.h", "zld_asset_index.inc"):
                stub = self.workspace / header
                stub.parent.mkdir(parents=True, exist_ok=True)
                stub.touch()
        (self.workspace / "esp_http_server.h").touch()
        PACK.write_index(assets, self.workspace / "zld_asset_index.inc")
        binary = self.workspace / "transaction"
        subprocess.run(["cc", "-std=gnu17", "-Wno-deprecated-declarations", "-include", str(FIXTURE / "host.h"),
                        "-I", str(self.workspace), "-I", str(UPDATE), "-I", str(UPDATE / "include"),
                        str(FIXTURE / "transaction.c"), "-lcrypto", "-o", str(binary)], check=True)
        return binary

    def test_firmware_transaction(self):
        subprocess.run([self.compile_transaction(self.assets)], cwd=self.workspace, check=True)

    def test_shipped_catalog_delta(self):
        # Use the shipping files and their exact flash index. Seed all but one
        # content-addressed blob, then ask the device which file to upload.
        directory = ROOT / "sdcard"
        rows = PACK.entries(directory)
        missing = next(i for i, (name, _, _) in enumerate(rows) if name == "catalog.json")
        data = self.workspace / "card" / "zld" / "blobs"
        for i, (name, _, sha) in enumerate(rows):
            if i == missing:
                continue
            path = data / sha[:2] / (sha + Path(name).suffix)
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.symlink_to(directory / name)
        (self.workspace / "release.idx").write_bytes(PACK.manifest_bytes(rows))
        subprocess.run([self.compile_transaction(directory), str(missing)], cwd=self.workspace, check=True)


if __name__ == "__main__":
    unittest.main()
