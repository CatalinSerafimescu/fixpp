#!/usr/bin/env python3
"""Assert the CI lane invariants this repo's mechanisms depend on (#300, #213).

    ci/assert-ci-lane-policy.py [<repo root>]

WHY THIS EXISTS

Two mechanisms landed whose CORRECTNESS TODAY was verified once, by hand, and
then pinned by nothing. A hostile review named both, and named them precisely:
the trees were complete, but "that completeness is nevertheless an unpinned
result". This repo's rule is that a result which nothing re-derives goes stale
silently — so each is turned into a check.

  1. #300 — every apt-backed install goes through ci/apt-guard.sh.
     The harness ci/test-apt-guard.sh tests the WRAPPER. It never looks at the
     callers, so adding one bare `sudo apt-get install ...` or one bare
     `llvm.sh <N> all` leaves every apt cell green while the "every apt-backed
     install is bounded" claim quietly becomes false. That is the same
     dead-call-site shape #299 exists to prevent, one layer out.

  2. #267 — the parallelism campaign stays on `workflow_dispatch`.
     `.github/workflows/parallelism-measure.yml` runs each named lane's suite
     THREE times, and on the slowest lane a single pass is most of an hour. One
     `push:` or `pull_request:` key added to its trigger block — by a copy-paste
     from another workflow, or by someone "making it run automatically" —
     multiplies the repo's CI bill without anything going red to say so. It is
     the one workflow here whose cost makes its TRIGGER a correctness property,
     and "we all know not to" is not a mechanism.

  3. #213 — the fuzz corpora are actually replayed somewhere.
     The corpus replays and their zero-registration FATAL_ERROR all live under
     `if(FIXPP_BUILD_FUZZ)`. Flipping the asan preset's value ON -> OFF does not
     trip any of them: the targets, the registrations and the guard simply stop
     being evaluated, and the lane returns to replaying zero seeds with every
     script gate still green. The guard cannot guard its own enabling flag.

EXIT
  0  every invariant holds
  1  at least one violated (each named, with the file that breaks it)
  2  the check could not run, or scanned nothing — an empty scan is an
     instrument failure here, not a pass
"""
import json
import re
import sys
import pathlib

# An apt-backed install, wherever it appears in a `run:` block.
#
# ⚠️ NO NEGATIVE LOOKBEHIND HERE, DELIBERATELY. A first version excluded the
# already-wrapped form in the pattern itself, which made every GUARDED site
# invisible to the scan: the census counted 11 where the tree has 18, and the
# wrapped sites were never actually verified as wrapped. The pattern must match
# EVERY site; whether it is guarded is then decided by looking for the wrapper on
# the line. A scanner that cannot see the passing cases cannot count them.
APT_INSTALL = re.compile(r"\bsudo apt-get install\b")
APT_UPDATE = re.compile(r"\bsudo apt-get update\b")
LLVM_SH = re.compile(r"\bsudo /tmp/llvm\.sh \d+ all\b")
GUARD = "ci/apt-guard.sh"

# The measurement campaign, and the only trigger its cost permits.
CAMPAIGN_WORKFLOW = "parallelism-measure.yml"
CAMPAIGN_TRIGGERS = {"workflow_dispatch"}

# Each campaign job mirrors a production tier job. A measurement of a
# differently-configured tree is a measurement of a suite that does not ship, so
# the job-level env has to track its source — see check_campaign_job_env.
CAMPAIGN_JOB_SOURCES = {
    "linux": ("tier1.yml", "linux"),
    "libcxx": ("tier3-libcxx.yml", "libcxx"),
    "windows": ("tier2.yml", "windows"),
}

# The lane that must build and replay the fuzz corpora, and the flag that does it.
FUZZ_PRESET = "linux-clang-asan"
# Set when the matrix-membership half actually ran; read by main()'s summary so
# a skipped half is never reported as a passing one.
MATRIX_CHECKED = False
FUZZ_FLAG = "FIXPP_BUILD_FUZZ"


def check_apt_callers(root, violations):
    """Every apt-backed invocation must be executed through the wrapper."""
    wf_dir = root / ".github" / "workflows"
    if not wf_dir.is_dir():
        print(f"::error::{wf_dir} is not a directory — the check could not run.")
        return None

    seen = 0
    for path in sorted(wf_dir.glob("*.yml")):
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("#"):
                continue                       # prose about a call is not a call
            for rx, what in ((APT_INSTALL, "sudo apt-get install"),
                             (APT_UPDATE, "sudo apt-get update"),
                             (LLVM_SH, "sudo /tmp/llvm.sh <N> all")):
                if not rx.search(line):
                    continue
                seen += 1
                if GUARD not in line:
                    violations.append(
                        f"UNGUARDED INSTALL: {path.name}:{n} runs `{what}` without "
                        f"{GUARD}. A bare apt-backed install has no bound: when a mirror "
                        f"is wedged the step does not fail, it HANGS, and burns the "
                        f"180-240 min JOB timeout with nothing red and nothing naming apt "
                        f"(#300). Wrap it: `{GUARD} <label> -- {what} ...`.")
    return seen


def check_fuzz_lane(root, violations):
    """The fuzz replays must be enabled on a lane that actually runs them."""
    presets_path = root / "CMakePresets.json"
    if not presets_path.is_file():
        print(f"::error::{presets_path} is missing — the check could not run.")
        return None
    presets = json.loads(presets_path.read_text(encoding="utf-8"))
    by_name = {p.get("name"): p for p in presets.get("configurePresets", [])}
    preset = by_name.get(FUZZ_PRESET)
    if preset is None:
        print(f"::error::no `{FUZZ_PRESET}` configure preset — this check's assumption "
              f"about which lane carries the fuzz replays is broken, so it cannot "
              f"report a meaningful result.")
        return None

    value = (preset.get("cacheVariables") or {}).get(FUZZ_FLAG)
    if str(value).upper() != "ON":
        violations.append(
            f"FUZZ REPLAYS DISABLED: CMakePresets.json's `{FUZZ_PRESET}` has "
            f"{FUZZ_FLAG}={value!r}, not ON. Every corpus replay, and the "
            f"zero-registration FATAL_ERROR that guards them, lives under "
            f"`if({FUZZ_FLAG})` — so turning this off does not trip any of them. It "
            f"silently returns the lane to replaying zero seeds while every script "
            f"gate stays green, which is the state #213 was filed about. The guard "
            f"cannot guard its own enabling flag; this check is what does.")

    # ...and the lane must be one the matrix actually runs, or the flag is moot.
    tier1 = root / ".github" / "workflows" / "tier1.yml"
    if tier1.is_file():
        try:
            import yaml
            doc = yaml.safe_load(tier1.read_text(encoding="utf-8"))
            matrix = (((doc.get("jobs") or {}).get("linux") or {})
                      .get("strategy", {}).get("matrix", {}).get("preset") or [])
            if FUZZ_PRESET not in matrix:
                violations.append(
                    f"FUZZ LANE NOT IN THE MATRIX: `{FUZZ_PRESET}` carries {FUZZ_FLAG}=ON "
                    f"but is not in tier1.yml's linux matrix, so nothing builds or replays "
                    f"the corpora in CI. The flag would be set on a lane that never runs.")
        except ImportError:
            print("::warning::PyYAML unavailable — skipped the matrix-membership half "
                  "of the fuzz-lane check. The preset-value half still ran.")
        else:
            global MATRIX_CHECKED
            MATRIX_CHECKED = True
    return 1


SCCACHE_PIN = re.compile(r"^\s*ver=(?P<ver>v[\d.]+)\s*$|^\s*sha256=(?P<sha>[0-9a-f]{64})\s*$",
                         re.M)


def sccache_pin(path):
    """The (version, digest) an `Install sccache` step pins, or None."""
    if not path.is_file():
        return None
    found = {}
    for m in SCCACHE_PIN.finditer(path.read_text(encoding="utf-8")):
        for key in ("ver", "sha"):
            if m.group(key):
                found.setdefault(key, m.group(key))
    return (found["ver"], found["sha"]) if len(found) == 2 else None


def check_sccache_pins(root, violations):
    """The sccache version+digest must agree wherever it is pinned.

    `parallelism-measure.yml` duplicates tier2.yml's `Install sccache` step —
    the repo has no composite actions, so the three tier workflows already
    duplicate their setup between themselves and this follows that convention.
    What does NOT follow is leaving a pinned SHA-256 in two files with nothing
    asserting they agree: a bump applied to one and not the other is silent, and
    the stale copy is whichever file the bumper was not looking at.

    Returns the number of pinning sites found.  ZERO IS A FAILURE, not a pass —
    if the step is renamed or the pin's shape changes, "0 sites, 0 mismatches"
    is indistinguishable from agreement.
    """
    wf = root / ".github" / "workflows"
    pins = {name: sccache_pin(wf / name)
            for name in ("tier2.yml", CAMPAIGN_WORKFLOW)
            if (wf / name).is_file()}
    pins = {k: v for k, v in pins.items() if v is not None}
    if len(pins) < 2:
        # One site is legitimate (the campaign may be retired); zero, or a site
        # whose pin no longer parses, is the check losing its subject.
        print(f"  sccache pin: {len(pins)} site(s) found "
              f"({', '.join(sorted(pins)) or 'none'}) — nothing to cross-check.")
        return len(pins)
    values = set(pins.values())
    if len(values) > 1:
        violations.append(
            "SCCACHE PIN DISAGREEMENT: " +
            "; ".join(f"{k} pins {v[0]} / {v[1][:12]}..." for k, v in sorted(pins.items())) +
            ". These are copies of one pinned download. A bump applied to one file and not the "
            "other is silent — the build still succeeds, on a different sccache than the lane "
            "it is supposed to mirror. Re-derive the digest by hand (download and hash out of "
            "band; the .sha256 sidecar from the same mutable release pins nothing) and update "
            "both.")
    else:
        ver, sha = values.pop()
        print(f"  sccache pin: {ver} / {sha[:12]}... agrees across {len(pins)} sites")
    return len(pins)


def check_campaign_job_env(root, violations):
    """Each campaign job must carry at least its source tier job's job-level env.

    ⚠️ WRITTEN BECAUSE THE OMISSION SHIPPED AND COST A DISPATCH. The campaign's
    `windows` job copied every STEP of tier2's faithfully and none of its
    job-level `env:`, so `ci/restore-sccache.sh` refused with "SCCACHE_DIR must
    be set (the workflow sets it job-wide)" — 20 minutes into a build, on the
    lane the campaign most needs. Production fidelity is not only about the step
    list, and "I copied it carefully" is the claim that failed.

    KEYS are the violation; differing VALUES are only disclosed. A measurement
    job legitimately differs in some values (its own cache directory, say), but
    a key present in production and absent here is the environment the shipping
    lane builds under simply not being applied.

    ⚠️ `env` ONLY — `permissions` IS DELIBERATELY NOT COMPARED, and extending
    this to it would redden a correct tree. The tier jobs take
    `packages: write` because they SAVE caches; the campaign restores and never
    saves, so it takes `packages: read` at workflow level. That narrowing is a
    STRONGER guarantee than the `save: false` inputs it backs up — a flag can be
    flipped by an edit, a missing token scope cannot — so the difference is the
    design, not drift.

    Returns True when a verdict was reached, False when it could not be — the
    caller must consume it, for the same reason as the trigger check.
    """
    wf_dir = root / ".github" / "workflows"
    campaign = wf_dir / CAMPAIGN_WORKFLOW
    if not campaign.is_file():
        print(f"  campaign job env: {CAMPAIGN_WORKFLOW} is not present — check stood down.")
        return True
    try:
        import yaml
    except ImportError:
        print("::warning::PyYAML unavailable — the campaign job-env check did NOT run.")
        return False

    try:
        campaign_doc = yaml.safe_load(campaign.read_text(encoding="utf-8"))
        mine = campaign_doc["jobs"]
    except (yaml.YAMLError, KeyError, TypeError) as exc:
        violations.append(f"CAMPAIGN JOB ENV UNREADABLE: {CAMPAIGN_WORKFLOW} ({exc!r}).")
        return True

    # ⚠️ EFFECTIVE env — workflow-level merged with job-level, on BOTH sides.
    # Comparing only the `jobs.<id>.env` mappings missed an entire tier of the
    # thing being compared: tier1.yml and tier3-libcxx.yml define `CONAN_HOME`
    # at WORKFLOW level, which every job in them inherits and which a job-level
    # comparison cannot see. The check reported that the campaign jobs "carry
    # their source tier job's env" while that variable was absent from them.
    def effective_env(doc, job_id):
        merged = dict(doc.get("env") or {})
        merged.update((doc.get("jobs") or {}).get(job_id, {}).get("env") or {})
        return merged

    # ⚠️ AN ADDED MEASUREMENT JOB WOULD NEVER BE CHECKED WITHOUT THIS. The loop
    # below iterates CAMPAIGN_JOB_SOURCES, not the workflow, so a renamed job
    # trips the UNCHECKABLE violation (loud, correct) while a FOURTH lane is
    # simply absent from the iteration and passes in silence — the repo's starred
    # shape: an assertion that proves nothing was LOST and cannot see something
    # ADDED. `plan` is excluded because it is the matrix builder, not a lane.
    unmapped = sorted(set(mine) - {"plan"} - set(CAMPAIGN_JOB_SOURCES))
    if unmapped:
        violations.append(
            f"CAMPAIGN JOB(S) WITH NO SOURCE LANE: {', '.join(unmapped)}. Every measurement job "
            f"must name the tier job whose environment it reproduces, or its environment is "
            f"unchecked — add it to CAMPAIGN_JOB_SOURCES. A job this check does not know about "
            f"is not a job this check passes.")

    checked = 0
    for job, (src_file, src_job) in CAMPAIGN_JOB_SOURCES.items():
        src_path = wf_dir / src_file
        if job not in mine or not src_path.is_file():
            violations.append(
                f"CAMPAIGN JOB ENV UNCHECKABLE: `{job}` or its source {src_file}:{src_job} is "
                f"missing, so the environment the measurement runs under cannot be compared with "
                f"the lane it describes. A renamed job must not silently stop this check.")
            continue
        try:
            src_doc = yaml.safe_load(src_path.read_text(encoding="utf-8"))
            src_doc["jobs"][src_job]          # presence check; env read below
        except (yaml.YAMLError, KeyError, TypeError) as exc:
            violations.append(f"CAMPAIGN JOB ENV UNCHECKABLE: {src_file}:{src_job} ({exc!r}).")
            continue
        want = effective_env(src_doc, src_job)
        got = effective_env(campaign_doc, job)
        checked += 1
        absent = sorted(set(want) - set(got))
        if absent:
            violations.append(
                f"CAMPAIGN JOB `{job}` IS MISSING JOB-LEVEL ENV its production lane sets: "
                f"{', '.join(absent)} (from {src_file}:{src_job}). The measurement would run under "
                f"a different environment than the lane it claims to describe — and at least one of "
                f"these is load-bearing: ci/restore-sccache.sh REFUSES without SCCACHE_DIR.")
        differing = sorted(k for k in set(want) & set(got) if str(want[k]) != str(got[k]))
        if differing:
            print(f"  campaign job env: `{job}` differs in value from {src_file}:{src_job} for "
                  f"{', '.join(differing)} — disclosed, not failed (a measurement job may "
                  f"legitimately use its own cache paths). Check each is deliberate.")
    if checked:
        # ⚠️ SAY WHAT WAS CHECKED, NOT WHAT ONE WISHES HAD BEEN. This read
        # "N job(s) carry their source tier job's env", which is stronger than
        # the test: only KEY PRESENCE is enforced, differing VALUES are disclosed
        # and allowed, and step-level env, `runs-on`, container and default-shell
        # settings are outside the comparison entirely.
        print(f"  campaign job env: {checked} job(s) define every env KEY their source tier "
              f"lane defines (workflow+job level, both sides). Values are disclosed above when "
              f"they differ, not enforced; step-level env and non-env job config are out of scope.")
    return True


def check_campaign_trigger(root, violations):
    """The A-B-A campaign must stay dispatch-only.

    Absence is NOT a violation: the campaign is explicitly a one-off and retiring
    it is a legitimate thing to do.  It IS disclosed, because a check that
    quietly reports clean over a subject that is not there is the failure mode
    every other file in this directory exists to remove.

    Returns True when a verdict was actually reached, False when the check could
    not run.  ⚠️ THE CALLER MUST CONSUME THIS. An earlier version returned a
    value nobody looked at, and with PyYAML unavailable the function warned and
    returned while `main()` printed **"ci lane policy: all invariants hold"** and
    exited 0 — over a tree whose campaign workflow was `push:`-triggered. A
    `::warning::` does not fail a job.

    That was not live only by step ordering: PyYAML reached this job from an
    UNRELATED earlier step in tier1.yml, so a reorder would have stood this
    invariant down silently.  The step now installs its own.

    ⚠️ `tools/check_workflows.py` is this repo's OTHER workflow-policy checker
    and independently solves the same bare-`on:`-is-`True` YAML trap (its
    `doc.get(True) or doc.get("on")`).  It takes the same line on missing
    PyYAML — refuse rather than warn.  They are deliberately NOT merged: that
    one runs only in `brain-freshness.yml`, this one in tier 1's
    `ci-script-pins`, so relocating either would weaken enforcement.
    """
    path = root / ".github" / "workflows" / CAMPAIGN_WORKFLOW
    if not path.is_file():
        print(f"  campaign trigger: {CAMPAIGN_WORKFLOW} is not present — check stood down "
              f"(retiring the campaign is legitimate; this is a disclosure, not a pass).")
        return True
    try:
        import yaml
    except ImportError:
        print("::warning::PyYAML unavailable — the campaign-trigger check did NOT run. "
              "Do not read this run as evidence that the campaign is still dispatch-only.")
        return False

    # ⚠️ A PARSE ERROR IS A VIOLATION, NOT A CRASH. Caught because a mutant
    # found it: the T8 cell of ci/test-ci-lane-policy.sh produced a workflow
    # whose YAML did not parse, and this function raised a traceback out of
    # `main()` instead of dispositioning it. An unparsable trigger block is
    # precisely the state where "it is dispatch-only" cannot be asserted, so it
    # has to fail closed and say why.
    try:
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        violations.append(
            f"CAMPAIGN TRIGGER UNREADABLE: {CAMPAIGN_WORKFLOW} does not parse as YAML "
            f"({exc.__class__.__name__}), so this check cannot say what triggers it — and "
            f"a workflow that does not parse does not run at all. Refusing to report it "
            f"dispatch-only.")
        return True
    # ⚠️ YAML 1.1 resolves a bare `on:` key to the BOOLEAN True, not the string
    # "on".  A check that looked up doc["on"] would find nothing, conclude there
    # were no triggers, and pass — silently, on every future version of the file.
    block = doc.get("on", doc.get(True))
    if block is None:
        violations.append(
            f"CAMPAIGN TRIGGER UNREADABLE: {CAMPAIGN_WORKFLOW} has no parsable `on:` block, so "
            f"this check cannot say what triggers it. Refusing to report it dispatch-only.")
        return True

    triggers = set(block) if isinstance(block, (dict, list)) else {str(block)}
    extra = sorted(triggers - CAMPAIGN_TRIGGERS)
    if extra:
        violations.append(
            f"CAMPAIGN IS NO LONGER DISPATCH-ONLY: {CAMPAIGN_WORKFLOW} triggers on "
            f"{', '.join(extra)} as well as workflow_dispatch. That workflow runs each named "
            f"lane's suite THREE times — on the slowest lane a single pass is most of an "
            f"hour — so an automatic trigger multiplies the CI bill with nothing going red "
            f"to say so. "
            f"It is a one-off campaign, not a standing job.")
    else:
        print(f"  campaign trigger: {CAMPAIGN_WORKFLOW} is {'/'.join(sorted(triggers))} only")
    return True


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    if not root.is_dir():
        print(f"::error::{root} is not a directory.")
        return 2

    violations = []
    apt_seen = check_apt_callers(root, violations)
    fuzz_seen = check_fuzz_lane(root, violations)
    campaign_judged = check_campaign_trigger(root, violations)
    campaign_judged = check_campaign_job_env(root, violations) and campaign_judged
    check_sccache_pins(root, violations)
    if apt_seen is None or fuzz_seen is None:
        return 2
    # A check that could not run must not be reported as one that passed.
    if not campaign_judged:
        # ⚠️ Names the FLAG, not one of its inputs. Two checks feed
        # `campaign_judged` (trigger and job-env); this said "the
        # campaign-trigger invariant", so a PyYAML-absent run — where it is the
        # job-env check that stands down — pointed the operator at a check that
        # had run fine.
        print("::error::a campaign invariant could not be evaluated (see the warning above): "
              "the trigger check, the job-env check, or both. Refusing to report "
              "`all invariants hold` over a check that did not run.")
        return 2

    # ⚠️ AN EMPTY SCAN IS AN INSTRUMENT FAILURE, NOT A PASS. If the workflows move
    # or the patterns stop matching, "0 violations over 0 sites" is
    # indistinguishable from a clean tree — the failure mode every check in this
    # directory exists to remove.
    if apt_seen == 0:
        print("::error::found ZERO apt-backed install sites across the workflows. Either "
              "they moved or this check's patterns are broken; refusing to report clean "
              "on an empty scan.")
        return 2

    print(f"  apt-backed install sites scanned: {apt_seen} (all must use {GUARD})")
    # ⚠️ The matrix-membership half is skipped when PyYAML is unavailable, so
    # this line must not assert it unconditionally — that would be a positive
    # result printed for a check that did not run.
    print(f"  fuzz lane: {FUZZ_PRESET} {FUZZ_FLAG}=ON"
          + (", in the tier1 linux matrix" if MATRIX_CHECKED else
             " (matrix membership NOT checked — PyYAML unavailable)"))

    if violations:
        for v in violations:
            print(f"::error::{v}")
        print(f"\nci lane policy: {len(violations)} violation(s).")
        return 1

    print("\nci lane policy: all invariants hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
