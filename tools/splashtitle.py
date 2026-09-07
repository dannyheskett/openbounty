#!/usr/bin/env python3
"""Compose the title screen (art/ui/splash_title.png, 256x164): the generated
eagle standard (build/art/splash_title/run01/01_raw.png) with the title
words rendered locally from C059 Bold at 1-bit, gold with a dark offset
shading, in the empty band across the top. Same route as the publisher
splash (tools/splashlogo.py): generated lettering garbles, drawn lettering
does not.

    python3 tools/splashtitle.py <eagle.png> <out.png>
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/opentype/urw-base35/C059-Bold.otf"
GOLD = (236, 200, 90, 255)
GOLD_DK = (150, 100, 20, 255)
INK = (60, 30, 70, 255)


def glyph_mask(text, size):
    f = ImageFont.truetype(FONT, size)
    m = Image.new("L", (400, 80), 0)
    ImageDraw.Draw(m).text((4, 4), text, font=f, fill=255)
    m = m.point(lambda v: 255 if v >= 128 else 0)
    return m.crop(m.getbbox())


def bitmap_text(text, size, col, shade):
    m = glyph_mask(text, size)
    w, h = m.size
    out = Image.new("RGBA", (w + 3, h + 3), (0, 0, 0, 0))
    for off in ((1, 1), (2, 2)):
        out.paste(Image.new("RGBA", m.size, shade), off, m)
    out.paste(Image.new("RGBA", m.size, col), (0, 0), m)
    return out


def compose(eagle_path):
    base = Image.open(eagle_path).convert("RGBA")
    W, H = base.size
    line1 = bitmap_text("OPEN BOUNTY", 20, GOLD, GOLD_DK)
    line2 = bitmap_text("THE GLORY OF ROME", 13, GOLD, GOLD_DK)
    # the title in the clear band above the eagle, the subtitle across the
    # pole at the foot of the picture; a dark halo keeps both legible
    for im, y in ((line1, 2), (line2, H - line2.height - 4)):
        x = (W - im.width) // 2
        halo = Image.new("RGBA", im.size, INK)
        for off in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            base.paste(halo, (x + off[0], y + off[1]), im)
        base.alpha_composite(im, (x, y))
    return base


if __name__ == "__main__":
    compose(sys.argv[1]).save(sys.argv[2])
    print("wrote", sys.argv[2])
