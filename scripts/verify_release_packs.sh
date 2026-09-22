#!/usr/bin/env bash
# The release's pack rule, checked on the archives themselves.
#
#   openbounty-*   the engine: NO pack file of any kind. The desktop player
#                  extracts King's Bounty's pack from their own KB.EXE.
#   gloryofrome-*  the game: MUST carry assets/glory-of-rome.openbounty, and
#                  nothing from King's Bounty.
#   *-web-*        a browser bundle: its game's pack embedded in
#                  openbounty.data (King's Bounty in openbounty-*, Glory of
#                  Rome in gloryofrome-*), never a loose pack file.
#
# Every archive in the given directory is checked. The lister's own status is
# tested separately: inside an `if`, a pipeline's status is its truth value,
# so a truncated archive once made the lister fail, grep report "no match",
# and the guard pass clean.
#
#   scripts/verify_release_packs.sh [dist-dir]

set -u
DIR="${1:-dist}"
fail=0

list() {
    case "$1" in
        *.tar.gz) tar -tzf "$1" ;;
        *.zip)    unzip -Z1 "$1" ;;
        *)        return 2 ;;
    esac
}

for f in "$DIR"/*.tar.gz "$DIR"/*.zip; do
    [ -e "$f" ] || continue
    name=$(basename "$f")
    if ! listing=$(list "$f" 2>/dev/null); then
        echo "FAIL: cannot read $name"; fail=1; continue
    fi
    if printf '%s\n' "$listing" | grep -qi "kings-bounty"; then
        echo "FAIL: $name contains King's Bounty data"; fail=1
    fi
    case "$name" in
        gloryofrome-*-web-*|openbounty-*-web-*)
            # A web bundle embeds its game's pack inside openbounty.data by
            # design (King's Bounty in openbounty-*, Glory of Rome in
            # gloryofrome-*); no loose pack file may appear beside it.
            if printf '%s\n' "$listing" | grep -qE '\.openbounty$'; then
                echo "FAIL: $name has a loose pack file"; fail=1
            else
                echo "ok:   $name (pack embedded in the .data image)"
            fi ;;
        gloryofrome-*)
            if printf '%s\n' "$listing" | grep -qE '(^|/)assets/glory-of-rome\.openbounty$'; then
                echo "ok:   $name carries the Glory of Rome pack"
            else
                echo "FAIL: $name is missing assets/glory-of-rome.openbounty"; fail=1
            fi ;;
        openbounty-*)
            if printf '%s\n' "$listing" | grep -qE '\.openbounty$'; then
                echo "FAIL: $name contains a pack file"; fail=1
            else
                echo "ok:   $name has no pack"
            fi ;;
    esac
done
exit $fail
