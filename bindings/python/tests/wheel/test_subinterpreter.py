"""Subinterpreter rejection witness for PY-004 Phase 3 (055 / US1)."""

import textwrap

import pytest


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
