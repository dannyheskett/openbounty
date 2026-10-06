# The docs

Where each fact about openbounty has lived. Start with
[ARCHITECTURE.md](ARCHITECTURE.md); [GLOSSARY.md](GLOSSARY.md) has defined
the terms, and [../CONTRIBUTING.md](../CONTRIBUTING.md) the house rules.

## Overview

| Doc | What it has covered |
|---|---|
| [../README.md](../README.md) | Releases, the repository layout, building on every platform, the command-line flags, the make targets, the runtime data, the rules and keys in brief. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | The layers, one step of play, packs, determinism, saves. |
| [GLOSSARY.md](GLOSSARY.md) | The terms the code and the docs have used. |
| [TESTING.md](TESTING.md) | The test layers, adding a test, what to run before a pull request, CI. |
| [../engine/README.md](../engine/README.md) | The engine library: its headers, building and linking it, the host callbacks. |

## The game

| Doc | What it has covered |
|---|---|
| [OPENBOUNTY-SPEC.md](OPENBOUNTY-SPEC.md) | Every rule and every piece of state, numbered REQ-NNN: the reference for the engine and the legacy presentation. |
| [PACK-FORMAT.md](PACK-FORMAT.md) | A pack's files and every `game.json` key, with its default. |
| [DESIGN-SPEC.md](DESIGN-SPEC.md) | The modern presentation, numbered DSGN-NNNN: layout, screens, touch, keys. |
| [GLORY-OF-ROME.md](GLORY-OF-ROME.md) | The Glory of Rome pack's design: each choice and the reason for it. |
| [OPENKB-SPEC.md](OPENKB-SPEC.md) | The predecessor project's reference for King's Bounty, kept as written and outside the house style. |

## Art

| Doc | What it has covered |
|---|---|
| [ART-SPEC.md](ART-SPEC.md) | The authoring sizes for a pack's art, derived from the numbers its `game.json` has declared, worked for Glory of Rome. |
| [ART-PIPELINE.md](ART-PIPELINE.md) | Glory of Rome's art routes: which generator, which settings, and why. |
| [ROME-ART.md](ROME-ART.md) | Every generation prompt, generated from the job files. |

## Players in code

| Doc | What it has covered |
|---|---|
| [DEMO-SPEC.md](DEMO-SPEC.md) | The demo player, numbered DM-NNN. |
| [AUTOPLAY-SPECS.md](AUTOPLAY-SPECS.md) | The winnability oracle, numbered AP-NNN. |
| [AUTOPLAY-BASELINE.md](AUTOPLAY-BASELINE.md) | The oracle's measured results on the reference pack, seed by seed and difficulty by difficulty. |

## Platforms and releases

| Doc | What it has covered |
|---|---|
| [IOS-BACKEND.md](IOS-BACKEND.md) | The native iOS backend behind the shell's five seams. |
| [RELEASE-PROCESS.md](RELEASE-PROCESS.md) | How a release has been cut and what it has published. |
| [STORE-SUBMISSION.md](STORE-SUBMISSION.md) | The App Store and Google Play listings and their scripts. |
