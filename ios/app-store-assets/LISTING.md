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

Use the full description from `android/play-assets/LISTING.md`, minus its final
line about building it yourself if the link is to be dropped — a GitHub link
itself is fine.

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
