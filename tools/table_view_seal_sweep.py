#!/usr/bin/env python3
"""Measure the compile-time blast radius of the fixpp#456 `table_view` seal.

Writes a SIMULATED sealed `table_view.hpp` into a scratch include root (the
sixteen mutators moved behind `private:` plus `friend class table_view_builder;`,
both assignment operators `= delete`, the `is_nothrow_move_assignable_v`
static_assert dropped), puts that root FIRST on the include path, and re-runs
every TU in a configured build's `compile_commands.json` with `-fsyntax-only
-ferror-limit=0`.

    python3 tools/table_view_seal_sweep.py \
        --root <library> --build build/linux-clang-debug \
        --filter /src/ --arm differential --scratch /abs/path/to/scratch

ARMS
  --arm control       identical command WITHOUT the scratch root
  --arm sealed        the measurement
  --arm differential  runs both and prints the SET DIFFERENCE (sealed minus
                      control). The differential is this tool's output, not
                      arithmetic performed in prose afterwards.

WHY THIS SCRIPT IS SHAPED THE WAY IT IS — every guard below exists because the
corresponding failure was OBSERVED, and each one fails LOUD rather than clean:

  1. `--scratch` is resolved with `os.path.abspath`, and a scratch include root
     that does not exist or is empty is a hard failure. TUs are compiled with
     `cwd=<build dir>`, so a RELATIVE `--scratch` resolves against the build
     directory and points at nothing — and clang ignores a nonexistent `-I`
     SILENTLY. The sealed arm then degrades into a bit-for-bit rerun of the
     control and reports zero errors. The control pairing cannot catch that: a
     degraded sealed arm agrees with the control perfectly, which is exactly
     what the pairing checks for.

  2. The sealed arm ASSERTS a seeded-`#error` shadow witness before it measures
     anything. A number produced by a run that did not shadow the header is not
     emitted at all: "0 errors" and "the overlay never reached the include path"
     are otherwise the same observation.

     ⚠️ The control arm runs the same check for the OPPOSITE outcome, and that
     half is weaker on purpose — say what it is. The control injects no `-I`, so
     the token is unreachable and the assertion cannot fail on any *input*. It is
     a SCRIPT-level regression guard, not evidence about a run: it fires if the
     `-I` injection ever becomes unconditional, which would silently make the
     control a second sealed arm and drive the differential to zero. Read it as
     a guard on this file, never as two-sided evidence for a measurement.

  3. The witness TU is chosen by a dependency scan, and a filter that selects no
     TU including `table_view.hpp` is a hard failure — a vacuous sweep cannot
     report a plausible zero.

  4. `EXPECTED_CONTROL_FAILURES` binds the repo's three must-fail
     negative-compile probes to their intended diagnostics. The control arm
     passes on IDENTITY, not on count: a listed probe that fails differently, a
     listed probe that stops failing, and any unlisted control failure are all
     fatal. In the sealed arm a listed probe carrying any diagnostic other than
     its bound one is fatal too, so a probe that additionally acquired a seal
     error cannot be absorbed by a subtraction.

  5. `-ferror-limit=0` is load-bearing. clang stops at 19 by default, so every
     heavily-hit TU reports the SAME plausible number and the census becomes a
     ceiling wearing the costume of a count.

  6. The scratch root must PRECEDE the build's own `-I`. It is injected ahead of
     every flag the compile database carries.

  7. EVERY compilation carries its RETURN CODE, and the classifier is not
     allowed to decide alone. `rc == 0` with no classified `": error:"`
     diagnostic is the ONLY clean state. `rc != 0` with nothing classified is
     fatal, printing the return code and the head of stderr — that is a driver
     `fatal error:` (whose ` error:` is preceded by a SPACE, not by a colon, so
     the classifier's pattern cannot see it), a signal death, or an ICE. `rc ==
     0` with something classified is fatal too: the two halves disagree and a
     census must not choose between them. The dependency probe in
     `pick_witness()` checks `rc` for the same reason — a failed `-MM` reads as
     "this TU does not include table_view.hpp" otherwise, which moves the
     witness silently and can raise the WRONG fatal ("this sweep is vacuous")
     for a broken probe.

SCOPE. This is the PRE-implementation bill instrument: it simulates the seal by
rewriting a header that is not yet sealed. Once the seal has actually landed the
anchors below are gone and the script HARD-FAILS with a named message rather
than silently producing a control-shaped run. Validating a completed migration
is the job of an ordinary full build of the migrated tree, not of this script.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

HEADER_REL = "include/fixpp/dict/table_view.hpp"
SHADOW_TOKEN = "FIXPP_456_SEAL_SWEEP_SHADOW"

# The repo's must-fail negative-compile probes: their PASSING state is a failed
# compile (`WILL_FAIL TRUE` in tests/tls/CMakeLists.txt). Each is bound to the
# diagnostic it is supposed to produce, so the control arm is checked by
# identity rather than by count.
EXPECTED_CONTROL_FAILURES = {
    "tests/tls/cipher_policy_banned_negative.cpp":
        "CBC token must be detected as banned",
    "tests/tls/security_profile_deprecated_negative.cpp":
        "'one_way_ca' is deprecated",
    "tests/session/security_profile_insecure_plain_tcp_deprecated_negative.cpp":
        "'insecure_plain_tcp' is deprecated",
}


class Fatal(Exception):
    """Every guard raises this. Nothing in this script fails toward clean."""


ERR_RE = re.compile(r": error:")

# An ordinary clang compile failure exits 1. ANY other non-zero status carrying
# classified diagnostics is an abnormal termination — a signal death (negative rc),
# an ICE (commonly 254), or a driver bail-out — and the diagnostics it produced are
# TRUNCATED at the point it died. fixpp#456 Gate A round 3 (456-R3-1): guard 7 used
# to test `if errs:` BEFORE the return code, so any abnormal exit that had already
# emitted one `file:line:col: error:` was accepted as a normal compilation failure.
ORDINARY_FAILURE_RCS = {1}

# Markers that mean "this compiler run did not finish", ANYWHERE in stderr — not
# only in the head. `clang: fatal error:` is the one the narrow ERR_RE cannot see
# (the boundary is the colon: ` error:` there is preceded by a space, not `:`).
ABNORMAL_RE = re.compile(
    r"clang: error: unable to execute|"
    r"fatal error:|"
    r"internal compiler error|"
    r"PLEASE submit a bug report|"
    r"Stack dump:|"
    r"Segmentation fault|"
    r"cc1plus: out of memory|"
    r"Killed")


def stderr_head(text, n=6):
    """The first n non-blank stderr lines, for a diagnostic nothing classified."""
    return "\n".join([l for l in text.split("\n") if l.strip()][:n]) or "(stderr empty)"


def classify(r):
    """Return (rc, classified errors) for one finished compilation.

    The classifier's pattern is deliberately narrow — it matches the compiler's
    per-location `file:line:col: error:` form. It does NOT match a driver-level
    `clang: fatal error:`, and it cannot match a signal death or an ICE at all.
    That is why the return code travels with it: see guards 7 and 8. Callers must
    treat all THREE of these as failures of the INSTRUMENT, never as a verdict
    about the TU:

      * `rc != 0 and not errs`                     — guard 7 (died reporting nothing)
      * `rc == 0 and errs`                         — guard 7's contradiction arm
      * `errs and (rc not in {1} or crash marker)` — guard 8 (died reporting SOMETHING)

    The third was missing until fixpp#456 Gate A round 3, and it is the one the
    other two structurally cannot reach: they ask whether a failure went
    unreported, and here it IS reported — just truncated.
    """
    return r.returncode, [l for l in r.stderr.split("\n") if ERR_RE.search(l)]


def sealed_header_text(src_path, seed_error=False):
    """Return the simulated-sealed header. Raise Fatal if the source moved."""
    lines = open(src_path, encoding="utf-8").read().split("\n")

    copy_assign = [k for k, s in enumerate(lines)
                   if s.strip().startswith("table_view& operator=(table_view const& other)")]
    move_assign = [k for k, s in enumerate(lines)
                   if s.strip() == "table_view& operator=(table_view&&) = default;"]
    banner = [k for k, s in enumerate(lines)
              if s.strip().startswith("// ── Build-time population surface")]

    missing = [n for n, got in (("copy-assignment definition", copy_assign),
                                ("move-assignment declaration", move_assign),
                                ("population-surface banner", banner)) if len(got) != 1]
    if missing:
        raise Fatal(
            "SOURCE HEADER DOES NOT MATCH THE SIMULATION ANCHORS: "
            + ", ".join(missing)
            + ".\n  Either the header moved, or the seal has ALREADY LANDED. This script "
              "simulates a seal that is not yet written; it must not run against a sealed "
              "tree, where it would produce a control-shaped run and a misleading zero. "
              "Validate a completed migration with a full build, not with this sweep.")

    # The three anchors are guarded above with `s.strip()`, which is indentation-
    # insensitive on purpose. Re-finding one afterwards by an EXACT four-space
    # spelling was not: a header whose indentation changed passed the guard and
    # then died on an uncaught ValueError -- the one exit path in this file that
    # bypassed `Fatal`, whose contract is that nothing here fails toward clean.
    # So each anchor is resolved ONCE, above, and used directly.
    #
    # ⚠️ THE ORDER IS THE OTHER HALF OF THE FIX. The copy-assignment splice
    # replaces 7 lines with 1, so applying these in file order would shift the two
    # indices resolved against the ORIGINAL line numbering. They sit in ascending
    # file order, so applying HIGHEST INDEX FIRST means no edit can move an index a
    # later edit still needs.
    if not copy_assign[0] < move_assign[0] < banner[0]:
        raise Fatal(
            "SIMULATION ANCHORS ARE NOT IN THE EXPECTED FILE ORDER "
            f"(copy-assignment {copy_assign[0]}, move-assignment {move_assign[0]}, "
            f"banner {banner[0]}).\n  The splices below are applied highest-index-first "
            "so that none shifts another; that is only shift-free while the anchors "
            "ascend. Re-derive the order before editing this block.")

    lines[banner[0]:banner[0]] = ["private:", "    friend class table_view_builder;"]
    lines[move_assign[0]] = "    table_view& operator=(table_view&&) = delete;"
    # the hand-written strong-guarantee copy-assignment body is 7 lines
    lines[copy_assign[0]:copy_assign[0] + 7] = [
        "    table_view& operator=(table_view const&) = delete;"]
    lines = [s for s in lines if "is_nothrow_move_assignable_v<table_view>" not in s]

    if seed_error:
        lines.insert(0, "#error " + SHADOW_TOKEN)
    return "\n".join(lines)


def write_scratch_header(root, inc, seed_error=False):
    out = os.path.join(inc, "fixpp/dict/table_view.hpp")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = sealed_header_text(os.path.join(root, HEADER_REL), seed_error)
    open(out, "w", encoding="utf-8").write(text)
    if not os.path.isfile(out) or os.path.getsize(out) == 0:
        raise Fatal(f"SCRATCH HEADER MISSING OR EMPTY after write: {out}")
    return out


def entries(build, filt):
    db_path = os.path.join(build, "compile_commands.json")
    if not os.path.isfile(db_path):
        raise Fatal(f"NO COMPILE DATABASE at {db_path} — configure the preset first.")
    db = json.load(open(db_path, encoding="utf-8"))
    seen, out = set(), []
    for e in db:
        f = e["file"]
        if filt not in f or f in seen:
            continue
        seen.add(f)
        cmd = e.get("command") or " ".join(e["arguments"])
        parts, keep, skip = shlex.split(cmd), [], False
        for p in parts:
            if skip:
                skip = False
                continue
            if p == "-o":
                skip = True
                continue
            if p.startswith("@") and p.endswith(".modmap"):
                # Dropping the module map is a semantic no-op only while this tree
                # contains no C++20 modules, and THIS is what keeps that true:
                # every modmap CMake emits here is 0 bytes. A non-empty one means
                # the TU genuinely needs it, and silently dropping it would
                # mis-parse that TU -- measuring the wrong thing rather than
                # failing. Same guard, for the same reason, as sanitize_db() in
                # tools/reconcile_co_spawn_census.py.
                mp = p[1:]
                if not os.path.isabs(mp):
                    mp = os.path.join(e["directory"], mp)
                if os.path.exists(mp) and os.path.getsize(mp) > 0:
                    raise Fatal(
                        f"{mp} IS {os.path.getsize(mp)} BYTES, NOT EMPTY. This tree has "
                        "started using C++20 modules and dropping the module map would "
                        "mis-parse that TU. Teach this script to keep it (and build the "
                        "modmaps first) rather than removing this check.")
                continue
            if p == "-c":
                continue
            keep.append(p)
        out.append((f, keep, e["directory"]))
    if not out:
        raise Fatal(f"FILTER {filt!r} SELECTED NO TU — a sweep over nothing reports zero.")
    return sorted(out)


def compile_cmd(cmd, inc, sealed):
    """Scratch root FIRST, ahead of every -I the compile database carries."""
    head = [cmd[0], "-fsyntax-only", "-ferror-limit=0"]
    return head + (["-I" + inc] if sealed else []) + cmd[1:]


def pick_witness(ents, inc, sealed):
    """First swept TU that actually includes table_view.hpp, by dependency scan.

    A filter selecting no such TU makes the whole sweep vacuous, so that is
    fatal rather than a clean zero.
    """
    for f, cmd, d in ents:
        full = compile_cmd(cmd, inc, sealed) + ["-MM", "-MF", "-"]
        full = [x for x in full if x != "-fsyntax-only"]
        r = subprocess.run(full, cwd=d, capture_output=True, text=True)
        if r.returncode != 0:
            raise Fatal(
                f"DEPENDENCY PROBE FAILED on {f} (rc={r.returncode}). A failing `-MM` "
                "produces no dependency list, which this loop would otherwise read as "
                "'this TU does not include table_view.hpp' — moving the witness silently, "
                "and raising the WRONG fatal (a vacuous sweep) if every probe fails.\n"
                "  stderr head:\n    " + stderr_head(r.stderr).replace("\n", "\n    "))
        if "fixpp/dict/table_view.hpp" in r.stdout:
            return (f, cmd, d)
    raise Fatal("NO SWEPT TU INCLUDES fixpp/dict/table_view.hpp — this sweep is "
                "vacuous and any zero it printed would be meaningless.")


def assert_shadow(ents, root, inc, sealed):
    """The sealed arm must SEE the seeded token. Unconditional, not a --self-test
    flag: a witness that can be skipped is the procedure nobody runs.

    The control arm asserts the token is ABSENT. That half cannot fail on any
    input — the control injects no `-I`, so the token is unreachable — and it is
    therefore a guard on THIS FILE (it fires if the `-I` injection ever becomes
    unconditional), not evidence about the run. Labelled as such in the output so
    it is not read as two-sided proof.
    """
    wf, wcmd, wd = pick_witness(ents, inc, sealed)
    rel = wf.replace(root + "/", "")
    write_scratch_header(root, inc, seed_error=True)
    try:
        r = subprocess.run(compile_cmd(wcmd, inc, sealed), cwd=wd,
                           capture_output=True, text=True)
        seen = SHADOW_TOKEN in r.stderr
    finally:
        write_scratch_header(root, inc, seed_error=False)

    if sealed and not seen:
        raise Fatal(
            f"SHADOW NOT WITNESSED. The scratch header at {inc} did not reach {rel}, "
            "so the sealed arm compiled the REAL header and its number would be the "
            "control's. Check that --scratch is absolute and that the include root exists.")
    if not sealed and seen:
        raise Fatal(
            f"CONTROL ARM IS SHADOWED. {rel} picked up the scratch header, so the control "
            "is not a control and the differential would collapse toward zero. This is a "
            "guard on this script: the usual cause is the `-I` injection in compile_cmd() "
            "having become unconditional.")
    if sealed:
        print(f"  shadow witness [sealed]: token OBSERVED in {rel} "
              f"— the measurement is against the SEALED header")
    else:
        print(f"  shadow guard [control]: token ABSENT in {rel} "
              f"— script-level only (no -I injected, so this cannot fail on input)")


def audit_manifest(arm, bad, rcs, swept_rel):
    """Check the negative-compile probes by IDENTITY. Returns nothing; raises."""
    problems = []
    for probe, diag in EXPECTED_CONTROL_FAILURES.items():
        if probe not in swept_rel:
            continue                        # not selected by this --filter
        errs = bad.get(probe)
        if not errs:
            problems.append(f"{probe}: expected to FAIL and did not — it has stopped "
                            f"being a must-fail probe (bound diagnostic: {diag!r})")
            continue
        # A must-fail probe passes on BOTH halves: its bound diagnostic AND a
        # non-zero exit. A diagnostic without a failed compile is a warning
        # wearing an error's spelling.
        if rcs.get(probe, 0) == 0:
            problems.append(f"{probe}: carries its bound diagnostic but the compiler "
                            f"exited 0 — its passing state is a FAILED compile")
        off = [e for e in errs if diag not in e]
        if off:
            problems.append(f"{probe}: {len(off)} diagnostic(s) other than its bound "
                            f"{diag!r} — first: {off[0].strip()[:160]}")
    if arm == "control":
        for f in sorted(bad):
            if f not in EXPECTED_CONTROL_FAILURES:
                problems.append(f"{f}: control failure not in the expected-failure "
                                f"manifest ({len(bad[f])} error(s)) — the harness is broken")
    if problems:
        raise Fatal("EXPECTED-FAILURE MANIFEST VIOLATED in the %s arm:\n  - %s"
                    % (arm, "\n  - ".join(problems)))


def run_arm(arm, root, ents, inc, jobs):
    sealed = (arm == "sealed")
    assert_shadow(ents, root, inc, sealed)

    def one(t):
        f, cmd, d = t
        r = subprocess.run(compile_cmd(cmd, inc, sealed), cwd=d,
                           capture_output=True, text=True)
        rc, errs = classify(r)
        # Search the WHOLE stderr, not stderr_head: a crash banner can follow
        # several diagnostics and would fall outside the six-line head.
        return f, rc, errs, stderr_head(r.stderr), bool(ABNORMAL_RE.search(r.stderr))

    bad, rcs, tot = {}, {}, 0
    unclassified, contradictory, abnormal = [], [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for f, rc, errs, head, marker in ex.map(one, ents):
            rel = f.replace(root + "/", "")
            if errs:
                bad[rel], rcs[rel] = errs, rc
                tot += len(errs)
                if rc == 0:
                    contradictory.append((rel, len(errs), head))
                elif rc not in ORDINARY_FAILURE_RCS or marker:
                    abnormal.append((rel, rc, len(errs), marker, head))
            elif rc != 0:
                unclassified.append((rel, rc, head))

    # Guard 7, BEFORE any figure is printed. A TU that died without a classified
    # diagnostic never enters `bad`: in the control arm audit_manifest's
    # "any unlisted control failure is fatal" rule would never see it, and in the
    # sealed arm it would be absent from the set difference, so the bill — and
    # `--emit-set` with it — would be UNDERCOUNTED. Neither is a weak check; both
    # are unreachable without the return code.
    if unclassified:
        raise Fatal(
            "COMPILATION FAILED WITH NO CLASSIFIED DIAGNOSTIC in the %s arm — a "
            "driver-level `fatal error:`, a signal death or an ICE. The classifier "
            "matches `\": error:\"` only, so this TU would otherwise have been counted "
            "CLEAN:\n  - %s" % (arm, "\n  - ".join(
                f"{rel} rc={rc}\n      " + head.replace("\n", "\n      ")
                for rel, rc, head in unclassified)))
    if contradictory:
        raise Fatal(
            "DIAGNOSTIC CLASSIFIED BUT THE COMPILER EXITED 0 in the %s arm — the "
            "return code and the classifier disagree, and a census must not choose "
            "between them:\n  - %s" % (arm, "\n  - ".join(
                f"{rel} {n} classified error(s), rc=0\n      " + head.replace("\n", "\n      ")
                for rel, n, head in contradictory)))

    # Guard 8 (fixpp#456 Gate A round 3, 456-R3-1). Guard 7 above catches an
    # abnormal exit that classified NOTHING. This catches the other half — an
    # abnormal exit that classified SOMETHING, which guard 7 cannot see because
    # `if errs:` is tested first and an ordinary failure is also non-zero.
    #
    # ⚠️ Guard 7 is a forced-MISS arm and STRUCTURALLY CANNOT catch this: it asks
    # "did a failure go unreported?", and here the failure IS reported — a crash
    # after one diagnostic still lands the TU in `bad`. The condition being watched
    # is satisfied by something OTHER than the thing it was written for, which is
    # the spurious HIT. Every mutant written for guard 7 forces a miss, so the
    # mutants for THIS guard must force a hit: an abnormal rc (or a crash banner)
    # arriving WITH a classified diagnostic.
    #
    # The cost is truncation: a run that died part-way emitted only the diagnostics
    # it reached, so the per-TU error count — and the `total errors` figure summed
    # from it — is short by an unknown amount, and a must-fail probe can satisfy
    # its manifest entry on a bound diagnostic it emitted before dying.
    if abnormal:
        raise Fatal(
            "ABNORMAL TERMINATION CARRYING CLASSIFIED DIAGNOSTICS in the %s arm — the "
            "compiler did not finish, so its diagnostics are TRUNCATED and every count "
            "derived from them is short. An ordinary compile failure exits %s:\n  - %s"
            % (arm, "/".join(str(c) for c in sorted(ORDINARY_FAILURE_RCS)),
               "\n  - ".join(
                   f"{rel} rc={rc} with {n} classified error(s)"
                   f"{' and a crash/fatal marker in stderr' if marker else ''}\n      "
                   + head.replace("\n", "\n      ")
                   for rel, rc, n, marker, head in abnormal)))

    swept_rel = {f.replace(root + "/", "") for f, _, _ in ents}
    audit_manifest(arm, bad, rcs, swept_rel)

    print(f"  arm={arm}  TUs={len(ents)}  TUs with errors: {len(bad)}  total errors: {tot}")
    for f in sorted(bad):
        note = "  [expected: must-fail probe]" if f in EXPECTED_CONTROL_FAILURES else ""
        # The rc travels with every failing TU so the published bill is auditable
        # from its own face. Round 3's severity call needed an rc audit and the
        # script could not supply one: it recorded rcs and printed none of them.
        print(f"    {f:<62} {len(bad[f])}  rc={rcs[f]}{note}")
    return bad, tot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="the library checkout")
    ap.add_argument("--build", required=True, help="configured build dir, relative to --root")
    ap.add_argument("--filter", default="/src/", help="substring match on the TU path")
    ap.add_argument("--arm", choices=["sealed", "control", "differential"], required=True)
    ap.add_argument("--scratch", required=True,
                    help="scratch directory; resolved to an ABSOLUTE path (see guard 1)")
    ap.add_argument("--emit-set",
                    help="write the failing-TU set here, one per line: the set difference under "
                         "--arm differential, or that arm's own failures (minus the must-fail "
                         "probes) under --arm sealed/control")
    ap.add_argument("-j", type=int, default=12)
    a = ap.parse_args()

    root = os.path.abspath(a.root)
    scratch = os.path.abspath(a.scratch)          # guard 1
    inc = os.path.join(scratch, "inc")
    write_scratch_header(root, inc)
    if not os.path.isdir(inc) or not os.listdir(inc):
        raise Fatal(f"SCRATCH INCLUDE ROOT IS MISSING OR EMPTY: {inc}")
    print(f"scratch include root (absolute): {inc}")

    ents = entries(os.path.join(root, a.build), a.filter)
    print(f"filter={a.filter}  TUs={len(ents)}")

    if a.arm != "differential":
        bad, _ = run_arm(a.arm, root, ents, inc, a.j)
        if a.emit_set:
            # Never hand a migration the must-fail negative-compile probes: their
            # failure is their purpose, and "migrating" them is the exact mistake
            # the control arm exists to prevent.
            out = sorted(set(bad) - set(EXPECTED_CONTROL_FAILURES))
            open(a.emit_set, "w", encoding="utf-8").write("\n".join(out) + "\n")
            print(f"  emit-set: {len(out)} TUs "
                  f"({len(bad) - len(out)} must-fail probe(s) withheld)")
        return 0

    cbad, _ = run_arm("control", root, ents, inc, a.j)
    sbad, _ = run_arm("sealed", root, ents, inc, a.j)
    only = sorted(set(sbad) - set(cbad))
    gone = sorted(set(cbad) - set(sbad))
    if gone:
        raise Fatal("TUs FAILING IN THE CONTROL BUT NOT SEALED: " + ", ".join(gone)
                    + " — the two arms are not the same command.")
    # Summed over the set difference, NOT `stot - ctot`: a by-count subtraction is
    # exactly what the expected-failure manifest exists to replace.
    diff_err = sum(len(sbad[f]) for f in only)
    print(f"\n  DIFFERENTIAL (set difference, sealed minus control): "
          f"{len(only)} TUs  {diff_err} errors")
    for f in only:
        print(f"    {f:<62} {len(sbad[f])}")
    if a.emit_set:
        open(a.emit_set, "w", encoding="utf-8").write("\n".join(only) + "\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fatal as e:
        print(f"\n!! {e}", file=sys.stderr)
        sys.exit(2)
