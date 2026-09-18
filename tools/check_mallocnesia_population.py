#!/usr/bin/env python3
"""tools/check_mallocnesia_population.py — the allocation gates' population pin (fixpp#448).

WHAT DRIFTED, AND WHY A LABEL IS THE THING WORTH PINNING.

CI selects the allocation-discipline gates by LABEL (`ctest -L mallocnesia`). Measured on
`main` before fixpp#448: 18 entries matched by NAME and 8 carried the label. Eleven gates
— every capi one, tls, log, two session ones — were therefore invisible to any
label-driven runner. Nothing reported that, because a label selecting 8 of 18 real tests
still runs 8 real tests and they pass.

THE INVARIANT IS ⊆, NOT ==, and this is the half #448's own text gets wrong. It asks for
a check that the two sets are EQUAL. They cannot be: `alloc_guard_markers_no_local_def`
is a static scan for locally-defined alloc_guard markers (the pattern that silently
disables LD_PRELOAD interception). It belongs in the gate population and needs no
preload, so it will never match the name pattern. Demanding equality would force deleting
a real gate to satisfy a checker.

So: every `*_mallocnesia` entry MUST carry the label, and anything else carrying the
label must be DECLARED below with a reason. An undeclared extra fails — that is what
stops the label quietly becoming a catch-all.

⚠️ VACUITY. Both sets are also required to be non-empty. A configure that registers no
gates at all (the old `if(EXISTS)` guards on a machine without the hand-built .so) makes
every set comparison trivially true, and `ctest -L mallocnesia` would then exit 0 having
run nothing. This is the single recurring defect class in this repo: an instrument that
reports clean because it could not report otherwise.
"""
import argparse
import re
import subprocess
import sys

# Entries that carry the label but do NOT match the *_mallocnesia name pattern.
# Adding a row is a deliberate act and needs a reason someone can check.
DECLARED_EXTRAS = {
    "alloc_guard_markers_no_local_def":
        "static scan for locally-defined alloc_guard markers — the pattern that silently "
        "disables interception. Belongs to the gate population; needs no preload, so it "
        "will never match the name pattern.",
    "mallocnesia_positive_control":
        "the planted-allocation control (fixpp#448). It must run in the same selection as "
        "the gates it vouches for, or a lane could run the gates without it and never "
        "learn that interception had stopped.",
}

NAME_RE = re.compile(r"^\s*Test\s+#\d+:\s+(\S+)", re.M)


def ctest_names(build_dir: str, selector: str, value: str) -> set[str]:
    out = subprocess.run(["ctest", "--test-dir", build_dir, "-N", selector, value],
                         capture_output=True, text=True)
    if out.returncode != 0:
        print(f"error: ctest -N {selector} {value} failed rc={out.returncode}\n{out.stderr}",
              file=sys.stderr)
        sys.exit(2)
    return set(NAME_RE.findall(out.stdout))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", required=True)
    args = ap.parse_args()

    by_name = ctest_names(args.build_dir, "-R", "_mallocnesia$")
    by_label = ctest_names(args.build_dir, "-L", "mallocnesia")

    failures = []

    # (0) VACUITY FIRST. Everything below is trivially satisfied by two empty sets.
    if not by_name:
        failures.append(
            "ZERO tests match the name pattern `_mallocnesia$`. Either the gates are not "
            "registered in this build (the failure fixpp#448 removed), or the naming "
            "convention moved. Refusing to report a clean population over nothing.")
    if not by_label:
        failures.append(
            "ZERO tests carry the `mallocnesia` label, so `ctest -L mallocnesia` would "
            "exit 0 having run NOTHING. That is the shape of a green CI step that "
            "measures nothing.")

    # (1) ⊆ : every named gate carries the label.
    unlabelled = sorted(by_name - by_label)
    if unlabelled:
        failures.append(
            f"{len(unlabelled)} gate(s) match `_mallocnesia$` but do NOT carry the "
            f"`mallocnesia` label, so a label-driven CI run skips them silently: "
            + ", ".join(unlabelled)
            + ". Register them via fixpp_add_mallocnesia_test(), which attaches the label "
              "in one place.")

    # (2) every label member that is not a named gate must be declared, with a reason.
    undeclared = sorted((by_label - by_name) - set(DECLARED_EXTRAS))
    if undeclared:
        failures.append(
            f"{len(undeclared)} test(s) carry the `mallocnesia` label but neither match "
            f"the name pattern nor are declared in DECLARED_EXTRAS: "
            + ", ".join(undeclared)
            + ". Either they belong to the gate population — add a row saying why — or "
              "the label is being used as a general tag, which is how the selection "
              "stops meaning anything.")

    # (3) a declared extra that has vanished is a stale row, not a pass.
    stale = sorted(set(DECLARED_EXTRAS) - by_label)
    if stale:
        failures.append(
            f"{len(stale)} DECLARED_EXTRAS row(s) name a test that no longer carries the "
            f"label: " + ", ".join(stale)
            + ". A declaration that describes nothing is a claim nobody re-checked; "
              "delete the row or restore the test.")

    if failures:
        for f in failures:
            print(f"::error::[mallocnesia-population] {f}")
        return 1

    print(f"[mallocnesia-population] OK: {len(by_name)} named gate(s), all labelled; "
          f"{len(by_label)} labelled total "
          f"({len(by_label - by_name)} declared non-gate member(s)).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
