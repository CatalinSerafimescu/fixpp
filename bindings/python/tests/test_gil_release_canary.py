"""bindings/python/tests/test_gil_release_canary.py — PY-002 FR-004 / SC-003.

The discriminating, two-mode GIL-RELEASE witness. Runs the shared teardown-vs-
in-flight-recv-callback scenario (_gil_staging.py) in a CHILD process under a hard
timeout, so a hung worker cannot wedge the parent pytest. Parameterised over ALL
THREE blocking wrappers that release the GIL (FR-001 audit table):

  op="engine_destroy"  — fixpp.engine_destroy  (the original shape)
  op="session_close"   — fixpp.session_close   (closes acc on the parked engine)
  op="session_send"    — fixpp.session_send    (sends from acc on the parked engine)

Each op targets acc/eng_a (the engine whose single worker is parked mid-callback),
so each leg independently witnesses that its band is load-bearing.

  * Normal build (default): each op's child COMPLETES within the timeout -> GREEN.
    This leg runs IN the Tier-1 python-bindings matrix (none/asan/tsan) — the
    pass-without-canary witness is exercised every PR, not skipped (FR-013).

  * FIXPP_PY_GIL_RELEASE_CANARY build: the GIL-release bands are elided, so the
    main thread holds the GIL in the blocking op and the worker can never run the
    in-flight callback -> EACH op's child HANGS -> the subprocess hard timeout
    fires -> RED. This leg is LOCAL-ONLY (deliberate deadlock); drive it by
    building the canary and setting FIXPP_EXPECT_GIL_DEADLOCK=1. Its CI-automation
    is waived under the 053 SC-004 precedent.

Distinct from 053's FIXPP_PY_GIL_CANARY (the reacquire canary -> segfault).
"""

import os
import subprocess

import pytest

from _gil_staging import run_staging

# Set to "1" against a FIXPP_PY_GIL_RELEASE_CANARY build to assert the RED hang.
EXPECT_DEADLOCK = os.environ.get("FIXPP_EXPECT_GIL_DEADLOCK") == "1"


@pytest.mark.parametrize("op", ["engine_destroy", "session_close", "session_send"])
def test_gil_release_discriminating_witness(op):
    """SC-003: the teardown-vs-in-flight-recv-callback scenario completes for each
    of the three blocking wrappers in a normal build (GREEN, in-matrix) and hangs
    under the GIL-release canary (RED, local-only)."""
    if EXPECT_DEADLOCK:
        # Canary build: each op's child must hang -> subprocess hard timeout.
        with pytest.raises(subprocess.TimeoutExpired):
            run_staging(op)
    else:
        # Normal build: the GIL is released, the worker drains, the op returns.
        r = run_staging(op)
        assert r.returncode == 0, (
            f"GIL-release teardown did not complete for op={op!r} (rc={r.returncode}); "
            f"a regression that drops the release band for this op would hang here.\n"
            f"stdout:\n{r.stdout}\nstderr:\n{r.stderr}")
        assert "COMPLETED" in r.stdout
