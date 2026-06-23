#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/check_capi_reentrancy.sh
#
# 049-c-abi-handles-errors (T020, FR-014 / SC-005 / data-model E-5 / research D-7)
# per-symbol reentrancy-completeness gate. DISCRETE — NOT folded into
# tools/check_capi_occupancy.sh (it gates a different invariant; [2i §4.10] / P2-5).
#
# For EVERY exported `fixpp_*` function declaration in include/fix/c_api/*.h
# (and the umbrella include/fix/c_api.h), assert the symbol's DOC-BLOCK — the
# contiguous comment block immediately preceding the declaration — contains
# EXACTLY ONE of the three [2i §4.10] reentrancy class tokens:
#   thread-safe | single-thread | requires-session-lock
# 0 unannotated and 0 double-classed both fail. "Token present anywhere in the
# file" is insufficient — it must be the symbol's own preceding doc-block (blocks
# reset on a blank line or an intervening non-comment code line). Matching is
# case-insensitive with superset masking so "Thread-safety: thread-safe" counts
# the standalone token ONCE, not twice.
#
# Usage: with no args, scans the canonical C-ABI public headers. With file args
# (used by the SC-004/T021 negative fixtures), scans exactly those files.
#
# Exit codes: 0 — every exported symbol has exactly one class; 1 — otherwise.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ $# -gt 0 ]]; then
  files=("$@")
else
  files=("${repo_root}/include/fix/c_api.h")
  for h in "${repo_root}"/include/fix/c_api/*.h; do files+=("$h"); done
fi

rc=0
for f in "${files[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "FAIL [check_capi_reentrancy] missing $f" >&2
    rc=1
    continue
  fi
  awk -v FNAME="$f" '
    # Count reentrancy-class tokens in a comment block (case-insensitive,
    # word-boundary via superset masking).
    function classes(s,   t, n) {
      t = tolower(s)
      gsub(/thread-safety/,   "_", t)   # mask superset of thread-safe
      gsub(/single-threaded/, "_", t)   # mask superset of single-thread
      n  = gsub(/thread-safe/,           "&", t)
      n += gsub(/single-thread/,         "&", t)
      n += gsub(/requires-session-lock/, "&", t)
      return n
    }
    BEGIN { incmt = 0; block = ""; fails = 0 }
    {
      line = $0
      if (incmt) {                       # inside a /* ... */ block comment
        block = block " " line
        if (line ~ /\*\//) incmt = 0
        next
      }
      stripped = line
      sub(/^[ \t]+/, "", stripped)
      if (stripped ~ /^$/)      { block = ""; next }                 # blank → reset
      if (stripped ~ /^\/\//)   { block = block " " line; next }      # // comment
      if (stripped ~ /^\/\*/) {                                       # /* ... */ open
        block = block " " line
        if (line !~ /\*\//) incmt = 1
        next
      }
      # CODE line. Is it an exported fixpp_* function declaration?
      if (line ~ /fixpp_[a-z0-9_]+[ \t]*\(/) {
        tmp = line; name = ""
        while (match(tmp, /fixpp_[a-z0-9_]+[ \t]*\(/)) {
          tok = substr(tmp, RSTART, RLENGTH)
          sub(/[ \t]*\($/, "", tok)
          name = tok
          tmp = substr(tmp, RSTART + RLENGTH)
        }
        c = classes(block)
        if (c != 1) {
          printf("FAIL [check_capi_reentrancy] %s: exported %s has %d reentrancy-class token(s) in its doc-block (need exactly 1)\n", FNAME, name, c) > "/dev/stderr"
          fails++
        }
        block = ""
      } else {
        block = ""                       # non-decl code line breaks adjacency
      }
    }
    END { exit (fails > 0 ? 1 : 0) }
  ' "$f" || rc=1
done

if [[ "$rc" -ne 0 ]]; then
  echo "check_capi_reentrancy: FAILED" >&2
  exit 1
fi
echo "check_capi_reentrancy: OK"
exit 0
