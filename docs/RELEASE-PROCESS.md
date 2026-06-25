# Release process

How to cut a release of OpenBounty. The pipeline is automated via
GitHub Actions; the maintainer's job is to click one button.

---

## 1. Cutting a release

Releases are sequential build numbers tagged `release-1`, `release-2`,
`release-3`, etc. No semver, no suffixes. The number is picked
**automatically** — you do not choose or tag it.

1. Go to the repo's **Actions** tab → the **release** workflow →
   **Run workflow** (on `main`).

2. That's it. The workflow:
   - computes the next number `N` (max existing `release-N` tag + 1),
   - builds the binaries on the dispatched commit,
   - on success, creates the `release-N` tag at that commit and
     publishes the GitHub Release.

3. Watch the run on the Actions page. Total time is about 3 minutes.

4. The release lands on the repo's GitHub Releases page under the new
   `release-N` tag, with four artifacts attached:

   - `openbounty-build-N-linux-x86_64.tar.gz`
   - `openbounty-build-N-windows-x86_64.zip`
   - `openbounty-build-N-windows-i686.zip`
   - `openbounty-build-N-macos-universal.zip`

   Release notes are auto-generated from commits since the previous
   tag.

The version baked into the binary is reported as `openbounty build N`
by `--version`.

> **Note:** there is no `VERSION` file and no `git tag` step on your
> part. The tag is created by the workflow *after* all builds succeed
> (see §2), so a failed build leaves `N` unused and the next dispatch
> reuses it.

---

## 2. What the workflow does

`.github/workflows/release.yml` is triggered manually
(`workflow_dispatch`). It runs four jobs:

- **prepare** — computes the next release number `N` from the existing
  `release-*` tags, and captures the dispatched commit SHA up front (so
  a mid-build push to `main` can't change what gets tagged).
- **linux + windows builds** (Ubuntu 22.04): rebuilds raylib from
  source against the runner's glibc, runs `make test` (the full
  suite — unit, e2e, autoplay, and the combat-formula regression
  digests), builds the Linux release binary and Win64+Win32 binaries,
  packages each into the user-facing archive, and asserts no
  `.openbounty` pack file leaked into any archive.
- **macOS universal build** (macOS 14, Apple Silicon): rebuilds raylib
  for arm64+x86_64 and lipos them into a universal static archive,
  builds the universal binary, ad-hoc codesigns it, packages it.
- **publish** (Ubuntu): downloads all build artifacts, creates the
  `release-N` **tag at the dispatched SHA**, and publishes the GitHub
  Release with auto-generated notes and the four archives attached.

Tagging happens in the publish job, after every build job succeeds. If
any build fails, no tag is created and `N` is reused next time.

Each archive contains: the binary, `README.txt` (rendered from
`dist/README.txt.in` with the build number substituted), `LICENSE`, and
`NOTICES.md`. **No `.openbounty` pack file ships** — the asset pack is
DOS-extracted and copyright-restricted; users supply their own via
`./openbounty --extract /path/to/KB.EXE`. The pack-leak check is
enforced in every build job.

---

## 3. CI on every PR

`.github/workflows/ci.yml` runs on every pull request. The Linux job
builds the dev binary (`make`) and runs the full test suite
(`make test`). Windows + macOS jobs run a cross-compile / universal
smoke build as cheap insurance that the other platforms still build
before a release is cut.

Doc-only changes (`**.md`, `docs/**`) skip CI. Pushes to `main` do not
run CI — releases are cut manually via the release workflow.

---

## 4. Recovering from a failed release

Because the tag is created **last** (only after all builds succeed), a
failed build leaves no tag and no release — just re-dispatch the
workflow once the issue is fixed; the same `N` is reused.

If a run failed *after* the publish job partially created the tag or
release:

```sh
# delete the tag locally and on origin
git push origin :refs/tags/release-3
git tag -d release-3

# if a GitHub Release was created, delete it too
gh release delete release-3 --yes
```

Then fix the issue, push to `main`, and re-dispatch the release
workflow.

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

To override locally for testing:

```sh
OPENBOUNTY_VERSION=99 make
./build/debug/openbounty --version   # → openbounty build 99
```

---

## 6. Local builds

The same Makefile targets the workflow uses are available locally:

```sh
make                # debug build (build/debug/openbounty) — compile only
make test           # build + run the full test suite
make release        # Linux release binary (build/release/openbounty, static libgcc)
make windows        # Win64 + Win32 cross-compile (needs mingw-w64)
make mac            # macOS universal (only on macOS)
make dist-linux     # Linux release archive in dist/
make dist-windows   # Windows zips in dist/
make dist-mac       # macOS zip in dist/ (only on macOS)
make dist           # All three at once
```

`dist/` archives are gitignored.
