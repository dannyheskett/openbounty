# Release process

How a release of OpenBounty and The Glory of Rome has been cut. The pipeline has
been automated via GitHub Actions; in the normal case the maintainer has done
nothing at all.

---

## 1. Cutting a release

Releases have been sequential build numbers tagged `release-1`, `release-2`,
`release-3`, etc. No semver, no suffixes. The number has been picked
**automatically**: you do not choose or tag it.

Work has reached `main` through `staging`: pull requests have been
squash-merged into `staging`, and a merge-commit pull request from `staging`
to `main` has been the release. Nothing has been pushed to `main` directly.

**Every merge to `main` has cut a release.** Merging the `staging` -> `main`
pull request has pushed its merge commit to `main`, and that push has run the
build workflow (`build.yml`), which has picked the next `N`, built every
target, and published the GitHub Release. The `main` ruleset has refused direct pushes, so that merge has been
the only way anything has reached `main`. Promotions that have changed only docs have
been skipped via `paths-ignore`. The workflow has also been runnable by hand
from the **Actions** tab → **build** → **Run workflow**, which is what you
want to retry a build that has failed for a reason outside the code (see §4); a
hand run has defaulted to `dry_run` (titled DRY RUN), which has built and
verified everything and tagged and published nothing. Shipping the result has
been the separate **release** workflow (§2).

The workflow has:
   - computed the next number `N` (max existing `release-N` tag + 1),
   - built on the triggering commit,
   - on success, created the `release-N` tag at that commit and published
     the GitHub Release.

The release has landed on the repo's GitHub Releases page under the new
`release-N` tag, with the artifacts in "What every release has shipped"
below. Release notes have been auto-generated from commits since the previous
tag.

The version baked into the binary has been reported as `openbounty build N`
by `--version`.

> **Note:** there has been no `VERSION` file and no `git tag` step on your
> part. The workflow has created the tag *after* all builds have succeeded (see
> §2), so a failed build has left `N` unused and the next run has reused it.

---

## 2. What the workflow has done

Building and shipping have been two separate workflows.
`.github/workflows/build.yml` has been triggered by the merge of a `staging` ->
`main` pull request (and by hand, as a dry run). It has built, tagged and
published the GitHub Release, and has never touched a store. Any failure has
failed the run and nothing after it has run. Its jobs:

- **guard**: the attribution guard (`attribution-guard.yml`, reused via
  `workflow_call`) has gated everything, so a violating commit has never
  reached the tag/publish step.
- **prepare**: has computed the next release number `N` from the existing
  `release-*` tags, and captured the triggering commit SHA up front (so a
  mid-build merge to `main` has never changed what has been tagged).
- **linux + windows builds** (Ubuntu 22.04): has rebuilt raylib from source
  against the runner's glibc, run `make test` (the full suite, unit, e2e,
  autoplay, and the combat-formula regression digests), built the Linux
  release binary and Win64+Win32 binaries, packaged each as an OpenBounty
  archive and an archive of The Glory of Rome, and verified the pack rule
  (`scripts/verify_release_packs.sh`).
- **macOS universal build** (macOS 14, Apple Silicon): has rebuilt raylib for
  arm64+x86_64 and lipo'd them into a universal static archive, built the
  universal binary, ad-hoc codesigned it, packaged both archives and verified
  the pack rule.
- **web (WASM) build** (Ubuntu): has set up emsdk, built raylib for both Linux
  and web, built both wasm bundles and packaged `dist-web`. It has needed the
  *Linux* toolchain as well as emsdk because the wasm target has depended on the
  asset pack, and the native binary has been what has zipped that pack.
- **iOS build** (macOS 26): has built raylib for macOS and the pack tool
  first -- the .app has embedded The Glory of Rome pack and the native binary
  has been what has zipped that pack -- then packaged the device `.ipa`. The
  `.ipa` has been App Store-signed in a throwaway keychain, with the
  entitlements the Makefile has written and the icon compiled into
  `Assets.car`; a missing Apple signing secret has failed the build. The build number has been the larger of `N` and
  the next number App Store Connect has not seen (`scripts/asc_next_build.py`),
  so it has never collided with a build Apple already held. The job has then
  verified the bundle has been App Store-shaped.
- **android build** (Ubuntu): has installed the NDK, build-tools and platform,
  built raylib for `arm64-v8a` and the Linux toolchain (the APK has embedded
  The Glory of Rome pack, and the native binary has been what has zipped that
  pack), then
  packaged a debug-signed sideload **APK** and an upload-signed **AAB**; a
  missing `PLAY_*` signing secret has failed the build. Mobile has
  been The Glory of Rome only; the job has asserted no King's Bounty pack has been
  inside either artifact.
- **publish** (Ubuntu): has downloaded all build artifacts, created the
  `release-N` **tag at the triggering SHA**, and published the GitHub Release
  with auto-generated notes and the archives attached.
`.github/workflows/release.yml` has been run by hand, for a chosen `release-N`,
and has shipped that release's own `.aab` and `.ipa` from the GitHub Release.
Its jobs have run one after another, and the first failure has stopped the
rest:

- **play** (Ubuntu): has uploaded the AAB to the Play track picked
  (`com.danheskett.gloryofrome` -- the app, not the repository), with the
  listing in the same edit, and has read the track back.
- **appstore-upload** (macOS 26): has validated and uploaded the `.ipa` (or
  said App Store Connect already had that build number), taking the build
  number from the `.ipa` itself.
- **appstore-submit** (Ubuntu): has waited for Apple's processing, written the
  "What to Test" note for internal testers, withdrawn any earlier version still
  pending App Review, submitted version 1.0.N, and read it back.

A missing secret has failed the job it was needed in; temporary store errors
have been retried three times and have then failed.

Tagging has happened in the publish job, after every build job has succeeded. If
any build has failed, no tag has been created and `N` has been reused next time.

Each desktop archive has contained: the binary, `README.txt` (rendered from
`dist/README.txt.in` with the build number substituted), `LICENSE`, and
`NOTICES.md`, and its game's pack in `assets/`: `kings-bounty.openbounty`
in an `openbounty-*` archive, `glory-of-rome.openbounty` in a `gloryofrome-*`
one. `scripts/verify_release_packs.sh` has checked that each archive carries
its own pack and nothing of the other game.

Each web archive has embedded its game's pack inside `openbounty.data`,
since it must carry a pack to run at all: `openbounty-*-web-wasm.zip` King's
Bounty, `gloryofrome-*-web-wasm.zip` The Glory of Rome. The site
(danheskett.com) has pulled each from the latest release by its prefix, into
`/dist/openbounty/` and `/dist/gloryofrome/`.

The Android and iOS artifacts have carried `glory-of-rome.openbounty` inside
them, which has been ours to distribute. Their own guards have checked the opposite
thing -- that the King's Bounty pack is *not* in there.

**Secrets the Android path has needed**: `PLAY_UPLOAD_KEYSTORE` (base64 of
the upload keystore), `PLAY_KEY_ALIAS`, `PLAY_KEYSTORE_PASSWORD`,
`PLAY_KEY_PASSWORD` for signing the AAB, and `PLAY_SERVICE_ACCOUNT_JSON` for
the Play push. Every one has been required: a missing signing secret has
failed `build.yml`, a missing `PLAY_SERVICE_ACCOUNT_JSON` the release.

**Secrets the iOS path has needed**: `IOS_CERT_P12` (base64 of the Apple
Distribution certificate and key), `IOS_CERT_PASSWORD`,
`IOS_PROVISIONING_PROFILE` (base64 of the App Store `.mobileprovision`) and
`IOS_TEAM_ID` to sign; `ASC_KEY_P8`, `ASC_KEY_ID` and `ASC_ISSUER_ID` (an App
Store Connect API key) to upload. Every one has been required: a missing
signing secret has failed `build.yml`, a missing `ASC_*` secret the release.

---

## What every release has shipped

Each merge of `staging` into `main` has produced one `release-N` with all of these:

| Artifact | What it has been |
|---|---|
| `openbounty-build-<N>-linux-x86_64.tar.gz`, `-windows-x86_64.zip`, `-windows-i686.zip`, `-macos-universal.zip` | **OpenBounty** -- King's Bounty: the binary with `assets/kings-bounty.openbounty`. |
| `gloryofrome-build-<N>-linux-x86_64.tar.gz`, `-windows-x86_64.zip`, `-windows-i686.zip`, `-macos-universal.zip` | **The Glory of Rome** -- the same binary with `assets/glory-of-rome.openbounty` beside it, where pack discovery has looked, so it has started with no flags. |
| `openbounty-build-<N>-web-wasm.zip` | The King's Bounty browser build (its pack embedded in `openbounty.data`), served at danheskett.com/dist/openbounty/. |
| `gloryofrome-build-<N>-web-wasm.zip` | The Glory of Rome browser build (its pack embedded in `openbounty.data`), served at danheskett.com/dist/gloryofrome/. |
| `gloryofrome-build-<N>-ios-arm64.ipa` | The App Store-signed iOS app, shipped by `release.yml`. |
| `gloryofrome-build-<N>-android-arm64.apk`, `gloryofrome-build-<N>-android.aab` | The Android sideload APK and the upload-signed Play bundle, shipped by `release.yml`. |

`scripts/verify_release_packs.sh` has enforced the pack rule on every
archive in each build job: no pack in an `openbounty-*` desktop archive,
Rome's pack present in every `gloryofrome-*` desktop one, nothing from King's
Bounty in any file name, and in a web zip the pack only inside
`openbounty.data`, never loose. The Linux PR job has built and checked the
Rome package too.

---

## 3. CI on every PR

`.github/workflows/ci.yml` has run on every pull request. The Linux job has
run the full test suite (`make test`), built the dev and release binaries
(`make all release`), packaged the Rome Linux archive and checked the pack
rule, and validated the store listings (`scripts/store_listing.py --check`).
Windows, macOS, web, iOS and Android jobs have run a cross-compile /
universal / wasm / Simulator / APK smoke build as cheap insurance that the
other targets have still built before a release has been cut. The Android
job has also unzipped the APK it has built and asserted it has carried the
pack, the `.so` and
`classes.dex`, and no King's Bounty pack.

CI has run on doc-only pull requests too; the release workflow has been the
one that has skipped them (`paths-ignore`).

CI has run on pull requests into `staging` and into `main` alike, so a
promotion has been tested as a pull request before its merge has reached `main`.
The `staging` -> `main` pull request has also had to pass the **from staging**
check (`.github/workflows/promotion.yml`): the `main` ruleset has required it,
because GitHub has had no setting that has limited a pull request's source
branch. The push that a merge has made to `main` has not run CI again; it has gone to the
release workflow, which has run the same test suite before publishing
anything.

---

## 4. Recovering from a failed release

Because the tag has been created **last** (only after all builds have succeeded), a
failed build has left no tag and no release, and the same `N` has been
reused by the next run. If the failure has been outside the code (a runner, an
expired secret), re-run `build.yml` by hand with `dry_run` off; it has built
`main`'s tip. A failed `release.yml` run has shipped nothing past the job that
failed, and has been re-run for the same `release-N` once the cause has been
fixed: a store that already has the build has said so and carried on. If the code needs a fix, land it on `staging`
through a pull request and promote again with a new `staging` -> `main` pull
request; `main` has taken no direct push.

If a run has failed *after* the publish job has partially created the tag or
release:

```sh
# delete the tag locally and on origin
git push origin :refs/tags/release-3
git tag -d release-3

# if a GitHub Release exists for it, delete it too
gh release delete release-3 --yes
```

Then re-run the workflow by hand, or land the fix on `staging` and promote it
with a new `staging` -> `main` pull request, as above.

---

## 5. Version handling

The build number has come from the **`release-*` git tags**: the Makefile has
derived `RELEASE_VERSION` as the highest `release-N` tag number, falling
back to `0` when there have been no tags (a fresh checkout), and
`OPENBOUNTY_VERSION` has defaulted to it. The release workflow has passed the
computed `N` explicitly via `RELEASE_VERSION=N` on every `make` invocation,
so the binary has been stamped with the release number even before the tag
has existed.

The number has been embedded into every binary at compile time (into
`build/version.h`) and exposed via:

```sh
./openbounty --version       # → openbounty build 3
```

To override locally for testing, pass either variable on the make command
line, the first being the form the release workflow has used:

```sh
make RELEASE_VERSION=99
./build/debug/openbounty --version   # → openbounty build 99
```

---

## 6. Local builds

The same Makefile targets the workflow has used have been available locally:

```sh
make                  # debug build (build/debug/openbounty), compile only
make test             # build + run the full test suite
make release          # Linux release binary (build/release/openbounty, static libgcc)
make windows          # Win64 + Win32 cross-compile (needs mingw-w64)
make mac              # macOS universal (only on macOS)
make web              # WebAssembly bundles, one per pack (needs emsdk on PATH)
make web-serve        # build + serve them on localhost:8080/<pack>/openbounty.html
make dist-linux       # OpenBounty Linux archive in dist/
make dist-windows     # OpenBounty Windows zips in dist/
make dist-mac         # OpenBounty macOS zip in dist/ (only on macOS)
make dist-rome-linux  # The Glory of Rome Linux archive (also -windows, -mac)
make dist-web         # the two WASM zips in dist/ (needs emsdk)
make dist-android     # sideload APK (also dist-android-play for the AAB)
make dist-ios         # device .ipa (only on macOS)
make dist             # linux + windows + mac at once (not web)
```

`dist/` archives have been gitignored.
