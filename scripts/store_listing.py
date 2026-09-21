#!/usr/bin/env python3
"""Read store listing copy out of the LISTING.md files.

The two listings live in Markdown so a human can read and edit them:

    android/play-assets/LISTING.md
    ios/app-store-assets/LISTING.md

Every field is already written as a fenced code block under a `## <Field>`
heading, so that format is the machine format too -- there is no second copy of
the text to drift out of step with the prose around it.

Parsing is deliberately strict. A renamed heading, a missing block or a field
over the store's length limit raises here, at `--check` time in CI, rather than
half way through an edit against a live store.

    python3 scripts/store_listing.py --check      # validate both listings

Import it for the parsed values:

    from store_listing import play_listing, asc_listing
"""
import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

PLAY_MD = REPO / "android/play-assets/LISTING.md"
ASC_MD = REPO / "ios/app-store-assets/LISTING.md"

# field key -> (heading prefix, store limit in characters)
#
# Limits are the stores' own, and are what the console would reject on. Play
# counts the title, short and full description; Apple counts subtitle, keywords,
# promotional text and description. Both count characters, not bytes.
PLAY_FIELDS = {
    "title":            ("App name", 30),
    "shortDescription": ("Short description", 80),
    "fullDescription":  ("Full description", 4000),
}
ASC_FIELDS = {
    "subtitle":        ("Subtitle", 30),
    "promotionalText": ("Promotional text", 170),
    "keywords":        ("Keywords", 100),
    "description":     ("Description", 4000),
}


class ListingError(Exception):
    pass


def _block(md, path, prefix):
    """The first fenced block under the `## <prefix>...` heading."""
    # The heading may carry a parenthesised limit ("## App name (<=30 chars)"),
    # so match on the prefix and let the rest of the line be anything.
    pattern = re.compile(
        r"^##[ \t]+" + re.escape(prefix) + r"[^\n]*\n(.*?)^```[ \t]*\n(.*?)^```",
        re.MULTILINE | re.DOTALL,
    )
    m = pattern.search(md)
    if not m:
        raise ListingError(f"{path.name}: no fenced block under a '## {prefix}' heading")
    # Guard against the block belonging to the NEXT heading: nothing between the
    # heading and its block may itself be a heading.
    if re.search(r"^##[ \t]", m.group(1), re.MULTILINE):
        raise ListingError(f"{path.name}: '## {prefix}' has no fenced block before the next heading")
    return m.group(2).strip("\n")


def _parse(path, fields):
    if not path.exists():
        raise ListingError(f"{path} is missing")
    md = path.read_text(encoding="utf-8")
    out = {}
    for key, (prefix, limit) in fields.items():
        text = _block(md, path, prefix)
        if not text.strip():
            raise ListingError(f"{path.name}: '## {prefix}' block is empty")
        if len(text) > limit:
            raise ListingError(
                f"{path.name}: '{prefix}' is {len(text)} chars, over the {limit} limit"
            )
        out[key] = text
    return out


def play_listing():
    return _parse(PLAY_MD, PLAY_FIELDS)


def asc_listing():
    return _parse(ASC_MD, ASC_FIELDS)


# Apple rejects a listing that names another platform or store. The rule is
# recorded in the listing files themselves; this makes it fail a build rather
# than a review. The other ban is ours: the engine is an open re-implementation
# and the original game's name is a trademark, so it appears in no listing on
# either store.
BANNED_EVERYWHERE = ("king's bounty", "kings bounty")
BANNED_ON_APPLE = ("android", "google play", "play store")


def _lint(name, values, banned):
    problems = []
    for key, text in values.items():
        low = text.lower()
        for word in banned:
            if word in low:
                problems.append(f"{name}: '{key}' contains banned term {word!r}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="validate both listings and exit")
    args = ap.parse_args()

    try:
        play = play_listing()
        asc = asc_listing()
    except ListingError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    problems = _lint("play", play, BANNED_EVERYWHERE)
    problems += _lint("asc", asc, BANNED_EVERYWHERE + BANNED_ON_APPLE)
    if problems:
        for p in problems:
            print(f"error: {p}", file=sys.stderr)
        return 1

    for name, values, fields in (("play", play, PLAY_FIELDS), ("asc", asc, ASC_FIELDS)):
        print(f"{name}:")
        for key, text in values.items():
            limit = fields[key][1]
            first = text.splitlines()[0]
            head = first if len(first) <= 60 else first[:57] + "..."
            print(f"  {key:<16} {len(text):>5}/{limit:<5} {head}")
    if not args.check:
        print("\n(use --check in CI; this prints the parse either way)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
