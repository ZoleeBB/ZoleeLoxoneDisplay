"""Render the original square wallpapers. Requires Python, Pillow and NumPy."""
from pathlib import Path

import numpy as np
from PIL import Image

SD = Path(__file__).resolve().parents[2] / "sdcard"
# identifier, Hungarian label, surface, shadow, base, light
PALETTES = [
    ("peacock-silk", "Pávaselyem", "silk", "101f2c", "276466", "87a290"),
    ("mulberry-silk", "Szederbordó", "silk", "261c31", "633950", "b67977"),
    ("slate-silk", "Kék palakő", "silk", "182637", "395772", "8aa1ad"),
    ("honey-silk", "Borostyán", "silk", "332923", "795338", "b89a66"),
    ("basalt-fold", "Hajtogatott bazalt", "fold", "171b22", "363f45", "76898d"),
    ("deep-sea-fold", "Óceánkék", "fold", "091f35", "155367", "53a9aa"),
    ("moss-fold", "Mohazöld", "fold", "202d2a", "546b49", "a3ae77"),
    ("terracotta-fold", "Terrakotta", "fold", "332325", "854d42", "c38b6e"),
    ("fjord-lines", "Fjordvonalak", "contour", "122d34", "365d61", "9ab3aa"),
    ("copper-lines", "Rézmetszet", "contour", "301d1c", "6a4141", "cd927a"),
    ("violet-lines", "Ametiszt", "contour", "261f33", "58446d", "a893c1"),
    ("marine-lines", "Tengerrajz", "contour", "19243b", "354c78", "83a9c6"),
    ("olive-hills", "Olajliget", "hills", "27352b", "687651", "abad7c"),
    ("umber-hills", "Rézhegyek", "hills", "3f2930", "935950", "e1b28a"),
    ("indigo-hills", "Indigóhorizont", "hills", "182a40", "506588", "9ca7c9"),
    ("rose-hills", "Rózsakvarc", "hills", "423347", "8f637f", "d7a7a6"),
    ("obsidian-orbit", "Obszidián", "orbit", "15191e", "303a42", "677580"),
    ("forest-orbit", "Erdei pálya", "orbit", "172c2e", "326351", "86a880"),
    ("ochre-orbit", "Okkersárga", "orbit", "352e28", "7c6745", "c6b075"),
    ("azure-orbit", "Azúr", "orbit", "10283d", "315d79", "84b6c1"),
    ("oat-linen", "Nyári lenvászon", "linen", "3d342a", "8f8067", "c8b599"),
    ("petrol-linen", "Petrolkék szövet", "linen", "132d31", "3b686a", "70a5a1"),
    ("berry-linen", "Szederszövet", "linen", "362535", "785468", "b794a4"),
    ("walnut-slats", "Diófa lamellák", "slats", "291f1a", "70513f", "ae8665"),
    ("graphite-slats", "Grafitlamellák", "slats", "1c252b", "45545b", "849097"),
    ("sage-slats", "Zsályalamellák", "slats", "2a362e", "606f5d", "a7b59b"),
    ("shell-terrazzo", "Kagylókő", "terrazzo", "443b36", "9c8776", "d8c0a3"),
    ("mint-terrazzo", "Mentamozaik", "terrazzo", "30403c", "6f9282", "adc5af"),
    ("night-terrazzo", "Éjféli mozaik", "terrazzo", "192533", "485e75", "8195ad"),
    ("ember-tiles", "Parázscsempe", "tile", "452b2b", "895249", "be8070"),
    ("lagoon-tiles", "Lagúnacsempe", "tile", "173534", "447473", "85aaa0"),
    ("plum-tiles", "Szilvacsempe", "tile", "2e2330", "69536e", "a189a5"),
]


def render(style, hex_colors, seed):
    n = 720
    y, x = np.mgrid[0:n, 0:n] / (n - 1)
    rng = np.random.default_rng(seed)
    phase = rng.uniform(0, 2*np.pi)
    x = x if seed % 2 else 1 - x
    if style == "silk":
        wave = 2.4*x + 3.1*y + 1.3*np.sin(2.8*x - 1.7*y + phase)
        tone = .48 + .20*np.cos(wave) + .16*np.sin(2.6*wave + .5) + .05*np.sin(6.5*wave)
    elif style == "fold":
        tone = .54 - .15*y + .12*x
        crease = .83*x + .22 + .10*np.sin(phase)
        for edge, slope, light in [(crease, .52, .27), (1.05 - .65*x, .36, -.23), (.52 - .36*x, .32, -.17)]:
            distance = y - edge
            tone += np.where(distance > 0, light * np.exp(-distance / slope), 0)
    elif style == "contour":
        height = np.sin(2.7*x + .5*phase) + np.cos(3.3*y + .4) + .4*np.sin(5*x - 3.3*y)
        tone = .45 + .11*height - .09*y
        # Symmetric contour strokes only a few pixels wide at the native size.
        fraction = np.abs((height*6 + .5) % 1 - .5)
        tone += .19*np.exp(-((fraction/.067)**2))
    elif style == "hills":
        tone = .78 - .21*y + .08*np.cos(x*2 + phase)
        for layer in range(5):
            ridge = (.38 + .113*layer + (.026 + .009*layer)*np.sin((4.4 + layer*.4)*x + phase + layer)
                     + .022*np.sin(9*x + layer))
            tone = np.where(y > ridge, .67 - .125*layer - .08*(y-ridge), tone)
    elif style == "orbit":
        distance = np.hypot(x-(.98 + .18*np.sin(phase)), y-(.82 + .13*np.cos(phase)))
        tone = .21 + .34*x + .12*(1-y)
        for radius, gain in [(.96,.16), (.71,-.24), (.48,.19), (.27,-.16)]:
            delta = radius - distance
            tone += np.where(delta > 0, gain*np.exp(-np.maximum(delta,0)/.27), 0)

    elif style == "linen":
        warp = np.cos(2*np.pi*(x*66 + .06*np.sin(y*8 + phase)))
        weft = np.cos(2*np.pi*(y*50 + .09*np.sin(x*7)))
        tone = .44 + .09*(1-y) + .05*x + .045*warp + .039*weft + .028*warp*weft
    elif style == "slats":
        across = (.91*x + .34*y)*11 + .12*np.cos(phase)
        fraction = across % 1
        tone = (.52 + .08*(1-y) - .08*x - .26*np.exp(-(fraction/.045)**2)
                + .13*np.exp(-((fraction-.11)/.085)**2) - .045*fraction
                + .009*np.cos(2*np.pi*(3*across + .14*np.sin(5*y + phase))))
    elif style == "terrazzo":
        tone = .46 + .065*(1-y) + .03*x
        # Small, irregular stone chips; limit each polygon to its own bounding box.
        for _ in range(280):
            cx, cy = rng.uniform(0,n,2)
            radius = rng.uniform(2.0,9.0)
            left, right = max(0,int(cx-radius-1)), min(n,int(cx+radius+2))
            top, bottom = max(0,int(cy-radius-1)), min(n,int(cy+radius+2))
            yy, xx = np.mgrid[top:bottom,left:right]
            count = rng.integers(3,7)
            angles = np.arange(count)*2*np.pi/count + rng.uniform(-.20,.20,count)
            lengths = radius*rng.uniform(.62,1,count)
            vx, vy = cx + lengths*np.cos(angles), cy + lengths*np.sin(angles)
            inside = np.ones(xx.shape,dtype=bool)
            for i in range(count):
                j = (i+1) % count
                inside &= (vx[j]-vx[i])*(yy-vy[i]) >= (vy[j]-vy[i])*(xx-vx[i])
            tone[top:bottom,left:right] += inside*rng.choice([-.17, -.10, .14, .21])
    elif style == "tile":
        # Offset arches meet at the rim of each row, like glazed scallop tiles.
        u, v = x*7, y*6
        row = np.floor(v)
        local_x = (u + .5*(row % 2)) % 1 - .5
        arch = (v % 1) - (.92 - np.sqrt(np.maximum(.25-local_x**2,0)))
        tone = (.49 + .055*(1-y) + .035*x + .075*np.sin(4*local_x + phase)
                - .17*np.exp(-(arch/.026)**2) + .10*np.exp(-((arch-.048)/.036)**2))
    else:
        raise ValueError(f"Unknown wallpaper style: {style}")

    # A little fine grain suppresses banding on both JPEG thumbnails and RGB565 panels.
    tone = np.clip(tone + rng.normal(0, .004, x.shape), 0, 1)
    stops = np.array([list(bytes.fromhex(color)) for color in hex_colors])
    pixels = np.stack([np.interp(tone, [0,.48,1], stops[:,i]) for i in range(3)],axis=-1)
    return Image.fromarray(np.uint8(np.clip(pixels, 0, 255)))


if __name__ == "__main__":
    (SD / "backgrounds").mkdir(parents=True, exist_ok=True)
    for index, (name, _, style, *colors) in enumerate(PALETTES):
        render(style, colors, index).save(SD / "backgrounds" / (name+".jpg"), "JPEG",
                                           quality=86, optimize=True, progressive=False)
