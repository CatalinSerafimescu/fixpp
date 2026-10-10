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
# mk <dir> <spec>...  — build a synthetic ctest tree. A spec is `<name>:<labels>`, or
# `<name>:<labels>:<args>` to give the test's command arguments after /bin/true.
mk() {
  local d="$TMP/$1"; shift
  mkdir -p "$d"; : > "$d/CTestTestfile.cmake"
  local name labels args
  for spec in "$@"; do
    name="${spec%%:*}"; labels="${spec#*:}"; args=""
    case "$labels" in *:*) args="${labels#*:}"; labels="${labels%%:*}" ;; esac
    printf 'add_test(%s "/bin/true" %s)\n' "$name" "$args" >> "$d/CTestTestfile.cmake"
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
# The entry point each control's command must name, spelled out for the same reason
# (empty: none is required).
declare -A CONTROL_ENTRY=(
  [alloc_guard_positive_control_mallocnesia]=malloc
  [alloc_guard_aligned_new_positive_control_mallocnesia]=aligned_alloc,posix_memalign
  [alloc_guard_calloc_positive_control_mallocnesia]=calloc
  [alloc_guard_realloc_positive_control_mallocnesia]=realloc
  [alloc_guard_aligned_alloc_positive_control_mallocnesia]=aligned_alloc
  [alloc_guard_posix_memalign_positive_control_mallocnesia]=posix_memalign
  [alloc_guard_memalign_positive_control_mallocnesia]=memalign
  [alloc_guard_valloc_positive_control_mallocnesia]=valloc
  [alloc_guard_pvalloc_positive_control_mallocnesia]=pvalloc
)
ctl() {  # ctl <control> [<entry>] — the control's spec, naming <entry> (default: its own)
  local e="${2-${CONTROL_ENTRY[$1]}}"
  echo "$1:mallocnesia:--expect-violation${e:+ --expect-entry $e}"
}
CONTROLS=()
for _c in "${CONTROL_NAMES[@]}"; do CONTROLS+=("$(ctl "$_c")"); done

# fixpp#544 (B35)'s gates, which the checker requires BY NAME (its REQUIRED_GATES). Every
# fixture meant to reach the later rules carries them, for the reason given for the
# controls above. SPELLED OUT, not derived from the checker, for the same reason as
# CONTROL_NAMES: the T10a arms assert each one's absence is reported by name.
REQUIRED_NAMES=(
  alloc_guard_544_arm_a_tracked_executor_mallocnesia
  alloc_guard_544_arm_e_slot_held_mallocnesia
  alloc_guard_544_arm_s_run_one_for_mallocnesia
  alloc_guard_544_ch_arm_mallocnesia
  alloc_guard_544_ch_no_edit_twin_mallocnesia
  alloc_guard_544_ch_production_twin_mallocnesia
  alloc_guard_544_cl_arm_mallocnesia
  alloc_guard_544_cl_production_twin_mallocnesia
  alloc_guard_544_clp_no_edit_twin_mallocnesia
  alloc_guard_544_cp_arm_mallocnesia
  alloc_guard_544_veto_then_zero_mallocnesia
  alloc_guard_544_wa_active_heartbeat_mallocnesia
  alloc_guard_544_wa_bl_arm_mallocnesia
  alloc_guard_544_wa_bl_twin_mallocnesia
  alloc_guard_544_wa_bs_arm_mallocnesia
  alloc_guard_544_wa_bs_twin_mallocnesia
  alloc_guard_544_wb_active_app_message_mallocnesia
  alloc_guard_544_wd_bl_arm_mallocnesia
  alloc_guard_544_wd_bl_twin_mallocnesia
  alloc_guard_544_wd_bs_arm_mallocnesia
  alloc_guard_544_wd_bs_twin_mallocnesia
  alloc_guard_544_wdr_bracket_arm_mallocnesia
  alloc_guard_544_wdr_bracket_twin_mallocnesia
  alloc_guard_544_wdr_store_live_read_mallocnesia
  alloc_guard_544_wdw_real_chain_arm_mallocnesia
  alloc_guard_544_wdw_real_chain_twin_mallocnesia
  alloc_guard_544_wdw_stand_in_arm_mallocnesia
  alloc_guard_544_wdw_stand_in_twin_mallocnesia
  alloc_guard_544_wdw_stand_in_window_mallocnesia
  alloc_guard_544_we_sends_interleaved_mallocnesia
  perf_session_recovery_heartbeat_mallocnesia
  perf_store_alloc_guard_wd_mallocnesia
  session_refresh_on_logon_w8_mallocnesia
)
REQUIRED=()
for _r in "${REQUIRED_NAMES[@]}"; do REQUIRED+=("$_r:mallocnesia"); done

# T0 — THE REAL TREES, one per Linux Release preset tier1.yml runs the gates on. Without
# this the suite proves only that the checker can say no. Each is skipped (not failed) when
# its configured build is absent, e.g. on a buildless lane.
for _preset in linux-clang-release linux-gcc-release; do
  if [ -f "$REPO/build/$_preset/CTestTestfile.cmake" ]; then
    check "T0 the real $_preset population reconciles" 0 \
      "named gate(s), all labelled" "$REPO/build/$_preset"
  else
    echo "skip  T0 (no configured build/$_preset — buildless lane)"
  fi
done

check "T1 a gate missing the label is REPORTED, not tolerated" 1 \
  "do NOT carry the \`mallocnesia\` label" \
  "$(mk t1 "${CONTROLS[@]}" "${REQUIRED[@]}" "a_mallocnesia:mallocnesia" "b_mallocnesia:alloc_guard" "${EXTRAS[@]}")"

check "T2 an UNDECLARED label member fails (the label cannot become a catch-all)" 1 \
  "nor are declared in DECLARED_EXTRAS" \
  "$(mk t2 "${CONTROLS[@]}" "${REQUIRED[@]}" "a_mallocnesia:mallocnesia" "something_else:mallocnesia" "${EXTRAS[@]}")"

check "T3 a STALE declared row fails (a declaration describing nothing)" 1 \
  "no longer carries the label" \
  "$(mk t3 "${CONTROLS[@]}" "${REQUIRED[@]}" "a_mallocnesia:mallocnesia" "alloc_guard_markers_no_local_def:mallocnesia")"

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
  "$(mk t6 "${CONTROLS[@]}" "${REQUIRED[@]}" "a_mallocnesia:mallocnesia" "b_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

# ── T7: the POSITIVE CONTROLS' own membership. The floor counts NAMES,
# and a name is cheap — a decoy satisfies it while a real gate is deleted. A control is
# a member whose absence means nobody is checking that interception of its entry point
# works, so each is asserted by identity rather than left to arithmetic.
# One arm per control, each with every OTHER control present: no control's presence
# may cover for another's absence, since each vouches for a different entry point.
for _missing in "${CONTROL_NAMES[@]}"; do
  _others=()
  for _c in "${CONTROL_NAMES[@]}"; do [ "$_c" = "$_missing" ] || _others+=("$(ctl "$_c")"); done
  check "T7a a missing control is rejected BY NAME: $_missing" 1 \
    "MISSING from the \`mallocnesia\` label: $_missing" \
    "$(mk "t7a-$_missing" "a_mallocnesia:mallocnesia" "b_mallocnesia:mallocnesia" "${_others[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")"
done

check "T7b an UNDECLARED positive control is rejected (nobody would notice losing it)" 1 \
  "UNDECLARED positive control(s) in the \`mallocnesia\` label: x_positive_control_mallocnesia" \
  "$(mk t7b "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "x_positive_control_mallocnesia:mallocnesia" "${EXTRAS[@]}")"

# ── T7d/T7e: each control names its OWN hook. --expect-violation accepts any violation,
# so without --expect-entry a control passes on an allocation that reached another hook.
# One T7d arm per control that requires an entry, each with every other control intact.
for _bare in "${CONTROL_NAMES[@]}"; do
  [ -n "${CONTROL_ENTRY[$_bare]}" ] || continue
  _set=()
  for _c in "${CONTROL_NAMES[@]}"; do
    if [ "$_c" = "$_bare" ]; then _set+=("$(ctl "$_c" "")"); else _set+=("$(ctl "$_c")"); fi
  done
  check "T7d a control that names no entry point is rejected BY NAME: $_bare" 1 \
    "positive control $_bare does not require its own entry point" \
    "$(mk "t7d-$_bare" "a_mallocnesia:mallocnesia" "${_set[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")"
done

_set=()
for _c in "${CONTROL_NAMES[@]}"; do
  if [ "$_c" = alloc_guard_memalign_positive_control_mallocnesia ]; then
    _set+=("$(ctl "$_c" calloc)")
  else
    _set+=("$(ctl "$_c")")
  fi
done
check "T7e a control that names ANOTHER control's entry point is rejected" 1 \
  "positive control alloc_guard_memalign_positive_control_mallocnesia does not require its own entry point: its command names --expect-entry ['calloc']" \
  "$(mk t7e "a_mallocnesia:mallocnesia" "${_set[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")"

# The entry must be the VALUE of --expect-entry, not merely a token in the command.
_set=()
for _c in "${CONTROL_NAMES[@]}"; do
  if [ "$_c" = alloc_guard_memalign_positive_control_mallocnesia ]; then
    _set+=("$_c:mallocnesia:--expect-violation memalign --expect-entry calloc")
  else
    _set+=("$(ctl "$_c")")
  fi
done
check "T7f an entry named elsewhere in the command does not count" 1 \
  "positive control alloc_guard_memalign_positive_control_mallocnesia does not require its own entry point: its command names --expect-entry ['calloc']" \
  "$(mk t7f "a_mallocnesia:mallocnesia" "${_set[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")"

check "T7g duplicate gate or control names are rejected before a command stands in" 1 \
  "duplicate test name(s)" \
  "$(mk t7g "$(ctl alloc_guard_memalign_positive_control_mallocnesia "")" \
            "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")"

# ── T7h: no label member may carry a property that alters its verdict. Each arm is the
# full happy set plus ONE property on ONE member; a broken control exits 1, so each of
# these turns `ctest -L mallocnesia` green over it. One arm per property on a control,
# and one on a plain gate and one on a declared extra, so the rule is not scoped to
# controls only.
with_prop() {  # with_prop <dir> <test> <property> <value> — append the property, echo dir
  printf 'set_tests_properties(%s PROPERTIES %s %s)\n' "$2" "$3" "$4" >> "$1/CTestTestfile.cmake"
  echo "$1"
}
_ctl=alloc_guard_memalign_positive_control_mallocnesia
for _pv in "DISABLED TRUE" "WILL_FAIL TRUE" "SKIP_RETURN_CODE 1" \
           "SKIP_REGULAR_EXPRESSION check_alloc" "PASS_REGULAR_EXPRESSION check_alloc"; do
  _p="${_pv%% *}"
  check "T7h a control carrying $_p is rejected BY NAME" 1 \
    "$_ctl carries $_p." \
    "$(with_prop "$(mk "t7h-$_p" "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")" \
                 "$_ctl" $_pv)"
done
check "T7h a plain gate carrying SKIP_RETURN_CODE is rejected BY NAME" 1 \
  "a_mallocnesia carries SKIP_RETURN_CODE." \
  "$(with_prop "$(mk t7h-gate "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")" \
               a_mallocnesia SKIP_RETURN_CODE 1)"
check "T7h a declared extra carrying DISABLED is rejected BY NAME" 1 \
  "${EXTRAS[0]%%:*} carries DISABLED." \
  "$(with_prop "$(mk t7h-extra "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")" \
               "${EXTRAS[0]%%:*}" DISABLED TRUE)"
# Its happy half: a property that only ADDS failures is not rejected.
check "T7h a member carrying TIMEOUT only is accepted" 0 \
  "named gate(s), all labelled" \
  "$(with_prop "$(mk t7h-timeout "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")" \
               "$_ctl" TIMEOUT 30)"

# ⚠️ And the floor itself, which is what P2 showed a decoy walking past.
out="$(python3 "$CHECK" --build-dir "$(mk t7c "a_mallocnesia:mallocnesia" "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")" --min-gates 50 2>&1)"; rc=$?
if [ "$rc" = 1 ] && printf '%s' "$out" | grep -qF "floor is 50"; then
  echo "ok    T7c the --min-gates floor fires when the population SHRINKS"; pass=$((pass+1))
else
  echo "FAIL  T7c floor did not fire: rc=$rc"; echo "$out" | sed 's/^/      /' | head -2; fail=$((fail+1))
fi

# ── T10: B35's gates, by identity. The floor counts names, so with --min-gates equal to the
# population a deleted gate plus a padding gate passes it. One arm per required gate, each
# with every other required gate present and the floor met exactly, so only the identity
# rule can fire: the arm also requires that the floor did NOT.
_floor=$(( ${#CONTROL_NAMES[@]} + ${#REQUIRED_NAMES[@]} ))
for _missing in "${REQUIRED_NAMES[@]}"; do
  _others=()
  for _r in "${REQUIRED_NAMES[@]}"; do [ "$_r" = "$_missing" ] || _others+=("$_r:mallocnesia"); done
  out="$(python3 "$CHECK" --min-gates "$_floor" --build-dir "$(mk "t10a-$_missing" \
          "${CONTROLS[@]}" "${_others[@]}" "padding_mallocnesia:mallocnesia" "${EXTRAS[@]}")" 2>&1)"; rc=$?
  if [ "$rc" = 1 ] && printf '%s' "$out" | grep -qF "MISSING by name: $_missing" \
     && ! printf '%s' "$out" | grep -qF "floor is"; then
    echo "ok    T10a a missing B35 gate is rejected BY NAME at an exact floor: $_missing"; pass=$((pass+1))
  else
    echo "FAIL  T10a $_missing: rc=$rc (want 1, named, floor not fired)"
    echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1))
  fi
done
# The label half of the same rule: every B35 gate registered, one without the label.
_set=()
for _r in "${REQUIRED_NAMES[@]}"; do
  if [ "$_r" = "${REQUIRED_NAMES[0]}" ]; then _set+=("$_r:alloc_guard"); else _set+=("$_r:mallocnesia"); fi
done
check "T10c a B35 gate without the label is rejected BY NAME" 1 \
  "MISSING from the \`mallocnesia\` label: ${REQUIRED_NAMES[0]}." \
  "$(mk t10c "${CONTROLS[@]}" "${_set[@]}" "${EXTRAS[@]}")"
out="$(python3 "$CHECK" --min-gates "$_floor" --build-dir "$(mk t10b \
        "${CONTROLS[@]}" "${REQUIRED[@]}" "${EXTRAS[@]}")" 2>&1)"; rc=$?
if [ "$rc" = 0 ] && printf '%s' "$out" | grep -qF "named gate(s), all labelled"; then
  echo "ok    T10b every B35 gate present at the same floor passes"; pass=$((pass+1))
else
  echo "FAIL  T10b rc=$rc (want 0)"; echo "$out" | sed 's/^/      /' | head -3; fail=$((fail+1))
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
