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

⚠️ VERDICT. A member that is registered, labelled and named can still be disabled, or have
a failure reported as a pass or a skip, by a CTest property. Every member is therefore
checked against `VERDICT_PROPERTIES`. A control's expected violation is inverted inside
check_alloc.py, never by CTest.
"""
import argparse
import collections
import json
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

# The positive controls, by NAME, each with the entry point set its command must name in
# `--expect-entry` and the allocation path it proves the
# interceptor sees. A control vouches only for the entry point it plants: the malloc
# control passed while every aligned entry point was unhooked (fixpp#497), so one control
# cannot stand in for another. Any label member whose name carries `positive_control` must
# be a row here, and every row must be registered.
#
# The entry set is SPELLED OUT here rather than read from the CMake registration, so
# dropping EXPECT_ENTRY there, or pointing it at another hook, is a failure here rather
# than a control that quietly accepts any violation.
POSITIVE_CONTROLS = {
    "alloc_guard_positive_control_mallocnesia": (
        "malloc", "a plain malloc (tests/alloc_guard/planted_alloc_witness.cpp)"),
    "alloc_guard_aligned_new_positive_control_mallocnesia": (
        "aligned_alloc,posix_memalign", "an over-aligned operator new, which reaches libc "
        "through an aligned entry point (tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_calloc_positive_control_mallocnesia": (
        "calloc", "calloc (tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_realloc_positive_control_mallocnesia": (
        "realloc", "realloc of a block allocated before the window "
                   "(tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_aligned_alloc_positive_control_mallocnesia": (
        "aligned_alloc", "aligned_alloc (tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_posix_memalign_positive_control_mallocnesia": (
        "posix_memalign", "posix_memalign (tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_memalign_positive_control_mallocnesia": (
        "memalign", "memalign (tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_valloc_positive_control_mallocnesia": (
        "valloc", "valloc (tests/alloc_guard/planted_entry_witness.cpp)"),
    "alloc_guard_pvalloc_positive_control_mallocnesia": (
        "pvalloc", "pvalloc (tests/alloc_guard/planted_entry_witness.cpp)"),
}

# fixpp#544 (B35)'s allocation gates: its zero cells, their arms and twins, and the
# pre-existing gates B35 re-shaped. They are #544's acceptance evidence, and the floor cannot
# keep them: it counts names, so once a later gate adds slack, or a padding gate replaces one,
# a dropped B35 gate passes it. So each is required BY NAME, spelled out like
# POSITIVE_CONTROLS rather than read from the registration. Re-derive the list on a Linux
# Release tree with `ctest --test-dir build/<preset> -N -L 544 | grep -o '[^ ]*_mallocnesia$'`.
# They register on Release build types only (design ruling R-4).
REQUIRED_GATES = frozenset({
    "alloc_guard_544_arm_a_tracked_executor_mallocnesia",
    "alloc_guard_544_arm_e_slot_held_mallocnesia",
    "alloc_guard_544_arm_s_run_one_for_mallocnesia",
    "alloc_guard_544_ch_arm_mallocnesia",
    "alloc_guard_544_ch_no_edit_twin_mallocnesia",
    "alloc_guard_544_ch_production_twin_mallocnesia",
    "alloc_guard_544_cl_arm_mallocnesia",
    "alloc_guard_544_cl_production_twin_mallocnesia",
    "alloc_guard_544_clp_no_edit_twin_mallocnesia",
    "alloc_guard_544_cp_arm_mallocnesia",
    "alloc_guard_544_veto_then_zero_mallocnesia",
    "alloc_guard_544_wa_active_heartbeat_mallocnesia",
    "alloc_guard_544_wa_bl_arm_mallocnesia",
    "alloc_guard_544_wa_bl_twin_mallocnesia",
    "alloc_guard_544_wa_bs_arm_mallocnesia",
    "alloc_guard_544_wa_bs_twin_mallocnesia",
    "alloc_guard_544_wb_active_app_message_mallocnesia",
    "alloc_guard_544_wd_bl_arm_mallocnesia",
    "alloc_guard_544_wd_bl_twin_mallocnesia",
    "alloc_guard_544_wd_bs_arm_mallocnesia",
    "alloc_guard_544_wd_bs_twin_mallocnesia",
    "alloc_guard_544_wdr_bracket_arm_mallocnesia",
    "alloc_guard_544_wdr_bracket_twin_mallocnesia",
    "alloc_guard_544_wdr_store_live_read_mallocnesia",
    "alloc_guard_544_wdw_real_chain_arm_mallocnesia",
    "alloc_guard_544_wdw_real_chain_twin_mallocnesia",
    "alloc_guard_544_wdw_stand_in_arm_mallocnesia",
    "alloc_guard_544_wdw_stand_in_twin_mallocnesia",
    "alloc_guard_544_wdw_stand_in_window_mallocnesia",
    "alloc_guard_544_we_sends_interleaved_mallocnesia",
    "perf_session_recovery_heartbeat_mallocnesia",
    "perf_store_alloc_guard_wd_mallocnesia",
    "session_refresh_on_logon_w8_mallocnesia",
})

# CTest properties under which a test whose command fails is reported as passed or
# skipped, or is not run at all. On a label member any of them turns `ctest -L mallocnesia`
# green over a broken gate or control: a broken control exits 1, and SKIP_RETURN_CODE 1
# reports it skipped. FAIL_REGULAR_EXPRESSION and TIMEOUT only add failures, so they
# are not here.
VERDICT_PROPERTIES = ("DISABLED", "WILL_FAIL", "SKIP_RETURN_CODE",
                      "SKIP_REGULAR_EXPRESSION", "PASS_REGULAR_EXPRESSION")

NAME_RE = re.compile(r"^\s*Test\s+#\d+:\s+(\S+)", re.M)


def ctest_names(build_dir: str, selector: str, value: str) -> set[str]:
    out = subprocess.run(["ctest", "--test-dir", build_dir, "-N", selector, value],
                         capture_output=True, text=True)
    if out.returncode != 0:
        print(f"error: ctest -N {selector} {value} failed rc={out.returncode}\n{out.stderr}",
              file=sys.stderr)
        sys.exit(2)
    return set(NAME_RE.findall(out.stdout))


def ctest_label_tests(build_dir: str, label: str) -> list[dict]:
    out = subprocess.run(["ctest", "--test-dir", build_dir, "--show-only=json-v1",
                          "-L", label], capture_output=True, text=True)
    if out.returncode != 0:
        print(f"error: ctest --show-only=json-v1 -L {label} failed rc={out.returncode}\n"
              f"{out.stderr}", file=sys.stderr)
        sys.exit(2)
    return json.loads(out.stdout).get("tests", [])


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
    # because ADDING a gate must not need a CI edit. So it sees a count below it, not
    # which gates are present: that is (0b) and (0b').
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
            "they would pass vacuously (run this against a Linux Release tree, "
            "linux-clang-release or linux-gcc-release); "
            "(b) the gates are not registered on this lane, the failure fixpp#448 "
            "removed; (c) the naming convention moved.")
    if not by_label:
        failures.append(
            "ZERO tests carry the `mallocnesia` label, so `ctest -L mallocnesia` would "
            "exit 0 having run NOTHING. That is the shape of a green CI step that "
            "measures nothing.")

    # (0b) THE POSITIVE CONTROLS MUST EXIST, each by name. The floor above counts NAMES,
    # and a name is cheap: `add_test(NAME padding_mallocnesia COMMAND cmake -E true)` with
    # the label satisfies the count, both set relations and the raw-preload scan, while a
    # real gate has been deleted. The count cannot tell a gate from a decoy — but a
    # control is a member whose ABSENCE means nobody is checking that interception works
    # for its entry point, so each is named here rather than left to arithmetic.
    controls = {n for n in by_label if "positive_control" in n}
    missing_controls = sorted(set(POSITIVE_CONTROLS) - controls)
    if missing_controls:
        failures.append(
            "positive control(s) MISSING from the `mallocnesia` label: "
            + ", ".join(f"{n} ({POSITIVE_CONTROLS[n][1]})" for n in missing_controls)
            + ". A control is the only member that fails when interception of its entry "
              "point silently stops working; without it '0 failed' is equally consistent "
              "with a clean tree and a blind interceptor. It must carry the label so it "
              "cannot be run separately from the gates it vouches for.")
    undeclared_controls = sorted(controls - set(POSITIVE_CONTROLS))
    if undeclared_controls:
        failures.append(
            "UNDECLARED positive control(s) in the `mallocnesia` label: "
            + ", ".join(undeclared_controls)
            + ". Add a POSITIVE_CONTROLS row naming the allocation path it proves, or "
              "rename it: a control nobody declared is one nobody will notice losing.")

    # (0b') B35'S GATES MUST EXIST, each by name and in the label. The floor sees a population
    # that shrinks; only this sees WHICH gates are in it.
    for kind, have in (("by name", by_name), ("from the `mallocnesia` label", by_label)):
        missing = sorted(REQUIRED_GATES - have)
        if missing:
            failures.append(
                f"fixpp#544 (B35) gate(s) MISSING {kind}: " + ", ".join(missing)
                + ". They are #544's acceptance evidence, and the floor cannot see a gate "
                  "replaced by another of the same count. They register on Release build "
                  "types only (R-4): on a Debug tree run this against linux-gcc-release or "
                  "linux-clang-release instead. If the removal is deliberate, delete the row "
                  "from REQUIRED_GATES in the same commit and say why.")

    # (0c) DUPLICATES BEFORE COMMAND LOOKUP. The command pin below reads one command by
    # name, so a duplicate can stand in for a different test with the same name.
    label_tests = ctest_label_tests(args.build_dir, "mallocnesia")
    dup_counts = collections.Counter(t["name"] for t in label_tests)
    duplicates = sorted(n for n, c in dup_counts.items()
                        if c > 1 and (n in by_name or n in POSITIVE_CONTROLS))
    if duplicates:
        failures.append(
            "duplicate test name(s) in the mallocnesia label: " + ", ".join(duplicates)
            + ". The entry pin reads one command per name, so a duplicate's command can "
              "stand in for another's.")

    # (0d) EACH CONTROL NAMES ITS OWN HOOK. --expect-violation accepts any violation, so a
    # control whose plant reaches a different hook (a memalign row that calls calloc)
    # passes; `--expect-entry <fn>` is what ties it to the entry point it vouches for.
    commands = {t["name"]: t.get("command", []) for t in label_tests}
    for name in sorted(set(POSITIVE_CONTROLS) & controls):
        entry = POSITIVE_CONTROLS[name][0]
        cmd = commands.get(name, [])
        named = [cmd[i + 1] for i in range(len(cmd) - 1) if cmd[i] == "--expect-entry"]
        if named != [entry]:
            failures.append(
                f"positive control {name} does not require its own entry point: its "
                f"command names --expect-entry {named or 'nothing'}, wanted exactly "
                f"[{entry!r}]. Without it the control passes on an allocation that reached "
                f"any hook. Register it with EXPECT_ENTRY {entry}.")

    # (0e) NO MEMBER MAY HAVE ITS VERDICT ALTERED. Every rule above reads names and
    # commands, which a property leaves intact: a DISABLED control is still registered,
    # labelled and named, and is never run. The controls' expected failure is inverted
    # inside check_alloc.py (--expect-violation), so no member needs any of these.
    for t in label_tests:
        altered = sorted(p["name"] for p in t.get("properties", [])
                         if p["name"] in VERDICT_PROPERTIES)
        if altered:
            failures.append(
                f"{t['name']} carries {', '.join(altered)}. With it CTest can report the "
                f"test passed or skipped although its command failed, or not run it, so "
                f"`ctest -L mallocnesia` stays green over a broken gate. A control's "
                f"expected violation is inverted by check_alloc.py --expect-violation; "
                f"remove the property.")

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
