#!/usr/bin/env bash
# apt-get install for CI, made robust against a slow or stuck package mirror:
# each apt call is time-limited (2 min update, 7 min install) and retries its
# downloads, and the whole update-and-install is tried up to three times. A
# step that cannot install FAILS -- well inside the job's 40-minute limit, so
# the job is never cut off by its timeout (which GitHub reports as cancelled,
# not failed). With APT_ARCHIVES set (the
# workflows cache that directory), the downloaded packages are kept there,
# so after one good run nothing is downloaded again; a retried download
# continues where it stopped (apt keeps the partial files there too).
#
#     APT_ARCHIVES=DIR scripts/apt_install.sh PACKAGE...

set -euo pipefail

# The runners' own mirror (azure.archive.ubuntu.com) has stalled for most of an
# hour on the mingw packages: on attempts after the first, Ubuntu's main
# archive is used instead.
use_main_archive() {
    for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
        [ -f "$f" ] && sudo sed -i 's|http://azure.archive.ubuntu.com/ubuntu|http://archive.ubuntu.com/ubuntu|g' "$f"
    done
    # GitHub's Ubuntu images point sources.list at a mirror LIST
    # (mirror+file:/etc/apt/apt-mirrors.txt) whose first entry is Azure's
    # mirror; apt moves down that list only when a mirror errors, not when it
    # is merely slow. Drop Azure from the list so Ubuntu's archive is used.
    if [ -f /etc/apt/apt-mirrors.txt ]; then
        sudo sed -i '/azure\.archive\.ubuntu\.com/d' /etc/apt/apt-mirrors.txt
        grep -q . /etc/apt/apt-mirrors.txt \
            || echo 'http://archive.ubuntu.com/ubuntu/' | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
        echo "[apt_install] mirrors now:" >&2
        cat /etc/apt/apt-mirrors.txt >&2
    fi
    return 0
}

OPTS=(-o Acquire::Retries=5 -o Acquire::http::Timeout=60 -o Acquire::https::Timeout=60)
if [ -n "${APT_ARCHIVES:-}" ]; then
    sudo mkdir -p "$APT_ARCHIVES/partial"
    OPTS+=(-o "Dir::Cache::Archives=$APT_ARCHIVES")
fi

for attempt in 1 2 3; do
    [ "$attempt" -gt 1 ] && use_main_archive
    if sudo timeout 120 apt-get "${OPTS[@]}" update &&
       sudo DEBIAN_FRONTEND=noninteractive timeout 420 \
           apt-get "${OPTS[@]}" install -y --no-install-recommends "$@"; then
        if [ -n "${APT_ARCHIVES:-}" ]; then
            sudo rm -f "$APT_ARCHIVES/lock"
            sudo chmod -R a+rwX "$APT_ARCHIVES"      # for the cache step to save it
        fi
        exit 0
    fi
    echo "[apt_install] attempt $attempt of 3 failed; retrying" >&2
    sleep 15
done
echo "[apt_install] could not install: $*" >&2
exit 1
