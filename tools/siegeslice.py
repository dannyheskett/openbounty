#!/usr/bin/env python3
"""Slice a 384x384 overhead castle picture into the 96px combat pieces and
compose the siege screen from them.

    python3 tools/siegeslice.py <scene.png> <out-dir>          (recipe mode)
    python3 tools/siegeslice.py <scene.png> <out-dir> --grid   (36 cells, untouched)

The picture is a 4x4 grid of 96 cells: the top row is the back wall (with
its two corners), the side columns are the left and right walls, the bottom
row is the front wall with its two corners and the two broken ends either
side of the breach.

The straight runs are rebuilt from feature-free strips of the picture,
because the generated walls carry a gatehouse, trees and torches that would
repeat on every cell; the recipe below records which strips (settled on
build/art/siege_scene_map/run02, 2026-09-07):

  field_grass          cell (1,1), a plain interior cell
  castle_wall_back     wall rows from x 224..296 and x 48..72 of the top
                       wall (both have the wall at rows 22..69); the outside
                       band above and the courtyard foot below are plain grass
  castle_wall_04       left wall: x 0..48 of cell (0,2) over plain grass,
                       the outside strip x 0..24 also plain grass
  castle_wall_05       the left piece mirrored
  corners, broken ends, back corners: the picture's own cells

Pieces written (pack names, combat.c codes):
  castle_wall_back_l, castle_wall_back, castle_wall_back_r   (band above row 0)
  castle_wall_04 (left, 8), castle_wall_05 (right, 9)
  castle_wall_01 (bottom-left corner, 5), castle_wall_03 (broken end left, 7),
  castle_wall_06 (broken end right, 10), castle_wall_02 (bottom-right corner, 6)
  field_grass

Also writes siege_mock.png: the 6x5 grid plus the back band, laid out as
combat.c's castle_omap lays it out, with a few troop sprites on the field.
"""
import os
import sys
from PIL import Image

src = sys.argv[1]
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
im = Image.open(src).convert("RGBA")

if "--grid" in sys.argv:
    # Grid mode: the whole picture is the siege board plus its back band, a
    # 6x6 grid of equal cells (sprites.ui.siege_grid). Every cell is written
    # untouched as cell_<x>_<y>.png at the picture's own cell size; the shell
    # scales each to the combat cell. No assembly, no mock.
    W, H = 6, 6
    assert im.width % W == 0 and im.height % H == 0, im.size
    cw, ch = im.width // W, im.height // H
    for y in range(H):
        for x in range(W):
            im.crop((x * cw, y * ch, x * cw + cw, y * ch + ch)).save(
                os.path.join(out, f"cell_{x}_{y}.png"))
    print(f"{W * H} cells of {cw}x{ch} in {out}")
    sys.exit(0)

assert im.size == (384, 384), im.size
T = 96


def cell(cx, cy):
    return im.crop((cx * T, cy * T, cx * T + T, cy * T + T))


grass = cell(1, 1)

back = grass.copy()
back.paste(im.crop((224, 0, 296, T)), (0, 0))
back.paste(im.crop((48, 0, 72, T)), (72, 0))
back.paste(grass.crop((0, 0, T, 22)), (0, 0))        # outside band above the wall
back.paste(grass.crop((0, 72, T, T)), (0, 72))       # courtyard foot below it

left = grass.copy()
left.paste(cell(0, 2).crop((0, 0, 48, T)), (0, 0))
left.paste(grass.crop((0, 0, 24, T)), (0, 0))        # outside strip
right = left.transpose(Image.FLIP_LEFT_RIGHT)

pieces = {
    "castle_wall_back_l": cell(0, 0),
    "castle_wall_back":   back,
    "castle_wall_back_r": cell(3, 0),
    "castle_wall_04":     left,
    "castle_wall_05":     right,
    "castle_wall_01":     cell(0, 3),
    "castle_wall_03":     cell(1, 3),
    "castle_wall_06":     cell(2, 3),
    "castle_wall_02":     cell(3, 3),
    "field_grass":        grass,
}
for name, p in pieces.items():
    p.save(os.path.join(out, name + ".png"))

code = {8: "castle_wall_04", 9: "castle_wall_05", 5: "castle_wall_01",
        7: "castle_wall_03", 10: "castle_wall_06", 6: "castle_wall_02"}
omap = [[8, 0, 0, 0, 0, 9]] * 4 + [[5, 7, 0, 0, 10, 6]]
W, H = 6, 5
mock = Image.new("RGBA", (W * T, (H + 1) * T))
for x in range(W):
    name = "castle_wall_back_l" if x == 0 else "castle_wall_back_r" if x == W - 1 else "castle_wall_back"
    mock.paste(pieces["field_grass"], (x * T, 0))
    mock.paste(pieces[name], (x * T, 0), pieces[name])
for y in range(H):
    for x in range(W):
        mock.paste(pieces["field_grass"], (x * T, (y + 1) * T))
        c = omap[y][x]
        if c:
            mock.paste(pieces[code[c]], (x * T, (y + 1) * T), pieces[code[c]])

# a few troops where combat.c's castle_umap puts them (defenders top, attackers bottom)
troops = "assets/glory-of-rome/art/troops"
placements = {(1, 0): "praetoriani_00", (2, 0): "hastati_00", (3, 0): "velites_00", (4, 0): "equites_00",
              (2, 1): "sarmatae_00", (1, 3): "ligures_00", (2, 3): "numidae_00", (3, 3): "gigantes_00",
              (2, 4): "lares_00", (3, 4): "tirones_00"}
for (x, y), n in placements.items():
    p = os.path.join(troops, n + ".png")
    if os.path.exists(p):
        s = Image.open(p).convert("RGBA")
        if x >= 1 and y <= 1:
            s = s.transpose(Image.FLIP_LEFT_RIGHT)   # defenders face left
        mock.paste(s, (x * T, (y + 1) * T), s)
mock.save(os.path.join(out, "siege_mock.png"))
print(f"{len(pieces)} pieces and siege_mock.png in {out}")
