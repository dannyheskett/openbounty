#!/usr/bin/env python3
"""Compose the publisher splash (art/ui/splash_logo.png, 320x84, transparent)
from locally rendered C059 Bold lettering and one generated 44x44 emblem.

    python3 tools/splashlogo.py build/art/splash_logo_emblem/run02/01_raw.png build/art/splash_logo_new.png
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/opentype/urw-base35/C059-Bold.otf"


def glyph_mask(text, size):
    f = ImageFont.truetype(FONT, size)
    m = Image.new("L", (400, 80), 0)
    ImageDraw.Draw(m).text((4, 4), text, font=f, fill=255)
    m = m.point(lambda v: 255 if v >= 128 else 0)
    return m.crop(m.getbbox())


def bitmap_text(text, size):
    m = glyph_mask(text, size)
    w, h = m.size
    out = Image.new("RGBA", (w + 3, h + 3), (0, 0, 0, 0))
    for off in ((1, 1), (2, 2), (0, 2), (2, 0)):
        out.paste(Image.new("RGBA", m.size, (180, 20, 20, 255)), off, m)
    out.paste(Image.new("RGBA", m.size, (255, 255, 255, 255)), (0, 0), m)
    return out


def coin(d, x, y, r):
    d.ellipse((x - r, y - r, x + r, y + r), fill=(214, 160, 40, 255), outline=(120, 80, 10, 255))
    d.ellipse((x - r + 2, y - r + 2, x - r + 4, y - r + 4), fill=(255, 236, 150, 255))


def star(d, x, y, c):
    for i in range(-3, 4):
        d.point((x + i, y), c)
        d.point((x, y + i), c)


def compose(emblem_path):
    canvas = Image.new("RGBA", (320, 84), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)
    left = bitmap_text("Dan", 34)
    right = bitmap_text("Heskett", 34)
    presents = bitmap_text("Presents...", 24)
    emb = Image.open(emblem_path).convert("RGBA")
    gap = 6
    total = left.width + gap + emb.width + gap + right.width
    x0 = (320 - total) // 2
    canvas.paste(left, (x0, 16), left)
    ex = x0 + left.width + gap
    canvas.alpha_composite(emb, (ex, 4))
    canvas.paste(right, (ex + emb.width + gap, 16), right)
    canvas.paste(presents, ((320 - presents.width) // 2, 58), presents)
    coin(d, 60, 58, 7)
    coin(d, 262, 52, 5)
    coin(d, 292, 44, 4)
    for (x, y, c) in ((88, 50, (90, 160, 255, 255)), (120, 60, (90, 160, 255, 255)),
                      (226, 58, (255, 90, 220, 255)), (250, 36, (90, 160, 255, 255))):
        star(d, x, y, c)
    return canvas


if __name__ == "__main__":
    compose(sys.argv[1]).save(sys.argv[2])
    print("wrote", sys.argv[2])
