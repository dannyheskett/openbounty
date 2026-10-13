"""Forest/grass corner-wang set, 32 px, quilted from RD landscapes. Every pixel is a source pixel.
Borders are shared pieces (corner patches, edge strips) so any two tiles with matching corners join exactly."""
import numpy as np, itertools, sys, os, json, random
from PIL import Image
from numpy.lib.stride_tricks import sliding_window_view
T, w, o = 32, 4, 6          # tile, mandatory shared border, overlap band for the cut
K = w + o                   # how far a shared piece reaches into a tile
import argparse
ap = argparse.ArgumentParser(description="cut a landscape render into a 32 px corner set (tiles_meta.json + tile_NN.png) for romeart.py stitch")
ap.add_argument("--src", nargs="+", required=True, help="landscape render(s), 256 px")
ap.add_argument("--upper", required=True, choices=("forest", "water", "wheat", "plough", "sand", "road"), help="the terrain inside")
ap.add_argument("--out", required=True, help="corner set folder")
ap.add_argument("--grass-tiles", type=int, default=0, metavar="N", help="also write N plain grass tiles (96 px) as grass_p1..pN.png beside the corner set: the all-grass variants laid at random, so plain grass does not repeat every 32 px")
ap.add_argument("--field", metavar="PNG", help="also write a 576x480 open field (the combat board) of the all-grass variants laid at random")
ap.add_argument("--grass", help="meadow render: every all-grass piece (corner, edge strips, plain tiles) comes from it, so every set's grass is the same")
ap.add_argument("--upper-windows", type=int, default=0, metavar="N", help="also write N whole 96 px windows of the upper terrain (<upper>_window_N.png) and its shared band (<upper>_band_*.png)")
ap.add_argument("--grass-variants", type=int, default=5, metavar="N", help="how many different all-grass 32 px interiors to quilt (default 5); more means plain grass repeats less")
ap.add_argument("--sea-range", type=float, default=9, help="with --even-sea: how far in shade the sea may vary (0: one flat colour)")
ap.add_argument("--tol", type=int, default=6, help="px a shore may cross a tile edge away from its middle (strict matching: 3)")
ap.add_argument("--loose", action="store_true", help="allow a loose crossing (14 px) for a shape nothing strict fits")
ap.add_argument("--count", action="store_true", help="only count the windows each shape has, then stop")
ap.add_argument("--straight", type=int, default=0, metavar="PX", help="straight-edge tiles only from windows whose boundary stays within PX of the middle all the way across")
ap.add_argument("--rot", action="store_true", help="with --flips: the transposed orientations too, so a straight horizontal edge serves the vertical sides")
ap.add_argument("--flips", action="store_true", help="use every render in all four orientations too (shores in every direction; the light's side swaps on flipped pieces)")
ap.add_argument("--even-sand", action="store_true", help="map every render's sand onto the first render's sand colours, by shade")
ap.add_argument("--sand-width", default=None, metavar="MIN,MAX", help="px of sand a shore may show where it crosses a tile edge (strict sets, e.g. 8,16)")
ap.add_argument("--even-sea", action="store_true", help="flatten every render's sea to one even tone (the middle of the first render's blues) before cutting")
ap.add_argument("--path", action="store_true", help="keep the old forest render's light path colour (142,167,72) out of every piece")
ap.add_argument("--tile", type=int, default=32, help="piece size: 32 for a corner set that romeart.py stitch lays 3x3; 96 for whole tiles written directly (--direct)")
ap.add_argument("--overlap", type=int, default=None, help="overlap band for the cuts (default 6 at 32 px, 16 at 96 px)")
ap.add_argument("--direct", metavar="TERRAIN", help="at --tile 96: write the finished tiles by name (<TERRAIN>, <TERRAIN>_edge_*, <TERRAIN>_inner_*) into --out, variants as _vN")
ap.add_argument("--grass-swap", action="store_true", help="with --direct and --grass: the grass inside each tile comes from the meadow, not the render")
ARGS = ap.parse_args() if __name__ == "__main__" else None
if ARGS:
    T = ARGS.tile; o = ARGS.overlap if ARGS.overlap is not None else (6 if T == 32 else 16); K = w + o
SRC = ARGS.src if ARGS else []
PATH = np.array([142, 167, 72])
imgs, masks, certs = [], [], []
_srcs = []
for f in SRC:
    _a0 = np.array(Image.open(f).convert("RGB")).astype(int)
    # a pale frame (some seeds draw one round the picture) is not terrain: drop every edge row and column brighter than 230
    _l = _a0.mean(2); _y0 = _y1 = _x0 = _x1 = 0
    _fr = lambda line: line.mean() > 230 or ((line[:, 2] > line[:, 1]) & (line[:, 1] > line[:, 0]) & (line[:, 2] - line[:, 0] > 50)).mean() > 0.5   # white, or a saturated blue canvas (never grey-blue stone)
    while _fr(_a0[_y0]): _y0 += 1
    while _fr(_a0[-1 - _y1]): _y1 += 1
    while _fr(_a0[:, _x0]): _x0 += 1
    while _fr(_a0[:, -1 - _x1]): _x1 += 1
    if _y0 + _y1 + _x0 + _x1: _a0 = _a0[_y0 + 2:_a0.shape[0] - _y1 - 2, _x0 + 2:_a0.shape[1] - _x1 - 2]; print(f"{f}: pale frame trimmed (top {_y0}, bottom {_y1}, left {_x0}, right {_x1}, plus 2)")
    _srcs.append(_a0)
    if ARGS and ARGS.flips: _srcs += [_a0[:, ::-1], _a0[::-1], _a0[::-1, ::-1]]
    if ARGS and ARGS.rot: _srcs += [_t for _b in (_a0, _a0[:, ::-1], _a0[::-1], _a0[::-1, ::-1]) for _t in (_b.transpose(1, 0, 2),)]
for a in _srcs:
    a = a.copy()
    lum = a.sum(2) / 3
    R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    up = ARGS.upper if ARGS else "forest"
    if up == "forest":  m = (lum < 95) & (G >= R)
    elif up == "water": m = (B > G) & (B > R)
    elif up == "wheat": m = (R > G) & (R - B > 20)              # the ears and the brown row gaps between them
    elif up == "plough": m = (R > G) & (lum < 130)
    elif up == "road":
        mx_, mn_ = a.max(2), a.min(2)
        m = ((mx_ - mn_) < 40) & (lum > 80) | ((R >= G) & (lum > 80) & (R - B < 70) & ~((G > R) & (G > B)))   # paving: any grey, cool or warm, or warm beige; never green
    else:               m = ~((G > R) & (G > B)) & (lum >= 110) & (lum < 236)   # sand: anything not green, not dark, not frame-white
    m = m.astype(float)
    k = 9
    c = sliding_window_view(np.pad(m, k // 2, mode="edge"), (k, k)).mean((2, 3))
    imgs.append(a); masks.append(m); certs.append(c)
# the light path colour of the first forest render had to be kept out of the pieces; only when asked (--path), since the palette uses that colour elsewhere
if ARGS and ARGS.even_sea and imgs:
    # one even sea: every blue pixel of every render mapped by shade onto the middle of the first render's sea ramp
    blue = lambda a: (a[..., 2] > a[..., 1] + 8) & (a[..., 2] > a[..., 0] + 25) & ((0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]) >= 110)
    _ramp = np.unique(imgs[0][blue(imgs[0])], axis=0); _lum = lambda c: 0.3 * c[..., 0] + 0.59 * c[..., 1] + 0.11 * c[..., 2]
    _rl = _lum(_ramp); _mid = float(np.median(_rl)); _keep = _ramp[np.abs(_rl - _mid) <= ARGS.sea_range]; _kl = _lum(_keep)
    for _a in imgs:
        _m = blue(_a)
        for _c in np.unique(_a[_m], axis=0):
            _t = _mid + (_lum(_c) - _mid) * 0.2
            _d = _keep[int(np.argmin((_kl - _t) ** 2 * 4 + ((_keep - _c) ** 2).sum(1) * 0.05))]
            _a[_m & (_a[..., 0] == _c[0]) & (_a[..., 1] == _c[1]) & (_a[..., 2] == _c[2])] = _d
def sandy(a):
    R_, G_, B_ = a[..., 0], a[..., 1], a[..., 2]; L_ = 0.3 * R_ + 0.59 * G_ + 0.11 * B_
    if ARGS and ARGS.upper == "sand": return ~((G_ > R_) & (G_ > B_)) & (L_ >= 110) & (L_ < 236)      # a desert: everything not green, not frame-white
    mx = np.maximum(np.maximum(R_, G_), B_); mn = np.minimum(np.minimum(R_, G_), B_)
    return (((R_ >= G_) & (G_ > B_)) | ((mx - mn) < 30)) & (L_ >= 150) & (L_ < 230)     # pale sand, or pale wet grey sand
if ARGS and ARGS.even_sand and imgs:
    # every render's sand takes the first render's sand colours, matched by rank of shade (a light-sanded
    # render's lightest pixels become the first render's lightest, its darkest its darkest), so one shore
    _u, _n = np.unique(imgs[0][sandy(imgs[0])], axis=0, return_counts=True); _ramp = _u[np.argsort(-_n)[:4]]
    _lum = lambda c: 0.3 * c[..., 0] + 0.59 * c[..., 1] + 0.11 * c[..., 2]; _rl = _lum(_ramp)
    _ref = np.sort(_lum(imgs[0][sandy(imgs[0])]))
    for _a in imgs[1:]:
        _m = sandy(_a)
        if not _m.any(): continue
        _own = np.sort(_lum(_a[_m]))
        for _c in np.unique(_a[_m], axis=0):
            _q = np.searchsorted(_own, _lum(_c)) / max(len(_own) - 1, 1)                 # this colour's rank among its own sand
            _t = _ref[min(int(_q * (len(_ref) - 1)), len(_ref) - 1)]                      # the same rank in the first render's sand
            _d = _ramp[int(np.argmin((_rl - _t) ** 2))]
            _a[_m & (_a[..., 0] == _c[0]) & (_a[..., 1] == _c[1]) & (_a[..., 2] == _c[2])] = _d
    _m0 = sandy(imgs[0])
    for _c in np.unique(imgs[0][_m0], axis=0):                                          # the first render itself: its four colours only
        if (_ramp == _c).all(1).any(): continue
        _d = _ramp[int(np.argmin((_rl - _lum(_c)) ** 2))]; imgs[0][_m0 & (imgs[0][..., 0] == _c[0]) & (imgs[0][..., 1] == _c[1]) & (imgs[0][..., 2] == _c[2])] = _d
paths = [((np.abs(im - PATH).sum(2) == 0).astype(float) if (ARGS and ARGS.path) else np.zeros(im.shape[:2])) for im in imgs]
GBASE = None          # the meadow's flat base colour: its shared bands must be only that
if ARGS and ARGS.grass:
    _g = np.array(Image.open(ARGS.grass).convert("RGB")).astype(int).reshape(-1, 3)
    _u, _c = np.unique(_g, axis=0, return_counts=True); GBASE = _u[_c.argmax()]

def rocky(reg):
    """grey pixels: neither green, blue nor sand; on a desert also anything dark (props: barrels, a well, logs) or blue"""
    r, g, b = reg[..., 0], reg[..., 1], reg[..., 2]; mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
    grey = ((mx - mn) < 28) & (mx > 60) & (mx < 210)
    if ARGS and ARGS.upper == "sand": return grey | (((0.3 * r + 0.59 * g + 0.11 * b) < 95) & ~((g > r) & (g > b))) | ((b > g) & (b > r))
    return grey

def sand_ok(i, pts):
    """True unless --sand-width is set and the sand run along these points falls outside it"""
    if not (ARGS and ARGS.sand_width): return True
    lo, hi = (int(v) for v in ARGS.sand_width.split(","))
    n = sum(1 for y_, x_ in pts if sandy(imgs[i][y_:y_ + 1, x_:x_ + 1])[0, 0])
    return lo <= n <= hi

def lab(i, y, x):            # label at a point, and how sure (0.5 = unsure)
    c = certs[i][y, x]; return int(c > 0.5), abs(c - 0.5)

def crossings(i, pts):
    labs = [int(certs[i][y, x] > 0.5) for y, x in pts]
    ch = [j for j in range(1, len(labs)) if labs[j] != labs[j - 1]]
    return labs, ch

def best_strip(a, b, vertical):
    """a 2K wide, T long strip whose centre line runs from terrain a to b, crossing once near the middle"""
    best = None
    for i, im in enumerate(imgs):
        H, W, _ = im.shape
        ys_ = range(0, H - T) if vertical else range(K, H - K)
        xs_ = range(K, W - K) if vertical else range(0, W - T)
        for y in ys_:
            for x in xs_:
                if vertical: pts = [(y + t, x) for t in range(T + 1)]
                else:        pts = [(y, x + t) for t in range(T + 1)]
                l0, s0 = lab(i, *pts[0]); l1, s1 = lab(i, *pts[-1])
                if (l0, l1) != (a, b) or min(s0, s1) < 0.4: continue
                labs, ch = crossings(i, pts)
                band_ = certs[i][y:y + T, x - K:x + K] if vertical else certs[i][y - K:y + K, x:x + T]
                if a == b:
                    if ch: continue
                    if ((band_ > 0.5) != bool(a)).any(): continue          # the whole band is that terrain, not just its centre line
                    score = -min(s0, s1)
                else:
                    if len(ch) != 1 or abs(ch[0] - T / 2) > 4 or not sand_ok(i, pts): continue      # the shore crosses the tile edge at its middle, with the set's sand width
                    score = abs(ch[0] - T / 2) - min(s0, s1)
                if vertical: pp = paths[i][y:y + T, x - K:x + K].mean()
                else:        pp = paths[i][y - K:y + K, x:x + T].mean()
                if pp > 0: continue          # no path pixels anywhere
                reg = im[y:y + T, x - K:x + K] if vertical else im[y - K:y + K, x:x + T]
                if a == b == 0:   # the field strip repeats along every field edge: take the plainest
                    band = reg[:, K - w:K + w] if vertical else reg[K - w:K + w, :]
                    score += 3 * (np.abs(reg - [114, 140, 58]).sum(2) > 0).mean()
                if ARGS and ARGS.upper in ("water", "sand"):   # a shore strip repeats along every shore: rocks or props only if nothing else exists
                    rk = rocky(reg).mean()
                    if rk > 0.02: continue
                    score += 200 * rk
                if best is None or score < best[0]:
                    best = (score, i, y, x)
    if best is None:
        sys.exit(f"landcut: no shared strip from terrain {a} to {b} ({'vertical' if vertical else 'horizontal'}) at {T} px: the renders hold no {T} px run crossing the shore once near its middle")
    _, i, y, x = best
    im = imgs[i]
    if vertical: return im[y:y + T, x - K:x + K]      # rows along the edge, cols across it
    return im[y - K:y + K, x:x + T]

def best_corner(t):
    best = None
    for i, im in enumerate(imgs):
        H, W, _ = im.shape
        for y in range(K, H - K, 2):
            for x in range(K, W - K, 2):
                if paths[i][y - K:y + K, x - K:x + K].any(): continue
                blk = masks[i][y - K:y + K, x - K:x + K].mean()
                s = (blk if t else 1 - blk) - 2 * paths[i][y - K:y + K, x - K:x + K].mean()
                if not t:   # the field corner repeats at every corner point: take the plainest patch
                    s -= 3 * (np.abs(im[y - K:y + K, x - K:x + K] - [114, 140, 58]).sum(2) > 0).mean()
                if ARGS and ARGS.upper in ("water", "sand"):
                    rk = rocky(im[y - K:y + K, x - K:x + K]).mean()
                    if rk > 0.02: continue
                    s -= 200 * rk
                if best is None or s > best[0]: best = (s, i, y, x)
    _, i, y, x = best
    return imgs[i][y - K:y + K, x - K:x + K]

def spiky(m):
    """share of forest pixels that a 5x5 opening removes: thin spikes and stray single trees"""
    p = np.pad(m, 2, mode="edge")
    er = sliding_window_view(p, (5, 5)).min((2, 3))
    di = sliding_window_view(np.pad(er, 2, mode="edge"), (5, 5)).max((2, 3))
    return (m - di).clip(0).sum() / max(m.sum(), 1)

def windows(sig, n=3, tol=None):
    tol = ARGS.tol if (tol is None and ARGS) else (tol or 6)
    """interior windows whose corners match sig, edges crossing within tol px of the middle"""
    out = []
    for i, im in enumerate(imgs):
        H, W, _ = im.shape
        for y in range(0, H - T, 2):
            for x in range(0, W - T, 2):
                cs = [(y, x), (y, x + T - 1), (y + T - 1, x), (y + T - 1, x + T - 1)]
                ls = [lab(i, *p) for p in cs]
                if tuple(l for l, _ in ls) != sig or min(s for _, s in ls) < 0.3: continue
                pen = 0
                for (p, q) in ((0, 1), (2, 3), (0, 2), (1, 3)):
                    if sig[p] != sig[q]:
                        (ya, xa), (yb, xb) = cs[p], cs[q]
                        pts = [(ya + (yb - ya) * t // (T - 1), xa + (xb - xa) * t // (T - 1)) for t in range(T)]
                        _, ch = crossings(i, pts)
                        if len(ch) != 1 or abs(ch[0] - T / 2) > tol or not sand_ok(i, pts): pen = None; break
                        pen += abs(ch[0] - T / 2)
                if pen is None: continue
                if ARGS and ARGS.straight and sum(sig) == 2 and sig not in ((0, 1, 1, 0), (1, 0, 0, 1)):
                    # a straight edge: the boundary must run straight across the whole window, within --straight px of the middle
                    lab_ = certs[i][y:y + T, x:x + T] > 0.5
                    if sig[0] == sig[1]: cross = [int(np.argmax(lab_[:, c] != lab_[0, c])) for c in range(T)]    # top/bottom: per column
                    else:                cross = [int(np.argmax(lab_[r] != lab_[r, 0])) for r in range(T)]       # left/right: per row
                    if max(cross) - min(cross) > ARGS.straight or any(abs(v - T / 2) > ARGS.straight for v in cross): continue
                    pen += (max(cross) - min(cross))
                pen += 600 * paths[i][y:y + T, x:x + T].mean()   # path pixels: only if nothing else fits
                if paths[i][y + K:y + T - K, x + K:x + T - K].any(): continue   # no path inside the cut band
                pen += 300 * spiky(masks[i][max(0, y - 3):y + T + 3, max(0, x - 3):x + T + 3])
                if ARGS and ARGS.upper in ("water", "sand"):
                    rk = rocky(im[y:y + T, x:x + T]).mean()
                    if ARGS.upper == "sand" and rk > 0.01: continue        # no props in any desert piece
                    pen += 400 * rk   # grey: rocks, cast shadows
                out.append((pen, i, y, x))
    out.sort()
    picked = []
    for pen, i, y, x in out:
        if all(abs(y - py) > 12 or abs(x - px) > 12 or i != pi for _, pi, py, px in picked):
            picked.append((pen, i, y, x))
        if len(picked) == n: break
    return [imgs[i][y:y + T, x:x + T] for _, i, y, x in picked]

def cut_mask(E, N):
    """Efros-Freeman: columns of a band (rows x o); True where the new piece N is kept (left of the min path)"""
    e = ((E - N) ** 2).sum(2).astype(float)
    h, b = e.shape
    D = e.copy(); back = np.zeros((h, b), int)
    for r in range(1, h):
        for c in range(b):
            lo, hi = max(0, c - 1), min(b, c + 2)
            j = lo + int(np.argmin(D[r - 1, lo:hi])); D[r, c] += D[r - 1, j]; back[r, c] = j
    keep = np.zeros((h, b), bool)
    c = int(np.argmin(D[-1]))
    for r in range(h - 1, -1, -1):
        keep[r, :c + 1] = True; c = back[r, c]
    return keep

TF = {"left": (lambda a: a, lambda a: a),
      "right": (lambda a: a[:, ::-1], lambda a: a[:, ::-1]),
      "top": (lambda a: a.transpose(1, 0, 2), lambda a: a.transpose(1, 0, 2)),
      "bottom": (lambda a: a[::-1].transpose(1, 0, 2), lambda a: a.transpose(1, 0, 2)[::-1])}

def place(tile, full, side):
    """full: a T x T canvas holding the shared piece in its region. Outer w kept exactly, cut in the next o."""
    f, inv = TF[side]
    t, p = f(tile).copy(), f(full)
    t[:, :w] = p[:, :w]
    keep = cut_mask(t[:, w:K], p[:, w:K])
    sub = t[:, w:K]; sub[keep] = p[:, w:K][keep]
    return inv(t).copy()

def place_corner(tile, full, corner):
    """outer w x w square kept exactly; inside the K x K square the patch is kept up to two cuts"""
    f = {"tl": lambda a: a, "tr": lambda a: a[:, ::-1], "bl": lambda a: a[::-1], "br": lambda a: a[::-1, ::-1]}[corner]
    t, p = f(tile).copy(), f(full)
    t[:w, :w] = p[:w, :w]
    E, N = t[w:K, w:K], p[w:K, w:K]
    v = cut_mask(E, N)                                             # keep left of a top-to-bottom path
    h = cut_mask(E.transpose(1, 0, 2), N.transpose(1, 0, 2)).T     # keep above a left-to-right path
    keep = v & h
    E[keep] = N[keep]
    return f(t).copy()

def canvas(region, arr):
    c = np.zeros((T, T, 3), int); y0, y1, x0, x1 = region; c[y0:y1, x0:x1] = arr; return c

def quilt_field(n, seed=7):
    """plain-field interiors: 16 px patches of plain field (no forest, shadow or path) joined along min-error cuts"""
    import random
    rng = random.Random(seed)
    a = imgs[0]; lum = a.sum(2) / 3
    bad = (masks[0] > 0) | (lum < 80) | (paths[0] > 0)
    P, ov = 16, 4
    cand = [(y, x) for y in range(0, a.shape[0] - P + 1) for x in range(0, a.shape[1] - P + 1) if not bad[y:y + P, x:x + P].any()]
    out = []
    for _ in range(n):
        S = P + 2 * (P - ov)
        img = np.zeros((S, S, 3), int)
        for r in range(3):
            for c in range(3):
                y0, x0 = r * (P - ov), c * (P - ov)
                best = None
                for y, x in rng.sample(cand, 60):
                    p = a[y:y + P, x:x + P]
                    e = 0
                    if c: e += ((img[y0:y0 + P, x0:x0 + ov] - p[:, :ov]) ** 2).sum()
                    if r: e += ((img[y0:y0 + ov, x0:x0 + P] - p[:ov, :]) ** 2).sum()
                    if best is None or e < best[0]: best = (e, p)
                p = best[1]
                keep = np.ones((P, P), bool)
                if c:
                    m = cut_mask(p[:, :ov], img[y0:y0 + P, x0:x0 + ov])   # True = keep existing (left of path)
                    keep[:, :ov] &= ~m
                if r:
                    m = cut_mask(p[:ov, :].transpose(1, 0, 2), img[y0:y0 + ov, x0:x0 + P].transpose(1, 0, 2)).T
                    keep[:ov, :] &= ~m
                reg = img[y0:y0 + P, x0:x0 + P]; reg[keep] = p[keep]
        out.append(img[:T, :T].copy())
    return out

if __name__ == "__main__":
  print("finding shared pieces ...", flush=True)
  # with --grass the all-grass pieces come from the meadow below, so the render need not hold any
  C = {t: best_corner(t) for t in (0, 1) if not (ARGS.grass and t == 0)}
  V = {(a, b): best_strip(a, b, True) for a in (0, 1) for b in (0, 1) if not (ARGS.grass and a == b == 0)}
  Hs = {(a, b): best_strip(a, b, False) for a in (0, 1) for b in (0, 1) if not (ARGS.grass and a == b == 0)}
  FIELD = None
  if ARGS.grass:      # the shared grass pieces from the meadow: all lower, so every set built with it has the same grass
      g = np.array(Image.open(ARGS.grass).convert("RGB")).astype(int)
      keep = (imgs, masks, certs, paths)
      z = np.zeros(g.shape[:2])
      imgs, masks, certs, paths = [g], [z], [z], [z]        # a meadow has no path, whatever its colours
      def flat_band(piece, band):
          """drop, whole, every clump of the piece that touches its shared band, so the band is the flat
          base and no clump is sliced at a tile line (the pieces are copies; the meadow is untouched)"""
          piece = piece.copy(); d = (np.abs(piece - GBASE).sum(2) > 0); h_, w_ = d.shape; lab = np.zeros((h_, w_), int); k = 0
          for yy in range(h_):
              for xx in range(w_):
                  if d[yy, xx] and lab[yy, xx] == 0:
                      k += 1; st = [(yy, xx)]; lab[yy, xx] = k; pts = []
                      while st:
                          y_, x_ = st.pop(); pts.append((y_, x_))
                          for dy in (-1, 0, 1):
                              for dx in (-1, 0, 1):
                                  ny, nx = y_ + dy, x_ + dx
                                  if 0 <= ny < h_ and 0 <= nx < w_ and d[ny, nx] and lab[ny, nx] == 0:
                                      lab[ny, nx] = k; st.append((ny, nx))
                      if any(band[y_, x_] for y_, x_ in pts):
                          for y_, x_ in pts: piece[y_, x_] = GBASE
          return piece
      C[0] = best_corner(0); V[(0, 0)] = best_strip(0, 0, True); Hs[(0, 0)] = best_strip(0, 0, False)
      bc = np.zeros((2 * K, 2 * K), bool); bc[K - w:K + w, :] = True; bc[:, K - w:K + w] = True
      bv = np.zeros((T, 2 * K), bool); bv[:, K - w:K + w] = True
      bh = np.zeros((2 * K, T), bool); bh[K - w:K + w, :] = True
      C[0], V[(0, 0)], Hs[(0, 0)] = flat_band(C[0], bc), flat_band(V[(0, 0)], bv), flat_band(Hs[(0, 0)], bh)
      FIELD = quilt_field(ARGS.grass_variants)
      imgs, masks, certs, paths = keep
  tiles = {}
  if ARGS.count:
      for sig in itertools.product((0, 1), repeat=4):
          if sig in ((0, 0, 0, 0), (0, 1, 1, 0), (1, 0, 0, 1)): continue
          print("windows", sig, len(windows(sig, n=99)))
      sys.exit(0)
  for sig in itertools.product((0, 1), repeat=4):
      tl, tr, bl, br = sig
      if sig == (0, 0, 0, 0) and ARGS.direct: continue          # the plain grass tile is the meadow's, cut by --grass-tiles
      ws = (FIELD or quilt_field(5)) if sig == (0, 0, 0, 0) else windows(sig)
      if not ws and T > 32 and ARGS.loose:
          ws = windows(sig, tol=14)                                 # a looser crossing only for a shape nothing strict fits
          if ws: print("loose crossing for", sig)
      if not ws:
          tl, tr, bl, br = sig; mirror = (tr, tl, br, bl)          # the left-right mirror of the shape
          ms = windows(mirror) or (windows(mirror, tol=14) if T > 32 else [])
          if not ms:
              print("no window for", sig); continue
          ws = [w_[:, ::-1] for w_ in ms]; print("no window for", sig, "- mirrored", mirror)
      vs = []
      for win in ws:
          t = win.astype(int).copy()
          t = place(t, canvas((0, T, 0, K), V[(tl, bl)][:, K:]), "left")
          t = place(t, canvas((0, T, T - K, T), V[(tr, br)][:, :K]), "right")
          t = place(t, canvas((0, K, 0, T), Hs[(tl, tr)][K:, :]), "top")
          t = place(t, canvas((T - K, T, 0, T), Hs[(bl, br)][:K, :]), "bottom")
          t = place_corner(t, canvas((0, K, 0, K), C[tl][K:, K:]), "tl")
          t = place_corner(t, canvas((0, K, T - K, T), C[tr][K:, :K]), "tr")
          t = place_corner(t, canvas((T - K, T, 0, K), C[bl][:K, K:]), "bl")
          t = place_corner(t, canvas((T - K, T, T - K, T), C[br][:K, :K]), "br")
          # the shared borders last, so no cut overwrote them: edges, then the corner squares
          L, R = V[(tl, bl)][:, K:], V[(tr, br)][:, :K]; Tp, Bt = Hs[(tl, tr)][K:, :], Hs[(bl, br)][:K, :]
          t[:, :w] = L[:, :w]; t[:, T - w:] = R[:, K - w:]; t[:w, :] = Tp[:w, :]; t[T - w:, :] = Bt[K - w:, :]
          t[:w, :w] = C[tl][K:K + w, K:K + w]; t[:w, T - w:] = C[tr][K:K + w, K - w:K]
          t[T - w:, :w] = C[bl][K - w:K, K:K + w]; t[T - w:, T - w:] = C[br][K - w:K, K - w:K]
          vs.append(t.astype(np.uint8))
      lit = [int((np.abs(v.astype(int) - PATH).sum(2) < 25).sum()) for v in vs]
      if not ARGS.grass:   # the meadow may use the path colour itself
          vs = [v for v, n in zip(vs, lit) if n <= 15] or [vs[int(np.argmin(lit))]]   # drop tiles showing the light path
      tiles[sig] = vs
  os.makedirs(ARGS.out, exist_ok=True)
  if ARGS.direct:
      # the game's names for the corner signatures (tl, tr, bl, br; 1 = the terrain): sides that border grass, lone open corners
      NAMES = {(1, 1, 1, 1): "", (0, 0, 1, 1): "_edge_n", (1, 1, 0, 0): "_edge_s", (0, 1, 0, 1): "_edge_w", (1, 0, 1, 0): "_edge_e",
               (0, 0, 0, 1): "_edge_nw", (0, 0, 1, 0): "_edge_ne", (0, 1, 0, 0): "_edge_sw", (1, 0, 0, 0): "_edge_es",
               (1, 1, 1, 0): "_inner_es", (1, 1, 0, 1): "_inner_sw", (1, 0, 1, 1): "_inner_ne", (0, 1, 1, 1): "_inner_nw"}
      meadow = np.array(Image.open(ARGS.grass).convert("RGB")).astype(int) if (ARGS.grass_swap and ARGS.grass) else None
      rng_ = random.Random(11); written = 0
      for sig, suffix in NAMES.items():
          if sig not in tiles: print("direct: missing", ARGS.direct + suffix); continue
          for v, t in enumerate(tiles[sig]):
              t = t.astype(int).copy()
              if meadow is not None and sig != (1, 1, 1, 1):
                  R_, G_, B_ = t[..., 0], t[..., 1], t[..., 2]; L_ = 0.3 * R_ + 0.59 * G_ + 0.11 * B_
                  water_ = (B_ > G_ + 8) & (B_ > R_ + 25) & (L_ >= 110); sand_ = (R_ >= G_) & (G_ > B_) & (L_ >= 165) & (G_ >= 150); foam_ = L_ >= 215   # pale sand only; soil becomes meadow
                  green = ~(water_ | sand_ | foam_)                     # all other land (grass, soil, stones) becomes the meadow
                  if ARGS.upper == "sand": green = (G_ > R_) & (G_ > B_)   # a desert keeps every non-green pixel
                  my, mx = rng_.randrange(0, meadow.shape[0] - T + 1), rng_.randrange(0, meadow.shape[1] - T + 1)
                  t[green] = meadow[my:my + T, mx:mx + T][green]
              name = ARGS.direct + suffix + (f"_v{v}" if v else "")
              Image.fromarray(t.astype(np.uint8)).save(os.path.join(ARGS.out, name + ".png")); written += 1
      print("direct:", written, "tiles written")
  meta = []
  for k, sig in enumerate(sorted(tiles)):
      Image.fromarray(tiles[sig][0]).save(os.path.join(ARGS.out, f"tile_{k:02d}.png"))
      nw, ne, sw, se = sig
      name = lambda v: "upper" if v else "lower"
      meta.append({"id": str(k), "name": f"wang_{k:02d}", "corners": {"NW": name(nw), "NE": name(ne), "SW": name(sw), "SE": name(se)},
                   "pattern_4x4": {"row_0": [255] * 4, "row_1": [255, nw, ne, 255], "row_2": [255, sw, se, 255], "row_3": [255] * 4}})
  json.dump(meta, open(os.path.join(ARGS.out, "tiles_meta.json"), "w"), indent=1)
  import random
  grass = tiles.get((0, 0, 0, 0), [])  # every variant has the same shared border, so any arrangement joins
  def lay(rows, cols, seed):
      r = random.Random(seed)
      out = np.zeros((rows * T, cols * T, 3), np.uint8)
      for y in range(rows):
          for x in range(cols):
              out[y * T:(y + 1) * T, x * T:(x + 1) * T] = r.choice(grass)
      return out
  def clumps(a):
      """the meadow's grass clumps: connected groups of pixels unlike its flat base colour, each kept
      whole with its own pixels; exact repeats (RD repeats a render every 128 px) counted once"""
      u, cnt = np.unique(a.reshape(-1, 3), axis=0, return_counts=True); base = u[cnt.argmax()]
      diff = (np.abs(a - base).sum(2) > 0)
      H, W = diff.shape; lab = np.zeros((H, W), int); n = 0; out = {}
      for y0 in range(H):
          for x0 in range(W):
              if diff[y0, x0] and lab[y0, x0] == 0:
                  n += 1; st = [(y0, x0)]; lab[y0, x0] = n; pts = []
                  while st:
                      y, x = st.pop(); pts.append((y, x))
                      for dy in (-1, 0, 1):
                          for dx in (-1, 0, 1):
                              ny, nx = y + dy, x + dx
                              if 0 <= ny < H and 0 <= nx < W and diff[ny, nx] and lab[ny, nx] == 0:
                                  lab[ny, nx] = n; st.append((ny, nx))
                  ys = [q[0] for q in pts]; xs = [q[1] for q in pts]
                  y1, y2, x1, x2 = min(ys), max(ys) + 1, min(xs), max(xs) + 1
                  if y2 - y1 > 40 or x2 - x1 > 40 or len(pts) < 6: continue
                  if y1 == 0 or x1 == 0 or y2 == H or x2 == W: continue          # cut by the render's edge
                  m = np.zeros((y2 - y1, x2 - x1), bool)
                  for y, x in pts: m[y - y1, x - x1] = True
                  out.setdefault((a[y1:y2, x1:x2].tobytes(), m.tobytes()), (a[y1:y2, x1:x2].copy(), m))
      per_tile = n * (T * 3) ** 2 / (H * W)       # the render's density, as clumps per 96 px tile
      return base, list(out.values()), per_tile
  def window_tile(a, base, seed, S=96):
      """a plain S px tile: a whole random window of the meadow, distinct modulo the render's 128 px repeat.
      Its edges are finished by grassband.py, which lays the one shared 96 px band on every grass-facing side."""
      rng = random.Random(seed); H, W = a.shape[:2]
      for _ in range(500):
          y0, x0 = rng.randrange(0, H - S + 1), rng.randrange(0, W - S + 1)
          if (y0 % 128, x0 % 128) not in window_tile.used: break
      window_tile.used.add((y0 % 128, x0 % 128))
      return a[y0:y0 + S, x0:x0 + S].astype(np.uint8)
  window_tile.used = set()
  if ARGS.grass and ARGS.grass_tiles:
      G_ = np.array(Image.open(ARGS.grass).convert("RGB")).astype(int)
      BASE, CL, PER = clumps(G_)
      print("meadow clumps:", len(CL), "distinct, about", round(PER), "per tile; base", tuple(int(v) for v in BASE))
      # the one shared band every grass-facing tile side carries (grassband.py): a cross of the meadow,
      # K px either side of the tile line, 96 px long: the corner square, the band below it, the band right of it
      cy, cx = 40, 40
      Image.fromarray(G_[cy - K:cy + K, cx - K:cx + K].astype(np.uint8)).save(os.path.join(ARGS.out, "band_c.png"))
      Image.fromarray(G_[cy + K:cy + 96 - K, cx - K:cx + K].astype(np.uint8)).save(os.path.join(ARGS.out, "band_v.png"))
      Image.fromarray(G_[cy - K:cy + K, cx + K:cx + 96 - K].astype(np.uint8)).save(os.path.join(ARGS.out, "band_h.png"))
  for k in range(1, ARGS.grass_tiles + 1):
      if ARGS.grass:
          Image.fromarray(window_tile(G_, BASE, 100 + k)).save(os.path.join(ARGS.out, f"grass_p{k}.png"))
      else:
          Image.fromarray(lay(3, 3, 100 + k)).save(os.path.join(ARGS.out, f"grass_p{k}.png"))
  if ARGS.field:
      Image.fromarray(lay(15, 18, 7)).save(ARGS.field)
  if ARGS.upper_windows:
      # whole 96 px windows of the upper terrain (calm ones: least texture change), distinct, plus its shared band
      S_ = 3 * T; best = []
      for i, im in enumerate(imgs):
          sure = certs[i] > 0.5
          for y in range(0, im.shape[0] - S_ + 1, 4):
              for x in range(0, im.shape[1] - S_ + 1, 4):
                  win_ = sure[y:y + S_, x:x + S_]
                  if ARGS.upper == "sand" and rocky(im[y:y + S_, x:x + S_]).mean() > 0.01: continue      # no props in a plain desert tile
                  if win_.mean() >= 0.9 and win_[0].all() and win_[-1].all() and win_[:, 0].all() and win_[:, -1].all():   # the terrain, with its own shadows and stones, sure along the rim
                      reg = im[y:y + S_, x:x + S_]; best.append((float(np.abs(np.diff(reg, axis=0)).mean() + np.abs(np.diff(reg, axis=1)).mean()), i, y, x))
      best.sort(); used = []; k = 0
      best = best[len(best) // 3:]            # not the calmest: the middle of the range, so plain tiles match the shore tiles' water
      for _, i, y, x in best:
          if any(i == j and abs(y - yy) < 24 and abs(x - xx) < 24 for j, yy, xx in used): continue
          used.append((i, y, x)); k += 1
          Image.fromarray(imgs[i][y:y + S_, x:x + S_].astype(np.uint8)).save(os.path.join(ARGS.out, f"{ARGS.upper}_window_{k}.png"))
          if k == 1:   # the band: the terrain's own flat base colour (its most common colour in this window), so it hides in it
              reg = imgs[i][y:y + S_, x:x + S_].reshape(-1, 3); u_, c_ = np.unique(reg, axis=0, return_counts=True); base_ = u_[c_.argmax()]
              for nm, shp in (("c", (2 * K, 2 * K)), ("v", (96 - 2 * K, 2 * K)), ("h", (2 * K, 96 - 2 * K))):
                  Image.fromarray(np.tile(base_, (shp[0], shp[1], 1)).astype(np.uint8)).save(os.path.join(ARGS.out, f"{ARGS.upper}_band_{nm}.png"))
          if k >= ARGS.upper_windows: break
      print(ARGS.upper, "windows written:", k, "(of", len(best), "candidates)")
  print("patterns", len(tiles), "variants", {s: len(v) for s, v in tiles.items()})
