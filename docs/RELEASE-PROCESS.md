# Release process

How to cut a release of OpenBounty. The pipeline is automated via
GitHub Actions; in the normal case the maintainer does nothing at all.

---

## 1. Cutting a release

Releases are sequential build numbers tagged `release-1`, `release-2`,
`release-3`, etc. No semver, no suffixes. The number is picked
**automatically**: you do not choose or tag it.

**Every push to `main` cuts a release.** Merging a PR (or pushing
directly) runs the release workflow, which picks the next `N`, builds
every target, and publishes. Docs-only pushes are skipped via
`paths-ignore`. The workflow can also be run by hand from the
**Actions** tab → **release** → **Run workflow**, which is what you
want after a force-push (see the note in §2).

The workflow:
   - computes the next number `N` (max existing `release-N` tag + 1),
   - builds on the triggering commit,
   - on success, creates the `release-N` tag at that commit and
     publishes the GitHub Release.

The release lands on the repo's GitHub Releases page under the new
`release-N` tag, with five artifacts attached:

   - `openbounty-build-N-linux-x86_64.tar.gz`
   - `openbounty-build-N-windows-x86_64.zip`
   - `openbounty-build-N-windows-i686.zip`
   - `openbounty-build-N-macos-universal.zip`
   - `openbounty-build-N-web-wasm.zip`

Release notes are auto-generated from commits since the previous tag.

The version baked into the binary is reported as `openbounty build N`
by `--version`.

> **Note:** there is no `VERSION` file and no `git tag` step on your
> part. The tag is created by the workflow *after* all builds succeed
> (see §2), so a failed build leaves `N` unused and the next run
> reuses it.

---

## 2. What the workflow does

`.github/workflows/release.yml` is triggered by any push to `main` and
by `workflow_dispatch`. It runs six jobs:

- **guard**: the attribution guard (`attribution-guard.yml`, reused via
  `workflow_call`) gates everything, so a violating commit can never
  reach the tag/publish step.
- **prepare**: computes the next release number `N` from the existing
  `release-*` tags, and captures the triggering commit SHA up front (so
  a mid-build push to `main` can't change what gets tagged).
- **linux + windows builds** (Ubuntu 22.04): rebuilds raylib from
  source against the runner's glibc, runs `make test` (the full
  suite, unit, e2e, autoplay, and the combat-formula regression
  digests), builds the Linux release binary and Win64+Win32 binaries,
  packages each into the user-facing archive, and asserts no
  `.openbounty` pack file leaked into any archive.
- **macOS universal build** (macOS 14, Apple Silicon): rebuilds raylib
  for arm64+x86_64 and lipos them into a universal static archive,
  builds the universal binary, ad-hoc codesigns it, packages it.
- **web (WASM) build + hosted publish** (Ubuntu): sets up emsdk, builds
  raylib for both Linux and web, builds the wasm bundle, pushes the
  hosted build to `danheskett.com/dist/openbounty`, and packages
  `dist-web`. It needs the *Linux* toolchain as well as emsdk because
  the wasm target depends on the asset pack, and the native binary is
  what zips that pack.
- **publish** (Ubuntu): downloads all build artifacts, creates the
  `release-N` **tag at the triggering SHA**, and publishes the GitHub
  Release with auto-generated notes and the five archives attached.

Tagging happens in the publish job, after every build job succeeds. If
any build fails, no tag is created and `N` is reused next time.

Each archive contains: the binary, `README.txt` (rendered from
`dist/README.txt.in` with the build number substituted), `LICENSE`, and
`NOTICES.md`. **No loose `.openbounty` pack file ships**: the asset
pack is DOS-extracted and copyright-restricted; desktop users supply
their own via `./openbounty --extract /path/to/KB.EXE`. The pack-leak
check is enforced in every build job.

The web archive is the exception: it must embed the pack to run at all,
so the pack rides inside `openbounty.data`. That is the intended
embedding, and the leak check still passes because no file named
`*.openbounty` is present.

---

## 3. CI on every PR

`.github/workflows/ci.yml` runs on every pull request. The Linux job
builds the dev binary (`make`) and runs the full test suite
(`make test`). Windows, macOS, and web jobs run a cross-compile /
universal / wasm smoke build as cheap insurance that the other targets
still build before a release is cut.

Doc-only changes (`**.md`, `docs/**`) skip CI.

Pushes to `main` skip CI and go straight to the release workflow, which
runs the same test suite before publishing anything.

---

## 4. Recovering from a failed release

Because the tag is created **last** (only after all builds succeed), a
failed build leaves no tag and no release. Fix the issue and push
again, or re-run the workflow by hand; the same `N` is reused.

If a run failed *after* the publish job partially created the tag or
release:

```sh
# delete the tag locally and on origin
git push origin :refs/tags/release-3
git tag -d release-3

# if a GitHub Release was created, delete it too
gh release delete release-3 --yes
```

Then fix the issue and push to `main`, which runs the release
workflow again.

---

## 5. Version handling

The build number comes from the **`release-*` git tags**: the Makefile
derives `OPENBOUNTY_VERSION` as the highest `release-N` tag number,
falling back to `0` when there are no tags (a fresh checkout). The
release workflow passes the computed `N` explicitly via
`OPENBOUNTY_VERSION=N` on every `make` invocation, so the binary is
stamped with the release number even before the tag exists.

The number is embedded into every binary at compile time (into
`build/version.h`) and exposed via:

```sh
./openbounty --version       # → openbounty build 3
```

To override locally for testing, pass it as a make variable, the same form
the release workflow uses:

```sh
make OPENBOUNTY_VERSION=99
./build/debug/openbounty --version   # → openbounty build 99
```

---

## 6. Local builds

The same Makefile targets the workflow uses are available locally:

```sh
make                # debug build (build/debug/openbounty), compile only
make test           # build + run the full test suite
make release        # Linux release binary (build/release/openbounty, static libgcc)
make windows        # Win64 + Win32 cross-compile (needs mingw-w64)
make mac            # macOS universal (only on macOS)
make web            # WebAssembly bundle (needs emsdk on PATH)
make web-serve      # build + serve the web build on localhost:8080
make dist-linux     # Linux release archive in dist/
make dist-windows   # Windows zips in dist/
make dist-mac       # macOS zip in dist/ (only on macOS)
make dist-web       # WASM zip in dist/ (needs emsdk)
make dist           # linux + windows + mac at once (not web)
```

`dist/` archives are gitignored.
