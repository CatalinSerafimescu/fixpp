#!/usr/bin/env bash
# ci/test-check-alloc.sh — pin tools/check_alloc.py's refusals (fixpp#448).
#
# check_alloc.py carries the heart of #448: the fail-CLOSED refusal, the interception
# WITNESS requirement, and the positive control's inverted verdict. Every one of those is
# a refusal, and a refusal nobody has seen fire is not a refusal — this repo's single
# most recurring defect is an instrument that reports clean because it could not report
# anything else. So each gets a mutant here, asserted to produce its OWN message rather
# than merely a non-zero exit, which any typo also produces.
#
# Self-contained: builds its own tiny interceptor and its own subject binaries with cc,
# so it needs no configured CMake tree and runs on a buildless lane.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CHECK="$REPO/tools/check_alloc.py"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CC="${CC:-cc}"
command -v "$CC" >/dev/null || { echo "SKIP: no C compiler"; exit 0; }

# The real interceptor, built from the shipped source — NOT a stand-in. A fake would
# make every arm below a test of the fake.
"$CC" -O1 -fPIC -shared -ldl -o "$TMP/libmn.so" "$REPO/tools/mallocnesia/mallocnesia.c" \
  2>"$TMP/cc.log" || { echo "FAIL: could not build the interceptor"; cat "$TMP/cc.log"; exit 1; }

# Subject binaries. The markers are weak-undefined: the preload defines them, and
# without it they are null and the call sites no-op.
cat > "$TMP/clean.c" <<'EOF'
__attribute__((weak)) void alloc_guard_start(void);
__attribute__((weak)) void alloc_guard_end(void);
int main(void){ if(alloc_guard_start) alloc_guard_start();
                if(alloc_guard_end) alloc_guard_end(); return 0; }
EOF
cat > "$TMP/dirty.c" <<'EOF'
#include <stdlib.h>
__attribute__((weak)) void alloc_guard_start(void);
__attribute__((weak)) void alloc_guard_end(void);
int main(void){ if(alloc_guard_start) alloc_guard_start();
                void* volatile p = malloc(16); free(p);
                if(alloc_guard_end) alloc_guard_end(); return 0; }
EOF
"$CC" -O1 -o "$TMP/clean" "$TMP/clean.c"
"$CC" -O1 -o "$TMP/dirty" "$TMP/dirty.c"
printf 'not an elf\n' > "$TMP/bogus.so"

# ⚠️ Codex r1 P1-B's mutant, as a permanent arm. The guard markers are WEAK UNDEFINED in
# the test binaries, so ANY strong definition in the link closure beats the preload: the
# constructor still runs and still writes "loaded" while g_active is never set and the
# planted allocation sails through. The repo's marker scanner only looks at tests/ and
# bench/, so a definition under src/ is invisible to it. This is the shape that made
# "the witness file exists" an insufficient proof of interception.
cat > "$TMP/shadow.c" <<'EOF'
void alloc_guard_start(void) {}
void alloc_guard_end(void) {}
EOF
"$CC" -O1 -c "$TMP/shadow.c" -o "$TMP/shadow.o"
"$CC" -O1 -o "$TMP/shadowed" "$TMP/dirty.c" "$TMP/shadow.o"

pass=0; fail=0
check() {  # check <name> <want-rc> <want-substring> -- <cmd...>
  local name="$1" want_rc="$2" want="$3"; shift 4
  local out rc
  out="$("$@" 2>&1)"; rc=$?
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

# ── the happy paths, first: an always-RED wrapper would pass every refusal arm ──
check "T1 a clean window under a real interceptor PASSES" 0 \
  "interception confirmed" -- \
  python3 "$CHECK" --binary "$TMP/clean" --mallocnesia "$TMP/libmn.so"

check "T2 an allocating window is CAUGHT (the gate can go RED at all)" 1 \
  "mallocnesia] FAIL" -- \
  python3 "$CHECK" --binary "$TMP/dirty" --mallocnesia "$TMP/libmn.so"

# ── fail CLOSED: the behaviour #448 replaced printed PASS here ─────────────────
check "T3 a MISSING interceptor is fatal, not a warning-and-PASS" 2 \
  "Refusing to run" -- \
  python3 "$CHECK" --binary "$TMP/clean" --mallocnesia "$TMP/nope.so"

# ── the witness: an exit code cannot see an IGNORED preload ────────────────────
# ld.so prints "cannot be preloaded ... ignored" and runs the binary UNINSTRUMENTED,
# which exits 0. This is the arm that distinguishes that from a real pass.
check "T4 a PRESENT but unloadable interceptor is refused, not read as clean" 2 \
  "left no witness" -- \
  python3 "$CHECK" --binary "$TMP/clean" --mallocnesia "$TMP/bogus.so"

# ── the positive control's three facts, one arm per way to break it ────────────
check "T5 the control PASSES when the plant is caught" 0 \
  "PASS (positive control)" -- \
  python3 "$CHECK" --binary "$TMP/dirty" --mallocnesia "$TMP/libmn.so" --expect-violation

# ⚠️ THE ARM THAT JUSTIFIES NOT USING ctest WILL_FAIL. WILL_FAIL accepts ANY nonzero
# exit, and the wrapper returns 2 here — so under WILL_FAIL this case would PASS, on
# precisely the lane where interception is broken.
check "T6 the control FAILS when the interceptor is missing (WILL_FAIL would pass)" 2 \
  "Refusing to run" -- \
  python3 "$CHECK" --binary "$TMP/dirty" --mallocnesia "$TMP/nope.so" --expect-violation

check "T7 the control FAILS when the plant is NOT caught (a control measuring nothing)" 1 \
  "the gate is not measuring what it claims" -- \
  python3 "$CHECK" --binary "$TMP/clean" --mallocnesia "$TMP/libmn.so" --expect-violation

# ── the explicit local escape hatch still works, and still says it proves nothing ──
check "T8 --allow-missing runs uninstrumented but SAYS SO" 0 \
  "proves nothing" -- \
  python3 "$CHECK" --binary "$TMP/clean" --mallocnesia "$TMP/nope.so" --allow-missing

# ── T9: LOADED is not INTERPOSED ──────────────────────────────────────────────
# The single most important arm here. The subject allocates inside its window and exits
# ZERO, because its own strong markers answered instead of ours; the interceptor loaded
# perfectly well. An exit-code check passes it. A witness that only records "loaded"
# passes it. Only the start/end notes — which nothing but OUR marker definitions write —
# can tell that this binary's window was never measured.
check "T9 a binary whose OWN markers beat the preload is REFUSED, not read as clean" 2 \
  "guard markers did NOT run" -- \
  python3 "$CHECK" --binary "$TMP/shadowed" --mallocnesia "$TMP/libmn.so"

# ⚠️ Its companion: the same refusal must NOT fire on an honest binary, or every gate
# breaks and the arm above is passing for the wrong reason.
check "T9b ... and an honest binary still satisfies the start/end requirement" 0 \
  "markers ran" -- \
  python3 "$CHECK" --binary "$TMP/clean" --mallocnesia "$TMP/libmn.so"

# The positive control is equally fooled by a shadowed binary: it would see no violation
# and must say so, rather than reporting the control merely "failed".
check "T9c the positive control on a shadowed binary names the MARKERS, not the plant" 2 \
  "guard markers did NOT run" -- \
  python3 "$CHECK" --binary "$TMP/shadowed" --mallocnesia "$TMP/libmn.so" --expect-violation

echo
echo "test-check-alloc: $pass passed, $fail failed"
[ "$fail" = 0 ]
