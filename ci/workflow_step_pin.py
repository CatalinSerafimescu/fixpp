"""ci/workflow_step_pin.py — the one rule ci/test-tier1-python-policy.sh uses to pin
a workflow step as "runs exactly these commands, every time it is reached".

Not a CLI: the policy harness imports it (sys.path) in each Python block that pins a
step, so every such pin applies the same rule.

The rule, per step, as two checks the caller reports separately:
  * keys — the step's raw key set must be exactly {name, run}. A step `if:`,
    `continue-on-error`, `shell`, `working-directory` or `env` is reported, since
    each can stop the step running, stop a failure failing the job, or change
    what its commands do.
  * lines — the logical lines of its run: must equal the caller's expected list,
    which the caller writes out itself. Comparing the whole run: is what reports a
    dead branch, an early `exit` or an `|| true`: each changes the lines while
    every literal the step names stays present.

What it does not see: a job-level `if:`, `continue-on-error` or `defaults`, and a
workflow-level `defaults`. A caller that needs those pins them separately.
"""

from __future__ import annotations


def logical_run_lines(run: str) -> list[str]:
    """run: split into shell logical lines: comment lines dropped, backslash
    continuations joined, whitespace runs collapsed, blank lines dropped."""
    lines = []
    buf = ""
    for raw in run.splitlines():
        if raw.lstrip().startswith("#"):
            continue
        line = raw.rstrip()
        if line.endswith("\\"):
            buf += line[:-1] + " "
            continue
        buf += line
        normalized = " ".join(buf.split())
        if normalized:
            lines.append(normalized)
        buf = ""
    if buf.strip():
        lines.append(" ".join(buf.split()))
    return lines


def step_pin_check(step: dict, want_lines: list[str]) -> tuple[list[str], str | None]:
    """(keys beside name/run, or missing from it; the first run: line difference,
    or None when the lines equal `want_lines`)."""
    keys = {str(k) for k in step.keys()}
    bad_keys = sorted(keys ^ {"name", "run"})
    got = logical_run_lines(str(step.get("run", "")))
    if got == want_lines:
        return bad_keys, None
    for i, (g, w) in enumerate(zip(got, want_lines)):
        if g != w:
            return bad_keys, f"line {i + 1} is {g!r}, expected {w!r}"
    if len(got) > len(want_lines):
        return bad_keys, f"line {len(want_lines) + 1} is {got[len(want_lines)]!r}, expected the end of run:"
    return bad_keys, f"ends after line {len(got)}, expected line {len(got) + 1}: {want_lines[len(got)]!r}"
