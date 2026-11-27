"""Protect the product identifiers, original donation buttons and linked local guides."""
import json
from pathlib import Path
import re
import unittest
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]


class DocumentationTests(unittest.TestCase):
    def test_hardware_and_support(self):
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for sku in ("31416", "31570"):
            self.assertRegex(readme, rf"\*\*Waveshare [^*\n]+SKU {sku}\)\*\*")
        links = json.loads((ROOT / "assets/support-links.json").read_text(encoding="utf-8"))
        for key in ("ko_fi_url", "revolut_me_url"):
            self.assertIn(f'href="{links[key]}"', readme)
        self.assertIn("docs/images/banner.jpg", readme)

    def test_linked_files(self):
        docs = sorted((ROOT / "docs").rglob("*.md")) + [ROOT / "README.md"]
        for page in docs:
            content = page.read_text(encoding="utf-8")
            targets = re.findall(r'\]\(([^)\s]+)\)|(?:src|href)="([^"]+)"', content)
            for markdown, html in targets:
                url = urlsplit(markdown or html)
                if url.scheme or url.netloc or not url.path:
                    continue
                with self.subTest(page=page.relative_to(ROOT), path=url.path):
                    self.assertTrue((page.parent / unquote(url.path)).exists())


if __name__ == "__main__":
    unittest.main()
