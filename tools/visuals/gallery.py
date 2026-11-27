"""Generate the Hungarian README/documentation wallpaper contact sheets (Pillow)."""
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
SD = ROOT / "sdcard"
OUT = ROOT / "docs/images"
BG, FG = "#111915", "#e6eee8"
COLUMNS, SIZE, GAP, MARGIN = 8, 145, 13, 24


def render(rows, file):
    """Use the checked-in originals so every tile matches the on-device crop."""
    line = SIZE + 34
    img = Image.new("RGB", (2*MARGIN + COLUMNS*(SIZE+GAP)-GAP,
                            2*MARGIN + math.ceil(len(rows)/COLUMNS)*line), BG)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype("DejaVuSans.ttf", 15)
    for index, row in enumerate(rows):
        x = MARGIN + (index % COLUMNS)*(SIZE+GAP)
        y = MARGIN + (index // COLUMNS)*line
        with Image.open(SD / "backgrounds" / (row["id"] + ".jpg")) as source:
            img.paste(ImageOps.fit(source, (SIZE,SIZE), method=Image.Resampling.LANCZOS), (x,y))
        # Preserve complete Hungarian names; long captions wrap once.
        words = row["name"].split()
        text = ""
        lines = []
        for word in words:
            candidate = f"{text} {word}".strip()
            if text and draw.textlength(candidate,font=font)>SIZE:
                lines.append(text)
                text = word
            else:
                text = candidate
        lines.append(text)
        for i, caption in enumerate(lines):
            draw.text((x,y+SIZE+2+15*i),caption,font=font,fill=FG)
    img.save(OUT / file, "JPEG", quality=88, optimize=True)


def main():
    rows = json.loads((SD / "catalog.json").read_text(encoding="utf-8"))["backgrounds"]
    render([r for r in rows if r["category"]=="landscape"], "backgrounds.jpg")
    render([r for r in rows if r["category"]=="pattern"], "background-patterns.jpg")


if __name__ == "__main__":
    main()
