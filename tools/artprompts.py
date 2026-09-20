#!/usr/bin/env python3
"""Rebuild docs/ART-PROMPTS.md: every prompt the pack's art was made from.

    python3 tools/artprompts.py [out.md]

One page, generated from art/jobs/*.json, so it cannot drift from the jobs
themselves. Each row carries the engine and its settings, the prompt exactly as
it was sent, and the job's own note, which is where the history lives (what a
run produced, what it superseded, what was wasted). A job whose `_pack_path`
is in the pack is marked INSTALLED; the rest are the record of what was tried.

Two engines make everything (docs/ART-PIPELINE.md): Retro Diffusion draws
figures, screens and objects; PixelLab makes the terrain sets and sprite
batches. The reference groups by what the art IS, not by engine.
"""
import json
import os
import sys

JOBS = "art/jobs"
PACK = "assets/glory-of-rome"
OUT = sys.argv[1] if len(sys.argv) > 1 else "docs/ART-PROMPTS.md"


def engine_of(d):
    if "lower_description" in d:
        ts = d.get("tile_size", {})
        size = ts.get("width", ts) if isinstance(ts, dict) else ts
        return "PixelLab create-tileset", f"{size} px, seed {d.get('seed', '-')}"
    if "batches" in d:
        return "PixelLab create-1-direction-object", f"96 px, {sum(len(b) for b in d['batches'])} items"
    if "tile_size" in d:
        return "PixelLab tiles", f"{d.get('tile_size')} px, seed {d.get('seed', '-')}"
    style = str(d.get("style", "?"))
    return f"Retro Diffusion {style}", f"{d.get('width', '?')}x{d.get('height', '?')}, seed {d.get('seed', '-')}"


def prompts_of(d):
    """Every prompt a job sends, labelled."""
    out = []
    if "lower_description" in d:
        for key, label in (("lower_description", "lower"),
                           ("upper_description", "upper"),
                           ("transition_description", "where they meet")):
            if d.get(key):
                out.append((label, d[key]))
    elif "batches" in d:
        out.append(("shared", d.get("description", "")))
        for i, b in enumerate(d["batches"], 1):
            out.append((f"batch {i}", " · ".join(b)))
    else:
        for key, label in (("prompt", "prompt"), ("description", "description")):
            if d.get(key):
                out.append((label, d[key]))
    return out


def group_of(name, d):
    p = d.get("_pack_path", "")
    for frag, g in (("art/troops/", "Troops"), ("art/portraits/", "Portraits and faces"),
                    ("art/villains/", "Villains"), ("art/classes/", "Hero classes"),
                    ("art/tiles/", "Map tiles and terrain"), ("art/scenes/", "Scenes"),
                    ("art/ui/", "Screens and UI")):
        if p.startswith(frag):
            return g
    if "lower_description" in d or name.startswith(("t16_", "t32_", "grass16", "grass32")):
        return "Map tiles and terrain"
    if "batches" in d:
        return "Sprite batches (trees, rocks)"
    if name.startswith("backdrop") or name.startswith("splash") or name.startswith("title"):
        return "Screens and UI"
    if name.startswith("troop_portrait") or "portrait" in name:
        return "Portraits and faces"
    # A job with no pack path is a step towards one: a troop still, its attack
    # loop, a pose the animation starts from. Group it with the troop it names.
    import glob as _g
    stem = name.split("_")[0]
    for f in _g.glob(os.path.join(PACK, "art", "troops", stem + "_*.png")):
        return "Troops"
    if str(d.get("style", "")).startswith("rd_advanced_animation"):
        return "Animations"
    return "Other"


def main():
    jobs = []
    for f in sorted(os.listdir(JOBS)):
        if not f.endswith(".json"):
            continue
        try:
            d = json.load(open(os.path.join(JOBS, f)))
        except Exception:
            continue
        jobs.append((f[:-5], d))

    groups = {}
    for name, d in jobs:
        groups.setdefault(group_of(name, d), []).append((name, d))

    lines = [
        "# The art prompts",
        "",
        "**Generated** by `tools/artprompts.py` from `art/jobs/*.json`. Do not edit by",
        "hand: change the job file and run the tool again.",
        "",
        f"{len(jobs)} jobs in all. **INSTALLED** marks a job whose output is in the pack;",
        "everything else is the record of what was tried, which is why the notes matter —",
        "they say what a run produced and what it superseded. The routes themselves (which",
        "engine, which settings, and why) are in `docs/ART-PIPELINE.md`.",
        "",
    ]
    for g in sorted(groups):
        lines += [f"## {g}", ""]
        for name, d in sorted(groups[g]):
            eng, settings = engine_of(d)
            pack = d.get("_pack_path", "")
            installed = pack and os.path.exists(os.path.join(PACK, pack))
            head = f"### {name}" + ("  — INSTALLED" if installed else "")
            lines += [head, "", f"- **Engine:** {eng} ({settings})"]
            if pack:
                lines.append(f"- **Pack path:** `{pack}`")
            for label, text in prompts_of(d):
                lines.append(f"- **{label}:** {text}")
            if d.get("_note"):
                lines.append(f"- **Note:** {d['_note']}")
            lines.append("")
    open(OUT, "w").write("\n".join(lines) + "\n")
    print(f"wrote {OUT}: {len(jobs)} jobs in {len(groups)} groups")


if __name__ == "__main__":
    main()
