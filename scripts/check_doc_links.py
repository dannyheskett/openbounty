#!/usr/bin/env python3
"""Check every relative link in the docs: the file exists and, for a
`#anchor`, a heading in that file has that GitHub slug.

    python3 scripts/check_doc_links.py

Exit 0 when every link resolves, 1 with one line per broken link otherwise.
docs/OPENKB-SPEC.md, the predecessor project's reference, is not checked.
"""
import glob
import os
import re
import sys

SKIP = {os.path.normpath("docs/OPENKB-SPEC.md")}


def slug(heading):
    h = heading.strip().lower()
    h = re.sub(r"[`*_]", "", h)
    h = re.sub(r"[^\w\- ]", "", h)
    return h.replace(" ", "-")


def headings(text):
    text = re.sub(r"```.*?```", "", text, flags=re.S)
    return {slug(m) for m in re.findall(r"^#+ (.*)$", text, flags=re.M)}


def main():
    files = sorted(glob.glob("docs/*.md")) + ["README.md", "CONTRIBUTING.md",
                                             "engine/README.md", "NOTICES.md"]
    files = [os.path.normpath(f) for f in files if os.path.exists(f)]
    text = {f: open(f, encoding="utf-8").read() for f in files}
    heads = {f: headings(t) for f, t in text.items()}
    bad = 0
    for f in files:
        if f in SKIP:
            continue
        for m in re.finditer(r"\]\(([^)\s]*)\)", text[f]):
            target = m.group(1)
            if re.match(r"[a-z]+:", target):
                continue
            path, _, anchor = target.partition("#")
            dest = os.path.normpath(os.path.join(os.path.dirname(f), path)) if path else f
            line = text[f][:m.start()].count("\n") + 1
            if path and not os.path.exists(dest):
                print(f"{f}:{line}: no such file: {target}")
                bad += 1
            elif anchor and dest in heads and anchor not in heads[dest]:
                print(f"{f}:{line}: no such heading: {target}")
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
