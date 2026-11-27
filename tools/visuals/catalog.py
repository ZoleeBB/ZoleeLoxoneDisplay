"""Regenerate the SD catalog and web thumbnails from the checked-in artwork."""
from pathlib import Path
import json

from PIL import Image
from palettes import PALETTES, SD

HERE = Path(__file__).resolve().parent


def table(name):
    return [row.split("\t") for row in (HERE / name).read_text(encoding="utf-8").splitlines()]


def main():
    backgrounds = table("landscapes.tsv") + [[row[0], row[1]] for row in PALETTES]
    icons = table("icons.tsv")
    catalog = {"backgrounds": [], "icons": []}
    (SD / "thumbs").mkdir(exist_ok=True)
    for asset_id, label in backgrounds:
        thumb = f"thumbs/{asset_id}.jpg"
        with Image.open(SD / "backgrounds" / (asset_id + ".jpg")) as im:
            if im.size != (720, 720) or im.mode != "RGB" or im.format != "JPEG" or im.info.get("progressive"):
                raise ValueError(f"{asset_id} must be a 720×720 baseline RGB JPEG")
            im.resize((120, 120), Image.Resampling.LANCZOS).save(
                SD / thumb, "JPEG", quality=82, optimize=True, progressive=False
            )
        catalog["backgrounds"].append({"id": asset_id, "name": label, "thumbnail": thumb})
    for asset_id, _, label in icons:
        with Image.open(SD / "icons" / (asset_id + ".png")) as im:
            if im.mode != "RGBA" or im.size != (64, 64) or im.getchannel("A").getextrema() != (0, 255):
                raise ValueError(f"{asset_id} must be a transparent 64×64 RGBA PNG")
        catalog["icons"].append({"id": asset_id, "name": label})
    for key, rows in catalog.items():
        if len({row["id"] for row in rows}) != len(rows):
            raise ValueError(f"Duplicate {key} identifier")
    (SD / "catalog.json").write_text(
        json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))+"\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
