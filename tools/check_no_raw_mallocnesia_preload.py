#!/usr/bin/env python3
"""tools/check_no_raw_mallocnesia_preload.py — recurrence guard (fixpp#448).

A gate that sets LD_PRELOAD itself bypasses EVERY property fixpp#448 adds:
check_alloc.py's fail-closed refusal, the interception witness, the `mallocnesia`
label, and therefore the population checker and the CI selection too. It is also the
quietest possible failure — ld.so IGNORES an unloadable preload, so the test runs
UNINSTRUMENTED and passes.

fixpp#448 named two such sites. Converting them is a fix; this is the mechanism, so the
third one cannot arrive unnoticed. Registered beside alloc_guard_markers_no_local_def,
which guards the sibling recurrence (a test defining the markers locally).

⚠️ COMMENTED-OUT SITES COUNT. Several live under `if(FALSE)` with the old pattern
preserved verbatim as the documented restore target. Those are inert today and are
ALLOWED — but only in that form, because the thing being prevented is someone
uncommenting one. The allowance is narrow on purpose: a raw preload outside a disabled
block is a finding wherever it appears.
"""
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# ⚠️ ANY LD_PRELOAD ASSIGNMENT, whatever the right-hand side.
#
# An earlier revision required the literal token `mallocnesia` on the same line, which
# ordinary CMake variable indirection walks straight past:
#
#     set(_mn "${CMAKE_BINARY_DIR}/missing/libmallocnesia.so")
#     COMMAND ${CMAKE_COMMAND} -E env "LD_PRELOAD=${_mn}" $<TARGET_FILE:x>
#
# The scan saw no `mallocnesia` on the LD_PRELOAD line; the population checker saw a
# correctly-named, correctly-labelled test; ld.so ignored the missing library; the weak
# markers no-oped; the binary exited 0. Green, uninstrumented.
#
# Matching the right-hand side at all is the mistake — it is a value, and a value can be
# spelled arbitrarily many ways. A TEST-REGISTRATION file has no legitimate reason to set
# LD_PRELOAD itself, so the LEFT-hand side is the whole condition and there is nothing
# left to evade. If a non-mallocnesia preload is ever genuinely needed here, this check
# is the right place to argue for it.
RAW = re.compile(r'LD_PRELOAD\s*=', re.I)


def main() -> int:
    offenders, scanned = [], 0
    # .cmake modules under tests/ are included INTO these files and can register tests
    # just as well; scanning only CMakeLists.txt left that door open.
    paths = sorted(set(REPO.glob("tests/**/CMakeLists.txt")) | set(REPO.glob("tests/**/*.cmake")))
    for path in paths:
        scanned += 1
        disabled = False
        # ⚠️ encoding="utf-8" is NOT optional. `read_text()` with no encoding uses the
        # LOCALE default, which is cp1252 on a Windows runner, and these CMakeLists are
        # full of UTF-8 (the repo's comments use -- and warning glyphs heavily). MEASURED
        # on windows-msvc-release: `UnicodeDecodeError: 'charmap' codec can't decode byte
        # 0x81`. The sibling scanner registered next to this one
        # (check_alloc_guard_markers.py) already reads this way, which is why it passes
        # there. errors="replace" so one odd byte reports a finding rather than a crash.
        for n, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith("if(FALSE)"):
                disabled = True
            elif stripped.startswith("endif("):
                disabled = False
            if not RAW.search(line):
                continue
            if stripped.startswith("#") or disabled:
                continue          # inert: a comment, or inside a disabled block
            offenders.append(f"{path.relative_to(REPO)}:{n}: {stripped[:100]}")

    # ⚠️ Prove the sweep reached something. A glob that matches no file reports clean,
    # which is this repo's most recurring defect shape.
    if scanned == 0:
        print("::error::[no-raw-preload] the sweep examined ZERO test CMake files -- "
              "the glob is wrong, so a clean result here means nothing.")
        return 2

    if offenders:
        # ⚠️ ASCII ONLY on every printed path. A Windows console is cp1252; a glyph that
        # codepage LACKS raises UnicodeEncodeError and the checker CRASHES INSTEAD OF
        # REPORTING -- precisely when it has a finding, which is the worst possible time.
        # The repo has hit this before (memory project_tier2_windows_runtime_fixes:
        # "Python arrow -> ASCII ... (cp1252)").
        #
        # ⚠️ Do NOT reason about this by picking one character: cp1252 CONTAINS the
        # em-dash (0x97), so an em-dash is harmless and a mutant built from one stays
        # green for a reason unrelated to the check. It is the arrow, warning sign and
        # set-operator glyphs that are absent. The arm in
        # ci/test-mallocnesia-population.sh therefore asserts the output is pure ASCII
        # rather than testing against any one codepage.
        print(f"::error::[no-raw-preload] {len(offenders)} test-registration site(s) set "
              f"LD_PRELOAD directly instead of using fixpp_add_mallocnesia_test(). Such a "
              f"site bypasses the fail-closed wrapper, the interception witness, the "
              f"`mallocnesia` label and therefore the CI selection -- and ld.so IGNORES an "
              f"unloadable preload, so it runs uninstrumented and PASSES:")
        for o in offenders:
            print(f"::error::  {o}")
        return 1

    print(f"[no-raw-preload] OK: {scanned} test CMake file(s) scanned, no raw LD_PRELOAD "
          f"outside the helper.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
