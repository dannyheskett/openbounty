#!/usr/bin/env bash
# Phase 14 regression suite for the combat damage formula.
#
# Each line is "ARG  EXPECTED-DIGEST". The harness re-runs the digest
# and asserts equality. A digest mismatch means the formula behavior
# changed — either intended (regenerate goldens with `--write`) or
# regression (bisect).
#
# Cases are picked to exercise distinct code paths: low-skill vs
# high-skill, a SCYTHE attacker, a MAGIC-vs-IMMUNE pair, and large
# stacks to stress turn_count snapshotting.

set -e
BIN="${BIN:-./build/openbounty}"

CASES=(
  # Original Phase 14 set
  "12345:peasants:50:orcs:30:5     a25e1141f58e75d3"
  "99:knights:10:dragons:5:3       b7c1e9858f457aaf"
  "777:demons:20:peasants:100:4   0fb49e22fa60e35c"
  "42:archers:25:wolves:20:6      265821b6835569f8"
  "1:dragons:5:peasants:100:1     5e51c26b49093a9c"
  # Same-troop matchups (uniform combat — exercises symmetry)
  "1:peasants:50:peasants:50:5     bcf382e16c5a05c4"
  "1:archers:20:archers:20:5       3fd3d372e8f3254c"
  "1:pikemen:15:pikemen:15:5       6c6dfc8cc97d3706"
  "1:knights:8:knights:8:5         14f9c77620c785ec"
  "1:dragons:3:dragons:3:3         e2be84f3c28dd790"
  # Asymmetric (unit-type advantages)
  "42:archers:20:peasants:100:4    9fd00c8626b2b9f2"
  "42:knights:10:orcs:20:5         6a33d496cd284eb9"
  "42:sprites:30:ogres:5:4         4aa3f46c732c1791"
  "42:wolves:20:militia:40:4       0b91a3557756238e"
  "42:ghosts:10:skeletons:20:5     1b2d1c4719873d4e"
  # Edge counts
  "7:dragons:1:peasants:200:3      9f8d7de4dc70890d"
  "7:peasants:1:peasants:1:2       4f23ac9fa0839fbd"
  "7:trolls:50:trolls:50:8         b8d2d54fdddefb59"
  # Seed variance — same matchup, different seeds
  "100:archers:25:orcs:15:5        07792e312e0a38a5"
  "200:archers:25:orcs:15:5        bbdf349cae479291"
  "300:archers:25:orcs:15:5        288a8154ba9816a4"
  "400:archers:25:orcs:15:5        a8c990b62a50a7c0"
  "500:archers:25:orcs:15:5        80caddaf7e21a98e"
  # Magic / undead matchups
  "13:vampires:10:zombies:30:5     73f3eabad93626e6"
  "21:archmages:5:demons:8:4       0da6c5a81262affe"
)

fail=0
write_mode=0
[[ "${1:-}" == "--write" ]] && write_mode=1

for case in "${CASES[@]}"; do
  arg="${case%% *}"
  expected="${case##* }"
  if [[ "$arg" == "$expected" ]]; then expected=""; fi

  out=$("$BIN" --combat-test "$arg")
  digest=$(echo "$out" | awk '{print $2}')

  if [[ -n "$expected" && "$digest" != "$expected" ]]; then
    echo "FAIL  $arg"
    echo "  expected $expected"
    echo "  got      $digest"
    fail=1
  elif [[ -z "$expected" ]]; then
    if [[ $write_mode -eq 1 ]]; then
      echo "wrote $arg -> $digest"
    else
      echo "  (no golden) $arg -> $digest"
    fi
  else
    echo "OK    $arg -> $digest"
  fi
done

exit $fail
