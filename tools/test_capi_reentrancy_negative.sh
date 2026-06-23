#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/test_capi_reentrancy_negative.sh
#
# 049-c-abi-handles-errors (T021, SC-005) positive/negative test for
# tools/check_capi_reentrancy.sh. Proves: the real 3+ symbol surface PASSES;
# an exported symbol with 0 class tokens FAILS; an exported symbol with 2
# DIFFERENT class tokens FAILS — all deterministically. Fixtures are temp
# headers passed to the gate via its file-args mode (the tree is never touched).
#
# Exit codes: 0 — real surface passes AND both fixtures fail; 1 — otherwise.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
gate="${repo_root}/tools/check_capi_reentrancy.sh"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
rc=0

# 1. Real surface MUST pass.
if bash "$gate" >/dev/null 2>&1; then
  echo "PASS: real C-ABI surface → gate exit 0"
else
  echo "FAIL: real surface should pass but gate returned non-zero" >&2
  rc=1
fi

# 2. Zero-class fixture MUST fail.
zero="${tmp}/zero_class.h"
cat > "$zero" <<'EOF'
#ifndef ZERO_CLASS_H
#define ZERO_CLASS_H
/* A perfectly ordinary description with NO reentrancy class token at all. */
int fixpp_zero_class_fn(int a);
#endif
EOF
if bash "$gate" "$zero" >/dev/null 2>&1; then
  echo "FAIL: 0-class symbol should fail but gate passed" >&2
  rc=1
else
  echo "PASS: 0-class symbol → gate exit non-zero"
fi

# 3. Two-different-class fixture MUST fail (thread-safe + single-thread).
two="${tmp}/two_class.h"
cat > "$two" <<'EOF'
#ifndef TWO_CLASS_H
#define TWO_CLASS_H
/* Reentrancy: thread-safe. Also documented as single-thread (contradiction). */
int fixpp_two_class_fn(int a);
#endif
EOF
if bash "$gate" "$two" >/dev/null 2>&1; then
  echo "FAIL: 2-class symbol should fail but gate passed" >&2
  rc=1
else
  echo "PASS: 2-class symbol → gate exit non-zero"
fi

if [[ "$rc" -eq 0 ]]; then
  echo "test_capi_reentrancy_negative: OK"
else
  echo "test_capi_reentrancy_negative: FAILED" >&2
fi
exit "$rc"
