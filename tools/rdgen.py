#!/usr/bin/env python3
"""Retro Diffusion art pipeline driver.

Drives one asset from a JSON job spec through generate -> transform -> QA,
keeping every request and response on disk so a result is auditable rather
than remembered. Written for ~116 assets, so the whole run is described by
the job file and nothing lives in shell history.

    tools/rdgen.py cost    art/jobs/hastati.json
    tools/rdgen.py run     art/jobs/hastati.json
    tools/rdgen.py balance

Two rules this enforces, both learned the hard way:

  * Every paid call goes out with async=true and its task id is written to
    disk BEFORE any polling starts. A synchronous call once timed out after
    the charge landed, and the result was unrecoverable because the id was
    never captured.
  * The token is read from a file, never from the environment and never from
    a command line, so it stays out of process listings and shell history.
    This project does not use environment variables anywhere.

Outputs land under build/art/<id>/ (build/ is gitignored). Approved finals
are copied into the pack by hand, deliberately -- nothing here writes to
assets/.
"""

import argparse
import base64
import io
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.request

API = "https://api.retrodiffusion.ai/v1"
DEFAULT_TOKEN_FILE = os.path.expanduser("~/.config/retrodiffusion/token")
OUT_ROOT = "build/art"

try:
    from PIL import Image, ImageSequence
except ImportError:
    sys.exit("rdgen: needs Pillow (pip install pillow)")


# ---- plumbing --------------------------------------------------------------

def read_token(path):
    if not os.path.exists(path):
        sys.exit(f"rdgen: no token at {path}\n"
                 f"  write your rdpk- key there, chmod 600.")
    tok = open(path).read().strip()
    if not tok.startswith("rdpk-"):
        sys.exit(f"rdgen: {path} does not look like an rdpk- key")
    return tok


def api(token, method, path, payload=None, timeout=60):
    url = f"{API}{path}"
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("X-RD-Token", token)
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        try:
            return json.loads(body)
        except Exception:
            return {"detail": {"code": str(e.code), "message": body[:400]}}


def save_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)


def new_run_dir(job_id):
    """A fresh numbered directory per run. Never reuse, never overwrite.

    Generated art is paid for and is not reproducible -- the same prompt and
    seed gave materially different results across runs. An earlier version of
    this wrote every run to build/art/<id>/ and a re-run destroyed the
    previous raw. Recovering it was only possible because the API retains
    outputs for 24 hours and the request id had been saved.
    """
    base = os.path.join(OUT_ROOT, job_id)
    os.makedirs(base, exist_ok=True)
    n = 1
    while os.path.exists(os.path.join(base, f"run{n:02d}")):
        n += 1
    d = os.path.join(base, f"run{n:02d}")
    os.makedirs(d)
    return d


def latest_run_dir(job_id):
    base = os.path.join(OUT_ROOT, job_id)
    runs = sorted(g for g in os.listdir(base)) if os.path.isdir(base) else []
    runs = [r for r in runs if r.startswith("run")]
    return os.path.join(base, runs[-1]) if runs else None


# ---- job spec --------------------------------------------------------------

# Every key a job may carry. Keys beginning with "_" are notes for the reader
# and are never sent. Anything else is refused rather than ignored: a job once
# carried return_non_bg_removed, which this file did not forward, and the run
# went out silently without it -- a paid call that could not answer the question
# it was made to answer.
JOB_KEYS = {
    # rdgen's own controls
    "id", "prompt", "style", "width", "height", "target",
    "raw_only", "figure", "headroom_rows",
    "input_image_path", "input_palette_path", "input_image_keep_alpha",
    "reference_image_paths", "pad_to",
    # forwarded to the API by request_payload()
    "seed", "remove_bg", "tile_x", "tile_y", "frames_duration",
    "return_spritesheet", "input_palette", "strength",
    "bypass_prompt_expansion", "return_non_bg_removed",
}


def load_job(path):
    job = json.load(open(path))
    for k in ("id", "prompt", "style", "width", "height", "target"):
        if k not in job:
            sys.exit(f"rdgen: job {path} missing required key '{k}'")
    unknown = sorted(k for k in job if not k.startswith("_") and k not in JOB_KEYS)
    if unknown:
        sys.exit(f"rdgen: job {path} has unknown key(s): {', '.join(unknown)}\n"
                 f"       rdgen would ignore them and submit a paid request that\n"
                 f"       is not the one described by the job. Add each to\n"
                 f"       JOB_KEYS (and to request_payload if the API takes it),\n"
                 f"       or prefix it with '_' if it is only a note.")
    job["_path"] = path
    return job


def request_payload(job, *, check_cost=False):
    p = {
        "prompt": job["prompt"],
        "prompt_style": job["style"],
        "width": job["width"],
        "height": job["height"],
        "num_images": 1,
    }
    for k in ("seed", "remove_bg", "tile_x", "tile_y", "frames_duration",
              "return_spritesheet", "input_palette", "strength",
              "bypass_prompt_expansion", "return_non_bg_removed"):
        if k in job:
            p[k] = job[k]
    # input_palette: DO NOT USE to "anchor" or "match" a colour. It HARD
    # CONSTRAINS the entire output to the supplied palette, and it collapses
    # structure along with it. Measured twice, a day apart, and forgotten in
    # between because the first finding was only ever said out loud:
    #   2026-08-15  on a figure: "forces flat limited colour but collapses the
    #               face and structure"
    #   2026-08-17  on two object tiles: passing grass.png (5 colours, all
    #               green) returned 3-colour all-green images with the ring and
    #               chest rendered in shades of grass. $0.076 wasted.
    # To match a colour, name it in the prompt instead.
    #
    # img2img. The API wants RGB with no alpha, so a transparent source is
    # flattened onto white first. Structure -- pose, proportion, framing,
    # outline weight -- carries over from the input far more reliably than it
    # can be described in words, which is the whole reason for using it.
    if job.get("input_palette_path"):
        pal = Image.open(job["input_palette_path"]).convert("RGB")
        pb = io.BytesIO(); pal.save(pb, format="PNG")
        p["input_palette"] = base64.b64encode(pb.getvalue()).decode()
    # input_image_keep_alpha: send the PNG exactly as it is, alpha intact.
    # The API reference says input_image must be RGB without transparency, but
    # the animation docs say "a transparent start frame yields a transparent
    # GIF" -- both cannot be true. Flattening onto white would guarantee opaque
    # frames, and restoring alpha afterwards is post-processing, which is
    # banned. A check_cost with an RGBA payload is accepted rather than
    # rejected, so this lets the claim be tested for real.
    if job.get("input_image_path"):
        src = Image.open(job["input_image_path"]).convert("RGBA")
        # pad_to: the vendor's motion-room rule -- "a sprite whose opaque
        # pixels touch the canvas edge animates badly -- pad it onto a larger
        # transparent canvas first". Every animation run before this one sent a
        # figure filling its frame, and every one of them under-moved. Nothing
        # is resampled: the still is composited into a bigger empty canvas,
        # centred horizontally and standing on the bottom edge.
        pad = job.get("pad_to")
        if pad:
            pw, ph = (pad, pad) if isinstance(pad, int) else pad
            if pw < src.width or ph < src.height:
                sys.exit(f"rdgen: pad_to {pw}x{ph} is smaller than the source "
                         f"{src.width}x{src.height}")
            canvas = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
            canvas.alpha_composite(src, ((pw - src.width) // 2,
                                         ph - src.height))
            src = canvas
        if job.get("input_image_keep_alpha"):
            out = src
        else:
            out = Image.new("RGB", src.size, (255, 255, 255))
            out.paste(src, (0, 0), src)
        buf = io.BytesIO()
        out.save(buf, format="PNG")
        p["input_image"] = base64.b64encode(buf.getvalue()).decode()
    # reference_images: RD Pro and the prompt-driven rd_animation__* styles
    # accept up to 9. Unlike input_image these are NOT redrawn -- they steer
    # style and content, which is how one character stays the same character
    # across separate generations.
    if job.get("reference_image_paths"):
        refs = []
        for rp in job["reference_image_paths"]:
            src = Image.open(rp).convert("RGBA")
            buf = io.BytesIO()
            src.save(buf, format="PNG")
            refs.append(base64.b64encode(buf.getvalue()).decode())
        p["reference_images"] = refs
    if check_cost:
        p["check_cost"] = True
    else:
        p["async"] = True
    return p


# ---- transforms ------------------------------------------------------------

def content_bbox(im, alpha_floor=12):
    """Tight bbox of non-transparent pixels; None when the image is empty."""
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    pts = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] > alpha_floor]
    if not pts:
        return None
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def fill_to_target(im, tw, th, headroom=0):
    """Crop to content, then scale so the figure fills the full target height.

    The spec's fill rule: feet on the bottom row, head within a pixel of the
    top. Generations come back with margin, and pasting them unscaled is why
    earlier sprites read as small and floaty against the reference art.
    """
    bb = content_bbox(im)
    if not bb:
        return im.resize((tw, th), Image.NEAREST)
    fig = im.convert("RGBA").crop(bb)
    fw, fh = fig.size
    # Reserve `headroom` rows at the top. The reference leaves row 0 clear and
    # seats the figure on the bottom row; asking the model for that never
    # worked, so it is imposed here instead.
    avail = th - headroom
    scale = avail / fh
    nw = max(1, min(tw, round(fw * scale)))
    fig = fig.resize((nw, avail), Image.LANCZOS)
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    out.paste(fig, ((tw - nw) // 2, headroom))
    return out


def k_centroid(token, im, tw, th, workdir):
    """Area-weighted downscale via the free RD edit tool, with a local fallback.

    A naive nearest resample of a 2x source turns to mush at 48x34; this is
    what keeps it legible. Free, so it never needs a cost check.
    """
    tmp = os.path.join(workdir, "_kc_in.png")
    im.save(tmp)
    payload = {"input_image": base64.b64encode(open(tmp, "rb").read()).decode(),
               "width": tw, "height": th}
    r = api(token, "POST", "/edit/tools/k_centroid_downscale", payload, timeout=90)
    save_json(os.path.join(workdir, "kcentroid_response.json"),
              {k: v for k, v in r.items() if k != "base64_images"})
    imgs = r.get("base64_images") or []
    if not imgs:
        print(f"  ! k_centroid failed ({str(r.get('detail'))[:80]}); "
              f"falling back to local LANCZOS")
        return im.resize((tw, th), Image.LANCZOS)
    out = os.path.join(workdir, "_kc_out.png")
    open(out, "wb").write(base64.b64decode(imgs[0]))
    return Image.open(out).convert("RGBA")


def threshold_alpha(im, cut=128):
    """Binary alpha. Soft edges halo against unknown terrain at integer scale."""
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255 if a >= cut else 0)
    return im


# ---- QA --------------------------------------------------------------------

def qa(im, job):
    """Measure the spec's done-criteria. Returns (rows, ok)."""
    tw, th = job["target"]
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    opaque = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] == 255]
    partial = sum(1 for y in range(h) for x in range(w) if 0 < px[x, y][3] < 255)
    colours = {px[x, y][:3] for (x, y) in opaque}
    xs = [p[0] for p in opaque] or [0]
    ys = [p[1] for p in opaque] or [0]
    fw = max(xs) - min(xs) + 1
    fh = max(ys) - min(ys) + 1
    figure = job.get("figure", True)

    rows = [
        ("dimensions", f"{w}x{h}", (w, h) == (tw, th)),
        ("partial-alpha px", partial, partial == 0),
        ("unique colours", len(colours), 12 <= len(colours) <= 40),
    ]
    if figure:
        top, bot = min(ys), max(ys)
        # Headroom, not maximum fill. Requiring fh == th rewards a figure whose
        # crest runs into the top edge and gets clipped -- which is exactly what
        # the first Hastati did, and this check passed it. The reference art
        # leaves row 0 empty and occupies rows 1..33.
        rows += [
            ("top row clear", f"starts row {top}", top >= 1),
            ("fills height", f"{fh}/{th} rows", fh >= th - 3),
            ("silhouette width", f"{fw}/{tw} cols", fw >= 30),
            ("feet on bottom row", bot == h - 1, bot == h - 1),
        ]
    return rows, all(ok for _, _, ok in rows)


def contact_sheet(final, job, workdir, pack="assets/glory-of-rome"):
    """The check that cannot be automated: the sprite over real terrain.

    A metric once scored a floating object as a seamless tile because its
    edges were uniform. Numbers get fooled; this gets looked at.
    """
    grounds = ["grass", "forest", "desert"]
    tiles = []
    for g in grounds:
        p = os.path.join(pack, "art/tiles", f"{g}.png")
        if os.path.exists(p):
            tiles.append((g, Image.open(p).convert("RGBA")))
    if not tiles:
        return None
    tw, th = job["target"]
    zooms = (1, 3, 8)
    pad = 10
    W = sum(tw * z + pad for z in zooms) * len(tiles) + pad
    H = th * max(zooms) + 30
    sheet = Image.new("RGBA", (W, H), (24, 24, 28, 255))
    x = pad
    for name, ground in tiles:
        for z in zooms:
            cell = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
            cell.paste(ground.resize((tw, th), Image.NEAREST), (0, 0))
            cell.alpha_composite(final)
            sheet.paste(cell.resize((tw * z, th * z), Image.NEAREST), (x, 20))
            x += tw * z + pad
    out = os.path.join(workdir, "contact_sheet.png")
    sheet.save(out)
    return out


def transform(token, raw, job, work):
    """No-op when the job is raw_only: the deliverable is the image exactly as
    Retro Diffusion returned it. Anything done here is post-processing."""
    if job.get("raw_only"):
        p = os.path.join(work, "01_raw.png")
        print("  raw_only: delivering the generated image untouched")
        return p

    """Raw -> final, resampling only when the source is genuinely larger.

    A raw already at target size is passed through untouched apart from the
    alpha threshold. Resampling it anyway -- which this did at first, by
    always scaling to 2x target and downscaling back -- turned a clean 31
    colour sprite into 779 colours of mush. Measured, not theorised.
    """
    tw, th = job["target"]
    rw, rh = raw.size
    stage = raw
    if (rw, rh) != (tw, th):
        if job.get("figure", True):
            stage = fill_to_target(stage, min(rw, tw * 2), min(rh, th * 2),
                                   headroom=job.get("headroom_rows", 0))
            stage.save(os.path.join(work, "02_filled.png"))
        stage = k_centroid(token, stage, tw, th, work)
        stage.save(os.path.join(work, "03_downscaled.png"))
    else:
        print("  raw already at target size; no resample")
    final = threshold_alpha(stage)
    p = os.path.join(work, "04_final.png")
    final.save(p)
    return p


def cmd_reprocess(args):
    """Redo transforms + QA from the saved raw. No generation, no charge."""
    tok = read_token(args.token_file)
    job = load_job(args.job)
    work = latest_run_dir(job["id"])
    if not work:
        sys.exit(f"rdgen: no runs for {job['id']}")
    raw_p = os.path.join(work, "01_raw.png")
    if not os.path.exists(raw_p):
        sys.exit(f"rdgen: no saved raw at {raw_p}; run it first")
    raw = Image.open(raw_p).convert("RGBA")
    print(f"reprocessing {job['id']} from raw {raw.size} (no charge)")
    frames = write_frames(raw_p, job, work)
    if frames:
        widths, growth = motion_report(frames)
        print(f"frames {len(frames)}  ->  {work}/frame_00..{len(frames)-1:02d}.png")
        print(f"  silhouette widths {widths}")
        print(f"  [{'PASS' if growth >= MOTION_GATE_PCT else 'FAIL'}] motion "
              f"{growth}% growth over frame 0 (gate {MOTION_GATE_PCT}%)")

    final_p = transform(tok, raw, job, work)
    final = Image.open(final_p).convert("RGBA")
    rows, ok = qa(final, job)
    print(f"\nQA  {job['id']}")
    for name, val, good in rows:
        print(f"  [{'PASS' if good else 'FAIL'}] {name:<20} {val}")
    sheet = contact_sheet(final, job, work)
    if sheet:
        print(f"\ncontact sheet: {sheet}")
    print(f"final: {final_p}")
    print(f"\nverdict: {'PASS' if ok else 'NEEDS WORK'} "
          f"(metrics only -- look at the contact sheet before accepting)")


# ---- commands --------------------------------------------------------------

def cmd_balance(args):
    tok = read_token(args.token_file)
    r = api(tok, "GET", "/inferences/credits")
    print(json.dumps(r))


def cmd_cost(args):
    tok = read_token(args.token_file)
    job = load_job(args.job)
    r = api(tok, "POST", "/inferences", request_payload(job, check_cost=True))
    print(f"{job['id']}: ${r.get('balance_cost')}  "
          f"(remaining ${r.get('remaining_balance')})")


def cmd_run(args):
    tok = read_token(args.token_file)
    job = load_job(args.job)
    work = new_run_dir(job["id"])
    shutil.copy(job["_path"], os.path.join(work, "job.json"))
    print(f"run dir {work}")

    payload = request_payload(job)
    save_json(os.path.join(work, "request.json"),
              {k: v for k, v in payload.items() if k != "input_image"})

    cost = api(tok, "POST", "/inferences", request_payload(job, check_cost=True))
    print(f"cost ${cost.get('balance_cost')}  balance ${cost.get('remaining_balance')}")
    if args.dry_run:
        print("dry run; nothing submitted")
        return

    sub = api(tok, "POST", "/inferences", payload)
    # Written before ANY polling: a lost task id means a paid result that
    # cannot be retrieved.
    save_json(os.path.join(work, "task.json"), sub)
    task = sub.get("task_id")
    if not task:
        sys.exit(f"rdgen: submit failed: {json.dumps(sub)[:300]}")
    print(f"task {task} (saved to {work}/task.json)")

    result = None
    for i in range(120):
        time.sleep(4)
        s = api(tok, "GET", f"/inferences/tasks/{task}")
        st = s.get("status")
        if st in ("succeeded", "failed"):
            save_json(os.path.join(work, "poll.json"),
                      {k: v for k, v in s.items() if k != "result"})
            result = s
            print(f"[{(i+1)*4}s] {st}")
            break
    if not result or result.get("status") != "succeeded":
        sys.exit(f"rdgen: job did not succeed: {json.dumps(result)[:300]}")

    res = result.get("result") or {}
    imgs = res.get("base64_images") or []
    if not imgs:
        sys.exit(f"rdgen: no image returned: {json.dumps(res)[:300]}")
    print(f"charged ${res.get('balance_cost')}  request {res.get('request_id')}")

    # Write EVERY image the response carries, not just the first. Options like
    # return_non_bg_removed and return_pre_palette return a second paid image in
    # the same list, and keeping only imgs[0] silently destroys it.
    raw_p = os.path.join(work, "01_raw.png")
    open(raw_p, "wb").write(base64.b64decode(imgs[0]))
    for i, extra in enumerate(imgs[1:], start=2):
        extra_p = os.path.join(work, f"01_raw_{i}.png")
        open(extra_p, "wb").write(base64.b64decode(extra))
        print(f"also returned: {extra_p}")
    raw = Image.open(raw_p).convert("RGBA")
    print(f"raw {raw.size}  ({len(imgs)} image(s) returned)")

    frames = write_frames(raw_p, job, work)
    if frames:
        widths, growth = motion_report(frames)
        print(f"frames {len(frames)}  ->  {work}/frame_00..{len(frames)-1:02d}.png")
        print(f"  silhouette widths {widths}")
        print(f"  [{'PASS' if growth >= MOTION_GATE_PCT else 'FAIL'}] motion "
              f"{growth}% growth over frame 0 (gate {MOTION_GATE_PCT}%)")
    final_p = transform(tok, raw, job, work)
    final = Image.open(final_p).convert("RGBA")
    rows, ok = qa(final, job)
    print(f"\nQA  {job['id']}")
    for name, val, good in rows:
        print(f"  [{'PASS' if good else 'FAIL'}] {name:<20} {val}")
    sheet = contact_sheet(final, job, work)
    if sheet:
        print(f"\ncontact sheet: {sheet}")
    print(f"final: {final_p}")
    print(f"\nverdict: {'PASS' if ok else 'NEEDS WORK'} "
          f"(metrics only -- look at the contact sheet before accepting)")


# ---- animation frames ------------------------------------------------------

def write_frames(raw_p, job, work):
    """Split an animation result into individual frames.

    The grid is DERIVED from what came back, never assumed. A 4-frame sheet is
    2x2 and a 6-frame sheet is 3x2; this was hardcoded to 2x2 by hand outside
    this file, which sliced every 6-frame run through the middle of each frame.
    Two good animations were discarded and the pipeline was locked at four
    frames on the strength of that mistake.

    A GIF response is authoritative about its own frame count, so nothing is
    cut at all in that case.
    """
    im = Image.open(raw_p)
    if getattr(im, "n_frames", 1) > 1:
        frames = [f.convert("RGBA") for f in ImageSequence.Iterator(im)]
    else:
        w, h = job["width"], job["height"]
        sw, sh = im.size
        if sw % w or sh % h:
            sys.exit(f"rdgen: sheet {sw}x{sh} is not a whole number of {w}x{h} "
                     f"cells -- refusing to cut it into sliced frames")
        cols, rows = sw // w, sh // h
        if cols * rows < 2:
            return []
        im = im.convert("RGBA")
        frames = [im.crop((c * w, r * h, c * w + w, r * h + h))
                  for r in range(rows) for c in range(cols)]
    out = []
    for i, f in enumerate(frames):
        fp = os.path.join(work, f"frame_{i:02d}.png")
        f.save(fp)
        out.append(fp)
    return out


# The line between an animation that reads as an attack and one that reads as
# walking, measured across all 25 runs generated before this gate existed: real
# attacks grew the silhouette by 28-34% over the first frame, everything that
# looked like a walk grew it by under 7%.
MOTION_GATE_PCT = 25


def motion_report(frame_paths):
    widths = []
    for fp in frame_paths:
        bb = Image.open(fp).convert("RGBA").getbbox()
        widths.append(bb[2] - bb[0] if bb else 0)
    base = widths[0] or 1
    return widths, round(100 * (max(widths) - base) / base)


def main():
    ap = argparse.ArgumentParser(description="Retro Diffusion art pipeline")
    ap.add_argument("--token-file", default=DEFAULT_TOKEN_FILE,
                    help=f"file holding the rdpk- key (default {DEFAULT_TOKEN_FILE})")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("balance"); b.set_defaults(fn=cmd_balance)
    c = sub.add_parser("cost");    c.add_argument("job"); c.set_defaults(fn=cmd_cost)
    r = sub.add_parser("run");     r.add_argument("job")
    r.add_argument("--dry-run", action="store_true",
                   help="cost-check only, submit nothing")
    r.set_defaults(fn=cmd_run)
    rp = sub.add_parser("reprocess"); rp.add_argument("job")
    rp.set_defaults(fn=cmd_reprocess)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
