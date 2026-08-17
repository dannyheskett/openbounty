#!/usr/bin/env python3
"""Build a side-by-side art comparison page: King's Bounty vs Glory of Rome,
plus any freshly generated candidate.

    tools/artpage.py                 -> build/art/compare.html
    tools/artpage.py --out foo.html

The page is one self-contained file with every image inlined, so it can be
opened from anywhere and mailed around without breaking. That costs a few MB
and is worth it -- a comparison page that only works from one directory stops
being used.

Pairing is three-tier, because the two packs do not use the same filenames:

  1. Same relative path (242 of 311 files) -- tiles, troops, combat, ui.
  2. Villains, whose art is named per pack (aimola_00 vs alaric_00). These map
     1:1 by their index in each manifest's villain list, so the manifests are
     what pair them, not the filenames.
  3. The font, one per pack, named for the pack.

A third column appears when a job in art/jobs/ has produced a run and declares
_pack_path -- that is the candidate, shown against what it would replace.
"""

import argparse, base64, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS = [("kings-bounty", "King's Bounty"), ("glory-of-rome", "Glory of Rome")]
CATEGORY_ORDER = ["tiles", "troops", "villains", "classes", "combat",
                  "sprites", "ui", "font"]


def art_root(pack):
    return os.path.join(ROOT, "assets", pack, "art")


def manifest(pack):
    with open(os.path.join(ROOT, "assets", pack, "game.json")) as f:
        return json.load(f)


def png_files(pack):
    out = {}
    root = art_root(pack)
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.endswith(".png"):
                p = os.path.join(dirpath, f)
                out[os.path.relpath(p, root)] = p
    return out


def data_uri(path, cache={}):
    if path not in cache:
        with open(path, "rb") as f:
            cache[path] = "data:image/png;base64," + base64.b64encode(f.read()).decode()
    return cache[path]


def dims(path, cache={}):
    """Width/height straight from the PNG IHDR -- no image library needed."""
    if path not in cache:
        with open(path, "rb") as f:
            head = f.read(24)
        cache[path] = (int.from_bytes(head[16:20], "big"),
                       int.from_bytes(head[20:24], "big"))
    return cache[path]


def villain_pairs():
    """Villain art is named per pack, so pair by manifest index + frame."""
    order = {}
    for pack, _ in PACKS:
        vs = manifest(pack).get("villains", [])
        order[pack] = [v.get("art") or v.get("id") for v in vs]
    pairs = []
    a, b = order[PACKS[0][0]], order[PACKS[1][0]]
    for i in range(max(len(a), len(b))):
        pairs.append((a[i] if i < len(a) else None,
                      b[i] if i < len(b) else None, i))
    return pairs


def candidates():
    """job id -> newest run's raw image, keyed by the pack path it replaces."""
    out = {}
    jobs_dir = os.path.join(ROOT, "art", "jobs")
    if not os.path.isdir(jobs_dir):
        return out
    for jf in sorted(os.listdir(jobs_dir)):
        if not jf.endswith(".json"):
            continue
        try:
            job = json.load(open(os.path.join(jobs_dir, jf)))
        except Exception:
            continue
        target = job.get("_pack_path")
        if not target:
            continue
        base = os.path.join(ROOT, "build", "art", job.get("id", ""))
        if not os.path.isdir(base):
            continue
        runs = sorted(r for r in os.listdir(base) if r.startswith("run"))
        for r in reversed(runs):
            raw = os.path.join(base, r, "01_raw.png")
            if os.path.exists(raw):
                rel = target[4:] if target.startswith("art/") else target
                out[rel] = (raw, job.get("id"), r)
                break
    return out


def build_rows():
    files = {p: png_files(p) for p, _ in PACKS}
    kb, rome = PACKS[0][0], PACKS[1][0]
    cand = candidates()
    used = {p: set() for p in files}
    rows = []

    # 2. villains, paired through the manifests
    for a, b, idx in villain_pairs():
        for frame in range(4):
            ka = f"villains/{a}_{frame:02d}.png" if a else None
            kb_ = f"villains/{b}_{frame:02d}.png" if b else None
            pa = files[kb].get(ka) if ka else None
            pb = files[rome].get(kb_) if kb_ else None
            if not pa and not pb:
                continue
            if ka: used[kb].add(ka)
            if kb_: used[rome].add(kb_)
            label = f"villain {idx:02d} frame {frame}"
            sub = f"{a or '-'}  /  {b or '-'}"
            rows.append(("villains", label, sub, pa, pb,
                         cand.get(kb_) if kb_ else None))

    # 3. font, one per pack
    fa = [k for k in files[kb] if k.startswith("font/")]
    fb = [k for k in files[rome] if k.startswith("font/")]
    if fa or fb:
        for k in fa: used[kb].add(k)
        for k in fb: used[rome].add(k)
        rows.append(("font", "bitmap font strip",
                     f"{fa[0] if fa else '-'}  /  {fb[0] if fb else '-'}",
                     files[kb].get(fa[0]) if fa else None,
                     files[rome].get(fb[0]) if fb else None, None))

    # 1. everything else, by identical relative path
    for rel in sorted(set(files[kb]) | set(files[rome])):
        if rel in used[kb] or rel in used[rome]:
            continue
        cat = rel.split("/")[0]
        rows.append((cat, os.path.basename(rel)[:-4], rel,
                     files[kb].get(rel), files[rome].get(rel), cand.get(rel)))
    return rows


CSS = """
:root { --bg:#15171a; --panel:#1e2126; --line:#2c3138; --fg:#e6e8ea; --dim:#8b939c; --accent:#6fb3ff; }
*{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--fg);
  font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace}
header{position:sticky;top:0;z-index:10;background:var(--panel);
  border-bottom:1px solid var(--line);padding:12px 20px;display:flex;
  gap:18px;align-items:center;flex-wrap:wrap}
h1{font-size:15px;margin:0;font-weight:600}
.meta{color:var(--dim)}
input[type=search]{background:#121417;border:1px solid var(--line);color:var(--fg);
  padding:6px 10px;border-radius:4px;min-width:220px;font:inherit}
button{background:#262b32;border:1px solid var(--line);color:var(--fg);
  padding:5px 11px;border-radius:4px;cursor:pointer;font:inherit}
button.on{background:var(--accent);color:#08101c;border-color:var(--accent)}
h2{margin:34px 20px 10px;font-size:13px;text-transform:uppercase;
  letter-spacing:.12em;color:var(--accent)}
table{width:calc(100% - 40px);margin:0 20px;border-collapse:collapse}
th{text-align:left;font-weight:600;color:var(--dim);padding:6px 10px;
  border-bottom:1px solid var(--line);font-size:11px;text-transform:uppercase;
  letter-spacing:.08em}
td{padding:10px;border-bottom:1px solid var(--line);vertical-align:middle}
td.name{white-space:nowrap}
.sub{color:var(--dim);font-size:11px}
.cell{display:flex;align-items:center;gap:10px}
.swwrap{border:1px solid var(--line);flex:none;line-height:0}
.sw{image-rendering:pixelated;background-repeat:repeat;background-position:0 0}
.checker{background-image:
  linear-gradient(45deg,#2a2a2a 25%,transparent 25%,transparent 75%,#2a2a2a 75%),
  linear-gradient(45deg,#2a2a2a 25%,transparent 25%,transparent 75%,#2a2a2a 75%);
  background-size:16px 16px;background-position:0 0,8px 8px}
.dim{color:var(--dim);font-size:11px;white-space:nowrap}
.missing{color:#c06;font-size:11px}
.swwrap.new{outline:2px solid #49c774}
.badge{background:#49c774;color:#062;padding:1px 6px;border-radius:3px;
  font-size:10px;font-weight:700;margin-left:6px}
tr.hide{display:none}
"""

JS = """
const state = { zoom: 2, tile: 1 };
function apply() {
  document.querySelectorAll('.sw').forEach(el => {
    const w = +el.dataset.w, h = +el.dataset.h;
    const cw = w * state.zoom * state.tile, ch = h * state.zoom * state.tile;
    el.style.width = cw+'px'; el.style.height = ch+'px';
    el.style.backgroundSize = (w*state.zoom)+'px '+(h*state.zoom)+'px';
    const wrap = el.parentElement;
    wrap.style.width = cw+'px'; wrap.style.height = ch+'px';
  });
  document.querySelectorAll('[data-zoom]').forEach(b =>
    b.classList.toggle('on', +b.dataset.zoom === state.zoom));
  document.querySelectorAll('[data-tile]').forEach(b =>
    b.classList.toggle('on', +b.dataset.tile === state.tile));
}
document.addEventListener('click', e => {
  const z = e.target.closest('[data-zoom]'), t = e.target.closest('[data-tile]');
  if (z) { state.zoom = +z.dataset.zoom; apply(); }
  if (t) { state.tile = +t.dataset.tile; apply(); }
});
const filt = document.getElementById('filter');
filt.addEventListener('input', () => {
  const q = filt.value.toLowerCase();
  document.querySelectorAll('tbody tr').forEach(tr =>
    tr.classList.toggle('hide', q && !tr.dataset.k.includes(q)));
  document.querySelectorAll('section').forEach(s => {
    const any = [...s.querySelectorAll('tbody tr')].some(r => !r.classList.contains('hide'));
    s.style.display = any ? '' : 'none';
  });
});
apply();
"""


def cell(path, label):
    if not path:
        return f'<td><span class="missing">— no {label} —</span></td>'
    w, h = dims(path)
    return (f'<td><div class="cell">'
            f'<div class="swwrap checker"><div class="sw" data-w="{w}" '
            f'data-h="{h}" style="background-image:url({data_uri(path)})"></div></div>'
            f'<span class="dim">{w}&times;{h}</span></div></td>')


def render(rows, out_path):
    by_cat = {}
    for r in rows:
        by_cat.setdefault(r[0], []).append(r)
    cats = [c for c in CATEGORY_ORDER if c in by_cat] + \
           [c for c in sorted(by_cat) if c not in CATEGORY_ORDER]

    n_new = sum(1 for r in rows if r[5])
    parts = [f"""<!doctype html><meta charset="utf-8">
<title>OpenBounty art: King's Bounty vs Glory of Rome</title>
<style>{CSS}</style>
<header>
  <h1>OpenBounty art</h1>
  <span class="meta">{len(rows)} assets &middot; {n_new} candidate{'s' if n_new!=1 else ''}</span>
  <input type="search" id="filter" placeholder="filter by name...">
  <span class="meta">zoom</span>
  <button data-zoom="1">1&times;</button><button data-zoom="2">2&times;</button>
  <button data-zoom="4">4&times;</button><button data-zoom="8">8&times;</button>
  <span class="meta">repeat</span>
  <button data-tile="1">off</button><button data-tile="3">3&times;3</button>
  <button data-tile="5">5&times;5</button>
</header>"""]

    for cat in cats:
        parts.append(f'<section><h2>{cat} <span class="meta">'
                     f'({len(by_cat[cat])})</span></h2><table><thead><tr>'
                     f'<th>asset</th><th>{PACKS[0][1]}</th><th>{PACKS[1][1]}</th>'
                     f'<th>candidate</th></tr></thead><tbody>')
        for _, label, sub, pa, pb, cnd in by_cat[cat]:
            key = f"{label} {sub}".lower()
            parts.append(f'<tr data-k="{key}"><td class="name">{label}'
                         f'<div class="sub">{sub}</div></td>')
            parts.append(cell(pa, PACKS[0][1]))
            parts.append(cell(pb, PACKS[1][1]))
            if cnd:
                p, jid, run = cnd
                w, h = dims(p)
                parts.append(f'<td><div class="cell">'
                             f'<div class="swwrap checker new"><div class="sw" '
                             f'data-w="{w}" data-h="{h}" '
                             f'style="background-image:url({data_uri(p)})"></div></div>'
                             f'<span class="dim">{w}&times;{h}'
                             f'<span class="badge">{jid} {run}</span></span></div></td>')
            else:
                parts.append('<td><span class="dim">&mdash;</span></td>')
            parts.append('</tr>')
        parts.append('</tbody></table></section>')

    parts.append(f"<script>{JS}</script>")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as f:
        f.write("\n".join(parts))
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "art", "compare.html"))
    a = ap.parse_args()
    rows = build_rows()
    p = render(rows, a.out)
    mb = os.path.getsize(p) / 1e6
    print(f"{p}  ({len(rows)} assets, {mb:.1f} MB)")


if __name__ == "__main__":
    main()
