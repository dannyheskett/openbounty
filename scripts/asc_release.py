#!/usr/bin/env python3
"""Push the store listing, and submit a build for review, on the App Store.

The mirror of scripts/play_release.py, with the same three verbs, so one
workflow can drive both stores the same way:

    listing   subtitle / description / keywords / promo text + screenshots
    release   attach an uploaded build to a version and submit it for review
    status    the current version, its state, and the builds available

What Apple does NOT expose: the App Privacy nutrition label (every endpoint
404s) and creating the app record. Both are one-time console work.

Two Apple-specific shapes worth knowing:

* Screenshots upload in three steps -- reserve the asset, PUT the bytes to the
  URL Apple hands back, then commit with an MD5 of what was sent.
* Submitting is not one call. appStoreVersionSubmissions is gone; the current
  flow is POST /reviewSubmissions, add a reviewSubmissionItem for the version,
  then PATCH the submission to submitted=true.

Credentials (same secrets the TestFlight upload already uses):
  $ASC_KEY_P8       the .p8 private key contents
  $ASC_KEY_ID       key id
  $ASC_ISSUER_ID    issuer id

Usage:
  asc_release.py status
  asc_release.py listing [--dry-run]
  asc_release.py release --build N [--submit] [--phased] [--dry-run] \
                         [--whats-new TEXT] [--skip-if-busy]

--whats-new is required by Apple on an update and defaults to a standard line;
override it when a release warrants real copy. --skip-if-busy exits 0 when a
version already holds Apple's single submission slot, which is what makes an
unattended submission safe.

`--build N` is the release number: the Makefile stamps CFBundleVersion with it
and CFBundleShortVersionString with 1.0.N, so the version record is 1.0.N.
"""
import argparse
import base64
import hashlib
import json
import os
import plistlib
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

sys.path.insert(0, str(Path(__file__).resolve().parent))
from store_listing import ListingError, asc_listing  # noqa: E402

BASE = "https://api.appstoreconnect.apple.com"
REPO = Path(__file__).resolve().parent.parent
LOCALE = "en-US"

# The app's identity and the device families it ships for come from its own
# Info.plist, so this file is the same in every game repo.
INFO_PLIST = plistlib.loads((REPO / "ios/Info.plist").read_bytes())
BUNDLE_ID = INFO_PLIST["CFBundleIdentifier"]

# One screenshot set per device family the app declares (UIDeviceFamily: 1 is
# iPhone, 2 is iPad). Apple requires a set for each: an app that declares iPad
# cannot be submitted without iPad screenshots.
#
# Apple's display-type enum is not named after the marketing sizes. There is no
# APP_IPHONE_69: the enum tops out at APP_IPHONE_67, which is the slot that
# takes 1290x2796 -- the size the 6.9" devices share with the 6.7" ones.
# Sending a name Apple does not know earns a 409 ENTITY_ERROR.ATTRIBUTE.TYPE
# that helpfully lists every valid value, which is how these two were picked.
# One set per family covers every current device; Apple scales them down.
FAMILY_SHOTS = {
    1: ("APP_IPHONE_67",         REPO / "ios/app-store-assets/screenshots/iphone-6.9"),
    2: ("APP_IPAD_PRO_3GEN_129", REPO / "ios/app-store-assets/screenshots/ipad-13"),
}
SHOT_SETS = [FAMILY_SHOTS[f] for f in INFO_PLIST.get("UIDeviceFamily", [1]) if f in FAMILY_SHOTS]

# Apple requires "What's New" on every update. A commit subject is written for
# other developers ("ios: submit each release to App Review automatically"), not
# for customers, so releases carry one deliberately boring standard line instead.
# Pass --whats-new to say something real when a release actually warrants it.
DEFAULT_WHATS_NEW = "Various minor bug fixes & performance enhancements"

# A version in one of these states is still editable; anything else means Apple
# has it and a new version record is needed.
EDITABLE = {
    "PREPARE_FOR_SUBMISSION", "DEVELOPER_REJECTED", "REJECTED",
    "METADATA_REJECTED", "INVALID_BINARY",
}

# Apple allows exactly one submission in flight per app. A version in any of
# these states is holding that slot, so a second submission would be refused.
# Unattended callers (--skip-if-busy) treat that as "nothing to do" rather than
# an error: the next release will carry the change anyway.
IN_FLIGHT = {
    "WAITING_FOR_REVIEW", "IN_REVIEW", "PENDING_APPLE_RELEASE",
    "PENDING_DEVELOPER_RELEASE", "PROCESSING_FOR_APP_STORE", "READY_FOR_REVIEW",
}


def token():
    key_pem = os.environ.get("ASC_KEY_P8")
    key_id = os.environ.get("ASC_KEY_ID")
    issuer = os.environ.get("ASC_ISSUER_ID")
    if not (key_pem and key_id and issuer):
        sys.exit("set ASC_KEY_P8, ASC_KEY_ID and ASC_ISSUER_ID")
    key = serialization.load_pem_private_key(key_pem.encode(), password=None)
    b64 = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=")
    now = int(time.time())
    hdr = b64(json.dumps({"alg": "ES256", "kid": key_id, "typ": "JWT"}).encode())
    pay = b64(json.dumps({"iss": issuer, "iat": now, "exp": now + 900,
                          "aud": "appstoreconnect-v1"}).encode())
    sig = key.sign(hdr + b"." + pay, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(sig)
    return (hdr + b"." + pay + b"." + b64(r.to_bytes(32, "big") + s.to_bytes(32, "big"))).decode()


# --dry-run stands in an "<id>" placeholder wherever a resource would have been
# created. Reads are still real, so anything that would dereference one has to
# check first.
def is_placeholder(value):
    return isinstance(value, str) and value.startswith("<")


class ASC:
    def __init__(self, dry_run=False):
        self.dry_run = dry_run

    def call(self, method, path, body=None, mutating=None):
        if mutating is None:
            mutating = method != "GET"
        if self.dry_run and mutating:
            shown = f" {json.dumps(body)[:400]}" if body is not None else ""
            print(f"  [dry-run] {method} {path}{shown}")
            return {}
        url = path if path.startswith("http") else BASE + path
        req = urllib.request.Request(
            url,
            data=json.dumps(body).encode() if body is not None else None,
            method=method,
            headers={"Authorization": "Bearer " + token(),
                     "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req) as r:
                raw = r.read()
            return json.loads(raw) if raw else {}
        except urllib.error.HTTPError as e:
            sys.exit(f"HTTP {e.code} on {method} {path}\n{e.read().decode()[:1500]}")

    def put_bytes(self, op, blob):
        """One upload operation from a reserved asset: raw PUT, Apple's headers."""
        if self.dry_run:
            print(f"  [dry-run] {op['method']} <upload {len(blob)} bytes>")
            return
        chunk = blob[op["offset"]:op["offset"] + op["length"]]
        req = urllib.request.Request(op["url"], data=chunk, method=op["method"])
        for h in op.get("requestHeaders", []):
            req.add_header(h["name"], h["value"])
        try:
            urllib.request.urlopen(req).read()
        except urllib.error.HTTPError as e:
            sys.exit(f"HTTP {e.code} uploading asset\n{e.read().decode()[:800]}")

    def app_id(self):
        r = self.call("GET", f"/v1/apps?filter[bundleId]={BUNDLE_ID}")
        data = r.get("data") or []
        if not data:
            sys.exit(f"no app record for {BUNDLE_ID}; creating one is console-only")
        return data[0]["id"]

    def editable_version(self, app):
        """The version record Apple will still let us change, if there is one."""
        r = self.call("GET", f"/v1/apps/{app}/appStoreVersions?limit=10")
        for v in r.get("data", []):
            if v["attributes"]["appStoreState"] in EDITABLE:
                return v
        return None

    def in_flight_version(self, app):
        """A version already occupying Apple's single submission slot, if any."""
        r = self.call("GET", f"/v1/apps/{app}/appStoreVersions?limit=10")
        for v in r.get("data", []):
            if v["attributes"]["appStoreState"] in IN_FLIGHT:
                return v
        return None


def cmd_status(asc, args):
    app = asc.app_id()
    print(f"app {app} ({BUNDLE_ID})")
    versions = asc.call("GET", f"/v1/apps/{app}/appStoreVersions?limit=5")
    for v in versions.get("data", []):
        a = v["attributes"]
        print(f"  version {a['versionString']:<10} {a['appStoreState']}")
    builds = asc.call("GET", f"/v1/builds?filter[app]={app}&limit=5")
    for b in builds.get("data", []):
        a = b["attributes"]
        print(f"  build   {a['version']:<10} {a.get('processingState')}")
    return 0


def cmd_listing(asc, args):
    try:
        text = asc_listing()
    except ListingError as e:
        sys.exit(f"error: {e}")
    shot_sets = []
    for display, folder in SHOT_SETS:
        shots = sorted(folder.glob("*.png"))
        if not shots:
            sys.exit(f"no screenshots for {display} in {folder}")
        shot_sets.append((display, shots))

    app = asc.app_id()
    version = asc.editable_version(app)
    if not version and not asc.dry_run:
        # There is nothing to write a listing onto. Unattended that is normal --
        # a version already in review holds Apple's only submission slot, so the
        # release step ahead of this one skipped too, and this must skip for the
        # same reason rather than failing the release behind it.
        if args.skip_if_busy:
            busy = asc.in_flight_version(app)
            where = (f"{busy['attributes']['versionString']} is "
                     f"{busy['attributes']['appStoreState']}") if busy else "nothing is pending"
            print(f"  no editable version ({where}); skipping the listing")
            return 0
        sys.exit("no editable version; run `release --build N` first to create one")
    version_id = version["id"] if version else "<version>"

    # Subtitle lives on the app INFO localization (it survives across versions);
    # description, keywords and promo text live on the VERSION localization.
    #
    # An app that is already on sale has TWO appInfo records: the live one, which
    # is frozen, and an editable one for the pending version. Patching the first
    # that happens to carry an en-US localization can hit the live one, which
    # answers 409 ENTITY_ERROR.ATTRIBUTE.INVALID.INVALID_STATE, "The field
    # 'subtitle' can not be modified in the current state." Select by state.
    infos = asc.call("GET", f"/v1/apps/{app}/appInfos")
    editable = [i for i in infos.get("data", [])
                if i["attributes"]["appStoreState"] in EDITABLE]
    if not editable:
        print("  subtitle: skipped (no editable appInfo; nothing pending)")
    for info in editable:
        locs = asc.call("GET", f"/v1/appInfos/{info['id']}/appInfoLocalizations")
        for loc in locs.get("data", []):
            if loc["attributes"]["locale"] == LOCALE:
                asc.call("PATCH", f"/v1/appInfoLocalizations/{loc['id']}",
                         {"data": {"type": "appInfoLocalizations", "id": loc["id"],
                                   "attributes": {"subtitle": text["subtitle"]}}})
                print(f"  subtitle: {len(text['subtitle'])}ch")
                break

    locs = asc.call("GET", f"/v1/appStoreVersions/{version_id}/appStoreVersionLocalizations") \
        if version else {}
    loc_id = None
    for loc in locs.get("data", []):
        if loc["attributes"]["locale"] == LOCALE:
            loc_id = loc["id"]
            break
    if loc_id is None and not asc.dry_run:
        sys.exit(f"no {LOCALE} localization on version {version_id}")
    asc.call("PATCH", f"/v1/appStoreVersionLocalizations/{loc_id or '<loc>'}",
             {"data": {"type": "appStoreVersionLocalizations", "id": loc_id or "<loc>",
                       "attributes": {"description": text["description"],
                                      "keywords": text["keywords"],
                                      "promotionalText": text["promotionalText"]}}})
    print(f"  text: description={len(text['description'])}ch "
          f"keywords={len(text['keywords'])}ch promo={len(text['promotionalText'])}ch")

    for display, shots in shot_sets:
        upload_screenshots(asc, loc_id, shots, display)
    return 0


def upload_screenshots(asc, loc_id, shots, display_type):
    """Replace one device family's screenshot set.

    Reserve -> PUT bytes -> commit with MD5, per file.
    """
    sets = asc.call("GET",
                    f"/v1/appStoreVersionLocalizations/{loc_id or '<loc>'}/appScreenshotSets") \
        if loc_id else {}
    set_id = None
    for s in sets.get("data", []):
        if s["attributes"]["screenshotDisplayType"] == display_type:
            set_id = s["id"]
            break

    if set_id:
        # Clear the slot first: uploads append, so re-running would pile up
        # duplicates against Apple's 10-per-set ceiling.
        existing = asc.call("GET", f"/v1/appScreenshotSets/{set_id}/appScreenshots")
        for shot in existing.get("data", []):
            asc.call("DELETE", f"/v1/appScreenshots/{shot['id']}")
    else:
        created = asc.call("POST", "/v1/appScreenshotSets", {
            "data": {"type": "appScreenshotSets",
                     "attributes": {"screenshotDisplayType": display_type},
                     "relationships": {"appStoreVersionLocalization": {
                         "data": {"type": "appStoreVersionLocalizations",
                                  "id": loc_id or "<loc>"}}}}})
        set_id = (created.get("data") or {}).get("id", "<set>")

    for path in shots:
        blob = path.read_bytes()
        reserved = asc.call("POST", "/v1/appScreenshots", {
            "data": {"type": "appScreenshots",
                     "attributes": {"fileName": path.name, "fileSize": len(blob)},
                     "relationships": {"appScreenshotSet": {
                         "data": {"type": "appScreenshotSets", "id": set_id}}}}})
        if asc.dry_run:
            print(f"  [dry-run] upload {path.name} ({len(blob)} bytes)")
            continue
        data = reserved["data"]
        for op in data["attributes"]["uploadOperations"]:
            asc.put_bytes(op, blob)
        asc.call("PATCH", f"/v1/appScreenshots/{data['id']}", {
            "data": {"type": "appScreenshots", "id": data["id"],
                     "attributes": {"uploaded": True,
                                    "sourceFileChecksum": hashlib.md5(blob).hexdigest()}}})
        print(f"  screenshot {display_type} {path.name}")


# The App Review contact fields. Apple refuses a submission whose version has
# no copyright line or no review details, and it only carries them forward from
# an earlier version -- an app's FIRST version has neither.
REVIEW_CONTACT = ("contactFirstName", "contactLastName", "contactPhone", "contactEmail")


def listing_field(heading):
    """The fenced block under `## <heading>` in the App Store LISTING.md, or
    the backticked value on a `- **<heading>:**` line."""
    md = (REPO / "ios/app-store-assets/LISTING.md").read_text()
    lines = md.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == f"## {heading}":
            block = []
            inside = False
            for l in lines[i + 1:]:
                if l.startswith("## "):
                    break
                if l.startswith("```"):
                    if inside:
                        return "\n".join(block).strip()
                    inside = True
                    continue
                if inside:
                    block.append(l)
        if line.startswith(f"- **{heading}:**") and "`" in line:
            return line.split("`")[1]
    sys.exit(f"LISTING.md has no {heading!r}")


def ensure_review_info(asc, app, version_id):
    """Give the version a copyright line and App Review details if it lacks them.

    Copyright and the review notes come from LISTING.md. The contact (name,
    phone, email) is personal, so it is never written into the repo: it is
    copied from another version that has one -- this app's earlier versions
    first, then the team's other apps, which share the same reviewer contact.
    """
    if is_placeholder(version_id):
        return
    v = asc.call("GET", f"/v1/appStoreVersions/{version_id}")["data"]["attributes"]
    if not v.get("copyright"):
        copyright_line = listing_field("Copyright")
        asc.call("PATCH", f"/v1/appStoreVersions/{version_id}",
                 {"data": {"type": "appStoreVersions", "id": version_id,
                           "attributes": {"copyright": copyright_line}}})
        print(f"  copyright: {copyright_line}")

    if asc.call("GET", f"/v1/appStoreVersions/{version_id}/appStoreReviewDetail").get("data"):
        return
    contact = None
    apps = [app] + [a["id"] for a in asc.call("GET", "/v1/apps?limit=50").get("data", [])
                    if a["id"] != app]
    for a in apps:
        for ver in asc.call("GET", f"/v1/apps/{a}/appStoreVersions?limit=10").get("data", []):
            if ver["id"] == version_id:
                continue
            d = asc.call("GET", f"/v1/appStoreVersions/{ver['id']}/appStoreReviewDetail").get("data")
            if d and all(d["attributes"].get(k) for k in REVIEW_CONTACT):
                contact = {k: d["attributes"][k] for k in REVIEW_CONTACT}
                break
        if contact:
            break
    if not contact:
        sys.exit("no App Review contact on any version of any app; set one in the "
                 "console (App Review Information) once, and later releases copy it")
    attrs = dict(contact, demoAccountRequired=False, notes=listing_field("App Review notes"))
    asc.call("POST", "/v1/appStoreReviewDetails", {
        "data": {"type": "appStoreReviewDetails", "attributes": attrs,
                 "relationships": {"appStoreVersion": {
                     "data": {"type": "appStoreVersions", "id": version_id}}}}})
    print("  review details: contact copied, notes from LISTING.md")


def cmd_release(asc, args):
    app = asc.app_id()
    version_string = f"1.0.{args.build}"

    # Apple's submission slot holds one version at a time. An unattended caller
    # says so and stops at exit 0: the release it was called for still shipped,
    # and the next one will carry these changes. Failing here would red a release
    # for something that is not wrong.
    if args.skip_if_busy:
        busy = asc.in_flight_version(app)
        if busy:
            a = busy["attributes"]
            print(f"  {a['versionString']} is {a['appStoreState']}; "
                  f"leaving it alone and skipping {version_string}")
            return 0

    version = asc.editable_version(app)
    if version:
        current = version["attributes"]["versionString"]
        if current != version_string:
            # The version string is what CUSTOMERS see, and Apple expects it to
            # match CFBundleShortVersionString. A git tag name has ended up here
            # before, so correct it rather than leaving it.
            asc.call("PATCH", f"/v1/appStoreVersions/{version['id']}",
                     {"data": {"type": "appStoreVersions", "id": version["id"],
                               "attributes": {"versionString": version_string}}})
            print(f"  version string {current} -> {version_string}")
        version_id = version["id"]
    else:
        created = asc.call("POST", "/v1/appStoreVersions", {
            "data": {"type": "appStoreVersions",
                     "attributes": {"platform": "IOS", "versionString": version_string},
                     "relationships": {"app": {"data": {"type": "apps", "id": app}}}}})
        version_id = (created.get("data") or {}).get("id", "<version>")
        print(f"  created version {version_string}")

    # Match on CFBundleVersion, which the Makefile stamps with the release number.
    builds = asc.call("GET", f"/v1/builds?filter[app]={app}&filter[version]={args.build}")
    data = builds.get("data") or []
    if not data and not asc.dry_run:
        sys.exit(f"no build with CFBundleVersion {args.build} in App Store Connect; "
                 f"the release workflow uploads it, and Apple takes 5-15 min to process it")
    build_id = data[0]["id"] if data else "<build>"
    asc.call("PATCH", f"/v1/appStoreVersions/{version_id}/relationships/build",
             {"data": {"type": "builds", "id": build_id}})
    print(f"  build {args.build} attached to {version_string}")

    # "What's New" is REQUIRED on an update, and a submission without it fails
    # Apple's validation. An unattended release has no human to type it, so it
    # comes from the release notes the caller passes.
    #
    # The app's FIRST version is the exception: there is nothing to be new
    # relative to, and Apple refuses the field outright (409 "Attribute
    # 'whatsNew' cannot be edited at this time"). A first version is one with no
    # other version record beside it.
    others = [v for v in asc.call("GET", f"/v1/apps/{app}/appStoreVersions?limit=10").get("data", [])
              if v["id"] != version_id]
    if args.whats_new and not others and not is_placeholder(version_id):
        print("  whats-new: skipped (first version of the app)")
    elif args.whats_new:
        # A dry run never created the version, so version_id is a placeholder and
        # there is nothing real to look the localization up on. GETs are not
        # suppressed by --dry-run (they are how current state is read), so this
        # has to be skipped explicitly rather than left to fail.
        loc_id = None
        if not is_placeholder(version_id):
            locs = asc.call(
                "GET", f"/v1/appStoreVersions/{version_id}/appStoreVersionLocalizations")
            for loc in locs.get("data", []):
                if loc["attributes"]["locale"] == LOCALE:
                    loc_id = loc["id"]
                    break
            if loc_id is None:
                sys.exit(f"no {LOCALE} localization on {version_string} to hold whatsNew")
        asc.call("PATCH", f"/v1/appStoreVersionLocalizations/{loc_id or '<loc>'}",
                 {"data": {"type": "appStoreVersionLocalizations", "id": loc_id or "<loc>",
                           "attributes": {"whatsNew": args.whats_new}}})
        print(f"  whats-new: {len(args.whats_new)}ch")

    if args.phased:
        asc.call("POST", "/v1/appStoreVersionPhasedReleases", {
            "data": {"type": "appStoreVersionPhasedReleases",
                     "attributes": {"phasedReleaseState": "INACTIVE"},
                     "relationships": {"appStoreVersion": {
                         "data": {"type": "appStoreVersions", "id": version_id}}}}})
        print("  phased release enabled")

    if not args.submit:
        print("  not submitting (pass --submit to send it to review)")
        return 0

    ensure_review_info(asc, app, version_id)

    # Reuse an unsent draft if one exists. Creating the submission is the step
    # that succeeds even when the version is not reviewable, so every failed
    # attempt used to leave an empty READY_FOR_REVIEW draft behind -- and Apple
    # refuses to cancel an empty one, so they pile up in the console.
    drafts = asc.call("GET", f"/v1/reviewSubmissions?filter[app]={app}"
                             "&filter[platform]=IOS&filter[state]=READY_FOR_REVIEW")
    if drafts.get("data"):
        sub_id = drafts["data"][0]["id"]
        print(f"  reusing draft submission {sub_id}")
    else:
        submission = asc.call("POST", "/v1/reviewSubmissions", {
            "data": {"type": "reviewSubmissions",
                     "attributes": {"platform": "IOS"},
                     "relationships": {"app": {"data": {"type": "apps", "id": app}}}}})
        sub_id = (submission.get("data") or {}).get("id", "<submission>")
    asc.call("POST", "/v1/reviewSubmissionItems", {
        "data": {"type": "reviewSubmissionItems",
                 "relationships": {
                     "reviewSubmission": {"data": {"type": "reviewSubmissions", "id": sub_id}},
                     "appStoreVersion": {"data": {"type": "appStoreVersions",
                                                  "id": version_id}}}}})
    asc.call("PATCH", f"/v1/reviewSubmissions/{sub_id}",
             {"data": {"type": "reviewSubmissions", "id": sub_id,
                       "attributes": {"submitted": True}}})
    print(f"  submitted {version_string} for review")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="verb", required=True)

    sub.add_parser("status", help="current version, state and builds")

    p_listing = sub.add_parser("listing", help="push listing text + screenshots")
    p_listing.add_argument("--skip-if-busy", action="store_true",
                           help="exit 0 when there is no editable version to write onto")
    p_listing.add_argument("--dry-run", action="store_true")

    p_rel = sub.add_parser("release", help="attach a build to a version, optionally submit")
    p_rel.add_argument("--build", type=int, required=True,
                       help="release number = CFBundleVersion; the version becomes 1.0.N")
    p_rel.add_argument("--submit", action="store_true", help="send it to App Review")
    p_rel.add_argument("--phased", action="store_true", help="enable phased release")
    p_rel.add_argument("--whats-new", default=DEFAULT_WHATS_NEW,
                       help=f"'What's New' text; Apple requires it on an update "
                            f"(default: {DEFAULT_WHATS_NEW!r})")
    p_rel.add_argument("--skip-if-busy", action="store_true",
                       help="exit 0 if a version already holds Apple's submission slot")
    p_rel.add_argument("--dry-run", action="store_true")

    args = ap.parse_args()
    asc = ASC(dry_run=getattr(args, "dry_run", False))
    return {"status": cmd_status, "listing": cmd_listing, "release": cmd_release}[args.verb](
        asc, args)


if __name__ == "__main__":
    sys.exit(main())
