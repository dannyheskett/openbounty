# Getting Glory of Rome into TestFlight

The release workflow has signed the `.ipa` and uploaded it to App Store
Connect whenever the Apple secrets are set; without them every Apple step has
gated on them and reported "skipping". This has been the one-time setup that
turns it on.

**You do not need a Mac.** The certificate has been created with `openssl`,
the provisioning profile downloaded from Apple's website, and the build,
signing and upload have all happened on GitHub's macOS runners. That has been
the whole reason this is automated.

**You do need a paid Apple Developer Program membership** ($99/year). A free
Apple ID has been able to sideload to your own device but not to use
TestFlight.

Throughout: the bundle ID has been `com.danheskett.gloryofrome`, and it has
had to match `ios/Info.plist` exactly.

**Already done this for another app on the same team?** The Apple Distribution
certificate (step 2) and the App Store Connect API key (step 5) have been
team-wide, so the existing `.p12`, its password and the `.p8` have been
reusable as this repo's secrets. Steps 1, 3 and 4 (App ID, provisioning
profile, app record) have been per-app and have had to be done for Glory of
Rome.

**Most of this has been scripted.** `scripts/asc_setup.py` has done steps 1
and 3 through the App Store Connect API (App ID, then the App Store profile
bound to the certificate whose SHA-1 you pass), and after step 4 has set the
category, content rights, age rating, privacy policy URL and a free price.
Step 4 (the app record) and the App Privacy label have had no API and have
stayed in the console.

---

## 1. Register the App ID

[developer.apple.com](https://developer.apple.com/account) → **Certificates,
Identifiers & Profiles** → **Identifiers** → **+**

- Type: **App IDs** → **App**
- Description: `Glory of Rome`
- Bundle ID: **Explicit** → `com.danheskett.gloryofrome`
- Capabilities: leave everything off. The app has used none.

While you are here, note your **Team ID** (Membership Details, a 10-character
string like `A1B2C3D4E5`). That has been `IOS_TEAM_ID`.

## 2. Create the distribution certificate (no Mac)

Generate a private key and a certificate signing request locally:

```sh
openssl genrsa -out ios_distribution.key 2048
openssl req -new -key ios_distribution.key -out ios_distribution.csr \
  -subj "/emailAddress=dan@danheskett.com/CN=Danny Heskett/C=US"
```

developer.apple.com → **Certificates** → **+** → **Apple Distribution**.

> ⚠️ It has to be **Apple Distribution**, not the legacy "iOS Distribution
> (App Store and Ad Hoc)". The release workflow has picked the signing identity
> with `awk '/Apple Distribution/'`, and the legacy certificate's common name
> has started with "iPhone Distribution" instead, so signing would fail to find
> an identity.

Upload `ios_distribution.csr`, download the resulting `distribution.cer`, then
bundle it with the private key into a `.p12`:

```sh
openssl x509 -in distribution.cer -inform DER -out distribution.pem -outform PEM
openssl pkcs12 -export \
  -inkey ios_distribution.key -in distribution.pem \
  -out distribution.p12 -name "Apple Distribution" \
  -passout pass:CHOOSE_A_PASSWORD
```

Keep `distribution.p12`, the password, and `ios_distribution.key` somewhere
safe and backed up. Generate them **outside the repo** — `~/apple-signing/` is
a good home. `.gitignore` has covered `*.key`, `*.csr`, `*.cer`, `*.pem`,
`*.p12` and `*.p8` as a backstop, but keep the private key out of the working
tree in the first place.

## 3. Create the App Store provisioning profile

developer.apple.com → **Profiles** → **+**

- Type: **App Store Connect** (under Distribution)
- App ID: `com.danheskett.gloryofrome`
- Certificate: the Apple Distribution certificate from step 2

Download it as `profile.mobileprovision`.

## 4. Create the app record in App Store Connect

[appstoreconnect.apple.com](https://appstoreconnect.apple.com) → **Apps** →
**+** → **New App**

| Field | Value |
| --- | --- |
| Platform | iOS |
| Name | `Glory of Rome` (see [LISTING.md](LISTING.md) for fallbacks if taken) |
| Primary Language | English (U.S.) |
| Bundle ID | `com.danheskett.gloryofrome` |
| SKU | `gloryofrome` |

This record has had to exist **before** the first upload: `altool` has
uploaded *to* an app, and `scripts/testflight_notes.py` has looked the app up
by bundle ID.

TestFlight internal testing has needed **no** screenshots, description or
privacy policy. Those have been for submitting to the store; the copy has been
in [LISTING.md](LISTING.md).

## 5. Create an App Store Connect API key

App Store Connect → **Users and Access** → **Integrations** → **App Store
Connect API** → **+**

- Name: `Glory of Rome CI`
- Access: **App Manager**

Download `AuthKey_XXXXXXXXXX.p8`. **Apple has allowed exactly one download.**
Note the **Key ID** (in the filename and the table) and the **Issuer ID** (shown
above the table, a UUID).

## 6. Set the seven repo secrets

Note the encodings — they have not all been the same. The two binary files
have been base64'd because a GitHub secret is text; the `.p8` has already been
text and has been stored raw, because the workflow writes it straight back out
as a PEM file.

```sh
base64 -w0 distribution.p12        | gh secret set IOS_CERT_P12
gh secret set IOS_CERT_PASSWORD    -b 'CHOOSE_A_PASSWORD'        # from step 2
base64 -w0 profile.mobileprovision | gh secret set IOS_PROVISIONING_PROFILE
gh secret set IOS_TEAM_ID          -b 'A1B2C3D4E5'               # from step 1

gh secret set ASC_KEY_P8 < AuthKey_XXXXXXXXXX.p8                 # raw, NOT base64
gh secret set ASC_KEY_ID     -b 'XXXXXXXXXX'
gh secret set ASC_ISSUER_ID  -b '69a6de70-....-....-....-........'
```

(Or add them in the GitHub UI: **Settings → Secrets and variables → Actions →
New repository secret**.)

The two groups have gated independently:

- `IOS_CERT_P12` + `IOS_CERT_PASSWORD` + `IOS_PROVISIONING_PROFILE` +
  `IOS_TEAM_ID` → `build-ios` has produced a **signed** `.ipa` instead of an
  unsigned one.
- `ASC_KEY_P8` + `ASC_KEY_ID` + `ASC_ISSUER_ID` → `publish-testflight` has
  validated and uploaded it, and `testflight-notes` has attached the release
  notes.

All seven have been needed end to end. With only the first four set, the
release has attached a signed `.ipa` to the GitHub Release for uploading by
hand.

## 6a. The app icon

One file, `ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png`, 1024x1024
and opaque; `actool` has derived every other size from it. A signed
`make ios` has stopped without it rather than building a bundle Apple would
reject.

## 7. Cut a release

Merge to `main`, or run the **release** workflow manually with
`dry_run` **unchecked**. Then:

1. `build-ios` has signed the `.ipa` and verified it is App Store-shaped.
2. `publish` has tagged `release-N` and attached the artifacts.
3. `publish-testflight` has run `altool --validate-app` first (its errors
   have been far better than the upload path's) and then `--upload-app`.
4. Apple has processed the build for 5–15 minutes.
5. `testflight-notes` has polled until the build is `VALID`, then written the
   commit message into "What to Test".

## 8. Add internal testers

App Store Connect → your app → **TestFlight** → **Internal Testing** → **+** on
Testers or Groups.

Internal testers have had to be Users on your App Store Connect account (up
to 100). **Internal testing has needed no Beta App Review**, so builds have
been installable as soon as processing finishes. External testers have needed
review — typically a day or two, and only once per major version.

---

## What the repo has handled

These have been the upload rejections that cost the most time, and the
`Makefile` and the release workflow have dealt with all of them:

| Problem | Handled by |
| --- | --- |
| ITMS-90713, missing top-level `CFBundleIconName` | `plutil -replace` after `actool` |
| App icon rejected for having an alpha channel | the one icon file must be opaque; see `ios/Assets.xcassets/AppIcon.appiconset/README.md` |
| A signed build assembled with no icon at all | `make ios` has failed rather than producing a bundle Apple would reject |
| "Built with a beta version of Xcode" | `DTXcode` / `DTXcodeBuild` / `DTSDK*` injected from the runner's toolchain |
| Export-compliance question on every upload | `ITSAppUsesNonExemptEncryption = false` in `Info.plist` |
| "Built with an SDK that is too old" | `build-ios` has failed early with a clear message if the runner's SDK is older |
| `CFBundleVersion` has to increase per upload | the larger of `release-N` and the next number App Store Connect has not seen (`scripts/asc_next_build.py`) |
| Missing `CFBundleSupportedPlatforms` | injected via `PlistBuddy` |

## Troubleshooting

**"no Apple Distribution identity found in the keychain"** — the certificate
has been the legacy iOS Distribution type, or the `.p12` has not included the
private key.
Redo step 2, making sure `openssl pkcs12 -export` got both `-inkey` and `-in`.

**"no app found for bundle id ..."** from `testflight-notes` — the App Store
Connect app record (step 4) has not existed, or its bundle ID has not matched
`ios/Info.plist`.

**Upload rejected: "The provided entity includes an attribute with an invalid
value"** — usually a duplicate `CFBundleVersion`. Each upload has needed a
higher number; cut a new release rather than re-running the old one.

**`publish-testflight` says "Skipping"** — one of the three `ASC_*` secrets
has been missing, or the `.ipa` has come out unsigned (so one of the four
`IOS_*` secrets has been missing). The step has printed which of the two
conditions failed.
