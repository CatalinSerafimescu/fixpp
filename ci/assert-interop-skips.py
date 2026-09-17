#!/usr/bin/env python3
"""assert-interop-skips — a SKIP must not read as a PASS (fixpp#431).

    ci/assert-interop-skips.py --json-dir DIR --bin-dir DIR --expected-skips FILE --expected-count N

Reads the per-binary gtest JSON reports `GTEST_OUTPUT=json:DIR/` writes for a
`ctest -L interop` run with no counterparty leased, and asserts things
the CI step's own count assertion (run before this script, over `ctest -N`)
does not:

  1. no case FAILED;
  2. every case actually RAN with a real result (status=RUN, result in
     {COMPLETED, SKIPPED}), and no report or suite has a nonzero `disabled`
     count — gtest reports a DISABLED_ case as status=NOTRUN,
     result=SUPPRESSED with a nonzero `disabled` count, and that is not the
     same as running and passing. A case carrying an epoch `timestamp` never
     started (gtest emits every case after a global-environment GTEST_SKIP()
     as RUN/COMPLETED that way) and is a violation too;
  3. the run report's case set matches the binary's OWN `--gtest_list_tests`
     enumeration exactly, in both directions. A GTEST_FILTER, a shard
     control, a wrapper, or a launcher can OMIT a case from the report
     entirely regardless of how it is spelled in the ctest registration —
     so this checker enumerates each binary itself, outside that
     registration, with inherited `GTEST_*` variables scrubbed from the
     enumeration's own environment AND its filter forced to `*` on the
     command line (googletest takes a filter default from
     `TESTBRIDGE_TEST_ONLY` when `GTEST_FILTER` is unset, and that name is
     not `GTEST_`-prefixed and so survives the environment scrub alone),
     and requires the listed and
     reported `Suite.Case` id sets to be equal. A case enumerated but absent
     from the report, and a case reported but not enumerated (a stale or
     wrong `--bin-dir`), are both violations;
  4. the set of SKIPPED `Suite.Case` ids is EXACTLY the checked-in list in
     `--expected-skips` (both directions: an id that skips and is not listed,
     and a listed id that no longer skips, are both violations);
  5. every skip's message, once its leading `<file>:<line>` location line is
     stripped, FULLMATCHES the ONE reason this leg is allowed to skip for —
     a counterparty port not leased (INTEROP_QUICKFIX_{CPP,J}_PORT unset) —
     never any other guard the same test file may also carry
     (tests/interop/support/counterparty_probe.hpp's other two ProbeResult
     reasons, or a `skip:not-applicable`/fixture-dir guard reached after the
     probe), and never that reason plus a prefix, suffix, or extra line.

EXIT
  0  every case ran or skipped for the one allowed reason, and the skip set
     matches exactly
  1  a NAMED invariant above is violated — a real defect in this run
  2  the check could not be trusted to answer at all: the JSON directory or
     the expected-skips file is missing, the number of JSON reports does not
     match --expected-count, a report does not parse as JSON or is not
     structurally a gtest report at any level (a non-object top level, a
     `testsuites`/`testsuite`/`skipped`/`failures` field of the wrong shape),
     the same `Suite.Case` id appears in two different reports, a single
     report has zero cases with status=RUN, zero cases were found across
     every report, a report's binary is missing under --bin-dir, or that
     binary could not be enumerated with --gtest_list_tests. An empty,
     partial, or malformed-but-parseable scan is an INSTRUMENT failure here,
     never a clean pass
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
    r"(?:quickfix-cpp unavailable: INTEROP_QUICKFIX_CPP_PORT|"
    r"quickfix-j unavailable: INTEROP_QUICKFIX_J_PORT)"
    r" not set \(parent harness did not lease a port\)"
)

# A real gtest JSON skip message is `<file>:<line>\n<reason>\n` — one leading
# location line (which may be a Windows `C:\...` path, hence `[^\n]*` rather
# than excluding `:`), then the reason. Reason matching strips exactly this
# much and nothing else, so a message with NO location line, or with a SECOND
# line after the reason, cannot silently pass by matching a substring of a
# longer string (Codex #3).
LOCATION_LINE_RE = re.compile(r"^[^\n]*:\d+\n")


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


def listed_case_ids(exe: str) -> set:
    """Enumerate the binary's own `Suite.Case` ids, bypassing whatever launcher,
    wrapper, filter, or shard control its ctest registration carries — the
    check that no scan of that registration's text can ever fully enumerate.
    """
    import subprocess
    import tempfile

    # Resolved to an absolute path BEFORE the subprocess's cwd is changed
    # below — a relative --bin-dir (what every calling workflow passes,
    # `build/${{ matrix.preset }}/bin`) contains a `/`, so POSIX exec takes
    # it as a path rather than a PATH lookup: run with cwd=td unresolved, it
    # would be interpreted relative to td and never found.
    exe = os.path.abspath(exe)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GTEST_")}
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "list.json")
        try:
            # `--gtest_filter=*` on the command line, not only the env scrub
            # above: googletest's GetDefaultFilter() falls back to
            # TESTBRIDGE_TEST_ONLY when GTEST_FILTER is unset, and that name
            # is not GTEST_-prefixed, so it would otherwise reach this
            # enumeration and the real run identically.
            subprocess.run(
                [exe, "--gtest_list_tests", "--gtest_filter=*",
                 f"--gtest_output=json:{out}"],
                env=env, cwd=td, capture_output=True, timeout=120, check=True)
            with open(out, encoding="utf-8") as f:
                doc = json.load(f)
            return {f"{ts['name']}.{tc['name']}"
                    for ts in doc["testsuites"] for tc in ts["testsuite"]}
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError) as e:
            gh_error(f"could not enumerate '{exe}' with --gtest_list_tests: {e!r}. "
                     "Fail-closed.")
            raise SystemExit(2)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json-dir", required=True,
                     help="directory GTEST_OUTPUT=json: wrote per-binary reports into")
    ap.add_argument("--bin-dir", required=True,
                     help="directory holding the binary each <stem>.json report names, "
                          "used to enumerate that binary's own cases with "
                          "--gtest_list_tests, outside its ctest registration")
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
    bad_status_cases = []
    never_started = []
    disabled_violations = []
    omitted_from_report = []  # enumerated by the binary but absent from its report
    extra_in_report = []      # reported but not enumerated (stale/wrong --bin-dir)
    total_cases = 0
    seen_case_ids = {}  # case_id -> path of the report it was first seen in

    for path in json_files:
        try:
            with open(path, encoding="utf-8") as f:
                doc = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            gh_error(f"'{path}' did not parse as JSON: {e}. Fail-closed — a report "
                     "this cannot read is not a report this can call clean.")
            return 2

        if not isinstance(doc, dict):
            gh_error(f"'{path}' top level is a {type(doc).__name__}, not a JSON object — "
                      "not a gtest JSON report this checker recognises. Fail-closed.")
            return 2

        top_disabled = doc.get("disabled", 0)
        if not isinstance(top_disabled, int):
            gh_error(f"'{path}' has a non-integer top-level `disabled` field "
                      f"({top_disabled!r}). Fail-closed.")
            return 2

        testsuites = doc.get("testsuites")
        if not isinstance(testsuites, list):
            gh_error(f"'{path}' has no `testsuites` list — not a gtest JSON report "
                     "this checker recognises. Fail-closed.")
            return 2

        report_cases = 0
        report_run_cases = 0
        report_ids = set()

        for ts in testsuites:
            if not isinstance(ts, dict):
                gh_error(f"'{path}' has a `testsuites` entry that is a "
                          f"{type(ts).__name__}, not a JSON object. Fail-closed.")
                return 2
            suite = ts.get("name")
            if not isinstance(suite, str) or not suite:
                gh_error(f"'{path}' has a suite without a non-empty `name`. Fail-closed.")
                return 2
            suite_disabled = ts.get("disabled", 0)
            if not isinstance(suite_disabled, int):
                gh_error(f"'{path}' suite '{suite}' has a non-integer `disabled` field "
                          f"({suite_disabled!r}). Fail-closed.")
                return 2
            if suite_disabled:
                disabled_violations.append((path, f"suite '{suite}'", suite_disabled))

            if "testsuite" not in ts:
                gh_error(f"'{path}' suite '{suite}' has no `testsuite` field. Fail-closed.")
                return 2
            testcases = ts.get("testsuite")
            if not isinstance(testcases, list):
                gh_error(f"'{path}' suite '{suite}' has a `testsuite` field that is a "
                          f"{type(testcases).__name__}, not a list. Fail-closed.")
                return 2

            for tc in testcases:
                if not isinstance(tc, dict):
                    gh_error(f"'{path}' suite '{suite}' has a testcase entry that is a "
                              f"{type(tc).__name__}, not a JSON object. Fail-closed.")
                    return 2
                report_cases += 1
                total_cases += 1
                case_name = tc.get("name")
                if not isinstance(case_name, str) or not case_name:
                    gh_error(f"'{path}' suite '{suite}' has a testcase without a non-empty "
                              "`name`. Fail-closed.")
                    return 2
                case_id = f"{suite}.{case_name}"
                if case_id in seen_case_ids:
                    gh_error(f"'{case_id}' appears in both {seen_case_ids[case_id]} and "
                              f"{path} — Suite.Case is unique only within a binary; the "
                              "skip set cannot be compared.")
                    return 2
                seen_case_ids[case_id] = path
                report_ids.add(case_id)
                status = tc.get("status")
                result = tc.get("result")

                if status == "RUN":
                    report_run_cases += 1

                # gtest reports a DISABLED_ case as status=NOTRUN,
                # result=SUPPRESSED; a GTEST_FILTER/shard/wrapper/launcher
                # instead omits the case from the report entirely, which the
                # enumeration comparison below catches. The gate requires
                # every emitted case to have a real (COMPLETED/SKIPPED)
                # result.
                if status != "RUN" or result not in ("COMPLETED", "SKIPPED"):
                    bad_status_cases.append((case_id, status, result))
                    continue

                # A case gtest never started — e.g. every case after a
                # GTEST_SKIP() in a global Environment::SetUp — is still
                # emitted as RUN/COMPLETED; its unset start time renders as
                # the epoch, which a case that ran never carries.
                started = tc.get("timestamp")
                if isinstance(started, str) and started[:4] in ("1969", "1970"):
                    never_started.append((case_id, started))
                    continue

                if result == "SKIPPED":
                    actual_skips.add(case_id)
                    skipped = tc.get("skipped", [])
                    if not isinstance(skipped, list):
                        gh_error(f"'{path}' case '{case_id}' has a `skipped` field that is "
                                  f"a {type(skipped).__name__}, not a list. Fail-closed.")
                        return 2
                    if not skipped:
                        bad_reason_skips.append((case_id, "<no skip message>"))
                    for entry in skipped:
                        if not isinstance(entry, dict) or not isinstance(entry.get("message"), str):
                            gh_error(f"'{path}' case '{case_id}' has a `skipped` entry that "
                                      "is not a JSON object with a string `message`. "
                                      "Fail-closed.")
                            return 2
                        msg = entry["message"]
                        # A valid gtest skip message is exactly
                        # `<file>:<line>\n<reason>\n` (with Windows paths
                        # accepted in the location line), and the interior
                        # reason must fullmatch the one allowed reason.
                        loc = LOCATION_LINE_RE.match(msg)
                        stripped = msg[loc.end():-1] if (loc and msg.endswith("\n")) else ""
                        if not PORT_REASON_RE.fullmatch(stripped):
                            bad_reason_skips.append((case_id, msg))
                elif "failures" in tc:
                    failures = tc.get("failures")
                    if not isinstance(failures, list):
                        gh_error(f"'{path}' case '{case_id}' has a `failures` field that is "
                                  f"a {type(failures).__name__}, not a list. Fail-closed.")
                        return 2
                    if failures:
                        failed_cases.append(case_id)
                # else: status=RUN, result=COMPLETED, no failures — an
                # ordinary pass (true: both status and result were checked
                # above, not assumed).

        if top_disabled:
            disabled_violations.append((path, "report", top_disabled))

        # Per-report zero, replacing the aggregate-only guard below: a binary
        # whose tests are all compiled out on one platform (an #ifdef) can
        # report zero RUN cases while every OTHER report in this run is
        # non-empty — the aggregate check alone cannot see that. Checked
        # BEFORE the enumeration below so an entirely-empty/suppressed
        # report is attributed to this named cause, not to an enumeration
        # mismatch or an "enumerates zero cases" instrument failure.
        if report_run_cases == 0:
            gh_error(f"'{path}' has {report_cases} test case(s) registered but ZERO with "
                      "status=RUN. An empty or entirely-suppressed report cannot be "
                      "trusted as a clean one — fail-closed.")
            return 2

        stem = os.path.splitext(os.path.basename(path))[0]
        exe = next((c for c in (os.path.join(args.bin_dir, stem),
                                 os.path.join(args.bin_dir, stem + ".exe"))
                    if os.path.isfile(c)), None)
        if exe is None:
            gh_error(f"no binary for report '{path}' under '{args.bin_dir}'. Fail-closed.")
            return 2
        listed = listed_case_ids(exe)
        if not listed:
            gh_error(f"'{exe}' enumerates zero cases with --gtest_list_tests. Fail-closed.")
            return 2
        for cid in sorted(listed - report_ids):
            omitted_from_report.append((cid, stem))
        for cid in sorted(report_ids - listed):
            extra_in_report.append((cid, stem))

    if total_cases == 0:
        gh_error(f"{len(json_files)} JSON report(s) present but ZERO test cases were "
                  "found across all of them. An empty scan cannot be trusted as a "
                  "clean one — fail-closed.")
        return 2

    violations = []

    if omitted_from_report:
        violations.append(
            f"{len(omitted_from_report)} case(s) enumerated but absent from the run report "
            "(a filter, shard, wrapper, or launcher omitted them): "
            + ", ".join(f"{stem}:{cid}" for cid, stem in omitted_from_report))

    if extra_in_report:
        violations.append(
            f"{len(extra_in_report)} case(s) reported but not enumerated by the binary "
            "under --bin-dir (a stale or wrong --bin-dir): "
            + ", ".join(f"{stem}:{cid}" for cid, stem in extra_in_report))

    if failed_cases:
        violations.append(
            f"{len(failed_cases)} case(s) FAILED (must be zero for this gate): "
            + ", ".join(sorted(failed_cases)))

    if bad_status_cases:
        detail = "; ".join(f"{cid} (status={status!r}, result={result!r})"
                            for cid, status, result in bad_status_cases)
        violations.append(
            f"{len(bad_status_cases)} case(s) did not run with a real result (status must "
            f"be RUN and result must be COMPLETED or SKIPPED): {detail}")

    if never_started:
        violations.append(
            f"{len(never_started)} case(s) reported RUN but never started (epoch "
            "timestamp — e.g. a GTEST_SKIP() in a global test environment): "
            + ", ".join(f"{cid} ({ts})" for cid, ts in never_started))

    if disabled_violations:
        detail = "; ".join(f"{path}:{level}=disabled({n})"
                            for path, level, n in disabled_violations)
        violations.append(
            f"{len(disabled_violations)} nonzero `disabled` count(s) (must be zero): "
            f"{detail} — a nonzero disabled count means gtest excluded a DISABLED_ case "
            "outright, which this gate cannot inspect.")

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
