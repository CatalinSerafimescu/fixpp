#!/usr/bin/env python3
"""Assert the ccache hit-floor opt-in is wired consistently at its CALL SITES.

    ci/assert-ccache-floor-callers.py [<workflow-dir>]

WHY THIS EXISTS (#299)

`ci/ccache-stats.sh` grew an optional 4th argument — a hit-rate floor — and for
a long time NOT ONE CALLER PASSED IT. The script was fully implemented and
thoroughly self-tested; the defect lived entirely in the callers, one argument
away from being used. A lane whose compiler cache silently regressed to a 0 %
hit rate was green, and stayed green, forever.

So the script's own harness could not have caught this, and cannot catch its
return: every cell there passes the floor explicitly. Only a check that reads
the WORKFLOWS can.

THE RULE THIS PINS — a condition, not a tally

The floor in ccache-stats.sh is fatal only when argument 2 (the restore
disposition) is the literal 'true'. That gate is what keeps a legitimate cold or
seeding run green. Two failure directions follow, and this file pins both:

  * A call site that CAN supply a restore disposition but passes NO floor is
    the #299 defect: the assertion exists and nothing invokes it.
  * A call site that CANNOT supply one but DOES pass a floor is the same defect
    wearing a fix's clothing: the floor can never evaluate, so it reports
    "NOT evaluated" every run while looking enforced in the diff.

⚠️ "CAN SUPPLY" IS A STRUCTURAL CLAIM, NOT A NON-EMPTY STRING.

This is the subtle half. A first version of this check treated any non-empty
argument 2 as a usable disposition. That is wrong in a way that reproduces the
very defect being fixed: `"${{ steps.nonexistent.outputs.hit }}"` is non-empty
in the YAML and resolves to the EMPTY STRING at run time, so the floor is
permanently inert while the check calls the site consistent. The literal-`''`
form is only the most visible member of that family.

GitHub expressions cannot be resolved statically, so this does not try. It
checks the actual PRECONDITION instead, which is structural: a restore
disposition comes from `ci/restore-ccache.sh`'s `hit` output or from nowhere. So
argument 2 must be one of

  * an empty literal                       -> the lane has no disposition, and
                                              must therefore pass no floor; or
  * `${{ steps.<id>.outputs.hit }}` where <id> names a step IN THE SAME JOB that
    invokes ci/restore-ccache.sh AND carries no `if:` -> a real disposition.

Anything else is refused rather than guessed at, because a disposition this
check cannot trace is one nobody can rely on. Two refusals are worth naming,
because a hostile review defeated an earlier version through each:

  * A LITERAL `true` / `false` is refused. It is not a measurement: `false`
    exempts the lane from its floor on every run whatever the cache did, and
    `true` asserts a hit nobody observed. Hardcoding `false` and dropping the
    floor was accepted as "consistent" before this.
  * A CONDITIONAL producer is refused. A restore step carrying an `if:` may be
    skipped, and a skipped step's `outputs.hit` is the empty string — so the
    floor stops evaluating. `if: false` on the real tier-3 restore step was
    accepted as "traceable" before this. Whether a given expression can be false
    is exactly the static evaluation this file declines to attempt, and guessing
    wrong fails toward clean.

Stated as a rule rather than as a list of lanes deliberately. A count or a lane
roster here would rot the moment a lane is added, and would rot SILENTLY —
which is the failure mode this whole file exists to prevent.

EXIT
  0  every call site is consistent
  1  at least one violates the rule (each is named, with which direction)
  2  the check could not run, or found no call sites at all — an empty result
     is an instrument failure here, not a pass
"""
import re
import sys
import pathlib

try:
    import yaml
except ImportError:                                    # pragma: no cover
    print("::error::PyYAML is required for this check (the call sites are only "
          "locatable by walking jobs -> steps; a text scan cannot tell which job "
          "a step belongs to).")
    sys.exit(2)

CALL = re.compile(r"ci/ccache-stats\.sh\s+(?P<args>.*)", re.S)
STEP_HIT = re.compile(r"^\$\{\{\s*steps\.(?P<id>[\w-]+)\.outputs\.hit\s*\}\}$")
RESTORE_SCRIPT = "ci/restore-ccache.sh"


def split_args(text):
    """Split a shell-ish argument list into tokens.

    Two shapes here are NOT what a naive splitter produces, and both were found
    by running this check against the real workflows rather than against a
    fixture written from the code:

      * `''` — an empty quoted argument. It is a REAL, PRESENT argument saying
        "this lane has no restore disposition", and it is the single most
        important token this check reads. A splitter that emits nothing for it
        shifts every later argument left by one, so the build outcome is read as
        the restore disposition and the lane looks like it CAN supply one.
      * `${{ ... }}` — a GitHub expression. It contains spaces, so splitting on
        whitespace shreds it into three tokens and every argument index after it
        is wrong. It must be atomic.

    A fixture written from this function's own behaviour would have certified
    both bugs.
    """
    out, cur, quote, started = [], "", None, False
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if quote:
            if ch == quote:
                quote = None
            else:
                cur += ch
            i += 1
            continue
        if text.startswith("${{", i):
            end = text.find("}}", i)
            end = n if end == -1 else end + 2
            cur += text[i:end]
            started = True
            i = end
            continue
        if ch in "\"'":
            quote = ch
            started = True          # `''` is an empty token, not an absent one
            i += 1
            continue
        if ch == "\\":              # a folded continuation inside the run block
            i += 1
            continue
        if ch.isspace():
            if started:
                out.append(cur)
                cur, started = "", False
            i += 1
            continue
        cur += ch
        started = True
        i += 1
    if started:
        out.append(cur)
    return out


def restore_step_ids(job):
    """Step ids in this job that UNCONDITIONALLY produce a restore disposition.

    ⚠️ CONDITIONAL PRODUCERS ARE NOT PRODUCERS. A step carrying an `if:` may not
    run, and a skipped step's `outputs.hit` is the EMPTY STRING — so the floor,
    gated on `restore == 'true'`, silently stops evaluating. A hostile review
    demonstrated it: adding `if: false` to the real tier-3 restore step left this
    check reporting "all consistent" while the floor could never fire again.

    An `if:` here is therefore refused rather than reasoned about. Deciding
    whether some particular expression can be false is exactly the static
    evaluation this file declines to attempt elsewhere, and guessing wrong fails
    toward clean.
    """
    ids, conditional = set(), {}
    for step in job.get("steps") or []:
        if not isinstance(step, dict):
            continue
        sid, run = step.get("id"), step.get("run") or ""
        if sid and RESTORE_SCRIPT in run:
            if "if" in step:
                conditional[sid] = str(step.get("if"))
            else:
                ids.add(sid)
    return ids, conditional


def classify(restore_arg, providers, conditional):
    """-> (can_supply, reason_if_untraceable)."""
    tok = restore_arg.strip()
    if tok == "":
        return False, None
    if tok in ("true", "false"):
        # ⚠️ A LITERAL IS NOT A MEASUREMENT. `"false"` at a call site reports a
        # cache MISS on every run whatever the cache did, which permanently
        # exempts the lane from its floor; `"true"` asserts a HIT nobody
        # observed. A review defeated the previous version by hardcoding
        # `"false"` and dropping the floor — the checker called it consistent.
        return None, (f"argument 2 is the LITERAL `{tok}`. A hardcoded disposition is "
                      f"not a measurement: `false` exempts the lane from its floor on "
                      f"every run regardless of what the cache did, and `true` asserts "
                      f"a hit nobody observed. It must come from a restore step's "
                      f"`outputs.hit`")
    m = STEP_HIT.match(tok)
    if not m:
        return None, (f"argument 2 is `{restore_arg}`, which is neither an empty "
                      f"literal, nor 'true'/'false', nor "
                      f"`${{{{ steps.<id>.outputs.hit }}}}`. A restore disposition "
                      f"this check cannot trace to its producer is one nobody can "
                      f"rely on")
    sid = m.group("id")
    if sid in conditional:
        return None, (f"argument 2 references step id `{sid}`, which invokes "
                      f"{RESTORE_SCRIPT} but carries `if: {conditional[sid]}`. A step that "
                      f"may not run is not a producer: when it is skipped its "
                      f"`outputs.hit` is the EMPTY STRING, so the floor — gated on "
                      f"`restore == 'true'` — silently stops evaluating")
    if sid not in providers:
        return None, (f"argument 2 references step id `{sid}`, but no step in this "
                      f"job has that id AND invokes {RESTORE_SCRIPT}. The expression "
                      f"resolves to the EMPTY STRING at run time, so the floor is "
                      f"gated on `'' == 'true'` and can NEVER fire — inert while "
                      f"looking wired in the diff")
    return True, None


def main():
    wf_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".github/workflows")
    if not wf_dir.is_dir():
        print(f"::error::{wf_dir} is not a directory — the check could not run.")
        return 2

    sites, violations = [], []
    for path in sorted(wf_dir.glob("*.yml")):
        try:
            doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        except yaml.YAMLError as exc:
            print(f"::error::{path.name} does not parse as YAML: {exc}")
            return 2
        if not isinstance(doc, dict):
            continue
        for job_name, job in (doc.get("jobs") or {}).items():
            if not isinstance(job, dict):
                continue
            providers, conditional = restore_step_ids(job)
            for step in job.get("steps") or []:
                if not isinstance(step, dict):
                    continue
                run = step.get("run") or ""
                if "ci/ccache-stats.sh" not in run:
                    continue
                m = CALL.search(run)
                if not m:
                    continue
                args = split_args(m.group("args"))
                preset = args[0] if args else "?"
                restore = args[1] if len(args) > 1 else ""
                floor = args[3] if len(args) > 3 else ""
                can_supply, untraceable = classify(restore, providers, conditional)

                where = f"{path.name}:{job_name}"
                sites.append((where, preset, can_supply, floor))

                if untraceable:
                    violations.append(f"{where}: call site for '{preset}' — {untraceable}.")
                    continue
                if can_supply and not floor:
                    violations.append(
                        f"{where}: call site for '{preset}' supplies a restore "
                        f"disposition but passes NO hit floor. The floor exists in "
                        f"ci/ccache-stats.sh and nothing invokes it — this lane can "
                        f"regress to a 0 % hit rate and stay green (#299).")
                if not can_supply and floor:
                    violations.append(
                        f"{where}: call site for '{preset}' passes hit floor "
                        f"'{floor}' but no usable restore disposition. The floor is "
                        f"gated on restore == 'true', so it can never evaluate — it "
                        f"would report 'NOT evaluated' every run while looking "
                        f"enforced. Give the lane a real restore disposition, or "
                        f"pass no floor.")

    # ⚠️ AN EMPTY RESULT IS AN INSTRUMENT FAILURE, NOT A PASS. If the call sites
    # move, are renamed, or the walk stops matching, "0 violations over 0 sites"
    # is indistinguishable from "everything is fine" — the single most recurring
    # defect in this repo. Refuse to report success without having seen a site.
    if not sites:
        print("::error::found ZERO ci/ccache-stats.sh call sites. Either the callers "
              "moved or this check's walk is broken. Refusing to report clean on an "
              "empty scan.")
        return 2

    for where, preset, can_supply, floor in sites:
        state = f"floor={floor}" if floor else "no floor"
        disp = {True: "traceable", False: "NONE", None: "UNTRACEABLE"}[can_supply]
        print(f"  {where}: {preset} — restore-disposition={disp}, {state}")

    if violations:
        for v in violations:
            print(f"::error::{v}")
        print(f"\nccache floor callers: {len(violations)} violation(s) over "
              f"{len(sites)} call site(s).")
        return 1

    print(f"\nccache floor callers: {len(sites)} call site(s), all consistent.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
