#!/usr/bin/env bash
# The release's pack rule, checked on the archives themselves.
#
#   openbounty-*   King's Bounty: MUST carry assets/kings-bounty.openbounty,
#                  and nothing from The Glory of Rome.
#   gloryofrome-*  The Glory of Rome: MUST carry assets/glory-of-rome.openbounty,
#                  and nothing from King's Bounty.
#   *-web-*        a browser bundle: its game's pack embedded in
#                  openbounty.data (King's Bounty in openbounty-*, Glory of
#                  Rome in gloryofrome-*), never a loose pack file.
#
# Every archive in the given directory is checked. The lister's own status is
# tested separately: inside an `if`, a pipeline's status is its truth value,
# so a truncated archive would make the lister fail, grep report "no match",
# and the guard pass clean.
#
#   scripts/verify_release_packs.sh [dist-dir]

set -u
DIR="${1:-dist}"
fail=0
checked=0

list() {
    case "$1" in
        *.tar.gz) tar -tzf "$1" ;;
        *.zip)    unzip -Z1 "$1" ;;
        *)        return 2 ;;
    esac
}

for f in "$DIR"/*.tar.gz "$DIR"/*.zip; do
    [ -e "$f" ] || continue
    checked=$((checked + 1))
    name=$(basename "$f")
    if ! listing=$(list "$f" 2>/dev/null); then
        echo "FAIL: cannot read $name"; fail=1; continue
    fi
    # Each game's archives carry nothing of the other's.
    case "$name" in
        gloryofrome-*)
            if printf '%s\n' "$listing" | grep -qi "kings-bounty"; then
                echo "FAIL: $name contains King's Bounty data"; fail=1
            fi ;;
        openbounty-*)
            if printf '%s\n' "$listing" | grep -qi "glory-of-rome"; then
                echo "FAIL: $name contains The Glory of Rome data"; fail=1
            fi ;;
    esac
    case "$name" in
        gloryofrome-*-web-*|openbounty-*-web-*)
            # A web bundle embeds its game's pack inside openbounty.data by
            # design (King's Bounty in openbounty-*, The Glory of Rome in
            # gloryofrome-*); no loose pack file may appear beside it.
            if printf '%s\n' "$listing" | grep -qE '\.openbounty$'; then
                echo "FAIL: $name has a loose pack file"; fail=1
            else
                echo "ok:   $name (pack embedded in the .data image)"
            fi ;;
        gloryofrome-*)
            if printf '%s\n' "$listing" | grep -qE '(^|/)assets/glory-of-rome\.openbounty$'; then
                echo "ok:   $name carries The Glory of Rome pack"
            else
                echo "FAIL: $name is missing assets/glory-of-rome.openbounty"; fail=1
            fi ;;
        openbounty-*)
            if printf '%s\n' "$listing" | grep -qE '(^|/)assets/kings-bounty\.openbounty$'; then
                echo "ok:   $name carries the King's Bounty pack"
            else
                echo "FAIL: $name is missing assets/kings-bounty.openbounty"; fail=1
            fi ;;
    esac
done
# Nothing to check is a failure too: an empty directory proves nothing.
if [ "$checked" -eq 0 ]; then
    echo "FAIL: no archives in $DIR"; fail=1
fi
exit $fail
