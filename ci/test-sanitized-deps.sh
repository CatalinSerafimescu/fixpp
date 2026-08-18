#!/usr/bin/env bash
# Regression pin for ci/assert-sanitized-deps.sh (#252).
#
# Wired into the `ci-script-pins` job, which is the ONLY pre-merge signal these
# scripts ever get: all three tier matrices skip until both gate labels land, so
# a checker that is only exercised by the matrix is unexercised at review time.
#
# ── WHAT IT PINS, AND WHY EACH CASE EXISTS ───────────────────────────────────
#
# assert-sanitized-deps.sh is an instrument, and this repo's rule for instruments
# is that a passing run proves nothing until the instrument has been shown able
# to FAIL, for the stated reason, on a tree where the defect is present. So every
# case below is a MUTANT: a fixture built to violate exactly one property, driven
# through the real script, and required to exit non-zero AND to say why.
#
# ⚠️ THE FIXTURES ARE REAL ARCHIVES, NOT CANNED `nm` OUTPUT. A fake `nm` would
# pin this harness's idea of what an instrumented archive looks like, which is
# precisely the belief under test. `cc -c -fsanitize=<x>` + `ar` needs no
# sanitizer RUNTIME (nothing is linked) and gcc and clang emit the same
# `__asan_`/`__tsan_`/`__ubsan_` reference prefixes — verified on both, 2026-08-18.
#
# Usage:  bash ci/test-sanitized-deps.sh
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
SCRIPT="$HERE/assert-sanitized-deps.sh"
CC_BIN="${CC:-cc}"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

ok()   { PASS=$((PASS + 1)); echo "  PASS  $1"; }
bad()  { FAIL=$((FAIL + 1)); echo "  FAIL  $1"; }

# ── fixture construction ─────────────────────────────────────────────────────
#
# A fixture is a `-of` directory shaped exactly like the one `conan install`
# leaves behind: a `*-data.cmake` naming an absolute package folder, and archives
# under `<pkg>/lib/`. The data file is named `OpenSSL-…` rather than `openssl-…`
# on purpose — CMakeDeps names it after the recipe's `cmake_file_name`, and the
# script must find it by the `_PACKAGE_FOLDER_` line rather than by filename.

# make_archive <out.a> <none|address|thread|undefined>
make_archive() {
  local out="$1" san="$2" src="$TMP/src.c" obj
  obj="$TMP/obj-$$-$RANDOM.o"
  if [ ! -f "$src" ]; then
    # Deliberately arithmetic-bearing: `-fsanitize=undefined` instruments
    # CHECKABLE OPERATIONS, not every function, so an empty TU would produce an
    # archive with zero `__ubsan_` refs and turn the ubsan cases into false REDs.
    cat > "$src" <<'EOF'
int g_accum;
int bump(int n) { g_accum += n; return g_accum; }
int scale(int a, int b) { return a * b + g_accum; }
EOF
  fi
  mkdir -p "$(dirname "$out")"
  if [ "$san" = none ]; then
    "$CC_BIN" -c "$src" -o "$obj" 2>/dev/null || return 1
  else
    "$CC_BIN" -c "$src" -fsanitize="$san" -o "$obj" 2>/dev/null || return 1
  fi
  ar rcs "$out" "$obj" 2>/dev/null || return 1
  rm -f "$obj"
}

# make_fixture <name> <san-for-openssl> [extra archive basenames...]
# Echoes the build dir. Extra archives are built with the same sanitizer.
make_fixture() {
  local name="$1" san="$2"; shift 2
  local root="$TMP/$name" build="$TMP/$name/build/preset" pkg="$TMP/$name/pkg/openssl"
  mkdir -p "$build" "$pkg/lib"
  printf 'set(OpenSSL_PACKAGE_FOLDER_DEBUG "%s")\n' "$pkg" > "$build/OpenSSL-debug-x86_64-data.cmake"
  make_archive "$pkg/lib/libcrypto.a" "$san" || return 1
  make_archive "$pkg/lib/libssl.a"    "$san" || return 1
  local a
  for a in "$@"; do
    make_archive "$pkg/lib/$a" "$san" || return 1
  done
  echo "$build"
  # `root` is referenced so a future edit that stops using it is a visible change
  : "$root"
}

# run_case <label> <expect-exit 0|1> <preset> <build-dir> [required error substring]
run_case() {
  local label="$1" want="$2" preset="$3" bdir="$4" needle="${5:-}"
  local out rc
  out="$(cd "$REPO" && bash "$SCRIPT" "$preset" "$bdir" 2>&1)"
  rc=$?
  if [ "$want" -eq 0 ] && [ "$rc" -ne 0 ]; then
    bad "$label — expected exit 0, got $rc"; printf '%s\n' "$out" | sed 's/^/        /'; return
  fi
  if [ "$want" -ne 0 ] && [ "$rc" -eq 0 ]; then
    bad "$label — expected NON-ZERO exit, got 0 (the mutant was not detected)"
    printf '%s\n' "$out" | sed 's/^/        /'; return
  fi
  # ⚠️ An exit code alone is not enough. A mutant that reddens the script for an
  # UNRELATED reason (a typo'd path, a missing tool) reads as a successful
  # detection and hides that the property under test is unpinned. Every RED case
  # therefore also asserts the script said WHY.
  if [ -n "$needle" ] && ! printf '%s' "$out" | grep -qF "$needle"; then
    bad "$label — exit code was right ($rc) but the message never mentioned '$needle', so this case does not pin the property it names"
    printf '%s\n' "$out" | sed 's/^/        /'; return
  fi
  ok "$label"
}

echo "== fixture toolchain =="
if ! make_archive "$TMP/probe/libprobe.a" thread; then
  echo "::error::test-sanitized-deps.sh: cannot build a sanitizer-instrumented fixture archive with '$CC_BIN'." >&2
  echo "::error::Refusing to skip — a harness that silently degrades to 'no cases ran' is the false green this file exists to prevent." >&2
  exit 1
fi
if [ "$(nm "$TMP/probe/libprobe.a" | grep -c '__tsan_' || true)" -eq 0 ]; then
  echo "::error::test-sanitized-deps.sh: the '$CC_BIN -fsanitize=thread' fixture carries ZERO __tsan_ references, so every positive case below would be a false RED and every negative case vacuously green." >&2
  exit 1
fi
echo "  OK  $CC_BIN builds instrumented fixtures that nm can read"

echo "== T1 — the two preset→sanitizer tables agree where both know the preset =="
# ⚠️ assert-sanitized-deps.sh deliberately carries its OWN case (it must cover the
# four linux-clang-libc++* presets, which derive-python-sanitizer.sh does not
# know). Two tables can drift; this is the assertion that stops it. The agreement
# is checked by OBSERVED BEHAVIOUR — a fixture instrumented with the sanitizer
# derive-python-sanitizer.sh names must satisfy assert-sanitized-deps.sh.
t1_fail=0
for p in linux-clang-debug linux-clang-release linux-gcc-release linux-clang-asan linux-clang-ubsan linux-clang-tsan; do
  san="$(bash "$HERE/derive-python-sanitizer.sh" "$p" 2>/dev/null | sed -n 's/^sanitizer=//p')"
  case "$san" in
    none)  cc_san=none ;;
    asan)  cc_san=address ;;
    tsan)  cc_san=thread ;;
    ubsan) cc_san=undefined ;;
    *)     bad "T1 $p — derive-python-sanitizer.sh emitted no sanitizer"; t1_fail=1; continue ;;
  esac
  b="$(make_fixture "agree-$p" "$cc_san")" || { bad "T1 $p — fixture build failed"; t1_fail=1; continue; }
  if (cd "$REPO" && bash "$SCRIPT" "$p" "$b" >/dev/null 2>&1); then
    ok "T1 $p — both tables say '$san'"
  else
    bad "T1 $p — derive-python-sanitizer.sh says '$san' but assert-sanitized-deps.sh rejects a closure built with exactly that. THE TWO TABLES HAVE DRIFTED."
    t1_fail=1
  fi
done
: "$t1_fail"

echo "== T2 — every preset in the tier1 + tier3 matrices has an arm =="
# A preset the matrix names but the case does not is the #251 class: the step
# runs, hits the fatal `*)` arm, and the leg goes red for a reason nobody
# anticipated. Derived from the workflow files, never from a hand list.
presets="$(python3 - "$REPO" <<'PY'
import sys, yaml, pathlib
repo = pathlib.Path(sys.argv[1])
out = []
for wf, job in (("tier1.yml", "linux"), ("tier3-libcxx.yml", "libcxx")):
    d = yaml.safe_load((repo / ".github/workflows" / wf).read_text())
    out += d["jobs"][job]["strategy"]["matrix"]["preset"]
print(" ".join(out))
PY
)"
if [ -z "$presets" ]; then
  bad "T2 — derived an EMPTY preset list from the workflows; the census is broken, not satisfied"
else
  echo "  (derived: $presets)"
  for p in $presets; do
    # An empty build dir: the script must get PAST the case and fail on the
    # locator, not on the unknown-preset arm.
    mkdir -p "$TMP/empty"
    out="$(cd "$REPO" && bash "$SCRIPT" "$p" "$TMP/empty" 2>&1)"
    if printf '%s' "$out" | grep -qF "unknown preset"; then
      bad "T2 $p — named by a CI matrix but has no arm in assert-sanitized-deps.sh"
    else
      ok "T2 $p — has an arm"
    fi
  done
fi

echo "== T3 — an unknown preset is FATAL, never a silent 'none' =="
b="$(make_fixture unknown none)"
run_case "T3 unknown preset rejected" 1 linux-clang-mystery "$b" "unknown preset"

echo "== T4 — the POSITIVE half, proven RED on an uninstrumented closure =="
# This is the #252 defect itself: real archives, real symbols, zero sanitizer
# references. It is the case that must be RED on the tree BEFORE the profile fix.
b="$(make_fixture uninstrumented none)"
run_case "T4a tsan lane vs uninstrumented deps"  1 linux-clang-tsan  "$b" "ZERO __tsan_ references"
run_case "T4b asan lane vs uninstrumented deps"  1 linux-clang-asan  "$b" "ZERO __asan_ references"
run_case "T4c ubsan lane vs uninstrumented deps" 1 linux-clang-ubsan "$b" "ZERO __ubsan_ references"
run_case "T4d libc++-tsan lane vs uninstrumented deps" 1 linux-clang-libc++-tsan "$b" "ZERO __tsan_ references"

echo "== T5 — the POSITIVE half, GREEN once the closure really is instrumented =="
for pair in "linux-clang-tsan thread" "linux-clang-asan address" "linux-clang-ubsan undefined" \
            "linux-clang-libc++-ubsan undefined"; do
  set -- $pair
  b="$(make_fixture "inst-$1" "$2")" || { bad "T5 $1 — fixture build failed"; continue; }
  run_case "T5 $1 accepts a closure built with -fsanitize=$2" 0 "$1" "$b"
done

echo "== T6 — the NEGATIVE half, both directions =="
b="$(make_fixture neg-clean none)"
run_case "T6a linux-clang-debug accepts an uninstrumented closure" 0 linux-clang-debug "$b"
run_case "T6b linux-clang-libc++ accepts an uninstrumented closure" 0 linux-clang-libc++ "$b"
b="$(make_fixture neg-dirty thread)"
run_case "T6c linux-clang-debug REJECTS an instrumented closure" 1 linux-clang-debug "$b" "is NOT a sanitizer lane"
# ⚠️ T6c is the half that makes the checker two-sided. Without it the negative
# assertion could be satisfied by a locator that finds nothing — and #252's own
# scope note calls a one-sided check the shape this repo has been burned by.

echo "== T7 — crosswise: the wrong sanitizer on a lane is a failure, not a pass =="
b="$(make_fixture cross address)"
run_case "T7 tsan lane REJECTS an ASan-instrumented closure" 1 linux-clang-tsan "$b" "a DIFFERENT sanitizer"

echo "== T8 — a broken LOCATOR fails loud; it never reads as a clean closure =="
mkdir -p "$TMP/no-data/build/preset"
run_case "T8a no *-data.cmake at all" 1 linux-clang-debug "$TMP/no-data/build/preset" "found NO Conan package folders"
run_case "T8b build dir does not exist" 1 linux-clang-debug "$TMP/does-not-exist" "does not exist"
# A package folder that resolves but holds no openssl: the required-archive
# branch, distinct from T8a's zero-folders branch.
mkdir -p "$TMP/no-ssl/build/preset" "$TMP/no-ssl/pkg/lib"
printf 'set(OpenSSL_PACKAGE_FOLDER_DEBUG "%s")\n' "$TMP/no-ssl/pkg" > "$TMP/no-ssl/build/preset/OpenSSL-debug-x86_64-data.cmake"
run_case "T8c package folder without libcrypto.a" 1 linux-clang-debug "$TMP/no-ssl/build/preset" "not found in any of the"

echo "== T9 — liveness: a zero-symbol archive is a BROKEN READ, not an absence =="
mkdir -p "$TMP/empty-ar/build/preset" "$TMP/empty-ar/pkg/lib"
printf 'set(OpenSSL_PACKAGE_FOLDER_DEBUG "%s")\n' "$TMP/empty-ar/pkg" > "$TMP/empty-ar/build/preset/OpenSSL-debug-x86_64-data.cmake"
ar rcs "$TMP/empty-ar/pkg/lib/libcrypto.a" 2>/dev/null
make_archive "$TMP/empty-ar/pkg/lib/libssl.a" none
run_case "T9 empty archive rejected on the NEGATIVE lane" 1 linux-clang-debug "$TMP/empty-ar/build/preset" "ZERO total symbols"
# ⚠️ Asserted on the NEGATIVE lane deliberately. On a sanitizer lane an empty
# archive would fail the positive assertion anyway and the case would prove
# nothing about liveness. `linux-clang-debug` expects zero references, so without
# the liveness check an unreadable archive would sail through as "clean" — that
# is the exact false green #252's non-circularity column exists to rule out.

echo
echo "== summary: $PASS passed, $FAIL failed =="
if [ "$FAIL" -ne 0 ]; then
  echo "::error::test-sanitized-deps.sh: $FAIL case(s) failed." >&2
  exit 1
fi
if [ "$PASS" -eq 0 ]; then
  echo "::error::test-sanitized-deps.sh: ZERO cases ran. A harness that asserts nothing exits 0 — failing loud instead." >&2
  exit 1
fi
exit 0
