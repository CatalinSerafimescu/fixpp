#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/test_capi_occupancy_negative.sh
#
# 049-c-abi-handles-errors (T015, SC-004) negative gate test for
# tools/check_capi_occupancy.sh. Proves the occupancy gate fails
# DETERMINISTICALLY on each failure class, and passes on the clean tree.
# Uses two source overrides (never mutating the tree):
#   - FIXPP_ERROR_H  — a tampered copy of include/fix/c_api/error.h  (Check A)
#   - FIXPP_CAPI_DOC — a tampered copy of .specify/2i-capi.md        (Check B)
#
# Cases:
#   1. clean tree → gate must pass (exit 0)
#   2. Check A: DECIMAL_INVALID slot redefined 800→999 → gate must fail
#   3. Check A: BUFFER_TOO_SMALL slot collision 6→100   → gate must fail
#   4. Check B: DECIMAL block-count tampered 4→5 in 2i-capi.md → gate must fail
#
# Registered as the ctest `capi_occupancy_negative`.
#
# Exit codes: 0 — clean passes AND every mutation is caught; 1 — otherwise.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="${repo_root}/tools/check_capi_occupancy.sh"
error_h="${repo_root}/include/fix/c_api/error.h"
capi_doc="${repo_root}/.specify/2i-capi.md"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

rc=0

# 1. Clean tree MUST pass.
if bash "$gate" >/dev/null 2>&1; then
  echo "PASS: clean tree → gate exit 0"
else
  echo "FAIL: clean tree should pass but gate returned non-zero" >&2
  rc=1
fi

# 2. Redefined slot MUST fail (DECIMAL_INVALID 800 → 999).
mutated="${tmp}/error_redef.h"
sed -E 's/(#define[[:space:]]+FIXPP_ERR_DECIMAL_INVALID[[:space:]]+\(\(fixpp_error_t\))800\)/\1999)/' \
    "$error_h" > "$mutated"
if ! cmp -s "$error_h" "$mutated"; then
  if FIXPP_ERROR_H="$mutated" bash "$gate" >/dev/null 2>&1; then
    echo "FAIL: redefined slot (DECIMAL_INVALID=999) should fail but gate passed" >&2
    rc=1
  else
    echo "PASS: redefined slot → gate exit non-zero"
  fi
else
  echo "FAIL: mutation sed did not change the header (anchor drift)" >&2
  rc=1
fi

# 3. Duplicated/colliding slot MUST fail (give BUFFER_TOO_SMALL the wire slot 100).
mutated2="${tmp}/error_dup.h"
sed -E 's/(#define[[:space:]]+FIXPP_ERR_BUFFER_TOO_SMALL[[:space:]]+\(\(fixpp_error_t\))6\)/\1100)/' \
    "$error_h" > "$mutated2"
if ! cmp -s "$error_h" "$mutated2"; then
  if FIXPP_ERROR_H="$mutated2" bash "$gate" >/dev/null 2>&1; then
    echo "FAIL: collided slot (BUFFER_TOO_SMALL=100) should fail but gate passed" >&2
    rc=1
  else
    echo "PASS: collided slot → gate exit non-zero"
  fi
else
  echo "FAIL: mutation sed did not change the header (anchor drift)" >&2
  rc=1
fi

# 4. Tampered [2i §4.3] block-count MUST fail (Check B: DECIMAL 4→5).
#    Anchors the sed on the table column `| 2a | 4 |` to avoid hitting §7.4.
mutated3="${tmp}/2i-capi_tampered.md"
sed 's/`FIXPP_ERR_DECIMAL_\*` | 2a | 4 |/`FIXPP_ERR_DECIMAL_*` | 2a | 5 |/' \
    "$capi_doc" > "$mutated3"
if ! cmp -s "$capi_doc" "$mutated3"; then
  if FIXPP_CAPI_DOC="$mutated3" bash "$gate" >/dev/null 2>&1; then
    echo "FAIL: tampered [2i §4.3] DECIMAL count (4→5) should fail but gate passed" >&2
    rc=1
  else
    echo "PASS: tampered [2i §4.3] DECIMAL block-count → gate exit non-zero (Check B)"
  fi
else
  echo "FAIL: mutation sed did not change the doc (anchor drift in 2i-capi.md)" >&2
  rc=1
fi

if [[ "$rc" -eq 0 ]]; then
  echo "test_capi_occupancy_negative: OK"
else
  echo "test_capi_occupancy_negative: FAILED" >&2
fi
exit "$rc"
