#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/test_capi_occupancy_negative.sh
#
# 049-c-abi-handles-errors (T015, SC-004) negative gate test for
# tools/check_capi_occupancy.sh. Proves the occupancy gate fails
# DETERMINISTICALLY when a published slot is redefined, and passes on the clean
# tree. Mutates a COPY of error.h (never the tree) via the gate's FIXPP_ERROR_H
# override. Registered as the ctest `capi_occupancy_negative`.
#
# Exit codes: 0 — clean passes AND every mutation is caught; 1 — otherwise.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="${repo_root}/tools/check_capi_occupancy.sh"
error_h="${repo_root}/include/fix/c_api/error.h"

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

if [[ "$rc" -eq 0 ]]; then
  echo "test_capi_occupancy_negative: OK"
else
  echo "test_capi_occupancy_negative: FAILED" >&2
fi
exit "$rc"
