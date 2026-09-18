#!/usr/bin/env python3
"""run-interop-live — run the live interop cells on a fixpp PR (fixpp#468).

    ci/run-interop-live.py --harness DIR --build-root DIR [--only CELL]... [--list]

#431 made the interop skips VISIBLE and pinned: a named step on each tier asserts the
exact set of cases that skip when no counterparty is leased, and their one allowed
reason. It did not make them RUN. Those cases run only under the parent harness, which
leases a port and starts a QuickFIX counterparty per cell, so a fixpp PR learned about a
live-interop regression only after it merged and its gitlink was bumped.

This driver closes that, using only the PUBLIC GHCR counterparties image: the image
already bundles `run_interop_cell.py`, the config templates, the parent goldens and the
TLS generator (ci/counterparties.Dockerfile), and interop-smoke.yml already proves the
layout can be recreated without checking out the private parent repo. This runs the
whole live set instead of one smoke cell.

WHAT IT REFUSES TO DO
---------------------
Fail open. Three ways the job could report success while testing less than it claims,
each closed here BEFORE any cell starts:

1. A cell that does not run. `skip:*` is a FAILURE here. interop-smoke tolerates a skip
   by design -- it is the light tier and a missing counterparty must not gate a PR
   (FR-023) -- and that shape must not be inherited, because on THIS job the
   counterparty is guaranteed present and a skip means the cell did not do its job.
2. A population that quietly shrinks. The set of cases some cell selects, plus the
   checked-in exclusions, must EQUAL #431's skip set, both directions.
3. A harness older or newer than the tree it is testing. The image carries a harness
   SNAPSHOT while `cell_results.yaml`, the goldens and the binaries come from the PR
   head. Skew is refused rather than absorbed.

[const XV.9]: tests/CI-only. Standard library plus the bundled harness module.
"""
from __future__ import annotations

import argparse
import importlib.util
import pathlib
import shutil
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SKIP_SET = REPO / "tests" / "interop" / "expected-skips-without-counterparty.txt"
EXCLUSIONS = REPO / "tests" / "interop" / "live-cells-excluded.txt"
# The authority live-cells-excluded.txt names for WHICH engine-capability reason
# applies to an excluded id. Read, not paraphrased -- see authority_tags().
CELL_RESULTS = REPO / "tests" / "interop" / "cell_results.yaml"
# The reason a case does not run here. The first two mean "this engine cannot drive
# this scenario" for two DIFFERENT mechanisms, and the vocabulary is the template's
# own (`cell_results.yaml`'s `matrix_disposition: deferred:*`) rather than a parallel
# one invented here — so the authority for WHICH applies to an id is that row, not a
# sentence in the exclusion file. Both share one structural condition, checked below.
SIBLING_COVERED_TAGS = ("qfj-only-at-g1", "qfcpp-no-possdup-injection")
REASON_TAGS = SIBLING_COVERED_TAGS + ("unregistered-tracked",)


def fail(msg: str) -> None:
    print(f"::error::{msg}")


def load_harness(harness_dir: pathlib.Path):
    """Import the run_interop_cell module the IMAGE shipped, not a local copy.

    The image is the authority on what the counterparty can do: its jar and its driver
    were built together. Importing a parent checkout's copy here would be the version
    skew this script exists to refuse, committed by the script itself."""
    path = harness_dir / "tools" / "run_interop_cell.py"
    # is_file(), not exists(): a DIRECTORY of that name passes exists() and then dies
    # inside importlib with an unhandled loader error -- a traceback where this
    # function's whole job is to report a named refusal (Codex r1 P2-e).
    if not path.is_file():
        fail(f"bundled harness driver not found at {path} -- the image layout changed, "
             f"or --harness is wrong. Refusing to run.")
        sys.exit(2)
    # Everything importlib can raise goes inside the guard, spec construction included.
    #
    # ⚠️ `except Exception` is NOT enough, and this is the interesting half: a bundled
    # driver whose module body reaches `sys.exit(0)` raises SystemExit, which derives
    # from BaseException, so it sails past `except Exception` and TERMINATES THIS
    # PROCESS WITH STATUS 0. Both entry points would then succeed having done nothing:
    # --list-binaries prints no targets, and the run phase exits before reconciling or
    # running a single cell. A green job that ran zero cells is the exact defect this
    # driver exists to make impossible, so the import refusal has to cover it.
    # KeyboardInterrupt and the rest of BaseException are deliberately NOT caught --
    # an operator's ^C is not an unusable image.
    #
    # BOUND, stated rather than chased: `os._exit()` in a module body cannot be caught
    # by anything, here or elsewhere. The workflow's `test -n "$targets"` is what
    # survives that one; see the derive step.
    try:
        spec = importlib.util.spec_from_file_location("ric_bundled", path)
        if spec is None or spec.loader is None:
            raise ImportError(f"no import spec for {path}")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    except (Exception, SystemExit) as e:         # noqa: BLE001 - any import failure
        # A bundled driver that will not import is an unusable image, not a crash of
        # this script: report it as the refusal it is, so the job log names the image
        # rather than showing a traceback through importlib.
        fail(f"the bundled harness at {path} failed to import "
             f"({type(e).__name__}: {e}) -- the image is unusable; republish it.")
        sys.exit(2)
    # ⚠️ Refused, not worked around. The inter-cell settle is a RELIABILITY property
    # (acceptor cells flake on port/process teardown when run back-to-back -- see
    # INTER_CELL_SETTLE_S in that module), and an image predating it would run every
    # cell with no settle and produce flakes indistinguishable from real failures.
    # Falling back to a local default would put the value in two places, which is
    # the duplication that lost it here in the first place.
    #
    # callable(), not hasattr(): `settle_between_cells = None` satisfies hasattr and
    # then raises mid-run, which is the compatibility refusal firing as a crash.
    if not callable(getattr(mod, "settle_between_cells", None)):
        fail(f"the bundled harness at {path} has no CALLABLE settle_between_cells "
             f"(fixpp#468) -- republish the counterparties image before running the "
             f"live cells. Running without the settle reintroduces a known "
             f"acceptor-cell flake.")
        sys.exit(2)
    return mod


def authority_tags(path: pathlib.Path) -> dict[str, str]:
    """{cell id -> the tag in its `matrix_disposition: deferred:<tag>`}, read from
    tests/interop/cell_results.yaml.

    This is the file live-cells-excluded.txt NAMES as the authority for which
    engine-capability reason applies to an id, so the tag is CHECKED against it rather
    than trusted. No vocabulary is written down here: whatever the row says after
    `deferred:` is the answer, so a new tag needs no change to this checker and a
    checker-local copy of the tag list cannot drift from the authority (the recorded
    failure in `every_spelling_was_found_by_reading`).

    ⚠️ The authority is keyed by CELL id and the exclusion file by GTEST id, and it
    does NOT carry a row for every excluded case -- see the caller, which is explicit
    about covering only the ids it can resolve rather than pretending to cover all."""
    import yaml
    doc = yaml.safe_load(path.read_text())
    rows = doc["cells"] if isinstance(doc, dict) and "cells" in doc else doc
    out = {}
    for row in rows:
        disposition = str(row.get("matrix_disposition", ""))
        if disposition.startswith("deferred:"):
            out[str(row["id"])] = disposition.split(":", 1)[1]
    return out


def read_ids(path: pathlib.Path) -> list[str]:
    if not path.exists():
        fail(f"{path} is missing -- refusing to run: an absent population file would "
             f"make every reconciliation below pass vacuously.")
        sys.exit(2)
    return [l.strip() for l in path.read_text().splitlines()
            if l.strip() and not l.lstrip().startswith("#")]


def read_exclusions(path: pathlib.Path) -> dict[str, tuple[str, str | None]]:
    """{gtest id -> (reason tag, cell id or None)}.

    The optional THIRD field is a pointer into tests/interop/cell_results.yaml, the
    file this list names as the authority for which engine-capability reason applies.
    It is optional because the template carries no deferred row for every excluded
    param -- but where it IS given it is VERIFIED (reconcile step 4), so a dangling or
    mis-pointed cell id fails rather than reading as "nothing to check"."""
    out: dict[str, tuple[str, str | None]] = {}
    for line in read_ids(path):
        parts = line.split()
        if len(parts) not in (2, 3) or parts[0] not in REASON_TAGS:
            fail(f"malformed exclusion line (want '<reason-tag> <gtest-id> [<cell-id>]', "
                 f"tag one of {'/'.join(REASON_TAGS)}): {line!r}")
            sys.exit(2)
        tag, gid = parts[0], parts[1]
        cid = parts[2] if len(parts) == 3 else None
        if tag == "unregistered-tracked" and cid is not None:
            fail(f"{gid} is unregistered-tracked yet names cell {cid}. That tag means no "
                 f"cell selects the case on ANY engine, so a cell id here contradicts it.")
            sys.exit(2)
        if gid in out:
            fail(f"duplicate exclusion for {gid}")
            sys.exit(2)
        out[gid] = (tag, cid)
    return out


def base(gid: str) -> str:
    """'Prefix/Suite.Case/param' -> 'Prefix/Suite.Case'. A value-parameterized case's
    siblings differ only in the trailing param."""
    return gid.rsplit("/", 1)[0] if gid.count("/") >= 2 else gid


def reconcile(cells: dict | None, skip_set: list[str],
              exclusions: dict[str, tuple[str, str | None]],
              cell_results: pathlib.Path | None = None) -> int:
    """Every named invariant, checked before a single cell runs. Returns an exit code.

    `cells is None` runs only the checks that need NOTHING but this repository -- the
    OFFLINE subset. That split is not cosmetic: the full reconciliation needs
    `ric.CELLS`, which lives in the harness bundled inside the GHCR image, so it can
    only run on a lane that pulls the image. The buildless `ci-script-pins` lane does
    not, and an arm that cannot run where it is wired is not an arm (this script's
    own first draft wired exactly that and would have gone red on tier1)."""
    rc = 0
    skip = set(skip_set)

    if not skip or not exclusions:
        fail(f"refusing to run against an empty population (skip set {len(skip)}, "
             f"exclusions {len(exclusions)}) -- a reconciliation over nothing succeeds "
             f"for the wrong reason.")
        return 2

    # ── OFFLINE: needs only this repository ──────────────────────────────────────
    absent = sorted(g for g in exclusions if g not in skip)
    if absent:
        fail(f"{len(absent)} exclusion(s) name a case that is not in the skip set at all "
             f"(renamed or deleted test?). " + ", ".join(absent[:5]))
        rc = 1

    if cells is None:
        if rc == 0:
            print(f"offline checks passed: {len(exclusions)} exclusion(s), all present in "
                  f"the {len(skip)}-id skip set. The covered-vs-excluded equality needs the "
                  f"image's harness and runs in the live job.")
        return rc

    # ── FULL: needs the harness registry from the image ──────────────────────────
    covered = {c.gtest_filter for c in cells.values()}
    if not cells:
        fail("the bundled registry holds ZERO cells -- refusing to run.")
        return 2

    # (1) SKEW. Every registered cell's filter must be an id this tree's skip set knows.
    # A harness whose filters have drifted from the library's test names -- renamed
    # suite, renamed param, a cell added against a newer tree -- lands here. That is the
    # image/tree version check, expressed against real names rather than a version pin
    # that someone has to remember to bump.
    stray = sorted(f for f in covered if f not in skip)
    if stray:
        fail(f"{len(stray)} registered cell filter(s) name a gtest case this tree's skip "
             f"set does not contain. The image's harness and this checkout disagree; "
             f"republish the image or rebase. " + ", ".join(stray[:5]))
        rc = 1

    # (2) COVERAGE, both directions.
    unaccounted = sorted(skip - covered - set(exclusions))
    if unaccounted:
        fail(f"{len(unaccounted)} interop case(s) neither run here nor are excluded with "
             f"a reason. Add a cell, or add a line to live-cells-excluded.txt saying why not. "
             + ", ".join(unaccounted[:5]))
        rc = 1

    stale = sorted(g for g in exclusions if g in covered)
    if stale:
        fail(f"{len(stale)} exclusion(s) name a case that a cell now RUNS -- delete the "
             f"line. An exclusion outliving its reason is how a population silently "
             f"stops meaning anything. " + ", ".join(stale[:5]))
        rc = 1

    # (3) The per-group STRUCTURAL conditions the exclusion file states. Without these
    # the file is a free pass: anything could be filed under any tag and the equality in
    # (2) would still hold.
    for gid, (tag, _cid) in sorted(exclusions.items()):
        sibling_covered = any(c != gid and base(c) == base(gid) for c in covered)
        if tag in SIBLING_COVERED_TAGS and not sibling_covered:
            fail(f"{gid} is filed {tag}, but NO param of {base(gid)} is covered by any "
                 f"cell. That is not 'this engine cannot drive this scenario', it is "
                 f"'this scenario runs nowhere' -- file it unregistered-tracked with "
                 f"an issue.")
            rc = 1
        if tag == "unregistered-tracked" and sibling_covered:
            fail(f"{gid} is filed unregistered-tracked, but {base(gid)} IS covered for "
                 f"another param -- the debt was partly paid and the entry must be "
                 f"re-argued, not inherited.")
            rc = 1

    # (4) AUTHORITY. For every excluded id whose CELL the template dispositions as
    # `deferred:<tag>`, that tag must be the one the exclusion file wrote down.
    #
    # ⚠️ WHY THIS EXISTS: (3)'s structural condition is IDENTICAL for both sibling
    # tags, so the two are freely interchangeable as far as it is concerned -- rename
    # any `qfcpp-no-possdup-injection` line to `qfj-only-at-g1` and everything above
    # stays green while the file states something false. (3) cannot see it; only the
    # authority can.
    #
    # ⚠️ AND ITS BOUND, stated because a partial check read as total is worse than
    # none: the template carries a deferred row only for cells it KNOWS, and several
    # excluded gtest params have no row at all -- their engine-capability claim rests
    # on the test source, not on this file. Those ids are counted and REPORTED below,
    # never silently treated as checked. Re-derive which they are; do not read a
    # number here.
    authority = cell_results or CELL_RESULTS
    tags = authority_tags(authority) if authority.exists() else {}
    checked, unbacked = 0, []
    for gid, (tag, cid) in sorted(exclusions.items()):
        if tag == "unregistered-tracked":
            continue                      # (3) re-derives this one in full
        if cid is None:
            unbacked.append(gid)
            continue
        if cid not in tags:
            fail(f"{gid} names cell {cid}, which {authority.name} does not disposition "
                 f"as deferred:* at all. A pointer that resolves to nothing is worse than "
                 f"no pointer -- it reads as checked. Fix the id, or drop the field.")
            rc = 1
            continue
        checked += 1
        if tags[cid] != tag:
            fail(f"{gid} is filed '{tag}', but {authority.name} dispositions its cell "
                 f"{cid} as 'deferred:{tags[cid]}'. The exclusion file names that template "
                 f"row as the authority for which reason applies -- fix the tag, or fix "
                 f"the row, but they cannot disagree.")
            rc = 1
    print(f"exclusion tags: {checked} checked against {authority.name}, "
          f"{len(unbacked)} carry no deferred row there and rest on the test source"
          + (": " + ", ".join(unbacked) if unbacked else ""))

    print(f"reconciled: {len(covered)} case(s) run, {len(exclusions)} excluded, "
          f"{len(skip)} in the skip set")
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--harness",
                    help="the recreated phase-9-harness dir extracted from the image. "
                         "Omit it for --offline-checks, which is the only mode that can "
                         "run on a lane without the image.")
    ap.add_argument("--build-root",
                    help="fixpp build root holding <preset>/bin/. Required only when cells "
                         "actually run (not for --offline-checks / --list-binaries / --list).")
    ap.add_argument("--config", default="normal")
    # Overridable ONLY so ci/test-run-interop-live.sh can drive each refusal below
    # against a fixture. CI passes neither, so the job always reconciles the real
    # population -- a checker whose inputs are always synthetic checks nothing.
    ap.add_argument("--skip-set", default=str(SKIP_SET))
    ap.add_argument("--exclusions", default=str(EXCLUSIONS))
    ap.add_argument("--cell-results", default=str(CELL_RESULTS),
                    help="the template whose deferred:* dispositions are the authority "
                         "for an exclusion's reason tag (reconcile step 4).")
    ap.add_argument("--only", action="append", default=[],
                    help="run just this cell id (repeatable). Reconciliation still runs "
                         "in full -- the population check is not scoped by --only.")
    ap.add_argument("--list", action="store_true", help="reconcile and list, run nothing")
    ap.add_argument("--offline-checks", action="store_true",
                    help="run only the checks that need nothing but this repository, "
                         "then exit. For the buildless ci-script-pins lane.")
    ap.add_argument("--list-binaries", action="store_true",
                    help="print the build targets the registered cells need, space-"
                         "separated, and exit. The live workflow derives its build list "
                         "with this instead of re-implementing the module load inline.")
    args = ap.parse_args()

    skip_set = read_ids(pathlib.Path(args.skip_set))
    exclusions = read_exclusions(pathlib.Path(args.exclusions))

    if args.offline_checks:
        if args.harness:
            fail("--offline-checks does not take --harness: the point of the mode is that "
                 "it needs no image. Drop one of the two so it is unambiguous which set "
                 "of checks ran.")
            return 2
        return reconcile(None, skip_set, exclusions)

    if not args.harness:
        fail("--harness is required unless --offline-checks is given.")
        return 2
    harness = pathlib.Path(args.harness).resolve()
    ric = load_harness(harness)

    if args.list_binaries:
        bins = sorted({c.binary for c in ric.CELLS.values()})
        if not bins:
            fail("the bundled registry named ZERO binaries -- refusing to build nothing.")
            return 2
        print(" ".join(bins))
        return 0

    rc = reconcile(ric.CELLS, skip_set, exclusions,
                   pathlib.Path(args.cell_results))
    if rc:
        fail("population reconciliation FAILED -- not running any cell. Whatever this "
             "job would have reported could not have been trusted.")
        return rc

    cells = [(cid, c) for cid, c in ric.CELLS.items() if not args.only or cid in args.only]
    if args.only:
        unknown = sorted(set(args.only) - set(ric.CELLS))
        if unknown:
            fail(f"--only names unknown cell(s): {', '.join(unknown)}")
            return 2
    if args.list:
        for cid, c in cells:
            print(f"{cid:44} {c.counterparty:13} {c.gtest_filter}")
        return 0

    if not args.build_root:
        fail("--build-root is required to run cells.")
        return 2
    build_root = pathlib.Path(args.build_root).resolve()
    failures: list[tuple[str, str]] = []
    for idx, (cid, cell) in enumerate(cells, start=1):
        run_dir = harness / "results" / cid
        # BEFORE each cell but the first, never after — so a cell that RAISED still
        # gets the drain before the next one starts. That is the case that needs it
        # most: a cell which blew up mid-run is the likeliest to have left a
        # counterparty process or a bound port behind. (An `after` placement past a
        # `continue` would skip exactly then.)
        if idx > 1:
            ric.settle_between_cells()  # see INTER_CELL_SETTLE_S in the harness module
        # ⚠️ Cleared BEFORE the cell, so the evidence witness below cannot be satisfied
        # by a PREVIOUS run's artefacts. Without this the check is sound only on a fresh
        # workspace -- true of the CI lane, false locally and on a re-run, and "sound
        # only where nobody re-runs it" is the shape of an instrument that fails toward
        # clean. Found by the witness's own no-evidence arm passing for the wrong reason.
        shutil.rmtree(run_dir, ignore_errors=True)
        print(f"[{idx}/{len(cells)}] {cid}", flush=True)
        try:
            res = ric.run_cell(cell, args.config, build_root, run_dir,
                               keep=True, update_goldens=False, arm=cell.arm)
        except Exception as e:                       # noqa: BLE001 - a raise is a failure
            failures.append((cid, f"raised: {type(e).__name__}: {e}"))
            continue
        status = res.get("status", "<none>")
        detail = res.get("_detail", "")
        # ⚠️ EXECUTION WITNESS. `status: pass` is the harness's SAY-SO, and this job
        # consumes a mutable `:latest` image, so the thing being trusted is exactly the
        # thing that can drift. A run_cell that launched no binary at all -- a broken or
        # rolled-back image -- reports pass and leaves the run dir empty, and every
        # check above is about the POPULATION, not about whether a cell ran. So the one
        # artefact this driver owns is checked: it passes run_dir in, so it is entitled
        # to require the cell to have written something there.
        #
        # This is deliberately a LIVENESS check, not a content one: asserting particular
        # files would be a second copy of the harness's own output contract, and the
        # thing it must catch (nothing ran) is visible without one.
        if status == "pass" and not (run_dir.is_dir() and any(run_dir.iterdir())):
            failures.append((cid, f"reported '{status}' but wrote NOTHING to {run_dir} -- "
                                  f"a cell that ran leaves evidence; this one did not, so "
                                  f"the pass is unsupported. Suspect a stale image."))
            print(f"    {status}  (NO EVIDENCE) {detail}", flush=True)
            continue
        # ⚠️ A skip is a FAILURE here. See the module docstring: interop-smoke tolerates
        # one because its counterparty may legitimately be absent; on this job it is
        # guaranteed present, so a skip means the cell did not run what it claims to.
        if status != "pass":
            failures.append((cid, f"{status}: {detail}"))
        print(f"    {status}  {detail}", flush=True)

    if failures:
        for cid, why in failures:
            fail(f"{cid}: {why}")
        fail(f"{len(failures)} of {len(cells)} live interop cell(s) did not pass.")
        return 1
    # ⚠️ --only runs a SUBSET while reconcile() above certified the WHOLE registry, so
    # its success says nothing about the cells it did not run. Saying "all N passed"
    # there would be true of the subset and read as true of the job. The CI lane never
    # passes --only; this wording is what stops a local subset run being pasted into a
    # PR as evidence.
    if args.only:
        print(f"SUBSET ONLY: {len(cells)} of {len(ric.CELLS)} cell(s) ran and passed. "
              f"This is NOT a live-matrix result and must not be reported as one.")
        return 0
    print(f"all {len(cells)} live interop cell(s) passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
