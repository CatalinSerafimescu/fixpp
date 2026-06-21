#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tools/check_no_raw_atomic_shared_ptr.sh
#
# 046-atomic-shared-ptr (NFR-017) census-regrowth guard (FR-005 / SC-004).
#
# Rejects any NEW raw `std::atomic<std::shared_ptr<...>>` (or the free-function
# std::atomic_{load,store,exchange,compare_exchange*} forms applied to a
# shared_ptr) in project-owned headers/sources. The raw form does not compile
# under libc++ (no working P0718), so a future edit re-introducing it would
# silently re-break the libc++ build. Production code MUST use the portable
# `fixpp::sync::atomic_shared_ptr<T>` instead.
#
# Enforces EXACT-SET completeness (not just "fix today's four sites"):
# [[feedback_completeness_gate_exact_set_not_subset]].
#
# Scope: include/ + src/ (project-owned). EXCLUDED:
#   - the primitive's own header (it legitimately references the std form on the
#     native path via the alias; the fallback never names the raw template),
#   - tests/ and bench/ (may exercise the std type directly for comparison),
#   - third-party / generated trees.
#
# Exit codes: 0 — clean; 1 — a raw occurrence found outside the allow-list.
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

# The one file allowed to mention the std form (its native-path alias).
ALLOW_RE='include/fixpp/core/sync/detail/atomic_shared_ptr(_detect)?\.hpp'

# Raw spellings that fail under libc++.
PATTERNS=(
  'std::atomic[[:space:]]*<[[:space:]]*std::shared_ptr'
  'std::atomic_load[[:space:]]*\([[:space:]]*&?[A-Za-z_].*shared'
  'std::atomic_store[[:space:]]*\([[:space:]]*&?[A-Za-z_].*shared'
  'std::atomic_exchange[[:space:]]*\([[:space:]]*&?[A-Za-z_].*shared'
  'std::atomic_compare_exchange'
)

violations=0
for sub in include src; do
  dir="$ROOT/$sub"
  [[ -d "$dir" ]] || continue
  while IFS= read -r -d '' f; do
    rel="${f#"$ROOT"/}"
    [[ "$rel" =~ $ALLOW_RE ]] && continue
    for pat in "${PATTERNS[@]}"; do
      if grep -nEH "$pat" "$f" >/dev/null 2>&1; then
        grep -nEH "$pat" "$f" | while IFS= read -r hit; do
          echo "VIOLATION: raw std::atomic<std::shared_ptr> form: $hit" >&2
        done
        violations=$((violations + 1))
      fi
    done
  done < <(find "$dir" \( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' \) -print0)
done

if [[ $violations -gt 0 ]]; then
  echo "" >&2
  echo "check_no_raw_atomic_shared_ptr: FAILED — raw std::atomic<std::shared_ptr> in project code." >&2
  echo "  Use fixpp::sync::atomic_shared_ptr<T> (NFR-017 / feature 046) — the raw form" >&2
  echo "  does not compile under libc++ (no working P0718)." >&2
  exit 1
fi

echo "check_no_raw_atomic_shared_ptr: OK — no raw std::atomic<std::shared_ptr> in project code."
exit 0
