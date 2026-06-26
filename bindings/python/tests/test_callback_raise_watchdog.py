"""bindings/python/tests/test_callback_raise_watchdog.py — PY-002/PY-003 FR-011 / SC-004.

The subprocess-watchdog regression test deferred from 053: a Python inbound
callback that RAISES must not deadlock the engine, terminate the process, or
corrupt the interpreter. The raise is contained at the as-built flat trampoline
(caught + PyErr_Print'd, execution continues — NOT propagated into the C++
worker; no FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED/1200 engine translation,
which needs the director = PY-004, D-8).

The raising callback is STAGED CONCURRENTLY against a blocking teardown
(_gil_staging.py 'raise': the recv callback blocks on a threading.Event so it is
provably mid-flight, then raises; the main thread enters a blocking teardown and
the releaser unparks it). That staging is what pins the 053 fix: a bare raising
callback with no concurrent teardown just prints and returns — it would exit
cleanly WITH OR WITHOUT the GIL release and so would NOT discriminate. With the
bands removed (canary) this scenario times out (verified locally, T014).

Runs in a child process under a hard timeout so a hung worker cannot wedge the
parent pytest. In-matrix (none/asan/tsan); expected outcome is no-hang.
"""

import os
import subprocess
import sys

import pytest

import fixpp  # parent import; used to pass the extension dir to the child (cwd-robust)

HERE = os.path.dirname(os.path.abspath(__file__))
STAGING = os.path.join(HERE, "_gil_staging.py")
HARD_TIMEOUT = float(os.environ.get("FIXPP_GIL_TIMEOUT", "45"))
_FIXPP_DIR = os.path.dirname(os.path.abspath(fixpp.__file__))


def _child_env():
    # Pass the extension's ABSOLUTE dir so the child (cwd=HERE) imports fixpp
    # regardless of a relative PYTHONPATH in the parent.
    env = dict(os.environ)
    env["PYTHONPATH"] = _FIXPP_DIR + os.pathsep + env.get("PYTHONPATH", "")
    return env


def _run_raising_scenario():
    return subprocess.run(
        [sys.executable, STAGING, "raise"],
        cwd=HERE, env=_child_env(), capture_output=True, text=True,
        timeout=HARD_TIMEOUT)


@pytest.mark.parametrize("rep", range(3))
def test_raising_callback_never_deadlocks(rep):
    """SC-004: across repeated runs, the raising-callback-+-concurrent-teardown
    child completes within the hard timeout (no deadlock) and the interpreter is
    not corrupted. A TimeoutExpired here means the engine deadlocked."""
    try:
        r = _run_raising_scenario()
    except subprocess.TimeoutExpired:
        pytest.fail(
            "raising-callback + concurrent teardown DEADLOCKED (subprocess hard "
            "timeout) — the engine did not drain; the GIL-release bands may have "
            "regressed.")
    assert r.returncode == 0, (
        f"child did not complete cleanly (rc={r.returncode}); a raise that "
        f"propagated into the worker or crashed the interpreter would show here.\n"
        f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
    assert "COMPLETED" in r.stdout
    # Containment: the raise surfaced via the interpreter error-print path at the
    # trampoline (not swallowed silently, not propagated into the C++ worker).
    assert "deliberate raise" in r.stderr, (
        "expected the raised exception to be PyErr_Print'd at the trampoline; "
        f"stderr was:\n{r.stderr}")
