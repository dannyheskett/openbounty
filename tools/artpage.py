#!/usr/bin/env python3
"""Rebuild art.html: every PNG in the Rome pack, grouped into sections and
sub-sections, at native size.

    python3 tools/artpage.py

Serve the repo root (python3 -m http.server 8000) and open /art.html.
"""
import os
import re
from html import escape

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = "assets/glory-of-rome/art"

# (section, [(subsection, regex over the file stem)]) -- first match wins;
# anything unmatched lands in "other" at the end of its section.
GROUPS = {
    "classes": [("portraits", r".*")],
    "combat": [("field", r"field_.*"), ("castle walls", r"castle_.*"),
               ("obstacles", r"obstacle_.*"), ("cursor", r"cursor_.*")],
    "font": [("font", r".*")],
    "sprites": [("hero", r"hero_.*"), ("boat", r"boat_.*")],
    "tiles": [("terrain", r"(grass|grass_variant|forest|mountain|desert|water)"),
              ("places", r"(castle|town|town_.*|dwelling_.*)"),
              ("objects", r"(artifact_.*|chest|sign|wandering_army.*|bridge_.*)"),
              ("desert edges", r"desert_edge_.*"), ("forest edges", r"forest_edge_.*"),
              ("mountain edges", r"mountain_edge_.*"), ("water edges", r"water_edge_.*")],
    "troops": [],       # one row per troop, filled below
    "villains": [],     # one row per villain
    "ui": [("backdrops", r"backdrop_.*"), ("endings", r"end_.*"),
           ("splash and picker", r"(splash_.*|class_select_.*)"),
           ("chrome", r"chrome_.*"), ("hud", r"hud_.*"),
           ("inventory", r"inventory_.*"), ("puzzle", r"puzzle_.*")],
}
FRAME_SETS = {"troops", "villains"}


def files(section):
    """PNG stems in a section folder; a sub-folder's files are returned as
    "<sub>/<stem>" (per-zone tile sets live in art/tiles/<zone>/)."""
    d = os.path.join(ROOT, ART, section)
    out = []
    for f in sorted(os.listdir(d)):
        if f.endswith(".png"):
            out.append(f[:-4])
        elif os.path.isdir(os.path.join(d, f)):
            out += [f"{f}/{g[:-4]}" for g in sorted(os.listdir(os.path.join(d, f)))
                    if g.endswith(".png")]
    return out


def group(section, stems):
    rules = GROUPS.get(section, [])
    if section in FRAME_SETS:
        out = {}
        for s in stems:
            out.setdefault(re.sub(r"_\d+$", "", s), []).append(s)
        return list(out.items())
    out = {name: [] for name, _ in rules}
    other = []
    subs = {}
    for s in stems:
        if "/" in s:
            # a tile set folder: one sub-section per folder, in file order
            subs.setdefault(s.split("/", 1)[0] + " tile set", []).append(s)
            continue
        for name, rx in rules:
            if re.fullmatch(rx, s):
                out[name].append(s)
                break
        else:
            other.append(s)
    rows = [(n, v) for n, v in out.items() if v]
    if other:
        rows.append(("other", other))
    rows += sorted(subs.items())
    return rows


def cell(section, stem):
    src = f"{ART}/{section}/{stem}.png"
    with open(os.path.join(ROOT, src), "rb") as f:
        f.seek(16)
        w = int.from_bytes(f.read(4), "big")
    span = max(1, -(-(w + 14) // 114))  # 108px columns, 6px gap; 96px content + 14px padding and border
    style = f' style="grid-column:span {span};width:auto"' if span > 1 else ""
    return f'<figure class="c"{style}><img src="{src}" alt="{escape(stem)}"><figcaption title="{escape(stem)}">{escape(stem.split("/", 1)[-1])}</figcaption></figure>'


def main():
    sections = sorted(GROUPS)
    nav = " ".join(f'<a href="#{s}">{s}</a>' for s in sections)
    body = []
    total = 0
    for s in sections:
        stems = files(s)
        total += len(stems)
        body.append(f'<section id="{s}"><h1>{s} <small>{len(stems)}</small></h1>')
        for name, items in group(s, stems):
            body.append(f'<h2>{escape(name)} <small>{len(items)}</small></h2><div class="row">')
            body.extend(cell(s, st) for st in items)
            body.append("</div>")
        body.append("</section>")
    html = f"""<!doctype html><meta charset=utf-8><title>Glory of Rome art</title>
<style>
body{{background:#1b1b1b;color:#9a9a9a;font:12px/1.4 sans-serif;margin:0;padding:0 24px 60px}}
nav{{position:sticky;top:0;background:#1b1b1b;padding:12px 0;border-bottom:1px solid #333;z-index:1}}
nav a{{color:#8cf;margin-right:14px;text-decoration:none}}
h1{{color:#eee;font-size:18px;margin:34px 0 4px;text-transform:capitalize}}
h2{{color:#8cf;font-size:13px;font-weight:normal;margin:14px 0 6px;text-transform:capitalize}}
small{{color:#666;font-weight:normal;font-size:11px}}
.row{{display:grid;grid-template-columns:repeat(auto-fill,108px);gap:6px;align-items:end}}
.c{{margin:0;padding:6px;background:#262626;border:1px solid #333;text-align:center;width:96px;box-sizing:content-box}}
.c img{{image-rendering:pixelated;display:block;margin:0 auto}}
figcaption{{font-size:10px;color:#aaa;margin-top:4px;background:#1b1b1b;padding:1px 4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}}
</style>
<nav>{nav} <small>{total} files</small></nav>
{''.join(body)}
"""
    with open(os.path.join(ROOT, "art.html"), "w") as f:
        f.write(html)
    print(f"art.html: {total} files")


if __name__ == "__main__":
    main()
