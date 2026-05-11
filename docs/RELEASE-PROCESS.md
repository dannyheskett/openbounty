# Release process

How to cut a release of OpenBounty. The pipeline is automated via
GitHub Actions; the maintainer's job is to push a tag.

---

## 1. Cutting a release

Releases are sequential build numbers: `v1`, `v2`, `v3`, etc. No
semver, no suffixes.

1. Pick the next number (the most recent release plus one).

2. Tag and push:

   ```sh
   git tag v3
   git push origin v3
   ```

   That's it. The workflow takes over: it writes `3` into the
   `VERSION` file, commits the bump back to `main` with a `[skip ci]`
   marker, builds the binaries, and publishes the GitHub Release.

3. Watch the workflow run on the repo's GitHub Actions page. Total
   time is about 3 minutes.

4. The release lands on the repo's GitHub Releases page under the new
   tag, with four artifacts attached:

   - `openbounty-build-3-linux-x86_64.tar.gz`
   - `openbounty-build-3-windows-x86_64.zip`
   - `openbounty-build-3-windows-i686.zip`
   - `openbounty-build-3-macos-universal.zip`

   Release notes are auto-generated from commits since the previous
   tag.

The version that ends up baked into the binary is reported as
`openbounty build 3` by `--version`.

---

## 2. What the workflow does

On a `v<N>` tag push, `.github/workflows/release.yml`:

- **Validates** the tag is a positive integer (rejects anything that
  isn't `v1`, `v2`, ...).
- **Bumps `VERSION`** in the repo to `<N>` and commits with
  `[skip ci]` to keep `main` aligned with the latest release.
- Runs three parallel jobs:
  - **linux + windows builds** (Ubuntu 22.04 runner): rebuilds raylib
    from source against the runner's glibc, runs unit tests + combat
    regression, builds the Linux release binary and Win64+Win32
    binaries, packages each into the user-facing archive, asserts no
    `.openbounty` pack file leaked into any archive.
  - **macOS universal build** (macOS 14 runner, Apple Silicon):
    rebuilds raylib for arm64+x86_64 and lipos them into a universal
    static archive, builds the universal binary, ad-hoc codesigns it,
    packages it.
  - **publish GitHub Release** (Ubuntu): downloads all build artifacts,
    creates the GitHub Release with auto-generated notes.

Each archive contains: the binary, `README.txt` (rendered from
`dist/README.txt.in` with the build number substituted), `LICENSE`,
and `NOTICES.md`. **No `.openbounty` pack file ships** — the asset
pack is DOS-extracted and copyright-restricted; users supply their
own via `./openbounty --extract /path/to/KB.EXE`.

---

## 3. CI on every push and PR

`.github/workflows/ci.yml` runs on every push to `main` and every PR.
It builds the Linux dev binary and runs the unit suite plus the combat
regression. PRs additionally trigger a Win64/Win32 cross-compile smoke
build and a macOS universal smoke build to catch cross-platform
breakage at review time.

Doc-only changes (`**.md`, `docs/**`) skip CI entirely.

---

## 4. Recovering from a failed release

If the workflow fails partway after the tag has been pushed, the tag
exists but the GitHub Release does not (or is incomplete).

```sh
# delete the tag locally and on origin
git push origin :refs/tags/v3
git tag -d v3

# if the workflow's publish step did create a release, delete that too
gh release delete v3 --yes
```

Then fix the underlying issue, commit, re-tag, and push.

---

## 5. Version handling

The repo-root `VERSION` file is the build number (a single integer)
used for untagged dev builds. On every release tag, the workflow
auto-rewrites it to match the tag and pushes the bump back to `main`,
so untagged dev builds always advertise the most recent release
number.

The build number is embedded into every binary at compile time and
exposed via:

```sh
./openbounty --version       # → openbounty build 3
```

To override locally for testing:

```sh
OPENBOUNTY_VERSION=99 make
./build/openbounty --version # → openbounty build 99
```

---

## 6. Local builds

The same Makefile targets the workflow uses are available locally:

```sh
make                # Linux dev + unit tests
make release        # Linux release (static libgcc)
make windows        # Win64 + Win32 cross-compile (needs mingw-w64)
make mac            # macOS universal (only on macOS)
make dist-linux     # Linux release archive in dist/
make dist-windows   # Windows zips in dist/
make dist-mac       # macOS zip in dist/ (only on macOS)
make dist           # All three at once
```

`dist/` archives are gitignored.
