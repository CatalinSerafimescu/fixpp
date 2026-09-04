#!/usr/bin/env python3
"""Enforce the tests/wheel <-> in-tree suite parity contract (#298).

    ci/assert-wheel-test-parity.py [<bindings/python/tests dir>]

WHY THIS EXISTS

`bindings/python/tests/conftest.py` states that `tests/wheel/` is a deliberate,
byte-faithful fork of the in-tree suite that runs the SHIPPED wheel out-of-repo,
and `tests/wheel/README.md` enumerates the membership file by file. Nothing
enforced any of it. The convention was held entirely by hand, and it drifted:

  * `test_reentrancy.py` diverged because a fix was applied to the WHEEL copy
    alone, leaving both defects live in the in-tree suite that runs on all six
    tier-1 linux legs. A one-sided edit is silent.
  * A test existed in the in-tree `test_roundtrip.py` and not in its twin, with
    no entry in the README's exclusion list — so the shipped artifact was not
    exercised on that path and nothing said so.

THE CONTRACT, READ FROM THE README RATHER THAN RESTATED HERE

The allowlist is `tests/wheel/README.md` itself — its Membership table and its
"Deliberate exclusions" section. That is deliberate: a second list maintained
here could drift from the documented one, and then two things would claim to be
the contract. The README is the contract; this file makes it fail.

Four conditions, each a RULE rather than a roster, so adding a file cannot
silently escape them:

  1. ENUMERATION — every file in tests/wheel/ is named in the README.
  2. NO DANGLING ROWS — every file the README names exists on disk.
  3. TEST-NAME PARITY — for every twin, the set of `def test_*` names is
     identical on both sides. This is the direction that caught nothing when a
     test was present in-tree and absent from the wheel copy.
  4. BYTE IDENTITY FOR "as-is" ROWS — a row claiming no divergence must have
     none. This is the `test_reentrancy.py` shape: a one-sided edit to a file
     the README says is a faithful copy.

Rows marked with a locator swap, `(suite-native)`, or `diverges` are exempt from
(4) only — they still carry (1), (2) and (3).

⚠️ AN EMPTY SCAN IS AN INSTRUMENT FAILURE, NOT A PASS (exit 2). A parity gate
that silently passes is worse than none, because it converts an unenforced
convention into a falsely enforced one.

EXIT
  0  the contract holds
  1  at least one violation, each named with its direction
  2  the check could not run, or scanned nothing
"""
import re
import sys
import pathlib

# A Membership row: | `name.py` | <source> | <locator swap> |
ROW = re.compile(r"^\|\s*`(?P<file>[\w.]+\.py)`\s*\|(?P<source>[^|]*)\|")
# Any `name.py` mentioned anywhere in the README — the enumeration check is
# about being NAMED, which the support-module prose does outside the table.
MENTION = re.compile(r"`([\w.]+\.py)`")
TESTDEF = re.compile(r"^def (test_\w+)", re.M)


def test_names(path):
    return set(TESTDEF.findall(path.read_text(encoding="utf-8")))


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "bindings/python/tests")
    wheel = root / "wheel"
    readme = wheel / "README.md"
    for p in (root, wheel):
        if not p.is_dir():
            print(f"::error::{p} is not a directory — the check could not run.")
            return 2
    if not readme.is_file():
        print(f"::error::{readme} is missing — it IS the allowlist, so the contract "
              f"cannot be checked without it.")
        return 2

    text = readme.read_text(encoding="utf-8")
    mentioned = set(MENTION.findall(text))
    # `as-is` in the "Source" column is the claim of byte-faithfulness.
    as_is = {m.group("file") for m in (ROW.match(l) for l in text.splitlines()) if m
             and m.group("source").strip() == "as-is"}

    wheel_files = sorted(p for p in wheel.glob("*.py"))
    intree_files = sorted(p for p in root.glob("*.py"))
    if not wheel_files or not intree_files:
        print("::error::scanned ZERO python files on one side. Refusing to report clean "
              "on an empty scan — this is an instrument failure, not a passing contract.")
        return 2

    violations = []

    # (1) ENUMERATION — every wheel file is named in the README.
    for p in wheel_files:
        if p.name == "__init__.py":
            continue
        if p.name not in mentioned:
            violations.append(
                f"UNENUMERATED: tests/wheel/{p.name} exists but the README names it "
                f"nowhere. Add it to the Membership table (with its Source disposition) "
                f"so the fork's contents stay auditable rather than tribal.")

    # (2) NO DANGLING ROWS — every file the README names exists somewhere.
    for name in sorted(mentioned):
        if not (wheel / name).exists() and not (root / name).exists():
            violations.append(
                f"DANGLING: the README names `{name}`, which exists in neither "
                f"tests/ nor tests/wheel/. A contract citing a file that is not there "
                f"is the same class as a comment recording a result.")

    # (3) TEST-NAME PARITY across every twin.
    twins = [p for p in wheel_files if (root / p.name).exists()]
    if not twins:
        print("::error::found ZERO twin files. Either the suites moved or this check's "
              "pairing is broken; an empty twin set cannot certify parity.")
        return 2
    for p in twins:
        a, b = test_names(root / p.name), test_names(p)
        only_intree, only_wheel = sorted(a - b), sorted(b - a)
        if only_intree:
            violations.append(
                f"DRIFT in {p.name}: present in-tree, ABSENT from the wheel copy: "
                f"{', '.join(only_intree)}. The shipped artifact is not exercised on "
                f"that path. Port it, or add it to the README's Deliberate exclusions "
                f"with a reason — absent and unexplained is the state this gate exists "
                f"to end.")
        if only_wheel:
            violations.append(
                f"DRIFT in {p.name}: present in the wheel copy, ABSENT in-tree: "
                f"{', '.join(only_wheel)}. A fix applied to the wheel copy alone leaves "
                f"the defect live in the in-tree suite that runs on every tier-1 linux "
                f"leg — the exact one-sided edit #298 was filed about.")

    # (4) BYTE IDENTITY for rows claiming `as-is`.
    for name in sorted(as_is):
        src, dst = root / name, wheel / name
        if not src.exists() or not dst.exists():
            continue                      # already reported by (2)
        if src.read_bytes() != dst.read_bytes():
            violations.append(
                f"NOT BYTE-FAITHFUL: the README marks {name} `as-is`, but the two "
                f"copies differ. Either restore the faithful copy, or change the row to "
                f"state the divergence — a row claiming no divergence while one exists "
                f"is a false contract.")

    print(f"  scanned {len(wheel_files)} wheel file(s), {len(twins)} twin(s), "
          f"{len(as_is)} `as-is` row(s)")
    for p in twins:
        mark = "as-is" if p.name in as_is else "may diverge"
        print(f"    twin {p.name} ({mark})")

    if violations:
        for v in violations:
            print(f"::error::{v}")
        print(f"\nwheel-test parity: {len(violations)} violation(s).")
        return 1

    print("\nwheel-test parity: contract holds.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
