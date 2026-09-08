"""Subinterpreter rejection witness for PY-004 Phase 3 (055 / US1)."""

import textwrap

import pytest


# The wheel's floor is 3.12 (requires-python / wheel.py-api), so the mandatory
# <3.12 arm this file used to carry is unreachable and has been removed with the
# 3.10/3.11 install-test legs. On 3.12+ the CPython import barrier covers the
# owed 1201 check.
#
# ⚠️ COVERAGE THIS SKIP COSTS, stated rather than absorbed: 3.13 renamed the
# module to `_interpreters`, so `importorskip` SKIPS on 3.13 and 3.14 and this
# witness now executes on the 3.12 leg ALONE. A green 3.13/3.14 leg is not
# evidence the subinterpreter rejection still works there. Making it run on the
# newer legs means porting to `_interpreters` (whose run-failure exception type
# also changed), which is a behaviour change to the witness, not a rename.
xx = pytest.importorskip("_xxsubinterpreters")


def test_engine_constructor_rejects_subinterpreter():
    interp = xx.create()
    try:
        try:
            xx.run_string(
                interp,
                textwrap.dedent(
                    """
                    import fixpp

                    cfg = fixpp.engine_config_create()
                    fixpp.engine_config_set_realtime_clock(cfg)
                    engine = None
                    try:
                        try:
                            engine = fixpp.Engine(cfg)
                        except fixpp.SubInterpreterRejected as exc:
                            if exc.code != 1201:
                                raise AssertionError(
                                    f"wrong code: expected 1201, got {exc.code}"
                                )
                        else:
                            raise AssertionError(
                                "fixpp.Engine(cfg) unexpectedly succeeded in a subinterpreter"
                            )
                    finally:
                        if engine is not None:
                            engine.close()
                        else:
                            fixpp.engine_config_destroy(cfg)
                    """
                ),
            )
        except xx.RunFailedError as exc:
            # The import-barrier text is a 3.12+ CPython message, and 3.12 is now
            # the wheel's floor — so the barrier is the ONLY RunFailedError this
            # test tolerates. Any other one is a real failure (RC#5, Gate B r2).
            barrier = "module _fixpp does not support loading in subinterpreters"
            if barrier not in str(exc):
                raise
    finally:
        xx.destroy(interp)
