#!/usr/bin/env python3
"""tools/test_check_admin_emit_observation.py — positive/negative test for
tools/check_admin_emit_observation.py (the SC-002 static gate).

Each arm writes one fixture .cpp into a temp directory and runs the gate on it,
so the source tree is never edited. A flagged arm must exit 1 AND print the
message of the defect it seeds: the gate also exits 1 when it cannot read its
input, and an arm that checked the exit code alone would pass on that.

  (a) builder, hook, emit, with more comment lines between them than the
      window and few code lines: the window counts code lines, so this passes.
  (b) builder with its emit beyond the window in code lines: flagged as having
      no emit within the window.
  (c) builder, then emit, with no hook between them: flagged as unwired.
  (d) as (c), with the hook only inside comments: flagged as unwired, because
      a hook token inside a comment is not a call.

The expected messages are spelled here, not read from the gate, so a change to
the gate's wording turns these arms RED instead of following it.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

GATE = Path(__file__).resolve().parent / "check_admin_emit_observation.py"

# More filler lines than the gate's window, whichever way it counts them.
FILLER = 100

OK_TEXT = "[check_admin_emit_observation] OK"
FAIL_TEXT = "[check_admin_emit_observation] FAIL"
NO_EMIT_TEXT = "builder 'build_logout(' has no store_then_emit within"
UNWIRED_TEXT = "'fire_to_admin_(' was NOT found before store_then_emit"


def fixture(body: list[str]) -> str:
    return "\n".join(["void Session::f() {", *body, "}", ""])


def comment_filler() -> list[str]:
    half = FILLER // 2
    lines = [f"    // comment line {k}" for k in range(half)]
    lines += ["    /*"] + [f"       block comment line {k}" for k in range(half)] + ["    */"]
    return lines


ARMS = {
    "a_comment_lines_do_not_count": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                *comment_filler(),
                "    if (!fire_to_admin_(*r)) return;",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        0,
        [OK_TEXT],
    ),
    "b_emit_beyond_the_window_in_code_lines": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                *[f"    int x{k} = {k};" for k in range(FILLER)],
                "    if (!fire_to_admin_(*r)) return;",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        1,
        [FAIL_TEXT, NO_EMIT_TEXT],
    ),
    "c_no_hook": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                "    int x = 0;",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        1,
        [FAIL_TEXT, UNWIRED_TEXT],
    ),
    "d_hook_only_in_comments": (
        fixture(
            [
                "    auto r = build_logout(buf);",
                "    // if (!fire_to_admin_(*r)) return;",
                "    /* fire_to_admin_(*r); */",
                "    auto e = co_await store_then_emit(seq, *r);",
            ]
        ),
        1,
        [FAIL_TEXT, UNWIRED_TEXT],
    ),
}


def main() -> int:
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, (source, want_rc, want_texts) in ARMS.items():
            path = Path(tmp) / f"{name}.cpp"
            path.write_text(source, encoding="utf-8")
            run = subprocess.run(
                [sys.executable, str(GATE), str(path)], capture_output=True, text=True
            )
            out = run.stdout + run.stderr
            missing = [t for t in want_texts if t not in out]
            ok = run.returncode == want_rc and not missing
            print(f"[{'PASS' if ok else 'FAIL'}] {name}: rc={run.returncode} (want {want_rc})")
            if not ok:
                failures += 1
                for t in missing:
                    print(f"    missing from the gate's output: {t!r}")
                print("    gate output:")
                for line in out.splitlines():
                    print(f"      {line}")
    print(f"test_check_admin_emit_observation: {len(ARMS) - failures} of {len(ARMS)} arms pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
