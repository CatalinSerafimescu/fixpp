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
disposition) is the literal 'true'. That gate is what keeps a legitimate cold
or seeding run green. Two failure directions follow, and this file pins both:

  * A call site that CAN supply a restore disposition but passes NO floor is
    the #299 defect: the assertion exists and nothing invokes it.
  * A call site that CANNOT supply one (it passes an empty argument 2) but DOES
    pass a floor is the same defect wearing a fix's clothing: the floor can
    never evaluate, so it reports "NOT evaluated" every run while looking
    enforced in the diff.

Stated as a rule rather than as a list of lanes deliberately. A count or a lane
roster here would rot the moment a lane is added, and would rot SILENTLY —
which is the failure mode this whole file exists to prevent. Adding a lane that
restores through ci/restore-ccache.sh will simply be required to pass a floor.

EXIT
  0  every call site is consistent
  1  at least one violates the rule (each is named, with which direction)
  2  the check could not run, or found no call sites at all — an empty result
     is an instrument failure here, not a pass
"""
import re
import sys
import pathlib

# A `run:` block may fold the invocation over several lines with trailing
# backslashes. Join those first, so a call site's arguments are on one logical
# line however the YAML happens to wrap it — otherwise a folded call reads as
# "no floor" purely because of formatting, which would be a false positive of
# exactly the kind this repo keeps paying for.
CONT = re.compile(r"\\\s*\n\s*")
CALL = re.compile(r"ci/ccache-stats\.sh\s+(?P<args>[^\n]*)")


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
        # `${{ ... }}` is one token, spaces and all.
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


def main():
    wf_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".github/workflows")
    if not wf_dir.is_dir():
        print(f"::error::{wf_dir} is not a directory — the check could not run.")
        return 2

    sites, violations = [], []
    for path in sorted(wf_dir.glob("*.yml")):
        raw = path.read_text(encoding="utf-8")
        joined = CONT.sub(" ", raw)
        for line in joined.splitlines():
            stripped = line.strip()
            # A commented invocation is prose about the call, not a call.
            if stripped.startswith("#"):
                continue
            m = CALL.search(line)
            if not m:
                continue
            args = split_args(m.group("args"))
            # args: <preset> <restore-disposition> <build-outcome> [<floor>]
            restore = args[1] if len(args) > 1 else ""
            floor = args[3] if len(args) > 3 else ""
            # An ${{ ... }} expression is a disposition the lane CAN supply; an
            # empty literal is one it cannot. That distinction is the whole
            # rule, so it is read from the argument rather than from lane names.
            can_supply = bool(restore.strip())
            sites.append((path.name, args[0] if args else "?", can_supply, floor))

            if can_supply and not floor:
                violations.append(
                    f"{path.name}: call site for '{args[0] if args else '?'}' supplies a restore "
                    f"disposition but passes NO hit floor. The floor exists in "
                    f"ci/ccache-stats.sh and nothing invokes it — this lane can regress to a 0 % "
                    f"hit rate and stay green (#299)."
                )
            if not can_supply and floor:
                violations.append(
                    f"{path.name}: call site for '{args[0] if args else '?'}' passes hit floor "
                    f"'{floor}' but an EMPTY restore disposition. The floor is gated on "
                    f"restore == 'true', so it can never evaluate — it would report "
                    f"'NOT evaluated' every run while looking enforced. Give the lane a real "
                    f"restore disposition, or pass no floor."
                )

    # ⚠️ AN EMPTY RESULT IS AN INSTRUMENT FAILURE, NOT A PASS. If the call sites
    # move, are renamed, or the regex stops matching, "0 violations over 0 sites"
    # is indistinguishable from "everything is fine" — the single most recurring
    # defect in this repo. Refuse to report success without having seen a site.
    if not sites:
        print("::error::found ZERO ci/ccache-stats.sh call sites. Either the callers moved or "
              "this check's pattern is broken. Refusing to report clean on an empty scan.")
        return 2

    for name, preset, can_supply, floor in sites:
        state = f"floor={floor}" if floor else "no floor"
        print(f"  {name}: {preset} — restore-disposition={'yes' if can_supply else 'NONE'}, {state}")

    if violations:
        for v in violations:
            print(f"::error::{v}")
        print(f"\nccache floor callers: {len(violations)} violation(s) over {len(sites)} call site(s).")
        return 1

    print(f"\nccache floor callers: {len(sites)} call site(s), all consistent.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
