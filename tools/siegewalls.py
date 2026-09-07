#!/usr/bin/env python3
"""Remake the Rome pack's combat set at the 96x96 cell from the original
48x34 pieces, programmatically: each original pixel is classified into a
material, the material map is scaled to the new cell (2x across, 96/34
down), and every material is re-rendered at pixel scale with the rules
pixel artists use (grout first, bricks in a three-tone bevel, two-tone
merlons over a black shadow band, a lighter top face, dithered banks,
scattered rubble). Layout, proportions, the moat's wobble, the corner step
and the breach diagonal therefore match the original exactly.

Output, art/combat/:
    castle_wall_01..06      as the engine's siege layout uses them
    castle_wall_back, _back_l, _back_r   the top-down back wall band
    castle_spike, obstacle_01..03, cursor_01..04, field_grass

    python3 tools/siegewalls.py [pack-dir] [reference-pack-dir]
"""
import math
import os
import shutil
import sys
from PIL import Image, ImageDraw

PACK = sys.argv[1] if len(sys.argv) > 1 else "assets/glory-of-rome"
REF = sys.argv[2] if len(sys.argv) > 2 else "assets/kings-bounty"
OUT = os.path.join(PACK, "art", "combat")
RC = os.path.join(REF, "art", "combat")
N = 96
CLEAR = (0, 0, 0, 0)

# ---- palette (the original's stone, black and moat) ---------------------------
MOAT = (93, 97, 255, 255)
MOAT_DK = (0, 0, 154, 255)
MOAT_LT = (150, 152, 255, 255)
BLACK = (0, 0, 0, 255)
GROUT = (32, 32, 32, 255)
DARKLINE = (44, 44, 44, 255)
STONE = (105, 105, 105, 255)
STONE_DK = (77, 77, 77, 255)
GREY = (85, 85, 85, 255)
HILITE = (121, 121, 121, 255)
MER_BRIGHT = (223, 223, 223, 255)
MER_LIGHT = (178, 178, 178, 255)
OLIVE = (65, 65, 0, 255)
BROWN = (138, 89, 48, 255)
BROWN_DK = (40, 32, 12, 255)
CHAR = (60, 60, 60, 255)
TREE = (0, 89, 89, 255)
TREE_DK = (0, 65, 65, 255)
TRUNK = (130, 93, 0, 255)
POND_DK = (0, 93, 158, 255)


def lcg(seed):
    s = seed & 0xFFFFFFFF
    while True:
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        yield s


# ---- classify the original --------------------------------------------------

def classify(p):
    """Material code for one original pixel; an unknown opaque colour is
    kept as itself (a tuple), so nothing the original drew is lost."""
    r, g, b, a = p
    if a == 0:
        return "."
    if (r, g, b) == (0, 0, 0):
        return "#"
    if b > 200 and r < 140:
        return "W"                       # moat water
    if (r, g, b) in ((0, 0, 154), (0, 125, 207), (0, 93, 158)):
        return "w"                       # dark water edge
    if g > 140 and r < 100 and b < 100:
        return " "                       # grass
    if (r, g, b) == (223, 223, 223):
        return "M"
    if (r, g, b) == (178, 178, 178):
        return "m"
    if (r, g, b) == (121, 121, 121):
        return "h"
    if (r, g, b) == (105, 105, 105):
        return "S"
    if (r, g, b) == (85, 85, 85):
        return "s"
    if (r, g, b) == (77, 77, 77):
        return "d"
    if (r, g, b) in ((32, 32, 32), (44, 44, 44)):
        return "g"
    if (r, g, b) == (65, 65, 0):
        return "o"
    if (r, g, b) == (60, 60, 60):
        return "c"
    if r > 100 and g < 100 and b < 80:
        return "B"                       # brown rubble
    if (r, g, b) == (0, 89, 89):
        return "T"
    if (r, g, b) == (0, 65, 65):
        return "t"
    if (r, g, b) == (130, 93, 0):
        return "k"                       # trunk
    return (r, g, b, 255)


def material_map(name):
    im = Image.open(os.path.join(RC, name + ".png")).convert("RGBA")
    px = im.load()
    return [[classify(px[x, y]) for x in range(im.width)] for y in range(im.height)]


def scaled(mm):
    """Nearest-neighbour scale of a material map to N x N."""
    h, w = len(mm), len(mm[0])
    return [[mm[min(h - 1, y * h // N)][min(w - 1, x * w // N)] for x in range(N)] for y in range(N)]


# ---- re-render materials ------------------------------------------------------

FLAT = {"#": BLACK, "M": MER_BRIGHT, "m": MER_LIGHT, "h": HILITE, "S": STONE, "s": GREY,
        "d": STONE_DK, "g": GROUT, "o": OLIVE, "c": CHAR, "B": BROWN, "T": TREE, "t": TREE_DK,
        "k": TRUNK, "?": None, ".": None, " ": None}


def render(mm, seed=1, relay_bricks=True):
    """Draw a scaled material map at N x N with pixel-scale detail."""
    sm = scaled(mm)
    im = Image.new("RGBA", (N, N), CLEAR)
    px = im.load()
    r = lcg(seed)
    # 1. flat pass
    for y in range(N):
        for x in range(N):
            c = sm[y][x]
            if isinstance(c, tuple):
                px[x, y] = c
            elif c == "W":
                px[x, y] = MOAT
            elif c == "w":
                px[x, y] = MOAT_DK
            elif FLAT.get(c):
                px[x, y] = FLAT[c]
    # 2. moat: ragged bank (dither the water/grass boundary) and ripples
    for y in range(N):
        for x in range(N):
            if isinstance(sm[y][x], str) and sm[y][x] in "Ww":
                nb = [sm[yy][xx] for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1))
                      if 0 <= yy < N and 0 <= xx < N]
                if " " in nb or "." in nb:
                    px[x, y] = MOAT_DK if (x + y) % 2 == 0 else (CLEAR if next(r) % 3 == 0 else MOAT_DK)
                elif sm[y][x] == "W" and next(r) % 23 == 0:
                    for k in range(3):
                        if x + k < N and sm[y][x + k] == "W":
                            px[x + k, y] = MOAT_LT
    # 3. brick faces: re-lay courses inside the brick mask (S/d/g/o runs below a grout line)
    if relay_bricks:
        brick = [[isinstance(sm[y][x], str) and sm[y][x] in "Sdgo" and y >= 40 for x in range(N)] for y in range(N)]
        course = 8
        row = 0
        for y0 in range(40, N, course):
            off = 6 if row % 2 else 0
            for x0 in range(-off, N, 12):
                for y in range(y0, min(y0 + course, N)):
                    tone = STONE if y - y0 < 3 else (STONE_DK if y - y0 < 6 else GROUT)
                    for x in range(x0, x0 + 12):
                        if 0 <= x < N and brick[y][x]:
                            if x == x0 + 11 or x == x0 + 10:
                                px[x, y] = GROUT
                            else:
                                px[x, y] = tone
                # an olive stone now and then
                if next(r) % 7 == 0 and 0 <= x0 + 2 < N:
                    for y in range(y0 + 3, min(y0 + 6, N)):
                        for x in range(x0 + 2, x0 + 9):
                            if 0 <= x < N and brick[y][x]:
                                px[x, y] = OLIVE
            row += 1
    return im



# ---- the moat as a shape ----------------------------------------------------
MOAT_A, MOAT_B = 8, 30          # the water band lies between these, from the outer edge
R_OUT = 38                      # rounded outer corner radius
R_IN = 14                       # rounded inner bank radius


def wobble(t, phase=0.0):
    """Bank wander along a run. Period N, so neighbouring cells continue it."""
    return round(2.5 * math.sin(2 * math.pi * t / N + phase) + 1.5 * math.sin(4 * math.pi * t / N + 2.1 + phase))


def water_side(xo, y):
    """Water for a run along the left edge (xo measured from that edge)."""
    return MOAT_A + wobble(y, 0.7) <= xo < MOAT_B + wobble(y, 2.9)


def water_top(x, y):
    return MOAT_A + wobble(x, 1.3) <= y < MOAT_B + wobble(x, 4.0)


def water_corner(xo, y):
    """The moat turning a corner: outside both outer banks, inside either
    inner bank (or the fillet that rounds the inner corner), and inside the
    arc that rounds the outer corner."""
    if xo < MOAT_A + wobble(y, 0.7) or y < MOAT_A + wobble(xo, 1.3):
        return False                                   # beyond an outer bank
    if xo < R_OUT and y < R_OUT and (R_OUT - xo) ** 2 + (R_OUT - y) ** 2 > R_OUT * R_OUT:
        return False                                   # the rounded outer corner
    if xo < MOAT_B + wobble(y, 2.9) or y < MOAT_B + wobble(xo, 4.0):
        return True                                    # inside an inner bank
    cx = cy = MOAT_B + R_IN
    return xo < cx and y < cy and (cx - xo) ** 2 + (cy - y) ** 2 > R_IN * R_IN   # the inner fillet


def paint_moat(im, member, left=True):
    """Fill the member(xo, y) region with water and give it a clean two-pixel
    dark bank, as the original: flat water, dark edge, the odd lighter fleck."""
    px = im.load()
    water = [[member(x if left else N - 1 - x, y) for x in range(N)] for y in range(N)]
    r = lcg(5)
    for y in range(N):
        for x in range(N):
            if not water[y][x]:
                continue
            edge = False
            for dy in (-2, -1, 0, 1, 2):
                for dx in (-2, -1, 0, 1, 2):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < N and 0 <= yy < N and not water[yy][xx] and abs(dx) + abs(dy) <= 2:
                        edge = True
            px[x, y] = MOAT_DK if edge else (POND_DK if next(r) % 41 == 0 else MOAT)
    return im


def clear_moat_region(im, left=True):
    """Remove the scaled original's moat pixels so the shaped moat replaces them."""
    px = im.load()
    for y in range(N):
        for x in range(N):
            if px[x, y] in (MOAT, MOAT_DK, MOAT_LT, POND_DK, CLEAR):
                px[x, y] = CLEAR
    return im


def flip(im):
    return im.transpose(Image.FLIP_LEFT_RIGHT)


def wall_piece(name, seed):
    im = render(material_map(name), seed)
    if name in ("castle_wall_04", "castle_wall_01"):
        paint_moat(clear_moat_region(im), water_side, left=True)
    if name in ("castle_wall_05", "castle_wall_02"):
        paint_moat(clear_moat_region(im), water_side, left=False)
    return im


def back_wall():
    # the side wall turned so the moat lies along the top
    im = render(material_map("castle_wall_04"), 21)
    clear_moat_region(im)
    im = im.transpose(Image.ROTATE_270)   # left edge to the top
    return paint_moat(im, lambda xo, y: water_top(xo, y))


def back_corner(left):
    """The band's end cell: the back wall across the top joined to the side
    wall coming down, with the moat turning the corner on a curve: a rounded
    outer corner at the cell's corner and a rounded inner bank, both with
    the same ragged dithered edge as the straight runs."""
    top = back_wall()
    side = wall_piece("castle_wall_04" if left else "castle_wall_05", 23)
    im = Image.new("RGBA", (N, N), CLEAR)
    im.alpha_composite(side)
    mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(mask).rectangle((0, 0, N - 1, 67), fill=255)
    im.paste(top, (0, 0), mask)
    # the two wall bands meet on a mitre: in the corner square where both
    # bands run (36..67 on each axis, measured from the outer edge), pixels
    # below the diagonal belong to the side band, above it to the back band,
    # so every stripe of one band turns the corner into the same stripe of
    # the other.
    sp, tp, ip = side.load(), top.load(), im.load()
    for y in range(36, 68):
        for xo in range(0, 68):
            x = xo if left else N - 1 - xo
            if xo < 36:
                ip[x, y] = sp[x, y]                       # outside the side wall: its moat gap
            else:
                ip[x, y] = sp[x, y] if (y - 36) > (xo - 36) else tp[x, y]
    clear_moat_region(im)
    paint_moat(im, water_corner, left=left)
    return im


def soften(im, seed=7):
    """Dither the boundary between any two different colours so a scaled
    piece does not read as 2x blocks: along each boundary, swap every other
    pixel with its neighbour's colour."""
    px = im.load()
    r = lcg(seed)
    src = im.copy().load()
    for y in range(1, N - 1):
        for x in range(1, N - 1):
            here = src[x, y]
            for nx, ny in ((x + 1, y), (x, y + 1)):
                there = src[nx, ny]
                if there != here and (x + y) % 2 == 0 and next(r) % 2 == 0:
                    px[x, y] = there
    return im


def obstacle(name, seed):
    return soften(render(material_map(name), seed, relay_bricks=False), seed)



# ---- obstacles, drawn: rubble, bushes, a broken wall ----------------------------

BUSH = (34, 110, 40, 255)
BUSH_LT = (70, 150, 60, 255)
BUSH_DK = (18, 70, 28, 255)
SHADOW = (0, 0, 0, 90)


def cast_shadow(d, box):
    x0, y0, x1, y1 = box
    d.ellipse((x0, y0, x1, y1), fill=SHADOW)


def stone_block(d, box, seed, tones=(HILITE, STONE, STONE_DK)):
    """One rounded rubble stone with a lit top, mid body, dark underside and an ink edge."""
    x0, y0, x1, y1 = box
    d.rounded_rectangle((x0, y0, x1, y1), radius=3, fill=tones[1], outline=GROUT)
    d.line((x0 + 2, y0 + 1, x1 - 2, y0 + 1), fill=tones[0])
    d.line((x0 + 2, y1 - 1, x1 - 2, y1 - 1), fill=tones[2])
    d.line((x1 - 1, y0 + 2, x1 - 1, y1 - 2), fill=tones[2])


def rubble():
    """A heap of fallen masonry: large blocks on top of small debris, olive
    stones among them, a shadow underneath."""
    im = Image.new("RGBA", (N, N), CLEAR)
    d = ImageDraw.Draw(im)
    r = lcg(51)
    cast_shadow(d, (10, 58, 88, 90))
    for _ in range(90):
        x, y = 14 + next(r) % 68, 44 + next(r) % 42
        if (x - 48) ** 2 / 36 ** 2 + (y - 68) ** 2 / 20 ** 2 < 1:
            d.point((x, y), fill=(STONE_DK, GROUT, OLIVE, STONE)[next(r) % 4])
    blocks = [(16, 62, 40, 78), (44, 66, 70, 82), (30, 48, 56, 64), (60, 52, 82, 66),
              (22, 74, 44, 86), (52, 76, 78, 88), (38, 36, 60, 50), (66, 42, 84, 54)]
    for i, b in enumerate(blocks):
        tone = (HILITE, STONE, STONE_DK) if i % 3 else (STONE, STONE_DK, GROUT)
        if i == 4:
            tone = (OLIVE, OLIVE, GROUT)
        stone_block(d, b, seed=i, tones=tone)
    return im


def bushes():
    """A clump of rounded bushes with a lit crown, a dark underside, leaf
    flecks, and a shadow on the ground."""
    im = Image.new("RGBA", (N, N), CLEAR)
    d = ImageDraw.Draw(im)
    r = lcg(61)
    cast_shadow(d, (12, 66, 84, 90))
    for (cx, cy, rx, ry) in ((30, 60, 22, 18), (62, 56, 24, 20), (46, 44, 18, 15), (46, 70, 16, 12)):
        d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=BUSH, outline=BUSH_DK)
        d.chord((cx - rx, cy - ry, cx + rx, cy + ry), 20, 160, fill=BUSH_DK)
        d.ellipse((cx - rx + 4, cy - ry + 3, cx + rx - 8, cy - 2), fill=BUSH_LT)
        for _ in range(40):
            x, y = cx - rx + next(r) % (2 * rx), cy - ry + next(r) % (2 * ry)
            if (x - cx) ** 2 / rx ** 2 + (y - cy) ** 2 / ry ** 2 < 0.8:
                d.point((x, y), fill=(BUSH_LT, BUSH_DK, BUSH)[next(r) % 3])
    return im


def broken_wall():
    """A stub of ruined wall: brick courses standing on a base, the top edge
    broken to a jagged line, rubble at its feet."""
    im = Image.new("RGBA", (N, N), CLEAR)
    d = ImageDraw.Draw(im)
    px = im.load()
    r = lcg(71)
    cast_shadow(d, (8, 70, 90, 92))
    x0, x1 = 14, 82
    tops = []
    h = 40
    for x in range(x0, x1):
        if next(r) % 5 == 0:
            h += (next(r) % 9) - 4
        h = max(28, min(56, h))
        tops.append(h)
    course = 8
    row = 0
    for y in range(24, 86, course):
        off = 6 if row % 2 else 0
        for bx in range(x0 - off, x1, 12):
            for x in range(bx, bx + 12):
                if not (x0 <= x < x1):
                    continue
                for yy in range(max(y, tops[x - x0]), min(y + course, 84)):
                    tone = HILITE if yy - y < 2 else (STONE if yy - y < 5 else STONE_DK)
                    if x >= bx + 10 or yy >= y + course - 1:
                        tone = GROUT
                    px[x, yy] = tone
        row += 1
    for x in range(x0, x1):
        t = tops[x - x0]
        px[x, t] = GROUT
        px[x, t + 1] = MER_LIGHT
    d.line((x0, tops[0], x0, 84), fill=GROUT)
    d.line((x1 - 1, tops[-1], x1 - 1, 84), fill=GROUT)
    d.rectangle((x0, 84, x1 - 1, 87), fill=GROUT)
    for _ in range(60):
        x, y = 6 + next(r) % 84, 80 + next(r) % 12
        px[x, y] = (STONE_DK, STONE, GROUT, OLIVE)[next(r) % 4]
    for b in ((4, 78, 16, 86), (78, 80, 92, 88), (40, 84, 54, 92)):
        stone_block(d, b, 1)
    return im


def burst():
    return soften(render(material_map("castle_spike"), 31, relay_bricks=False), 31)


def cursor(name):
    im = Image.open(os.path.join(RC, name + ".png")).convert("RGBA")
    # the ring, re-drawn at the new size from its bounding box
    bb = im.getbbox()
    col = None
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            if px[x, y][3] and col is None:
                col = px[x, y]
    out = Image.new("RGBA", (N, N), CLEAR)
    d = ImageDraw.Draw(out)
    w = (bb[2] - bb[0]) * 2
    h = round((bb[3] - bb[1]) * N / 34)
    rad = min(w, h) // 2
    for k in range(3):
        d.ellipse((48 - rad + k, 48 - rad + k, 48 + rad - k, 48 + rad - k), outline=col)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    pieces = {
        "castle_wall_01": wall_piece("castle_wall_01", 1),
        "castle_wall_02": wall_piece("castle_wall_02", 2),
        "castle_wall_03": wall_piece("castle_wall_03", 3),
        "castle_wall_04": wall_piece("castle_wall_04", 4),
        "castle_wall_05": wall_piece("castle_wall_05", 5),
        "castle_wall_06": wall_piece("castle_wall_06", 6),
        "castle_wall_back": back_wall(),
        "castle_wall_back_l": back_corner(True),
        "castle_wall_back_r": back_corner(False),
        "castle_spike": burst(),
        # obstacle_01..03 are generated (art/jobs/obstacle_0N.json), not drawn
        "cursor_01": cursor("cursor_01"),
        "cursor_02": cursor("cursor_02"),
        "cursor_03": cursor("cursor_03"),
        "cursor_04": cursor("cursor_04"),
    }
    for name, im in pieces.items():
        im.save(os.path.join(OUT, name + ".png"))
    shutil.copy(os.path.join(PACK, "art", "tiles", "grass.png"), os.path.join(OUT, "field_grass.png"))
    print("wrote", len(pieces) + 1, "pieces to", OUT)


if __name__ == "__main__":
    main()
