#!/usr/bin/env python3
"""Calm a supplied field painting: mask its clutter, fill the mask from the
painting's own texture, and cut the 6x5 field cells with siegeslice.

    python3 tools/fieldcalm.py <painting.png> <out-dir> <prefix> [--level light|medium|strong] [--fill patchmatch|lama] [--keep-mask mask.png]

Levels (what is repainted):
  light   grey boulders and small rocks, dark clumps (ferns, dense clover)
  medium  light + brown earth patches
  strong  medium + mid-dark leaf clusters and pale yellow-green moss patches
The mask is colour/size thresholds (OpenCV); --keep-mask writes it out so a
hand-edited copy can be passed back with --mask. Fill: G'MIC inpaint_matchpatch
(native resolution) or LaMa on CPU (run at 1024 px). Writes <out-dir>/mask.png,
<out-dir>/inpainted.png and the cells <out-dir>/cells/<prefix>_<x>_<y>.png.
Needs: gmic, python3-opencv, python3-numpy (apt); for --fill lama: torch (cpu),
torchvision, simple-lama-inpainting (pip --user).
Neither fill is deterministic (patch matching is randomly initialised, LaMa is
run at 1024 px), so the inpainted painting that shipped is kept beside the
source; re-running gives an equivalent but not identical field.
"""
import sys, os, subprocess, argparse
import cv2, numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("painting"); ap.add_argument("out"); ap.add_argument("prefix")
ap.add_argument("--level", default="medium", choices=["light", "medium", "strong"])
ap.add_argument("--fill", default="patchmatch", choices=["patchmatch", "lama"])
ap.add_argument("--mask", help="use this mask instead of building one (white = repaint)")
ap.add_argument("--dilate", type=int, default=15)
a = ap.parse_args()
os.makedirs(a.out + "/cells", exist_ok=True)
src = cv2.imread(a.painting, cv2.IMREAD_UNCHANGED)
if src.shape[2] == 3: src = np.dstack([src, np.full(src.shape[:2], 255, np.uint8)])
bgr, alpha = src[:, :, :3], src[:, :, 3]
K = lambda n: cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (n, n))

def blobs(mask, min_area):
    n, lab, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8); keep = np.zeros_like(mask)
    for i in range(1, n):
        if stats[i, cv2.CC_STAT_AREA] >= min_area: keep |= (lab == i)
    return keep

if a.mask:
    mask = cv2.imread(a.mask, cv2.IMREAD_GRAYSCALE)
else:
    hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV); H, S, V = [hsv[:, :, i].astype(int) for i in range(3)]
    content = (alpha > 16) & ~((bgr[:, :, 0] > 235) & (bgr[:, :, 1] > 235) & (bgr[:, :, 2] > 235))
    rocks = blobs(content & (S < 0.20 * 255) & (V > 0.42 * 255), 120)
    dark = blobs(content & (V < 0.40 * 255), 120)
    m = rocks | dark
    if a.level in ("medium", "strong"):
        m |= blobs(content & (H >= 8) & (H <= 30) & (S > 0.25 * 255) & (V > 0.35 * 255) & (V < 0.75 * 255), 400)
    if a.level == "strong":
        leaf = cv2.morphologyEx((content & (V < 0.50 * 255) & (S > 0.30 * 255)).astype(np.uint8), cv2.MORPH_OPEN, K(7)).astype(bool)
        moss = cv2.morphologyEx((content & (H >= 28) & (H <= 42) & (S > 0.35 * 255) & (V > 0.60 * 255)).astype(np.uint8), cv2.MORPH_OPEN, K(7)).astype(bool)
        m |= blobs(leaf, 250) | blobs(moss, 400)
    mask = cv2.dilate(m.astype(np.uint8) * 255, K(a.dilate)) & (content.astype(np.uint8) * 255)
cv2.imwrite(a.out + "/mask.png", mask)
print(f"mask: {(mask > 0).sum()} px repainted ({(mask > 0).sum() / max(1, (alpha > 16).sum()) * 100:.1f}% of the painting)")

if a.fill == "patchmatch":
    rgb_p = a.out + "/inpainted_rgb.png"
    r = subprocess.run(["gmic", a.painting, "-channels[0]", "0,2", a.out + "/mask.png", "-inpaint_matchpatch[0]", "[1],0,15,10,7,1", "-keep[0]", "-o", rgb_p], capture_output=True, text=True)
    if r.returncode: sys.exit("gmic failed: " + r.stderr[-400:])
    rgb = cv2.imread(rgb_p); out = np.dstack([rgb, alpha])
else:
    from PIL import Image
    from simple_lama_inpainting import SimpleLama
    S = 1024
    rgb_s = cv2.resize(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), (S, S), interpolation=cv2.INTER_AREA)
    m_s = cv2.resize(mask, (S, S), interpolation=cv2.INTER_NEAREST)
    res = np.array(SimpleLama()(Image.fromarray(rgb_s), Image.fromarray(m_s)).convert("RGB"))[:S, :S]
    out = np.dstack([cv2.cvtColor(res, cv2.COLOR_RGB2BGR), cv2.resize(alpha, (S, S), interpolation=cv2.INTER_AREA)])
cv2.imwrite(a.out + "/inpainted.png", out)
r = subprocess.run([sys.executable, "tools/siegeslice.py", a.out + "/inpainted.png", a.out + "/cells", "--field", a.prefix], capture_output=True, text=True)
print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr[-300:])
