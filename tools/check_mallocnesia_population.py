#!/usr/bin/env python3
"""tools/check_mallocnesia_population.py — the allocation gates' population pin (fixpp#448).

WHAT DRIFTED, AND WHY A LABEL IS THE THING WORTH PINNING.

CI selects the allocation-discipline gates by LABEL (`ctest -L mallocnesia`). Immediately
BEFORE fixpp#448 the label carried fewer than half the gates that matched by name —
every capi one, plus tls, log and two session entries were invisible to any
label-driven runner. Nothing reported it, because a label selecting SOME real tests
still runs real tests and they pass. (A historical measurement of a fixed tree. This
checker prints the current sizes on every run; do not read a count from this prose.)

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
#
# ⚠️ KEEP THIS LIST AS SHORT AS THE NAMING ALLOWS. It is a checker holding part of the
# authority's membership, which this repo has a recorded lesson against. The planted
# control was briefly in here purely because of what it was CALLED; renaming it into the
# convention removed the row rather than justifying it. Prefer that fix to a new row.
DECLARED_EXTRAS = {
    "alloc_guard_no_raw_preload":
        "static scan for gates that register a raw LD_PRELOAD instead of going through "
        "fixpp_add_mallocnesia_test — the shape that bypasses the fail-closed wrapper, "
        "the witness and this very label. Belongs to the population; needs no preload.",
    "alloc_guard_markers_no_local_def":
        "static scan for locally-defined alloc_guard markers — the pattern that silently "
        "disables interception. Belongs to the gate population; needs no preload, so it "
        "will never match the name pattern.",
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
    ap.add_argument("--min-gates", type=int, default=0,
                    help="FLOOR on the number of registered gates. Vacuity-at-zero is not "
                         "enough: the failure being closed here is GRADUAL. Delete 17 of 18 "
                         "gates and every set rule below still passes — one named gate, "
                         "labelled, extras intact. CI pins this; local runs need not.")
    args = ap.parse_args()

    by_name = ctest_names(args.build_dir, "-R", "_mallocnesia$")
    by_label = ctest_names(args.build_dir, "-L", "mallocnesia")

    failures = []

    # (0a) FLOOR. Deliberately brittle, and the same shape as tier1.yml's neighbouring
    # `-L consumer -N` / `-L packaging -N` pins: a gate population that SHRINKS is the
    # hazard, and no set relation below can see it. A floor rather than an exact count
    # because ADDING a gate must not need a CI edit, while removing one must.
    if args.min_gates and len(by_name) < args.min_gates:
        failures.append(
            f"only {len(by_name)} gate(s) registered, floor is {args.min_gates}. Gates have "
            f"been REMOVED or are no longer registered on this lane. Every set rule below "
            f"still passes in that state — that is why the floor exists. If the removal is "
            f"deliberate, lower the floor in the same commit and say why.")

    # (0) VACUITY FIRST. Everything below is trivially satisfied by two empty sets.
    if not by_name:
        failures.append(
            "ZERO tests match the name pattern `_mallocnesia$`. Refusing to report a "
            "clean population over nothing. Expected causes, in order of likelihood: "
            "(a) this is a SANITIZER build, where the gates deliberately do not register "
            "at all — a sanitizer's allocator interposes ahead of the interceptor, so "
            "they would pass vacuously (run this against linux-clang-release); "
            "(b) the gates are not registered on this lane, the failure fixpp#448 "
            "removed; (c) the naming convention moved.")
    if not by_label:
        failures.append(
            "ZERO tests carry the `mallocnesia` label, so `ctest -L mallocnesia` would "
            "exit 0 having run NOTHING. That is the shape of a green CI step that "
            "measures nothing.")

    # (0b) THE POSITIVE CONTROL MUST EXIST, exactly once. The floor above counts NAMES,
    # and a name is cheap: `add_test(NAME padding_mallocnesia COMMAND cmake -E true)` with
    # the label satisfies the count, both set relations and the raw-preload scan, while a
    # real gate has been deleted. The count cannot tell a gate from a decoy — but the
    # control is the one member whose ABSENCE means nobody is checking that interception
    # works at all, so it is named here rather than left to arithmetic.
    controls = sorted(n for n in by_label if "positive_control" in n)
    if len(controls) != 1:
        failures.append(
            f"expected exactly ONE positive control in the `mallocnesia` label, found "
            f"{len(controls)}: {', '.join(controls) or '(none)'}. The control is the only "
            f"member that fails when interception silently stops working; without it "
            f"'0 failed' is equally consistent with a clean tree and a dead interceptor. "
            f"It must carry the label so it cannot be run separately from the gates it "
            f"vouches for.")

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
