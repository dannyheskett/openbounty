#!/usr/bin/env python3
"""Print the next free CFBundleVersion for this app.

App Store Connect refuses an upload whose build number is not higher than
every build it already holds -- including builds from other branches and
expired ones. A release on main derives its number from the release-N tag,
but a one-off TestFlight build from a working branch has no tag to use, so it
asks Apple what the highest number is and adds one.

Credentials: the same ASC_KEY_P8 / ASC_KEY_ID / ASC_ISSUER_ID as the rest.
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asc_release import ASC, BUNDLE_ID  # noqa: E402


def main():
    asc = ASC()
    r = asc.call("GET", f"/v1/apps?filter[bundleId]={BUNDLE_ID}")
    if not r.get("data"):
        sys.exit(f"no app record for {BUNDLE_ID}")
    app = r["data"][0]["id"]

    highest = 0
    url = f"/v1/apps/{app}/builds?limit=200"
    while url:
        page = asc.call("GET", url)
        for b in page.get("data", []):
            v = b["attributes"].get("version") or "0"
            # Apple stores it as a string; anything non-numeric is somebody
            # else's convention and is ignored rather than guessed at.
            if v.isdigit():
                highest = max(highest, int(v))
        nxt = page.get("links", {}).get("next")
        url = nxt.split("appstoreconnect.apple.com")[-1] if nxt else None

    print(highest + 1)


if __name__ == "__main__":
    main()
