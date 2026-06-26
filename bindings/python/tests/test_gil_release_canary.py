"""bindings/python/tests/test_gil_release_canary.py — PY-002 FR-004 / SC-003.

The discriminating, two-mode GIL-RELEASE witness. Runs the shared teardown-vs-
in-flight-recv-callback scenario (_gil_staging.py) in a CHILD process under a hard
timeout, so a hung worker cannot wedge the parent pytest:

  * Normal build (default): the child COMPLETES within the timeout -> GREEN. This
    leg runs IN the Tier-1 python-bindings matrix (none/asan/tsan) — the
    pass-without-canary witness is exercised every PR, not skipped (FR-013).

  * FIXPP_PY_GIL_RELEASE_CANARY build: the GIL-release bands are elided, so the
    main thread holds the GIL in the blocking teardown and the worker can never
    run the in-flight callback -> the child HANGS -> the subprocess hard timeout
    fires -> RED. This leg is LOCAL-ONLY (deliberate deadlock); drive it by
    building the canary and setting FIXPP_EXPECT_GIL_DEADLOCK=1. Its CI-automation
    is waived under the 053 SC-004 precedent.

Distinct from 053's FIXPP_PY_GIL_CANARY (the reacquire canary -> segfault).
"""

import os
import subprocess
import sys

import pytest

import fixpp  # parent import; used to pass the extension dir to the child (cwd-robust)

HERE = os.path.dirname(os.path.abspath(__file__))
STAGING = os.path.join(HERE, "_gil_staging.py")
_FIXPP_DIR = os.path.dirname(os.path.abspath(fixpp.__file__))


def _child_env():
    # The child runs with cwd=HERE; pass the extension's ABSOLUTE dir so a
    # relative PYTHONPATH in the parent does not break `import fixpp` (CI sets an
    # absolute path; this makes the test robust either way).
    env = dict(os.environ)
    env["PYTHONPATH"] = _FIXPP_DIR + os.pathsep + env.get("PYTHONPATH", "")
    return env

# Generous for a cold loopback establish + teardown, including under asan/tsan;
# short enough that the deliberate-deadlock leg fails fast. Exceeding it IS the
# RED signal.
HARD_TIMEOUT = float(os.environ.get("FIXPP_GIL_TIMEOUT", "45"))

# Set to "1" against a FIXPP_PY_GIL_RELEASE_CANARY build to assert the RED hang.
EXPECT_DEADLOCK = os.environ.get("FIXPP_EXPECT_GIL_DEADLOCK") == "1"


def _run_scenario():
    return subprocess.run(
        [sys.executable, STAGING],
        cwd=HERE, env=_child_env(), capture_output=True, text=True,
        timeout=HARD_TIMEOUT)


def test_gil_release_discriminating_witness():
    """SC-003: the teardown-vs-in-flight-recv-callback scenario completes in a
    normal build (GREEN, in-matrix) and hangs under the GIL-release canary (RED,
    local-only)."""
    if EXPECT_DEADLOCK:
        # Canary build: the scenario must hang -> subprocess hard timeout.
        with pytest.raises(subprocess.TimeoutExpired):
            _run_scenario()
    else:
        # Normal build: the GIL is released, the worker drains, teardown returns.
        r = _run_scenario()
        assert r.returncode == 0, (
            f"GIL-release teardown did not complete (rc={r.returncode}); a "
            f"regression that drops the release bands would hang here.\n"
            f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        assert "COMPLETED" in r.stdout
