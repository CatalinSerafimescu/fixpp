#!/usr/bin/env bash
# CI-side: assert the DEPENDENCY CLOSURE really carries the sanitizer the preset
# claims — and, on a non-sanitizer preset, that it carries none.
#
# #252. `conan/profiles/linux-clang-{asan,tsan,ubsan}` and their three
# `linux-clang-libc++-*` twins set only `CXXFLAGS` / `tools.build:cxxflags`.
# `tools.build:cflags` is LISTED in `tools.info.package_id:confs` and never SET,
# so every package gets a distinct package id under a sanitizer profile — the id
# moves, the object code does not. Measured under `linux-clang-tsan` before the
# fix: `libcrypto.a` 50,830 symbols and ZERO `__tsan_*`; same for libz, libssl,
# libcurl, libcivetweb. The C++ archives (`libcivetweb-cpp.a`, `libpugixml.a`)
# were instrumented, which is why nobody looked.
#
# Usage (from the library submodule dir, as the workflow's working directory,
# AFTER `conan install -of build/<preset>`):
#   ci/assert-sanitized-deps.sh linux-clang-tsan
#   ci/assert-sanitized-deps.sh linux-clang-debug build/linux-clang-debug
#
# Exit 0 when every assertion holds. On any failure: `::error::` on stderr, the
# full inventory of what WAS found on stdout, exit 1.
#
# ── WHY IT IS TWO-SIDED, AND WHY THAT IS THE WHOLE POINT ─────────────────────
#
# A checker that only asserts `> 0` on the fixed tree is the shape this repo has
# been burned by repeatedly: a verification grep proven only where it passes is
# not an instrument, because a zero can mean "clean" or "I looked in the wrong
# place" and nothing tells them apart. So:
#
#   positive  on a sanitizer preset, the asserted archives must carry a NON-ZERO
#             count of the expected sanitizer's symbol references;
#   negative  on a NON-sanitizer preset (`linux-clang-debug`, `linux-clang-release`,
#             `linux-clang-libc++`, `linux-gcc-release`) every located archive must
#             carry ZERO references to ALL THREE sanitizers;
#   crosswise on a sanitizer preset the two OTHER sanitizers must read zero, so a
#             profile that leaks the wrong runtime into a lane is caught too;
#   liveness  every located archive must report a NON-ZERO total symbol count.
#             That is the non-circularity check from #252's own measurement table:
#             it is what makes each `0` an ABSENCE rather than a broken read.
#
# ⚠️ THE ORDER OF PROOF MATTERS MORE THAN THE ASSERTIONS. This script is landed
# and run on a tree where the profiles are still UNFIXED, so its positive half is
# observed RED before the fix exists. A checker whose first ever run is on the
# fixed tree has never been shown able to fail.
#
# ── WHY OPENSSL CARRIES THE POSITIVE ASSERTION ───────────────────────────────
#
# `libcrypto.a` / `libssl.a` are the ones guaranteed on EVERY lane: openssl is a
# direct `requires` in conanfile.py. `libcurl.a` and `libcivetweb.a` arrive
# transitively through opentelemetry-cpp, which the three Tier 3 libc++ profiles
# switch OFF (`with_otel=False`, see their headers), so they are absent there.
# Asserting on an archive that is absent by design on half the lanes would need a
# per-lane exception list — a second census, and the wrong one to maintain.
#
# ⚠️ `libz.a` IS NOT ONE OF THE OTEL-ONLY ONES, and the first version of this
# comment said it was. Measured on the Tier 3 libc++-ubsan leg of run 32069887717
# — `with_otel=False` — zlib is present anyway (357 symbols), because it reaches
# the graph through openssl. It stays in the reported set for the UBSan reason
# below, not for an availability reason.
#
# ⚠️ AND BECAUSE UBSan's FOOTPRINT IS OPERATION-DEPENDENT, NOT PER-TU. `-fsanitize=
# thread` and `-fsanitize=address` instrument every function, so any non-trivial
# archive references them; `-fsanitize=undefined` emits `__ubsan_handle_*` only
# where a CHECKABLE operation exists. Measured locally with clang-22 on a
# two-line C file: thread=5 refs, address=5, undefined=1, plain=0. A small,
# arithmetic-free archive can therefore read 0 under UBSan while being correctly
# instrumented — which would make it a false-RED here. OpenSSL is bignum
# arithmetic and pointer casts from end to end, so it has no such failure mode,
# and it is the archive #252 measured. Every OTHER located archive is REPORTED
# and cross-checked, never required to be non-zero.
set -euo pipefail

PRESET="${1:?usage: assert-sanitized-deps.sh <preset> [build-dir]}"
BUILD_DIR="${2:-build/${PRESET}}"
NM="${NM:-nm}"

# ── preset → expected sanitizer ──────────────────────────────────────────────
#
# ⚠️ AN UNKNOWN PRESET IS FATAL, NEVER A SILENT `none`. Defaulting would make a
# future sanitizer leg assert the NEGATIVE side and pass while uninstrumented —
# reporting green for exactly the defect this script exists to catch. Same
# property, and same reason, as ci/derive-python-sanitizer.sh's fatal `*)` arm.
#
# ⚠️ THIS IS A SECOND TABLE AND IT IS DELIBERATE. Reusing
# ci/derive-python-sanitizer.sh would be tighter if it covered the same
# population, but it does not: it is keyed to the Tier 1 matrix and knows none of
# the four `linux-clang-libc++*` presets, which are half of this script's job.
# The drift risk that creates is closed by a test, not by prose —
# ci/test-sanitized-deps.sh asserts the two tables AGREE on every preset both
# know. Do not "unify" them without moving that assertion with you.
case "$PRESET" in
  linux-clang-asan|linux-clang-libc++-asan)   EXPECT=asan ;;
  linux-clang-tsan|linux-clang-libc++-tsan)   EXPECT=tsan ;;
  linux-clang-ubsan|linux-clang-libc++-ubsan) EXPECT=ubsan ;;
  linux-clang-debug|linux-clang-release|linux-clang-libc++|linux-gcc-release)
    EXPECT=none ;;
  *)
    echo "::error::assert-sanitized-deps.sh: unknown preset '$PRESET'." >&2
    echo "::error::Refusing to default to 'none' — that would assert the NEGATIVE side on a lane that may be a sanitizer lane, i.e. report green for an UNINSTRUMENTED closure (#252 class)." >&2
    echo "::error::Add the preset to the case in ci/assert-sanitized-deps.sh; ci/test-sanitized-deps.sh drives it over the tier1 + tier3 matrices and will fail until you do." >&2
    exit 1
    ;;
esac

# Archives asserted NON-ZERO on a sanitizer preset. See the OpenSSL note above.
ASSERTED_ARCHIVES="libcrypto.a libssl.a"
# Reported and cross-checked when present; never required. zlib/curl/civetweb
# arrive via opentelemetry-cpp and are absent on the libc++ lanes by design.
REPORTED_ARCHIVES="libz.a libcurl.a libcivetweb.a libcivetweb-cpp.a libpugixml.a"

# ── locate the package folders ───────────────────────────────────────────────
#
# Source is the CMakeDeps data files `conan install -of build/<preset>` just
# wrote: each carries `set(<Pkg>_PACKAGE_FOLDER_<CONFIG> "<abs path>")`. Read
# from the generated files rather than globbing `$CONAN_HOME/p/*` because the
# cache holds packages for OTHER presets too — a glob would happily count
# another lane's instrumented archive and report this lane clean of nothing.
#
# ⚠️ Matched on the `_PACKAGE_FOLDER_` line, NOT on a per-package filename. The
# data file is named after the recipe's `cmake_file_name`, which is `ZLIB` for
# zlib and `OpenSSL` for openssl — a filename-keyed lookup would need that
# mapping and would silently find nothing when a recipe renames.
if [ ! -d "$BUILD_DIR" ]; then
  echo "::error::assert-sanitized-deps.sh: build dir '$BUILD_DIR' does not exist. Run this AFTER 'conan install -of $BUILD_DIR'." >&2
  exit 1
fi

PKG_DIRS=()
while IFS= read -r d; do
  [ -n "$d" ] && PKG_DIRS+=("$d")
done < <(find "$BUILD_DIR" -maxdepth 2 -name '*-data.cmake' -print0 2>/dev/null \
         | xargs -0 -r grep -hoE '_PACKAGE_FOLDER_[A-Z]+[[:space:]]+"[^"]+"' 2>/dev/null \
         | sed -E 's/.*"(.*)"/\1/' | sort -u)

if [ "${#PKG_DIRS[@]}" -eq 0 ]; then
  echo "::error::assert-sanitized-deps.sh: found NO Conan package folders under '$BUILD_DIR'." >&2
  echo "::error::Expected '*-data.cmake' files carrying set(<Pkg>_PACKAGE_FOLDER_<CONFIG> \"...\"). Zero folders means the LOCATOR is broken, not that the closure is clean — failing loud rather than asserting zero against an empty set." >&2
  echo "--- *-data.cmake files present under $BUILD_DIR (first 20):" >&2
  find "$BUILD_DIR" -maxdepth 2 -name '*-data.cmake' 2>/dev/null | head -20 >&2 || true
  exit 1
fi

echo "assert-sanitized-deps: preset=$PRESET expect=$EXPECT build_dir=$BUILD_DIR pkg_folders=${#PKG_DIRS[@]}"

# ── inventory + assertions ───────────────────────────────────────────────────
FAILURES=0
FOUND_ASSERTED=0

# `grep -c` exits 1 when it matches nothing, which under `set -e` would abort on
# the very case the negative half exists to observe. `|| true` on every count,
# and `if/fi` for every comparison — never `[ … ] && fail`, which aborts on its
# own PASSING branch the moment this file grows a `set -e` sibling.
count_refs() { $NM "$1" 2>/dev/null | grep -c "$2" || true; }

check_archive() {
  local path="$1" name="$2" asserted="$3"
  local total asan tsan ubsan expected_count

  total="$($NM "$path" 2>/dev/null | wc -l || true)"
  asan="$(count_refs "$path" '__asan_')"
  tsan="$(count_refs "$path" '__tsan_')"
  ubsan="$(count_refs "$path" '__ubsan_')"

  echo "  $name  symbols=$total  __asan_=$asan  __tsan_=$tsan  __ubsan_=$ubsan  [$path]"

  # liveness — a zero count from an unreadable archive is not evidence of anything
  if [ "$total" -eq 0 ]; then
    echo "::error::assert-sanitized-deps.sh: $name reports ZERO total symbols ($path). The archive is unreadable or empty, so every sanitizer count below it is a BROKEN READ, not an absence. This is #252's own non-circularity check failing." >&2
    FAILURES=$((FAILURES + 1))
    return
  fi

  # Counted here, not in the `asserted = yes` block below: the vacuity guard asks
  # "did we look at the archives we said we would", and that question is the same
  # on the negative lanes, where the block below never runs.
  if [ "$asserted" = yes ]; then
    FOUND_ASSERTED=$((FOUND_ASSERTED + 1))
  fi

  if [ "$EXPECT" = none ]; then
    if [ "$asan" -ne 0 ] || [ "$tsan" -ne 0 ] || [ "$ubsan" -ne 0 ]; then
      echo "::error::assert-sanitized-deps.sh: preset '$PRESET' is NOT a sanitizer lane, but $name carries sanitizer references (__asan_=$asan __tsan_=$tsan __ubsan_=$ubsan). A non-sanitizer profile must not pull an instrumented package; this usually means a package id collision handed this lane another profile's binary." >&2
      FAILURES=$((FAILURES + 1))
    fi
    return
  fi

  case "$EXPECT" in
    asan)  expected_count="$asan"  ;;
    tsan)  expected_count="$tsan"  ;;
    ubsan) expected_count="$ubsan" ;;
  esac

  # crosswise — the two sanitizers this lane is NOT
  local a="$asan" t="$tsan" u="$ubsan"
  case "$EXPECT" in
    asan)  a=0 ;;
    tsan)  t=0 ;;
    ubsan) u=0 ;;
  esac
  if [ "$a" -ne 0 ] || [ "$t" -ne 0 ] || [ "$u" -ne 0 ]; then
    echo "::error::assert-sanitized-deps.sh: on the '$EXPECT' lane, $name carries references to a DIFFERENT sanitizer (__asan_=$asan __tsan_=$tsan __ubsan_=$ubsan). The profiles' tools.info.package_id:confs exists to keep these apart — a crossover means a package id is shared where it must not be." >&2
    FAILURES=$((FAILURES + 1))
  fi

  if [ "$asserted" = yes ]; then
    if [ "$expected_count" -eq 0 ]; then
      echo "::error::assert-sanitized-deps.sh: #252 — $name has $total symbols and ZERO __${EXPECT}_ references on the '$PRESET' lane. The archive is real and readable, so this is an ABSENCE: the dependency is UNINSTRUMENTED while its package id still moves. Fix: set CFLAGS in [buildenv] and tools.build:cflags in [conf] of conan/profiles/$PRESET, mirroring the existing C++ entries." >&2
      FAILURES=$((FAILURES + 1))
    fi
  fi
}

locate() {
  local name="$1"
  find "${PKG_DIRS[@]}" -type f -name "$name" 2>/dev/null | sort -u | head -1
}

echo "asserted archives (must be non-zero under a sanitizer preset):"
for a in $ASSERTED_ARCHIVES; do
  p="$(locate "$a")"
  if [ -z "$p" ]; then
    echo "::error::assert-sanitized-deps.sh: required archive '$a' not found in any of the ${#PKG_DIRS[@]} package folders. openssl is a direct requires in conanfile.py, so its absence means the LOCATOR is wrong, not that the package is gone." >&2
    echo "--- package folders searched:" >&2
    printf '    %s\n' "${PKG_DIRS[@]}" >&2
    echo "--- static archives actually present (first 40):" >&2
    find "${PKG_DIRS[@]}" -type f -name 'lib*.a' 2>/dev/null | head -40 >&2 || true
    FAILURES=$((FAILURES + 1))
    continue
  fi
  check_archive "$p" "$a" yes
done

echo "reported archives (cross-checked; absent by design on the libc++ lanes):"
for a in $REPORTED_ARCHIVES; do
  p="$(locate "$a")"
  if [ -z "$p" ]; then
    echo "  $a  (not present on this lane)"
    continue
  fi
  check_archive "$p" "$a" no
done

if [ "$FOUND_ASSERTED" -eq 0 ]; then
  echo "::error::assert-sanitized-deps.sh: ZERO asserted archives were checked. Every assertion below is then vacuously satisfied, which is a passing run that proves nothing — failing loud instead." >&2
  FAILURES=$((FAILURES + 1))
fi

if [ "$FAILURES" -ne 0 ]; then
  echo "::error::assert-sanitized-deps.sh: $FAILURES assertion(s) failed on preset '$PRESET' (expected sanitizer: $EXPECT). See #252." >&2
  exit 1
fi

echo "assert-sanitized-deps: OK — preset '$PRESET', expected '$EXPECT', $FOUND_ASSERTED asserted archive(s) checked."
