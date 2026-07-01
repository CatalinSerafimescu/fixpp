"""Subprocess staging for OO reentrancy watchdog tests."""

import os
import subprocess
import sys
import threading

import fixpp

from oo_test_support import (
    RECV_TIMEOUT,
    close_pair,
    establish_pair,
    send_app_message,
)

THIS_FILE = os.path.abspath(__file__)
FIXPP_DIR = os.path.dirname(os.path.abspath(fixpp.__file__))
DEFAULT_HARD_TIMEOUT = float(os.environ.get("FIXPP_OO_REENTRANCY_TIMEOUT", "20"))


def child_env():
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        [os.path.dirname(THIS_FILE), FIXPP_DIR, env.get("PYTHONPATH", "")]
    )
    return env


def run_staging(op, *, worker_threads=1, timeout=DEFAULT_HARD_TIMEOUT):
    return subprocess.run(
        [sys.executable, THIS_FILE, op, str(worker_threads)],
        cwd=os.path.dirname(THIS_FILE),
        env=child_env(),
        capture_output=True,
        text=True,
        timeout=timeout,
    )


class _ReentrantApp(fixpp.Application):
    def __init__(self, op, done):
        self._op = op
        self._done = done
        self.error = None

    def fromApp(self, session, msg):
        try:
            if self._op == "send":
                reply = session.create_message("D")
                reply.set_string(11, "REENTRANT")
                session.send(reply)
            elif self._op == "session_close":
                session.close()
            elif self._op == "engine_close":
                session._engine.close()
            else:
                raise AssertionError(f"unknown op {self._op!r}")
        except Exception as exc:
            self.error = exc
        finally:
            self._done.set()


def run(op, worker_threads):
    done = threading.Event()
    app = _ReentrantApp(op, done)
    dict_h, acc_engine, ini_engine, _acc, ini = establish_pair(
        app, acceptor_workers=worker_threads)
    try:
        send_app_message(ini)
        assert done.wait(timeout=RECV_TIMEOUT), "callback did not complete"
        assert isinstance(app.error, fixpp.CallbackReentrantClose), (
            f"expected CallbackReentrantClose, got {app.error!r}"
        )
        assert app.error.code == 1204
    finally:
        close_pair(ini_engine, acc_engine, dict_h)


if __name__ == "__main__":
    run(sys.argv[1], int(sys.argv[2]))
