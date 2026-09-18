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
# The pattern is LD_PRELOAD pointing at mallocnesia by any spelling.
RAW = re.compile(r'LD_PRELOAD\s*=\s*[^"\']*mallocnesia', re.I)


def main() -> int:
    offenders, scanned = [], 0
    for path in sorted(REPO.glob("tests/**/CMakeLists.txt")):
        scanned += 1
        disabled = False
        for n, line in enumerate(path.read_text().splitlines(), 1):
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
        print("::error::[no-raw-preload] the sweep examined ZERO CMakeLists.txt files — "
              "the glob is wrong, so a clean result here means nothing.")
        return 2

    if offenders:
        print(f"::error::[no-raw-preload] {len(offenders)} site(s) set LD_PRELOAD to "
              f"mallocnesia directly instead of using fixpp_add_mallocnesia_test(). Such a "
              f"site bypasses the fail-closed wrapper, the interception witness, the "
              f"`mallocnesia` label and therefore the CI selection — and ld.so IGNORES an "
              f"unloadable preload, so it runs uninstrumented and PASSES:")
        for o in offenders:
            print(f"::error::  {o}")
        return 1

    print(f"[no-raw-preload] OK: {scanned} CMakeLists.txt scanned, no raw mallocnesia "
          f"LD_PRELOAD outside the helper.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
