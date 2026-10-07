#!/usr/bin/env bash
# ci/test-mallocnesia-population.sh — pin fixpp#448's two static checkers:
# tools/check_mallocnesia_population.py and tools/check_no_raw_mallocnesia_preload.py.
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

# The checker also requires every DECLARED positive control in the label. Every fixture
# that is meant to reach the LATER rules needs them, or it fails on this rule first and
# the arm stops discriminating what it was written for. T4/T5 deliberately omit them —
# they assert the vacuity rules, which fire before this one matters.
#
# ⚠️ SPELLED OUT, not derived from the checker (unlike EXTRAS above). The T7 arms assert
# that each control's absence is reported BY NAME; reading the names from the checker
# would let a control dropped from its declaration vanish from these arms too.
CONTROL_NAMES=(
  alloc_guard_positive_control_mallocnesia
  alloc_guard_aligned_new_positive_control_mallocnesia
  alloc_guard_calloc_positive_control_mallocnesia
  alloc_guard_realloc_positive_control_mallocnesia
  alloc_guard_aligned_alloc_positive_control_mallocnesia
  alloc_guard_posix_memalign_positive_control_mallocnesia
  alloc_guard_memalign_positive_control_mallocnesia
  alloc_guard_valloc_positive_control_mallocnesia
  alloc_guard_pvalloc_positive_control_mallocnesia
)
CONTROLS=()
for _c in "${CONTROL_NAMES[@]}"; do CONTROLS+=("$_c:mallocnesia"); done

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
  "$(mk t1 "${CONTROLS[@]}" "a_mallocnesia:mallocnesia" "b_mallocnesia:alloc_guard" "${EXTRAS[@]}")"

check "T2 an UNDECLARED label member fails (the label cannot become a catch-all)" 1 \
  "nor are declared in DECLARED_EXTRAS" \
  "$(mk t2 "${CONTROLS[@]}" "a_mallocnesia:mallocnesia" "something_else:mallocnesia" "${EXTRAS[@]}")"

check "T3 a STALE declared row fails (a declaration describing nothing)" 1 \
  "no longer carries the label" \
  "$(mk t3 "${CONTROLS[@]}" "a_mallocnesia:mallocnesia" "alloc_guard_markers_no_local_def:mallocnesia")"

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
  "$(mk t6 "${CONTROLS[@]}" "a_mallocnesia:mallocnesia" "b_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

# ── T7: the POSITIVE CONTROLS' own membership. The floor counts NAMES,
# and a name is cheap — a decoy satisfies it while a real gate is deleted. A control is
# a member whose absence means nobody is checking that interception of its entry point
# works, so each is asserted by identity rather than left to arithmetic.
# One arm per control, each with every OTHER control present: no control's presence
# may cover for another's absence, since each vouches for a different entry point.
for _missing in "${CONTROL_NAMES[@]}"; do
  _others=()
  for _c in "${CONTROL_NAMES[@]}"; do [ "$_c" = "$_missing" ] || _others+=("$_c:mallocnesia"); done
  check "T7a a missing control is rejected BY NAME: $_missing" 1 \
    "MISSING from the \`mallocnesia\` label: $_missing" \
    "$(mk "t7a-$_missing" "a_mallocnesia:mallocnesia" "b_mallocnesia:mallocnesia" "${_others[@]}" "${EXTRAS[@]}")"
done

check "T7b an UNDECLARED positive control is rejected (nobody would notice losing it)" 1 \
  "UNDECLARED positive control(s) in the \`mallocnesia\` label: x_positive_control_mallocnesia" \
  "$(mk t7b "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "x_positive_control_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

# ⚠️ And the floor itself, which is what P2 showed a decoy walking past.
out="$(python3 "$CHECK" --build-dir "$(mk t7c "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${EXTRAS[@]}")" --min-gates 50 2>&1)"; rc=$?
if [ "$rc" = 1 ] && printf '%s' "$out" | grep -qF "floor is 50"; then
  echo "ok    T7c the --min-gates floor fires when the population SHRINKS"; pass=$((pass+1))
else
  echo "FAIL  T7c floor did not fire: rc=$rc"; echo "$out" | sed 's/^/      /' | head -2; fail=$((fail+1))
fi

# ── T8: the raw-preload scanner under a NON-UTF-8 LOCALE ─────────────────────
# MEASURED on windows-msvc-release: `path.read_text()` with no encoding uses the LOCALE
# codec (cp1252 on a Windows runner), and this repo's CMakeLists are full of UTF-8, so the
# scanner died with `UnicodeDecodeError: charmap codec can't decode byte 0x81`. It runs on
# Windows because it is a static scan registered unconditionally -- correctly, since a raw
# LD_PRELOAD is wrong on any platform.
#
# ⚠️ THE LEVER IS THE LOCALE, NOT PYTHONIOENCODING. A first version of these arms used
# PYTHONIOENCODING=cp1252 and BOTH mutants stayed green: that variable sets stdio only,
# while read_text() consults locale.getpreferredencoding(). LC_ALL=C + PYTHONUTF8=0 +
# PYTHONCOERCECLOCALE=0 is what actually forces an ASCII codec on Linux.
RAW="$REPO/tools/check_no_raw_mallocnesia_preload.py"
ASCII_LOCALE=(env LC_ALL=C PYTHONUTF8=0 PYTHONCOERCECLOCALE=0)

U="$TMP/utf8/tests/x"; mkdir -p "$U" "$TMP/utf8/tools"
printf '# non-ASCII comment: \xe2\x80\x94 \xe2\x9a\xa0\nadd_test(NAME t COMMAND true)\n' > "$U/CMakeLists.txt"
cp "$RAW" "$TMP/utf8/tools/c.py"
out="$(cd "$TMP/utf8" && "${ASCII_LOCALE[@]}" python3 tools/c.py 2>&1)"; rc=$?
if [ "$rc" = 0 ] && ! printf '%s' "$out" | grep -q "Traceback"; then
  echo "ok    T8a UTF-8 CMake text under an ASCII locale is READ, not a UnicodeDecodeError"
  pass=$((pass+1))
else
  echo "FAIL  T8a rc=$rc"; echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1))
fi

# ⚠️ The reporting half is asserted STRUCTURALLY, not against a chosen codec. A first
# version printed an em-dash under cp1252 expecting a crash -- but cp1252 CONTAINS the
# em-dash (0x97), so that mutant was green for a reason that had nothing to do with the
# check. The glyphs that actually break are the ones cp1252 lacks (arrow, warning sign,
# subset-of), which is what the repo's recorded fix was about. Requiring pure-ASCII output
# is codec-independent and cannot be defeated by picking the wrong test codepage.
V="$TMP/enc/tests/x"; mkdir -p "$V" "$TMP/enc/tools"
printf 'set_property(TEST t APPEND PROPERTY ENVIRONMENT "LD_PRELOAD=/x.so")\n' > "$V/CMakeLists.txt"
cp "$RAW" "$TMP/enc/tools/c.py"
out="$(cd "$TMP/enc" && "${ASCII_LOCALE[@]}" python3 tools/c.py 2>&1)"; rc=$?
if [ "$rc" = 1 ] && printf '%s' "$out" | grep -q "set LD_PRELOAD directly" \
   && ! printf '%s' "$out" | grep -q "Traceback" \
   && LC_ALL=C grep -qP '^[\x00-\x7F]*$' <<<"$out"; then
  echo "ok    T8b the finding is REPORTED, and its text is pure ASCII (any codepage)"
  pass=$((pass+1))
else
  echo "FAIL  T8b rc=$rc (want 1, finding reported, no traceback, ASCII-only output)"
  echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1))
fi

# ── T9: the raw-preload scanner's only exemption is a comment line (#497) ─────
# A raw preload inside `if(FALSE)` was once exempt, for disabled companions kept as
# restore targets. Those are deleted, and so is the exemption: T9a must be REPORTED.
# T9b is its positive control, so T9a cannot pass by the scanner reporting everything.
W="$TMP/iffalse/tests/x"; mkdir -p "$W" "$TMP/iffalse/tools"
printf 'if(FALSE)\n  set_property(TEST t APPEND PROPERTY ENVIRONMENT "LD_PRELOAD=/x.so")\nendif()\n' > "$W/CMakeLists.txt"
cp "$RAW" "$TMP/iffalse/tools/c.py"
out="$(cd "$TMP/iffalse" && python3 tools/c.py 2>&1)"; rc=$?
if [ "$rc" = 1 ] && printf '%s' "$out" | grep -q "tests/x/CMakeLists.txt:2"; then
  echo "ok    T9a a raw preload inside if(FALSE) is REPORTED (no disabled-block exemption)"
  pass=$((pass+1))
else
  echo "FAIL  T9a rc=$rc (want 1 naming tests/x/CMakeLists.txt:2)"; echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1))
fi

C="$TMP/comment/tests/x"; mkdir -p "$C" "$TMP/comment/tools"
printf '# set_property(TEST t APPEND PROPERTY ENVIRONMENT "LD_PRELOAD=/x.so")\nadd_test(NAME t COMMAND true)\n' > "$C/CMakeLists.txt"
cp "$RAW" "$TMP/comment/tools/c.py"
out="$(cd "$TMP/comment" && python3 tools/c.py 2>&1)"; rc=$?
if [ "$rc" = 0 ]; then
  echo "ok    T9b a commented-out raw preload stays exempt"; pass=$((pass+1))
else
  echo "FAIL  T9b rc=$rc (want 0)"; echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1))
fi

echo
echo "test-mallocnesia-population: $pass passed, $fail failed"
[ "$fail" = 0 ]
