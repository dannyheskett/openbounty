#!/usr/bin/env python3
"""Attach "What to Test" notes to a freshly uploaded TestFlight build.

Distribution itself needs no help: the internal beta group is created with
hasAccessToAllBuilds, and builds default to autoNotifyEnabled, so every upload
reaches internal testers on its own. What Apple does NOT fill in is the note
testers see next to the build -- without it they get a version number and no
idea what changed. This fills it from the release notes.

Runs on Linux (pure App Store Connect REST, no Xcode), so it can poll through
Apple's 5-15 minute processing window without burning macOS runner minutes.

Environment:
  ASC_KEY_P8      the .p8 private key contents
  ASC_KEY_ID      key id
  ASC_ISSUER_ID   issuer id
  BUNDLE_ID       app bundle id, e.g. com.danheskett.gloryofrome
  BUILD_VERSION   CFBundleVersion to wait for (the release number)
  NOTES           the "What to Test" text
  TIMEOUT_MIN     how long to wait for processing (default 25)
"""
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

BASE = "https://api.appstoreconnect.apple.com"
KEY_ID = os.environ["ASC_KEY_ID"]
ISSUER = os.environ["ASC_ISSUER_ID"]
KEY_PEM = os.environ["ASC_KEY_P8"].encode()


def token():
    key = serialization.load_pem_private_key(KEY_PEM, password=None)
    b64 = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=")
    now = int(time.time())
    hdr = b64(json.dumps({"alg": "ES256", "kid": KEY_ID, "typ": "JWT"}).encode())
    pay = b64(json.dumps({"iss": ISSUER, "iat": now, "exp": now + 900,
                          "aud": "appstoreconnect-v1"}).encode())
    sig = key.sign(hdr + b"." + pay, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(sig)
    return (hdr + b"." + pay + b"." + b64(r.to_bytes(32, "big") + s.to_bytes(32, "big"))).decode()


def call(method, path, body=None):
    req = urllib.request.Request(
        path if path.startswith("http") else BASE + path,
        data=json.dumps(body).encode() if body is not None else None,
        method=method,
        headers={"Authorization": "Bearer " + token(),
                 "Content-Type": "application/json"})
    try:
        raw = urllib.request.urlopen(req).read()
        return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        detail = e.read().decode()
        try:
            errs = json.loads(detail).get("errors", [])
            detail = "; ".join(f"{x.get('code')}: {x.get('detail')}" for x in errs)
        except Exception:
            pass
        raise SystemExit(f"{method} {path} -> HTTP {e.code}: {detail}")


def main():
    bundle = os.environ["BUNDLE_ID"]
    want = os.environ["BUILD_VERSION"]
    notes = os.environ["NOTES"].strip()[:4000]  # Apple caps whatsNew at 4000
    timeout_min = int(os.environ.get("TIMEOUT_MIN", "25"))

    apps = call("GET", f"/v1/apps?filter[bundleId]={bundle}&limit=1")["data"]
    if not apps:
        raise SystemExit(f"no app found for bundle id {bundle}")
    app_id = apps[0]["id"]
    print(f"app {bundle} -> {app_id}; waiting for build {want}")

    # Apple takes 5-15 min to process an upload before the build is addressable.
    deadline = time.time() + timeout_min * 60
    build = None
    while time.time() < deadline:
        for b in call("GET", f"/v1/builds?filter[app]={app_id}&limit=20")["data"]:
            if b["attributes"].get("version") == want:
                build = b
                break
        state = build["attributes"].get("processingState") if build else None
        if build and state != "PROCESSING":
            print(f"build {want} is {state}")
            break
        print(f"  waiting... (build {'found, ' + str(state) if build else 'not visible yet'})")
        time.sleep(45)
    else:
        raise SystemExit(f"timed out after {timeout_min} min waiting for build {want}")

    if build["attributes"].get("processingState") != "VALID":
        raise SystemExit(f"build {want} is {build['attributes'].get('processingState')}, not VALID")

    # "What to Test" is per-build and per-locale.
    existing = call("GET", f"/v1/builds/{build['id']}/betaBuildLocalizations")["data"]
    en = next((x for x in existing if x["attributes"].get("locale") == "en-US"), None)
    if en:
        call("PATCH", f"/v1/betaBuildLocalizations/{en['id']}",
             {"data": {"type": "betaBuildLocalizations", "id": en["id"],
                       "attributes": {"whatsNew": notes}}})
        print("updated existing en-US notes")
    else:
        call("POST", "/v1/betaBuildLocalizations",
             {"data": {"type": "betaBuildLocalizations",
                       "attributes": {"locale": "en-US", "whatsNew": notes},
                       "relationships": {"build": {"data": {"type": "builds",
                                                            "id": build["id"]}}}}})
        print("created en-US notes")

    detail = call("GET", f"/v1/builds/{build['id']}/buildBetaDetail")["data"]
    if not detail["attributes"].get("autoNotifyEnabled"):
        call("PATCH", f"/v1/buildBetaDetails/{detail['id']}",
             {"data": {"type": "buildBetaDetails", "id": detail["id"],
                       "attributes": {"autoNotifyEnabled": True}}})
        print("enabled auto-notify")

    groups = call("GET", f"/v1/apps/{app_id}/betaGroups?limit=20")["data"]
    for g in groups:
        a = g["attributes"]
        n = len(call("GET", f"/v1/betaGroups/{g['id']}/betaTesters?limit=200")["data"])
        print(f"  group {a.get('name')!r}: internal={a.get('isInternalGroup')} "
              f"allBuilds={a.get('hasAccessToAllBuilds')} testers={n}")
    if not groups:
        print("  WARNING: no beta groups exist, so no testers will receive this build")

    print(f"build {want} is distributed to internal testers with release notes")


if __name__ == "__main__":
    sys.exit(main())
