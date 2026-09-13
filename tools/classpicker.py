#!/usr/bin/env python3
"""Pre-render the Rome class-select carousel.

Writes art/ui/class_select_picker_0..3.png beside class_select_picker.png: the
same painting with every figure but one dimmed, and that figure ringed in gold.
The painting itself is read only, never changed.

Which pixels belong to which figure: the landscape is found by scanning each
column down from the top edge to the first dark outline pixel (plus short
sideways steps under overhangs); the figures overlap, so three hand-placed
dividing lines decide whose pixel is whose, and two small hand fixes remain.

    python3 tools/classpicker.py [pack dir]   (default assets/glory-of-rome)
"""
import os, sys
from PIL import Image, ImageChops, ImageFilter

PACK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'glory-of-rome')
SRC = os.path.join(PACK, 'art', 'ui', 'class_select_picker.png')
DIM = 150                  # black over the other figures, out of 255
GOLD = (255, 255, 85, 255) # the palette's yellow
LIMIT = 90                 # the landscape never reaches below this row
TH = 50                    # darker than this is an outline

im = Image.open(SRC).convert('RGB'); W, H = im.size; px = im.load()
def lum(c): return 0.299*c[0]+0.587*c[1]+0.114*c[2]
# 1) background: flood from the top edge through pixels that are not dark
#    outline, never below the landscape strip.
bg=[[False]*W for _ in range(H)]
for x in range(W):
    for y in range(min(H,LIMIT)):
        if lum(px[x,y])<TH: break
        bg[y][x]=True
# hand-drawn dividers between neighbours: x as a polyline in y
DIV=[
 [(60,0),(60,40),(68,48),(65,56),(62,64),(60,72),(58,80),(58,96),(57,120),(58,164)],
 [(124,0),(124,20),(130,48),(140,56),(142,62),(140,72),(140,80),(134,96),(128,104),(120,116),(120,136),(128,142),(133,150),(136,164)],
 [(186,0),(186,40),(190,52),(194,66),(199,80),(201,100),(201,140),(199,164)],
]
def divx(poly,y):
    for (x0,y0),(x1,y1) in zip(poly,poly[1:]):
        if y0<=y<=y1: return x0+(x1-x0)*(y-y0)/max(1,y1-y0)
    return poly[-1][0]
# narrow slivers of background (a gap in the outline) belong to the figure
for y in range(H):
    x=0
    while x<W:
        if bg[y][x]:
            e=x
            while e<W and bg[y][e]: e+=1
            if e-x<=4 and x>0 and e<W:
                for i in range(x,e): bg[y][i]=False
            x=e
        else: x+=1
# sideways: a few pixels past known background, for short overhangs (hair
# over a face) that a top-down scan cannot see under
ext=[row[:] for row in bg]
for y in range(min(H,LIMIT)):
    for x in range(W):
        if not bg[y][x]: continue
        for d in (1,-1):
            for i in range(1,5):
                nx=x+d*i
                if not (0<=nx<W) or bg[y][nx]: break
                if lum(px[nx,y])<70: break
                c0=px[x,y]; c1=px[nx,y]
                if sum(abs(c0[j]-c1[j]) for j in range(3))>90: break
                ext[y][nx]=True
bg=ext
# narrow slivers of background (a gap in the outline) belong to the figure
for y in range(H):
    x=0
    while x<W:
        if bg[y][x]:
            e=x
            while e<W and bg[y][e]: e+=1
            if e-x<=4 and x>0 and e<W:
                for i in range(x,e): bg[y][i]=False
            x=e
        else: x+=1
# hand fixes: (x0,y0,x1,y1) inclusive
FORCE_BG=[(22,33,26,39)]          # trees showing beside the Legatus's neck
FORCE_FIG=[(160,15,162,15),(152,16,162,16),(151,17,162,17),(150,18,162,18)]+[(149,y,162,y) for y in range(19,25)]
                                  # the top of the Sibylla's veil and fillet
for x0,y0,x1,y1 in FORCE_BG:
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            if lum(px[x,y])>=70: bg[y][x]=True
for x0,y0,x1,y1 in FORCE_FIG:
    for y in range(y0,y1+1):
        for x in range(x0,x1+1): bg[y][x]=False

pic = im.convert('RGBA')
for k in range(4):
    m = Image.new('L', (W, H), 0); mp = m.load()
    for y in range(H):
        lo = divx(DIV[k-1], y) if k > 0 else -1
        hi = divx(DIV[k], y) if k < 3 else W + 1
        for x in range(W):
            if not bg[y][x] and lo <= x < hi: mp[x, y] = 255
    out = Image.alpha_composite(pic, Image.new('RGBA', (W, H), (0, 0, 0, DIM)))
    ring = ImageChops.subtract(m.filter(ImageFilter.MaxFilter(3)), m)
    out.paste(GOLD, (0, 0), ring)
    out.paste(pic, (0, 0), m)
    dst = os.path.join(PACK, 'art', 'ui', 'class_select_picker_%d.png' % k)
    out.convert('RGB').save(dst)
    print(dst)
