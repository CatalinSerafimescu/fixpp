#!/usr/bin/env python3
r"""Second instrument for the #354 named-closure census — a SET diff, not a count.

WHY A SECOND INSTRUMENT AT ALL
------------------------------
#354 states the population as "~190 sites", measured with

    callExpr(callee(functionDecl(hasName("co_spawn"))),
             hasAnyArgument(cxxOperatorCallExpr(hasOverloadedOperatorName("()"))))

through clang-query. That matcher, in clang-query's DEFAULT traversal mode,
reports **0 matches on a file containing 3 real sites** (measured on
tests/sync/test_fifo_fairness.cpp): implicit nodes — materialize-temporary,
construct-expr, casts — sit between the co_spawn argument and the operator call,
and only `set traversal IgnoreUnlessSpelledInSource` sees through them.

So the issue's own census figure came from an instrument that CAN report a false
low, and reconciling against it would either falsely reassure or send someone
hunting a phantom gap. It is re-derived here instead, twice, by two instruments
that share no code:

  * tools/audit_co_spawn_named_closure.py — libclang, walks the AST itself, and
    additionally resolves the closure VARIABLE and its scope.
  * this script — clang-query with the corrected traversal, parsing its diagnostic
    output.

⚠️ A COUNT IDENTITY IS NOT AGREEMENT. Two instruments can report the same total
while disagreeing about which sites those are — one over-matching by N and
under-matching by N. This compares SITE SETS keyed on (file, line) and prints
both differences separately. Equal counts with a non-empty symmetric difference is
a FINDING, not a pass.

⚠️ NEITHER SIDE IS THE ORACLE. A disagreement means one of them is wrong and the
site must be read. clang-query's matcher is over-inclusive by design here — it
matches ANY `operator()` call in an argument, including one on a non-closure
functor — while the libclang walker requires the callee to resolve to a VarDecl of
lambda type. So clang-query >= libclang is the EXPECTED direction, and every extra
must be inspected rather than assumed benign; libclang > clang-query is the
alarming direction and means the matcher is missing a shape.
"""

from __future__ import annotations

import argparse
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys

MATCHER = (
    'callExpr(callee(functionDecl(hasName("co_spawn"))), '
    'hasAnyArgument(cxxOperatorCallExpr(hasOverloadedOperatorName("()"))))'
)
LOC_RE = re.compile(r'^(/[^:]+):(\d+):(\d+): note: "root" binds here', re.MULTILINE)


def _one_file(job: tuple) -> tuple:
    cq, build_dir, f, timeout = job
    cmd = [
        cq, "-p", build_dir, f,
        "-c", "set traversal IgnoreUnlessSpelledInSource",
        "-c", "set output diag",
        "-c", f"match {MATCHER}",
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return (f, [], "timeout")
    out = r.stdout + r.stderr
    # ⚠️ FAIL CLOSED. clang-query prints "0 matches." both for a clean file and for
    # one it could not compile, and a compile error here is severity-error rather
    # than fatal — so it does NOT stop the run. A file that errored is UNSEEN, not
    # clean. This is the same trap that bit the libclang walker (which originally
    # bailed only on Fatal) and it is the reason both instruments check for it.
    if "error:" in out and "0 matches" in out:
        return (f, [], "compile error(s) — file is UNSEEN, not clean")
    if "matches." not in out and "match." not in out:
        return (f, [], "no match tally in output")
    return (f, [(os.path.realpath(m.group(1)), int(m.group(2))) for m in LOC_RE.finditer(out)], None)


def run_clang_query(cq: str, build_dir: str, files: list[str], timeout: int,
                    jobs: int) -> tuple[set, list]:
    sites: set[tuple[str, int]] = set()
    failures: list[tuple[str, str]] = []
    done = 0
    with cf.ProcessPoolExecutor(max_workers=jobs) as pool:
        for f, found, err in pool.map(
            _one_file, [(cq, build_dir, x, timeout) for x in files], chunksize=1
        ):
            done += 1
            if err:
                failures.append((f, err))
            sites.update(found)
            if done % 20 == 0:
                print(f"  ... {done}/{len(files)} files, {len(sites)} sites", file=sys.stderr)
    return sites, failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--build-dir", default="build/linux-clang-debug")
    ap.add_argument("--clang-query", default="/opt/llvm22/bin/clang-query")
    ap.add_argument("--audit-json", required=True, help="--json-out from the libclang walker")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()

    with open(args.audit_json, encoding="utf-8") as fh:
        audit = json.load(fh)
    lib_sites = {(os.path.realpath(s["file"]), s["line"]) for s in audit["sites"]}

    # ⚠️ THE POPULATION COMES FROM THE AUDIT RUN, NEVER RE-DERIVED HERE.
    # A set-diff is only sound if both instruments saw the same files. This script
    # used to rebuild the list from compile_commands.json with a hand-copied
    # equivalent of the audit's own filter -- which silently diverged the moment
    # the audit was run with `--filter`, putting every site outside the filter into
    # the "only clang-query" bucket whose entire purpose is that each entry gets
    # hand-inspected. `--limit` here did the mirror-image damage, manufacturing
    # "only libclang (ALARMING)" hits, which is the signal reserved for "the
    # matcher is missing a shape". Both readings look like findings and are
    # artefacts of the population, not the detectors.
    #
    # The independence this script claims is between the two DETECTORS -- a
    # clang-query matcher and a libclang walker, sharing no code. It was never
    # about the population, and sharing the population is what makes the diff mean
    # anything.
    if "files" not in audit:
        print(
            "ERROR: the audit JSON has no `files` key, so the population it scanned is\n"
            "  unknown and a set-diff against it would be meaningless. Re-run\n"
            "  tools/audit_co_spawn_named_closure.py --json-out <file> to produce one."
        )
        return 1
    files = [f for f in audit["files"] if os.path.exists(f)]
    missing = len(audit["files"]) - len(files)
    if missing:
        print(f"⚠️ {missing} file(s) from the audit population no longer exist — the audit "
              f"JSON is stale relative to the tree.")

    cq_sites, failures = run_clang_query(
        args.clang_query, args.build_dir, files, args.timeout, args.jobs
    )

    only_cq = sorted(cq_sites - lib_sites)
    only_lib = sorted(lib_sites - cq_sites)

    print(f"files queried              : {len(files)}")
    print(f"clang-query sites (line)   : {len(cq_sites)}")
    print(f"libclang walker sites      : {len(lib_sites)}")
    print(f"agreed                     : {len(cq_sites & lib_sites)}")
    print(f"only clang-query (expected): {len(only_cq)}")
    print(f"only libclang  (ALARMING)  : {len(only_lib)}")
    print(f"files clang-query could NOT see: {len(failures)}")

    for f, line in only_cq:
        print(f"  [only clang-query] {os.path.relpath(f)}:{line}")
    for f, line in only_lib:
        print(f"  [ONLY LIBCLANG]    {os.path.relpath(f)}:{line}")
    for f, why in failures[:15]:
        print(f"  [unseen by clang-query] {os.path.relpath(f)}: {why}")

    if not files or not cq_sites:
        print("ERROR: the reconciliation instrument reached nothing.")
        return 1

    # ⚠️ A DISAGREEMENT IS THE FINDING, SO IT MUST FAIL. This used to print the
    # buckets and exit 0 — including the one labelled ALARMING — so the whole
    # cross-check was advisory text no caller could act on. A set-diff whose exit
    # code cannot express "the sets differ" is not a cross-check.
    #
    # `only clang-query` is expected-but-not-benign: that matcher is deliberately
    # over-inclusive (any operator() call in an argument), so each entry needs a
    # human look. It fails for that reason, not because it is wrong.
    bad = 0
    if only_lib:
        print(f"\nERROR: {len(only_lib)} site(s) seen ONLY by the libclang walker — the "
              f"clang-query matcher is missing a shape.")
        bad = 1
    if only_cq:
        print(f"\nERROR: {len(only_cq)} site(s) seen ONLY by clang-query. Each must be "
              f"inspected; the matcher is over-inclusive by design, so these are not "
              f"automatically benign.")
        bad = 1
    if failures:
        print(f"\nERROR: {len(failures)} file(s) clang-query could not see. UNSEEN, not clean.")
        bad = 1
    if missing:
        print(f"\nERROR: {missing} file(s) in the audit population no longer exist.")
        bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main())
