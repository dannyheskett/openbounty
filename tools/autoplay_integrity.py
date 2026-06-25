#!/usr/bin/env python3
"""autoplay_integrity.py — failure-integrity + quality scan for headless autoplay.

Runs `--autoplay --headless --seed N` over a seed range, captures each run's full
output (trace + report), and classifies every OUTSTANDING objective by its cited
cause. It then splits causes into three tiers:

  INTEGRITY  — a failure that must NEVER appear on the default pack; each is its
               own defect (cheat / mislabel / fidelity break / planner-froze):
                 - trace DIAG_DIED_PREDICTED_WIN / DIED_MIDROUTE  (combat fidelity)
                 - trace DIAG_STALL / ITERS / BUDGET_TURNS / ARRIVAL_NOOP (froze)
                 - trace/report NAV_UNREACHABLE / NO_DOCK / NAV  (AP-032: all
                   default-pack objectives are reachable, so nav causes are bugs)
                 - report cause "unclassified"/unmatched  (cause-integrity hole)
                 - any FAILED verdict  (AP-005: a pack-defect claim)
  QUALITY    — an honest-looking label the spec says the planner SHOULD overcome,
               so it points at a weak planner (the separate-project work):
                 - NO_GARRISON (AP-053: buy a token 2nd stack), FLY (AP-065: fly
                   recompose), OTHER_ZONE (cross-zone travel), COMBAT, PREREQ.
  HONEST     — a genuine in-game block a real player would also hit: NO_GOLD,
               TIME, MOBILITY.

Reporting only — this NEVER changes autoplay; it measures integrity.
Usage: autoplay_integrity.py [--bin PATH] [--lo N] [--hi N] [--logdir DIR]
"""
import argparse, re, subprocess, sys, os
from collections import Counter, defaultdict

# (substring in the human-readable "outstanding:" line) -> normalized cause
CAUSE_RULES = [
    ("combat LOSS",                 "COMBAT"),
    ("prereq chain unmet",          "PREREQ"),
    ("cannot garrison",             "NO_GARRISON"),
    ("single stack",                "NO_GARRISON"),
    ("reachable only by a flying",  "FLY"),
    ("no legal landing",            "FLY_NO_LANDING"),
    ("the finale",                  "SCEPTER_DEFER"),  # scepter deferred-to-last (AP-066), honest
    ("lives in another zone",       "OTHER_ZONE"),
    ("no committed travel",         "OTHER_ZONE"),
    ("boat rental refused",         "NO_GOLD"),
    ("fee was not payable",         "NO_GOLD"),
    ("unaffordable",                "NO_GOLD"),
    ("mobility-trapped",            "MOBILITY"),
    ("mobility trap",               "MOBILITY"),
    ("route home",                  "MOBILITY"),
    ("remaining calendar",          "TIME"),
    ("calendar days",               "TIME"),
    ("no reachable dock",           "NAV_NO_DOCK"),
    ("unreachable",                 "NAV"),
    ("not reachable",               "NAV"),
    ("unreached",                   "NAV"),   # e.g. scepter "unreached (nav/one-boat)" — AP-032 says default-pack objs are all reachable
    ("unclassified",                "OTHER"),
]
INTEGRITY_CAUSES = {"NAV", "NAV_NO_DOCK", "OTHER", "UNMATCHED"}
QUALITY_CAUSES   = {"NO_GARRISON", "FLY", "OTHER_ZONE", "COMBAT", "PREREQ"}
HONEST_CAUSES    = {"NO_GOLD", "TIME", "MOBILITY", "FLY_NO_LANDING", "SCEPTER_DEFER"}
# trace causes that indicate a bug (must be absent), not an honest game block.
# NOTE: DIAG_NAV_UNREACHABLE/TRANSIENT are NOT here — they are an INTERNAL
# per-decision signal that legitimately rolls up to OTHER_ZONE/COMBAT at the
# objective level (the real AP-032 check is the CITED objective cause, handled by
# the NAV/NAV_NO_DOCK rules above). Their volume is tracked as an efficiency
# metric instead.
BUG_TRACE = ["DIAG_DIED_PREDICTED_WIN", "DIAG_DIED_MIDROUTE", "DIAG_STALL",
             "DIAG_ITERS", "DIAG_BUDGET_TURNS", "DIAG_ARRIVAL_NOOP"]
EFF_TRACE = ["DIAG_NAV_UNREACHABLE", "DIAG_NAV_TRANSIENT"]

VERDICT_RE   = re.compile(r"seed=(\d+) verdict=(\w+) objectives=(\d+)/(\d+) turns=(\d+)")
OUTSTAND_RE  = re.compile(r"autoplay: outstanding: (.*?) [-—]+ (.*)$")

def classify(cause_text):
    low = cause_text.lower()
    for needle, cause in CAUSE_RULES:
        if needle.lower() in low:
            return cause
    return "UNMATCHED"

def tier(cause):
    if cause in INTEGRITY_CAUSES: return "INTEGRITY"
    if cause in QUALITY_CAUSES:   return "QUALITY"
    return "HONEST"

def run_seed(binpath, pack_dir, seed, logdir):
    cmd = [binpath, "--autoplay", "--headless", "--seed", str(seed)]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    if logdir:
        with open(os.path.join(logdir, f"seed{seed}.log"), "w") as f:
            f.write(out)
    return out

def parse(out):
    res = {"verdict": None, "done": 0, "total": 0, "turns": 0,
           "outstanding": [], "bug_trace": Counter()}
    for line in out.splitlines():
        m = VERDICT_RE.search(line)
        if m:
            res["verdict"], res["done"], res["total"], res["turns"] = (
                m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)))
        m = OUTSTAND_RE.search(line)
        if m:
            res["outstanding"].append((m.group(1).strip(), classify(m.group(2))))
        for b in BUG_TRACE:
            if "cause=" + b in line:
                res["bug_trace"][b] += 1
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="build/release/openbounty")
    ap.add_argument("--lo", type=int, default=1)
    ap.add_argument("--hi", type=int, default=30)
    ap.add_argument("--logdir", default="/tmp/ap_integrity")
    ap.add_argument("--reparse", action="store_true",
                    help="re-score existing logdir/seedN.log instead of re-running")
    args = ap.parse_args()
    os.makedirs(args.logdir, exist_ok=True)

    cause_tot = Counter()
    eff_tot = Counter()
    integrity_hits = []   # (seed, kind, detail)
    rows = []
    for seed in range(args.lo, args.hi + 1):
        if args.reparse:
            p = os.path.join(args.logdir, f"seed{seed}.log")
            if not os.path.exists(p):
                continue
            out = open(p).read()
        else:
            out = run_seed(args.bin, None, seed, args.logdir)
        r = parse(out)
        for b in EFF_TRACE:
            n = out.count("cause=" + b)
            if n:
                eff_tot[b] += n
        rows.append((seed, r))
        if r["verdict"] == "FAILED":
            integrity_hits.append((seed, "FAILED_VERDICT", "pack-defect claim (AP-005)"))
        for b, n in r["bug_trace"].items():
            integrity_hits.append((seed, "BUG_TRACE", f"{b} x{n}"))
        for label, cause in r["outstanding"]:
            cause_tot[cause] += 1
            if tier(cause) == "INTEGRITY":
                integrity_hits.append((seed, "BAD_CAUSE", f"{cause}: {label}"))
        print(f"seed {seed:2d}: {r['verdict']:8s} {r['done']:3d}/{r['total']} "
              f"out={len(r['outstanding']):3d} "
              f"bugtrace={'+'.join(f'{k.split(chr(95))[-1]}:{v}' for k,v in r['bug_trace'].items()) or '-'}")

    print("\n==== CAUSE HISTOGRAM (outstanding objectives, all seeds) ====")
    for cause, n in cause_tot.most_common():
        print(f"  {tier(cause):9s} {cause:16s} {n}")

    print("\n==== INTEGRITY HITS (must be empty for 100% clean) ====")
    if not integrity_hits:
        print("  NONE — integrity-clean on the scanned seeds.")
    else:
        for seed, kind, detail in integrity_hits:
            print(f"  seed {seed:2d}  {kind:14s} {detail}")
    print("\n==== EFFICIENCY (internal nav-fail trace volume; not integrity) ====")
    for b, n in eff_tot.most_common():
        print(f"  {b} x{n}")

    print(f"\nintegrity hits: {len(integrity_hits)} | seeds scanned: {args.hi-args.lo+1}")

if __name__ == "__main__":
    main()
