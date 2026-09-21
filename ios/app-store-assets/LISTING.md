# Glory of Rome — App Store listing

Copy these into App Store Connect. It mirrors `android/play-assets/LISTING.md`
with the fields Apple has and Play does not, and one rule that differs:

- **Never mention Android, Google Play, or any other store** in the
  description. Apple rejects listings that point at a competing store.

## New App form (Apps → + → New App)

| Field | Value |
| --- | --- |
| Platform | iOS |
| Name | `Glory of Rome` (must be unique App Store-wide) |
| Primary Language | English (U.S.) |
| Bundle ID | `com.danheskett.gloryofrome` |
| SKU | `gloryofrome` |
| User Access | Full Access |

The name is public, capped at 30 characters, and can change with any later
version. The SKU and the bundle ID cannot.

## Subtitle (≤30 chars)

```
Turn-based fantasy strategy
```

## Promotional text (≤170 chars)

Editable without submitting a build.

```
Raise an army, hunt the villains holding Italia, and get them to the Senate before your commission runs out.
```

## Description

Apple's copy, kept separate from Play's because a listing that names another
store is rejected. `scripts/store_listing.py` reads this block.

```
Rome needs a champion. Raise an army, sweep the roads of Italia, hunt down the villains who have carved the peninsula up between them, and deliver them to the Senate before your commission runs out.

Glory of Rome is a turn-based fantasy strategy game: recruit troops, fight tactical battles on a grid, take castles, find artefacts, and track each villain through the provinces that shelter them.

WHAT YOU DO
• Recruit legionaries, auxiliaries and stranger things from dwellings across four zones
• Fight deliberate, readable battles — position, initiative, morale and a handful of spells
• Capture castles, interrogate prisoners, and narrow down where a villain is hiding
• Spend your gold on the army you can actually afford to pay each week
• Race the clock: the commission has a deadline, and the map does not wait

BUILT RIGHT
• Completely offline. No ads, no tracking, no accounts, no in-app purchases.
• Designed for touch: everything is a tap, with an on-screen letter picker instead of the keyboard
• Landscape, full screen, and clear of the notch and the home indicator

FREE AND OPEN SOURCE
Glory of Rome is built on OpenBounty, an open-source engine. Read the code, report a bug, or build it yourself: https://github.com/dannyheskett/openbounty
```

## Keywords (≤100 chars, comma separated, no spaces)

```
strategy,turnbased,fantasy,tactics,rome,roman,army,rpg,offline,retro
```

## Support and marketing URLs

| Field | Value |
| --- | --- |
| Support URL | https://github.com/dannyheskett/openbounty/issues |
| Marketing URL | https://github.com/dannyheskett/openbounty |
| Privacy policy URL | the published URL of `../../android/play-assets/PRIVACY.md` |

## App Privacy

"Data Not Collected" for every category. The app has no network code at all.

## Age rating

Fantasy violence, infrequent/mild. Everything else: none. That lands at 9+.

## Screenshots

Required: 6.9" iPhone (2868×1320 or 1320×2868) landscape, up to 10. iPad is
only required if the app is offered on iPad — `UIDeviceFamily` currently
includes iPad, so either supply 13" iPad captures or drop the 2 from
`UIDeviceFamily` in `ios/Info.plist` before submitting.

## Export compliance

`ITSAppUsesNonExemptEncryption` is already `false` in `ios/Info.plist`, so App
Store Connect stops asking.
