#!/usr/bin/env python3
"""One-time App Store setup through the App Store Connect API.

Everything Apple lets an API key do for a new app, so the console work shrinks
to the two things it does not expose (see asc_release.py): creating the app
record, and the App Privacy label.

    bundle-id   register the explicit App ID com.danheskett.gloryofrome
    profile     create (or reuse) the App Store provisioning profile for it,
                bound to the team's existing Apple Distribution certificate,
                and write the .mobileprovision to --out
    app-info    once the app record exists: category, content rights, age
                rating (all "None" -> 4+), privacy policy, support and marketing
                URLs, a free price, and
                availability in every territory except mainland China

The distribution certificate is team-wide and shared by every app, so this never
creates one: `profile` picks the certificate whose SHA-1 matches --cert-sha1
(the fingerprint of the .p12 the release workflow signs with), so the profile
and the signing identity cannot disagree.

Credentials: the same ASC_KEY_P8 / ASC_KEY_ID / ASC_ISSUER_ID as asc_release.py.
Registering App IDs and creating profiles needs an Admin or App Manager key.

Usage:
  asc_setup.py bundle-id [--dry-run]
  asc_setup.py profile --cert-sha1 HEX --out PATH [--dry-run]
  asc_setup.py app-info [--dry-run]
"""
import argparse
import base64
import hashlib
import os
import sys

from asc_release import ASC, BUNDLE_ID, is_placeholder

APP_NAME = "Glory of Rome"
PROFILE_NAME = "Glory of Rome App Store"
# Dan publishes the privacy policy; its text is android/play-assets/PRIVACY.md.
# Change this the day it has a URL -- `app-info` writes whatever is here to
# every localization, and review rejects a listing with no policy.
PRIVACY_URL = "https://danheskett.com/app/privacy-policy/"
SUPPORT_URL = "https://github.com/dannyheskett/openbounty/issues"
MARKETING_URL = "https://github.com/dannyheskett/openbounty"

# ios/app-store-assets/LISTING.md: Games -> Strategy, with Role Playing as the
# second games subcategory.
PRIMARY_CATEGORY = "GAMES"
SUBCATEGORIES = ("GAMES_STRATEGY", "GAMES_ROLE_PLAYING")

# Age rating. Everything here is NONE or false EXCEPT the one thing the game
# actually contains, which is below: troops fight in an abstract grid battle
# and a beaten troop vanishes. That is cartoon/fantasy violence, infrequent and
# mild, and it lands the app at 9+. Only attributes the declaration actually
# carries are sent, because Apple adds questions over time and rejects unknown
# ones.
AGE_VALUES = {
    "violenceCartoonOrFantasy": "INFREQUENT_OR_MILD",
}
AGE_NONE = {
    "alcoholTobaccoOrDrugUseOrReferences", "contests", "gamblingSimulated",
    "horrorOrFearThemes", "matureOrSuggestiveThemes", "medicalOrTreatmentInformation",
    "profanityOrCrudeHumor", "sexualContentGraphicAndNudity", "sexualContentOrNudity",
    "violenceCartoonOrFantasy", "violenceRealistic",
    "violenceRealisticProlongedGraphicOrSadistic", "gunsOrOtherWeapons",
}
AGE_FALSE = {
    "gambling", "unrestrictedWebAccess", "lootBox", "messagingAndChat",
    "userGeneratedContent", "advertising", "parentalControls", "ageAssurance",
    "healthOrWellnessTopics", "socialMedia",
}


def bundle_id(asc):
    r = asc.call("GET", f"/v1/bundleIds?filter[identifier]={BUNDLE_ID}")
    exact = [d for d in r.get("data", []) if d["attributes"]["identifier"] == BUNDLE_ID]
    if exact:
        print(f"bundle id {BUNDLE_ID} already registered ({exact[0]['id']})")
        return exact[0]["id"]
    r = asc.call("POST", "/v1/bundleIds", {"data": {
        "type": "bundleIds",
        "attributes": {"identifier": BUNDLE_ID, "name": APP_NAME, "platform": "IOS"},
    }})
    new_id = r.get("data", {}).get("id", "<bundleId>")
    print(f"registered bundle id {BUNDLE_ID} ({new_id})")
    return new_id


def distribution_cert(asc, sha1):
    want = sha1.replace(":", "").lower()
    r = asc.call("GET", "/v1/certificates?filter[certificateType]=DISTRIBUTION&limit=200")
    for d in r.get("data", []):
        der = base64.b64decode(d["attributes"]["certificateContent"])
        if hashlib.sha1(der).hexdigest() == want:
            print(f"distribution certificate {d['id']} (expires {d['attributes']['expirationDate']})")
            return d["id"]
    sys.exit(f"no DISTRIBUTION certificate with SHA-1 {sha1}; is --cert-sha1 from the right .p12?")


def profile(asc, sha1, out):
    bid = bundle_id(asc)
    cert = distribution_cert(asc, sha1)

    content = None
    if not is_placeholder(bid):
        r = asc.call("GET", f"/v1/bundleIds/{bid}/profiles?limit=200")
        for d in r.get("data", []):
            a = d["attributes"]
            if a["profileType"] == "IOS_APP_STORE" and a["profileState"] == "ACTIVE":
                certs = asc.call("GET", f"/v1/profiles/{d['id']}/certificates")
                if any(c["id"] == cert for c in certs.get("data", [])):
                    print(f"reusing active profile '{a['name']}' ({d['id']}, expires {a['expirationDate']})")
                    content = a["profileContent"]
                    break

    if content is None:
        r = asc.call("POST", "/v1/profiles", {"data": {
            "type": "profiles",
            "attributes": {"name": PROFILE_NAME, "profileType": "IOS_APP_STORE"},
            "relationships": {
                "bundleId": {"data": {"type": "bundleIds", "id": bid}},
                "certificates": {"data": [{"type": "certificates", "id": cert}]},
            },
        }})
        if asc.dry_run:
            return
        a = r["data"]["attributes"]
        print(f"created profile '{a['name']}' ({r['data']['id']}, expires {a['expirationDate']})")
        content = a["profileContent"]

    if asc.dry_run:
        return
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(base64.b64decode(content))
    print(f"wrote {out}")


def app_info(asc, privacy_url=None):
    privacy = privacy_url or PRIVACY_URL
    r = asc.call("GET", f"/v1/apps?filter[bundleId]={BUNDLE_ID}")
    if not r.get("data"):
        sys.exit(f"no app record for {BUNDLE_ID}; create it in App Store Connect first")
    app = r["data"][0]["id"]
    print(f"app {app}")

    asc.call("PATCH", f"/v1/apps/{app}", {"data": {
        "type": "apps", "id": app,
        "attributes": {"contentRightsDeclaration": "DOES_NOT_USE_THIRD_PARTY_CONTENT"},
    }})
    print("content rights: does not use third-party content")

    infos = asc.call("GET", f"/v1/apps/{app}/appInfos").get("data", [])
    # The editable appInfo is the one not yet live; a brand-new app has one.
    info = next((i for i in infos if i["attributes"].get("appStoreState") != "READY_FOR_SALE"
                 and i["attributes"].get("state") != "READY_FOR_DISTRIBUTION"), infos[0])
    iid = info["id"]

    rel = {"primaryCategory": {"data": {"type": "appCategories", "id": PRIMARY_CATEGORY}}}
    for key, cat in zip(("primarySubcategoryOne", "primarySubcategoryTwo"), SUBCATEGORIES):
        rel[key] = {"data": {"type": "appCategories", "id": cat}}
    asc.call("PATCH", f"/v1/appInfos/{iid}", {"data": {
        "type": "appInfos", "id": iid, "relationships": rel}})
    print(f"category: {PRIMARY_CATEGORY} / {', '.join(SUBCATEGORIES)}")

    for loc in asc.call("GET", f"/v1/appInfos/{iid}/appInfoLocalizations").get("data", []):
        asc.call("PATCH", f"/v1/appInfoLocalizations/{loc['id']}", {"data": {
            "type": "appInfoLocalizations", "id": loc["id"],
            "attributes": {"privacyPolicyUrl": privacy}}})
        print(f"privacy policy URL ({loc['attributes']['locale']}): {privacy}")

    # Support URL is required before review; it lives on the version's
    # localization, not on appInfo.
    for v in asc.call("GET", f"/v1/apps/{app}/appStoreVersions?limit=10").get("data", []):
        if v["attributes"]["appStoreState"] != "PREPARE_FOR_SUBMISSION":
            continue
        for loc in asc.call("GET", f"/v1/appStoreVersions/{v['id']}/appStoreVersionLocalizations").get("data", []):
            asc.call("PATCH", f"/v1/appStoreVersionLocalizations/{loc['id']}", {"data": {
                "type": "appStoreVersionLocalizations", "id": loc["id"],
                "attributes": {"supportUrl": SUPPORT_URL, "marketingUrl": MARKETING_URL}}})
            print(f"support / marketing URL ({loc['attributes']['locale']}): {SUPPORT_URL}, {MARKETING_URL}")

    decl = asc.call("GET", f"/v1/appInfos/{iid}/ageRatingDeclaration").get("data")
    if decl:
        attrs = {}
        for k in decl["attributes"]:
            if k in AGE_VALUES:
                attrs[k] = AGE_VALUES[k]
            elif k in AGE_NONE:
                attrs[k] = "NONE"
            elif k in AGE_FALSE:
                attrs[k] = False
        asc.call("PATCH", f"/v1/ageRatingDeclarations/{decl['id']}", {"data": {
            "type": "ageRatingDeclarations", "id": decl["id"], "attributes": attrs}})
        print(f"age rating: {len(attrs)} questions answered "
              f"({', '.join(f'{k}={v}' for k, v in AGE_VALUES.items() if k in attrs)} , rest None / No)")

    # Free: a price schedule with the zero price point in the base territory.
    points = asc.call("GET", f"/v1/apps/{app}/appPricePoints?filter[territory]=USA&limit=200")
    free = next((p for p in points.get("data", [])
                 if float(p["attributes"]["customerPrice"]) == 0.0), None)
    if not free:
        sys.exit("no zero price point for USA")
    asc.call("POST", "/v1/appPriceSchedules", {
        "data": {"type": "appPriceSchedules", "relationships": {
            "app": {"data": {"type": "apps", "id": app}},
            "baseTerritory": {"data": {"type": "territories", "id": "USA"}},
            "manualPrices": {"data": [{"type": "appPrices", "id": "${price0}"}]},
        }},
        "included": [{"type": "appPrices", "id": "${price0}", "relationships": {
            "appPricePoint": {"data": {"type": "appPricePoints", "id": free["id"]}},
        }}],
    })
    print("price: free")

    availability(asc, app)


# Mainland China requires a government games licence (ISBN) number for any game,
# which this app does not have, so it is left out; every other territory is on,
# and territories Apple adds later are opted into automatically.
EXCLUDED_TERRITORIES = {"CHN"}


def availability(asc, app):
    territories = []
    url = "/v1/territories?limit=200"
    while url:
        r = asc.call("GET", url)
        territories += [t["id"] for t in r.get("data", [])]
        url = r.get("links", {}).get("next")
    # Apple wants every territory in the request, so excluded ones are sent as
    # available=false rather than left out.
    items = [{"type": "territoryAvailabilities", "id": f"${{t{t}}}",
              "attributes": {"available": t not in EXCLUDED_TERRITORIES},
              "relationships": {"territory": {"data": {"type": "territories", "id": t}}}}
             for t in sorted(territories)]
    asc.call("POST", "/v2/appAvailabilities", {
        "data": {"type": "appAvailabilities",
                 "attributes": {"availableInNewTerritories": True},
                 "relationships": {
                     "app": {"data": {"type": "apps", "id": app}},
                     "territoryAvailabilities": {"data": [{"type": i["type"], "id": i["id"]} for i in items]},
                 }},
        "included": items,
    })
    on = sum(1 for i in items if i["attributes"]["available"])
    print(f"availability: {on} of {len(items)} territories (excluded: {', '.join(sorted(EXCLUDED_TERRITORIES))})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("bundle-id", help="register the App ID")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("profile", help="create or reuse the App Store profile")
    p.add_argument("--cert-sha1", required=True, help="SHA-1 of the signing certificate")
    p.add_argument("--out", required=True, help="where to write the .mobileprovision")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("app-info", help="category, rights, age rating, privacy URL, price")
    p.add_argument("--privacy-url", help="override the privacy policy URL")
    p.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    asc = ASC(dry_run=args.dry_run)
    if args.cmd == "bundle-id":
        bundle_id(asc)
    elif args.cmd == "profile":
        profile(asc, args.cert_sha1, args.out)
    else:
        app_info(asc, args.privacy_url)


if __name__ == "__main__":
    main()
