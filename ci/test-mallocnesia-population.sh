#!/usr/bin/env bash
# ci/test-mallocnesia-population.sh — pin tools/check_mallocnesia_population.py (fixpp#448).
#
# That checker exists because a label silently selected 8 of 18 real gates on main, and a
# checker which cannot report that is worth nothing. So every branch gets a mutant here,
# and each is asserted to produce its OWN message — not merely a nonzero exit, which a
# typo also produces.
#
# BUILDLESS on purpose: `ctest -N` reads CTestTestfile.cmake and nothing else, so each
# case is a three-line synthetic file rather than a configured tree. Driving the real
# real population could only ever exercise the passing path; T0 covers that one.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CHECK="$REPO/tools/check_mallocnesia_population.py"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0
# mk <dir> <line>...  — build a synthetic ctest tree
mk() {
  local d="$TMP/$1"; shift
  mkdir -p "$d"; : > "$d/CTestTestfile.cmake"
  local name labels
  for spec in "$@"; do
    name="${spec%%:*}"; labels="${spec#*:}"
    printf 'add_test(%s "/bin/true")\n' "$name" >> "$d/CTestTestfile.cmake"
    printf 'set_tests_properties(%s PROPERTIES LABELS "%s")\n' "$name" "$labels" \
      >> "$d/CTestTestfile.cmake"
  done
  echo "$d"
}

check() {  # check <name> <want-rc> <want-substring> <dir>
  local name="$1" want_rc="$2" want="$3" dir="$4" out rc
  out="$(python3 "$CHECK" --build-dir "$dir" 2>&1)"; rc=$?
  if [ "$rc" != "$want_rc" ]; then
    echo "FAIL  $name: exit $rc, wanted $want_rc"; echo "$out" | sed 's/^/      /' | head -3
    fail=$((fail+1)); return
  fi
  if ! printf '%s' "$out" | grep -qF -- "$want"; then
    echo "FAIL  $name: exit $rc as wanted, but no mention of: $want"
    echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1)); return
  fi
  echo "ok    $name"; pass=$((pass+1))
}

# ⚠️ DERIVED from the checker, never restated. Every fixture below must contain all of
# DECLARED_EXTRAS or rule (3) fires and four arms fail with a rule-3 message instead of
# the one they assert. A hand-written copy here HAS already gone stale once: renaming
# the positive control into the *_mallocnesia convention silently broke this list.
EXTRAS=()
while IFS= read -r _e; do EXTRAS+=("${_e}:mallocnesia"); done < <(
  python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import check_mallocnesia_population as m; print("\n".join(m.DECLARED_EXTRAS))' "$REPO/tools")
[ "${#EXTRAS[@]}" -gt 0 ] || { echo "FAIL: could not read DECLARED_EXTRAS from the checker"; exit 1; }

# The checker also requires exactly one positive control in the label. Every fixture that
# is meant to reach the LATER rules needs one, or it fails on this rule first and the arm
# stops discriminating what it was written for. T4/T5 deliberately omit it — they assert
# the vacuity rules, which fire before this one matters.
CONTROL="alloc_guard_positive_control_mallocnesia:mallocnesia"

# T0 — THE REAL TREE. Without this the suite proves only that the checker can say no.
# Skipped (not failed) when no configured build is present, e.g. on a buildless lane.
if [ -f "$REPO/build/linux-clang-release/CTestTestfile.cmake" ]; then
  check "T0 the real linux-clang-release population reconciles" 0 \
    "named gate(s), all labelled" "$REPO/build/linux-clang-release"
else
  echo "skip  T0 (no configured build/linux-clang-release — buildless lane)"
fi

check "T1 a gate missing the label is REPORTED, not tolerated" 1 \
  "do NOT carry the \`mallocnesia\` label" \
  "$(mk t1 "$CONTROL" "a_mallocnesia:mallocnesia" "b_mallocnesia:alloc_guard" "${EXTRAS[@]}")"

check "T2 an UNDECLARED label member fails (the label cannot become a catch-all)" 1 \
  "nor are declared in DECLARED_EXTRAS" \
  "$(mk t2 "$CONTROL" "a_mallocnesia:mallocnesia" "something_else:mallocnesia" "${EXTRAS[@]}")"

check "T3 a STALE declared row fails (a declaration describing nothing)" 1 \
  "no longer carries the label" \
  "$(mk t3 "$CONTROL" "a_mallocnesia:mallocnesia" "alloc_guard_markers_no_local_def:mallocnesia")"

# ⚠️ THE ONE THAT MATTERS. Zero registered gates makes every set comparison trivially
# true, and `ctest -L mallocnesia --no-tests=error` still exits 0 when ANY label member
# survives — measured on the real tree: "100% tests passed, 0 failed out of 1" while 18
# gates had vanished. This is the branch that sees it.
check "T4 ZERO named gates is RED, not a vacuously clean population" 1 \
  "ZERO tests match the name pattern" \
  "$(mk t4 "${EXTRAS[@]}")"

check "T5 ZERO labelled tests is RED (a CI step that would run nothing)" 1 \
  "would exit 0 having run NOTHING" \
  "$(mk t5 "a_mallocnesia:alloc_guard")"

check "T6 the happy case passes (the checker is not simply always-RED)" 0 \
  "named gate(s), all labelled" \
  "$(mk t6 "$CONTROL" "a_mallocnesia:mallocnesia" "b_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

# ── T7: the POSITIVE CONTROL's own membership. Codex r1 P2: the floor counts NAMES,
# and a name is cheap — a decoy satisfies it while a real gate is deleted. The control
# is the one member whose absence means nobody is checking that interception works, so
# it is asserted by identity rather than left to arithmetic.
check "T7a a missing positive control is REJECTED (the floor cannot see this)" 1 \
  "expected exactly ONE positive control" \
  "$(mk t7a "a_mallocnesia:mallocnesia" "b_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

check "T7b TWO positive controls are rejected (ambiguous: which one vouches?)" 1 \
  "expected exactly ONE positive control" \
  "$(mk t7b "a_mallocnesia:mallocnesia" "$CONTROL" "x_positive_control_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

# ⚠️ And the floor itself, which is what P2 showed a decoy walking past.
out="$(python3 "$CHECK" --build-dir "$(mk t7c "a_mallocnesia:mallocnesia" "$CONTROL" "${EXTRAS[@]}")" --min-gates 5 2>&1)"; rc=$?
if [ "$rc" = 1 ] && printf '%s' "$out" | grep -qF "floor is 5"; then
  echo "ok    T7c the --min-gates floor fires when the population SHRINKS"; pass=$((pass+1))
else
  echo "FAIL  T7c floor did not fire: rc=$rc"; echo "$out" | sed 's/^/      /' | head -2; fail=$((fail+1))
fi

echo
echo "test-mallocnesia-population: $pass passed, $fail failed"
[ "$fail" = 0 ]
