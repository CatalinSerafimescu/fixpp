"""Subinterpreter rejection witness for PY-004 Phase 3 (055 / US1)."""

import sys
import textwrap

import pytest


if sys.version_info < (3, 12):
    # On the owed band (3.10/3.11) the CPython import barrier does not fire;
    # a missing _xxsubinterpreters module is a broken runner, not a skip.
    # Gate B owes a real 1201 witness on this band — make it mandatory.
    import _xxsubinterpreters as xx
else:
    # 3.12+: the CPython import barrier covers the owed check.
    # 3.13 renamed the module to _interpreters, so tolerate a skip here.
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
            if "module _fixpp does not support loading in subinterpreters" not in str(exc):
                raise
    finally:
        xx.destroy(interp)
