#!/usr/bin/env python3
"""assert-interop-skips — a SKIP must not read as a PASS (fixpp#431).

    ci/assert-interop-skips.py --json-dir DIR --expected-skips FILE --expected-count N

Reads the per-binary gtest JSON reports `GTEST_OUTPUT=json:DIR/` writes for a
`ctest -L interop` run with no counterparty leased, and asserts three things
the CI step's own count assertion (run before this script, over `ctest -N`)
does not:

  1. no case FAILED;
  2. the set of SKIPPED `Suite.Case` ids is EXACTLY the checked-in list in
     `--expected-skips` (both directions: an id that skips and is not listed,
     and a listed id that no longer skips, are both violations);
  3. every skip's message matches the ONE reason this leg is allowed to skip
     for — a counterparty port not leased (INTEROP_QUICKFIX_{CPP,J}_PORT unset)
     — never any other guard the same test file may also carry
     (tests/interop/support/counterparty_probe.hpp's other two ProbeResult
     reasons, or a `skip:not-applicable`/fixture-dir guard reached after the
     probe).

EXIT
  0  every case ran or skipped for the one allowed reason, and the skip set
     matches exactly
  1  a NAMED invariant above is violated — a real defect in this run
  2  the check could not be trusted to answer at all: the JSON directory or
     the expected-skips file is missing, the number of JSON reports does not
     match --expected-count, a report does not parse, or zero cases were
     found across every report. An empty or partial scan is an INSTRUMENT
     failure here, never a clean pass
     (feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree).

`--expected-count` is REQUIRED and must be a positive integer: this is the
`ctest -L interop` binary count with the one known non-gtest ctest entry
(`interop_cell_results_schema_check`, a pytest case — it never emits a gtest
JSON report) excluded by the caller. A count of 0 is refused outright — a
derivation that lands on zero must never be able to pass vacuously against
zero JSON files found.
"""
import argparse
import glob
import json
import os
import re
import sys

PORT_REASON_RE = re.compile(
    r"quickfix-(?:cpp|j) unavailable: "
    r"INTEROP_QUICKFIX_(?:CPP|J)_PORT not set \(parent harness did not lease a port\)"
)


def gh_error(msg: str) -> None:
    print(f"::error title=Interop gate::{msg}")


def load_expected_skips(path: str) -> set:
    ids = set()
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            ids.add(line)
    return ids


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json-dir", required=True,
                     help="directory GTEST_OUTPUT=json: wrote per-binary reports into")
    ap.add_argument("--expected-skips", required=True,
                     help="checked-in sorted Suite.Case list "
                          "(tests/interop/expected-skips-without-counterparty.txt)")
    ap.add_argument("--expected-count", required=True, type=int,
                     help="expected number of gtest JSON reports (binaries), "
                          "non-gtest ctest entries already excluded by the caller")
    args = ap.parse_args()

    if args.expected_count <= 0:
        gh_error(f"--expected-count was {args.expected_count} — refusing to run: a "
                  "derivation that lands on zero or negative must never be able to "
                  "pass vacuously against zero JSON files found.")
        return 2

    if not os.path.isdir(args.json_dir):
        gh_error(f"JSON directory '{args.json_dir}' does not exist. "
                  "GTEST_OUTPUT=json: did not write anything, or the wrong path was "
                  "passed here. Refusing to report a scan that never happened as clean.")
        return 2

    if not os.path.isfile(args.expected_skips):
        gh_error(f"expected-skips file '{args.expected_skips}' does not exist.")
        return 2

    try:
        expected_skips = load_expected_skips(args.expected_skips)
    except OSError as e:
        gh_error(f"could not read '{args.expected_skips}': {e}")
        return 2

    json_files = sorted(glob.glob(os.path.join(args.json_dir, "*.json")))
    if len(json_files) != args.expected_count:
        gh_error(f"found {len(json_files)} gtest JSON report(s) under "
                  f"'{args.json_dir}', expected exactly {args.expected_count}. A "
                  "missing report means a binary registered by `ctest -N -L interop` "
                  "did not run or did not write GTEST_OUTPUT — that is an instrument "
                  "failure, not a clean run, and this refuses to report one.")
        return 2

    failed_cases = []
    actual_skips = set()
    bad_reason_skips = []
    total_cases = 0

    for path in json_files:
        try:
            with open(path, encoding="utf-8") as f:
                doc = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            gh_error(f"'{path}' did not parse as JSON: {e}. Fail-closed — a report "
                     "this cannot read is not a report this can call clean.")
            return 2

        testsuites = doc.get("testsuites")
        if not isinstance(testsuites, list):
            gh_error(f"'{path}' has no `testsuites` list — not a gtest JSON report "
                     "this checker recognises. Fail-closed.")
            return 2

        for ts in testsuites:
            suite = ts.get("name", "")
            for tc in ts.get("testsuite", []):
                total_cases += 1
                case_id = f"{suite}.{tc.get('name', '')}"
                result = tc.get("result")
                if result == "SKIPPED":
                    actual_skips.add(case_id)
                    msgs = "\n".join(m.get("message", "") for m in tc.get("skipped", []))
                    if not PORT_REASON_RE.search(msgs):
                        bad_reason_skips.append((case_id, msgs))
                elif tc.get("failures"):
                    failed_cases.append(case_id)
                # else: COMPLETED with no failures — an ordinary pass.

    if total_cases == 0:
        gh_error(f"{len(json_files)} JSON report(s) present but ZERO test cases were "
                  "found across all of them. An empty scan cannot be trusted as a "
                  "clean one — fail-closed.")
        return 2

    violations = []

    if failed_cases:
        violations.append(
            f"{len(failed_cases)} case(s) FAILED (must be zero for this gate): "
            + ", ".join(sorted(failed_cases)))

    unexpected = sorted(actual_skips - expected_skips)
    not_skipped = sorted(expected_skips - actual_skips)
    if unexpected or not_skipped:
        violations.append(
            "the SKIPPED set differs from "
            f"'{args.expected_skips}'.\n"
            f"  unexpected skips ({len(unexpected)}, skipped now but not listed): "
            + (", ".join(unexpected) if unexpected else "(none)") + "\n"
            f"  listed-but-not-skipped ({len(not_skipped)}, listed but ran/absent this time): "
            + (", ".join(not_skipped) if not_skipped else "(none)"))

    if bad_reason_skips:
        detail = "; ".join(f"{cid}: {msg!r}" for cid, msg in bad_reason_skips)
        violations.append(
            f"{len(bad_reason_skips)} skip(s) gave a reason other than a counterparty "
            f"port not being leased: {detail}")

    if violations:
        for v in violations:
            gh_error(v)
        return 1

    print(f"PASS: {total_cases} case(s) across {len(json_files)} binaries — "
          f"{len(actual_skips)} skipped, all matching the checked-in list and the "
          "port-not-set reason; 0 failed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
