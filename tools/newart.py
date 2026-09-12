#!/usr/bin/env python3
"""Rebuild newart.html: the newest art shown ASSEMBLED, not as a file grid.

    python3 tools/newart.py              # relative paths, refreshes from disk
    python3 tools/newart.py --inline     # one self-contained file, images embedded

art.html (tools/artpage.py) lists every PNG in the pack, which is the right
page for "does this file exist and what does it look like". It is the wrong
page for judging a tile set, because a road piece on its own says nothing
about whether it joins the piece beside it, and a figure on its own says
nothing about whether it stands correctly on its backdrop.

So this page puts each new thing where the game puts it: road pieces laid into
runs on the pack's own grass, at the engine's tile size; the alcove figure over
the alcove backdrop at the exact offset draw_location_backdrop uses; the map
tiles on grass beside the art they replaced. Everything is positioned in CSS
over the real PNGs -- nothing here composites or edits an image, so the page
shows exactly the bytes the game loads.

Sections come from BATCHES below. Adding the next batch is a dict, not code.
"""
import base64
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = "assets/glory-of-rome/art"
TILE = 96          # render.tile_w for glory-of-rome
INLINE = "--inline" in sys.argv


def t(name):
    return f"{PACK}/tiles/{name}.png"


def u(name):
    return f"{PACK}/ui/{name}.png"


# A road run drawn as a grid of cells. "." is bare grass; every other letter is
# a piece. These are the generator's own contract letters (tools/roadtile.py),
# so a layout here is checkable against the layout the generator mocks.
ROAD_CODE = {
    "f": "road_ns", "g": "road_ew", "h": "road_ne", "i": "road_es",
    "j": "road_sw", "k": "road_wn", "l": "road_nesw", "m": "road_nwse",
    "n": "road_n_sw", "o": "road_n_se", "p": "road_s_nw", "q": "road_s_ne",
    "r": "road_e_nw", "s": "road_e_sw", "t": "road_w_ne", "u": "road_w_se",
    "v": "road_c_nw", "w": "road_c_ne", "x": "road_c_sw", "y": "road_c_se",
    "1": "road_n", "2": "road_e", "3": "road_s", "4": "road_w",
}

BATCHES = [
    {
        "id": "roads",
        "title": "Cobblestone roads",
        "blurb": "All 24 pieces swept from art/jobs/t32_cobble_203.json. The "
                 "border is the sweep's own rim (two pixels of the paving at "
                 "0.78), not anything the model drew. Every straight exit is "
                 "the same 32 px band and every diagonal the same corner "
                 "triangle, which is why any piece joins any other -- these "
                 "runs are the proof of it.",
        "runs": [
            ("A run that turns, goes diagonal, and stops",
             ["..f........",
              "..hggj.....",
              ".....f.....",
              ".....ox....",
              ".....wmx...",
              "......wrgg4",
              "...3.......",
              "...f...2gg4",
              "...f.......",
              "...1......."]),
            ("The four ends. Each connects on its named side and dies inside "
             "its own tile: road_n joins the piece above it, road_s the piece "
             "below, road_e the piece to its right, road_w to its left",
             ["..................",
              ".f......2gggg.....",
              ".f................",
              ".1................",
              "..................",
              ".....3....gggggg4.",
              ".....f............",
              ".....f............"]),
        ],
        "pieces": [
            ("straights", ["road_ns", "road_ew"]),
            ("curves", ["road_ne", "road_es", "road_sw", "road_wn"]),
            ("diagonals", ["road_nesw", "road_nwse"]),
            ("straight-to-diagonal joins",
             ["road_n_sw", "road_n_se", "road_s_nw", "road_s_ne",
              "road_e_nw", "road_e_sw", "road_w_ne", "road_w_se"]),
            ("corner companions",
             ["road_c_nw", "road_c_ne", "road_c_sw", "road_c_se"]),
            ("ends (new)", ["road_n", "road_e", "road_s", "road_w"]),
        ],
        "before": [("road_ns", "build/art/old/road_ns.png"),
                   ("road_ne", "build/art/old/road_ne.png"),
                   ("road_ew", "build/art/old/road_ew.png"),
                   ("road_nwse", "build/art/old/road_nwse.png")],
    },
    {
        "id": "alcove",
        "title": "The magic alcove: an auguraculum",
        "blurb": "Where a non-priest class buys the knowledge of magic. An "
                 "augur read a quartered region of sky, and templum was his "
                 "word for it, so the precinct is roofless on purpose. The "
                 "figure is a state priest with the lituus and a raven -- "
                 "deliberately unlike the Sibylla class portrait, which is a "
                 "veiled priestess in white, so the oracle and the player are "
                 "never the same figure.",
        "screen": {
            "backdrop": u("backdrop_alcove"),
            "figure": u("alcove_augur_00"),
            "old_backdrop": "build/art/old/backdrop_hillcave.png",
        },
        "map": [("alcove (new)", t("alcove")),
                ("dwelling_hills (what it borrowed)",
                 "build/art/old/dwelling_hills.png")],
    },
]

CSS = """
body{background:#1b1b1b;color:#9a9a9a;font:13px/1.5 sans-serif;margin:0;padding:0 28px 80px}
nav{position:sticky;top:0;background:#1b1b1b;padding:12px 0;border-bottom:1px solid #333;z-index:2}
nav a{color:#8cf;margin-right:16px;text-decoration:none}
h1{color:#eee;font-size:20px;margin:38px 0 6px}
h2{color:#8cf;font-size:14px;font-weight:normal;margin:26px 0 8px}
p.blurb{max-width:76ch;color:#a8a8a8;margin:0 0 18px}
small{color:#666;font-size:11px}
img{image-rendering:pixelated;display:block}
.scene{position:relative;margin:0 0 22px;border:1px solid #333;overflow:hidden;
       background-repeat:repeat;background-color:#2a2a2a}
.scene img{position:absolute}
.strip{display:flex;flex-wrap:wrap;gap:10px;margin:0 0 20px}
.cell{background:#262626;border:1px solid #333;padding:7px;text-align:center}
.cell figcaption{font-size:11px;color:#bbb;margin-top:5px}
.grass{background-repeat:repeat}
.pair{display:flex;gap:22px;flex-wrap:wrap;align-items:flex-start;margin-bottom:22px}
.note{color:#777;font-size:11px;margin:-10px 0 18px}
"""


def data_uri(rel):
    p = os.path.join(ROOT, rel)
    with open(p, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def src(rel):
    return data_uri(rel) if INLINE else rel


def scene(rows, code, zoom):
    """A grid of tile cells over a tiled grass background."""
    w = max(len(r) for r in rows)
    px = TILE * zoom
    out = [f'<div class="scene grass" style="width:{w * px}px;height:{len(rows) * px}px;'
           f'background-image:url({src(t("grass"))});background-size:{px}px {px}px">']
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            name = code.get(ch)
            if not name:
                continue
            out.append(f'<img src="{src(t(name))}" alt="{name}" title="{name}" '
                       f'style="left:{x * px}px;top:{y * px}px;width:{px}px;height:{px}px">')
    out.append("</div>")
    return "".join(out)


def on_grass(path, zoom, caption):
    px = TILE * zoom
    return (f'<figure class="cell"><div class="grass" style="width:{px}px;height:{px}px;'
            f'background-image:url({src(t("grass"))});background-size:{px}px {px}px">'
            f'<img src="{src(path)}" style="width:{px}px;height:{px}px"></div>'
            f'<figcaption>{caption}</figcaption></figure>')


def build():
    h = ['<!doctype html><meta charset=utf-8><title>Glory of Rome &mdash; new art in context</title>',
         f"<style>{CSS}</style>", "<nav>"]
    for b in BATCHES:
        h.append(f'<a href="#{b["id"]}">{b["title"]}</a>')
    h.append('<a href="art.html">the whole pack</a> '
             '<small>assembled as the game draws it &mdash; nothing on this page is composited</small></nav>')

    for b in BATCHES:
        h.append(f'<h1 id="{b["id"]}">{b["title"]}</h1>')
        h.append(f'<p class="blurb">{b["blurb"]}</p>')

        for title, rows in b.get("runs", []):
            h.append(f"<h2>{title}</h2>")
            # 1x: a run is wide (18 tiles is 1728 px) and a run that scrolls
            # sideways cannot be judged as a run. The pieces below are at 2x
            # for looking at the stones.
            h.append(scene(rows, ROAD_CODE, 1))

        if b.get("pieces"):
            h.append("<h2>Every piece, on the pack's own grass</h2>")
            for group, names in b["pieces"]:
                h.append(f'<p class="note">{group}</p><div class="strip">')
                for n in names:
                    h.append(on_grass(t(n), 2, n))
                h.append("</div>")

        if b.get("before"):
            h.append("<h2>What it replaced</h2><div class=\"strip\">")
            for n, p in b["before"]:
                h.append(on_grass(p, 2, n + " (old dirt)"))
            h.append("</div>")

        sc = b.get("screen")
        if sc:
            h.append("<h2>The location screen, as draw_location_backdrop composes it</h2>")
            h.append('<p class="note">The figure stands where the pack places him, in the '
                     'backdrop&rsquo;s own 240&times;102 units, so he scales with the card. Shown at '
                     'ui_scale 1 (card 240&times;102) and 2 (card 480&times;204), both at 2&times; zoom.</p>')
            # The pack's own placement, in the backdrop's 240x102 design units,
            # exactly as draw_location_backdrop reads it. Without one, the
            # figure falls back to the tile-sized troop slot.
            import json as _json
            ui_decl = _json.load(open(os.path.join(ROOT, "assets/glory-of-rome/game.json")))["sprites"]["ui"]
            pl = ui_decl.get("alcove_figure_place")
            for ui in (1, 2):
                bw, bh = 240 * ui, 102 * ui
                z = 2
                if pl:
                    fx, fy = pl["x"] * ui, pl["y"] * ui
                    fw, fh = pl["w"] * ui, pl.get("h", pl["w"]) * ui
                    how = "pack placement x %d y %d, %dx%d units" % (pl["x"], pl["y"], pl["w"], pl.get("h", pl["w"]))
                else:
                    fx, fy, fw, fh = TILE, bh - TILE - 4 * ui, TILE, TILE
                    how = "no placement declared: the troop slot"
                h.append(f'<p class="note">ui_scale {ui} &mdash; {how}</p>')
                frames = ["assets/glory-of-rome/" + f for f in ui_decl.get("alcove_figure_animation", [])] or [sc["figure"]]
                ms = ui_decl.get("alcove_figure_frame_ms") or 50
                cyc = ms * len(frames) / 1000.0
                pct = 100.0 / len(frames)
                h.append(f'<style>@keyframes fr{len(frames)}{{0%{{opacity:1}}{pct:.4f}%{{opacity:0}}100%{{opacity:0}}}}</style>')
                h.append(f'<div class="scene" style="width:{bw * z}px;height:{bh * z}px">'
                         f'<img src="{src(sc["backdrop"])}" style="left:0;top:0;'
                         f'width:{bw * z}px;height:{bh * z}px">')
                # One <img> per frame, each visible for exactly one step. The
                # frames are the real PNGs in the order game.json declares, at
                # the pack's own frame_ms -- a playback, not a composite.
                for i, fp in enumerate(frames):
                    anim = (f'opacity:0;animation:fr{len(frames)} {cyc:.3f}s steps(1) infinite;'
                            f'animation-delay:{i * ms / 1000.0 - cyc:.3f}s;') if len(frames) > 1 else ''
                    h.append(f'<img src="{src(fp)}" style="left:{fx * z}px;top:{fy * z}px;'
                             f'width:{fw * z}px;height:{fh * z}px;{anim}">')
                h.append('</div>')
            frames = ["assets/glory-of-rome/" + f for f in ui_decl.get("alcove_figure_animation", [])]
            if frames:
                h.append("<h2>The loop, frame by frame in play order</h2>")
                h.append('<p class="note">The raven spreads its wings, beats once and folds them while '
                         'the augur turns to watch: a bird&rsquo;s sign is what an augur reads. Played '
                         'forward then back, so no step is a jump the model did not draw; on disk the 14 '
                         'frames are a plain numbered run, 08&ndash;13 copies of 06 down to 01.</p>'
                         '<div class="strip">')
                for i, fp in enumerate(frames):
                    h.append(on_grass(fp, 1, "%d: %s" % (i, os.path.basename(fp)[-6:-4])))
                h.append("</div>")
            h.append("<h2>The backdrop it replaced</h2><div class=\"pair\">")
            for lbl, p in (("backdrop_alcove (new)", sc["backdrop"]),
                           ("backdrop_hillcave (borrowed, still the hill dwelling's)",
                            sc["old_backdrop"])):
                h.append(f'<figure class="cell"><img src="{src(p)}" '
                         f'style="width:{240 * 2}px;height:{102 * 2}px">'
                         f'<figcaption>{lbl}</figcaption></figure>')
            h.append("</div>")

        if b.get("map"):
            h.append("<h2>On the map</h2><div class=\"strip\">")
            for lbl, p in b["map"]:
                h.append(on_grass(p, 2, lbl))
            h.append("</div>")

    out = os.path.join(ROOT, "newart.html")
    with open(out, "w") as f:
        f.write("\n".join(h) + "\n")
    kb = os.path.getsize(out) // 1024
    print(f"newart.html: {len(BATCHES)} batches, {kb} KB"
          f"{' (self-contained)' if INLINE else ''}")


build()
