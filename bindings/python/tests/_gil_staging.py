"""Shared GIL-release discriminating staging (data-model E-4/E-5, research D-2).

The teardown-vs-in-flight-recv-callback scenario reused by BOTH the GIL-release
canary (test_gil_release_canary.py, PY-002 FR-004) and the raising-callback
watchdog (test_callback_raise_watchdog.py, PY-002/PY-003 FR-011). It is the exact
053 deadlock shape: a blocking teardown on the main thread that can only complete
if the engine worker can (re)acquire the GIL to run the in-flight recv callback.

Discrimination — why this pins the GIL release:
  * The recv callback parks mid-flight on a `release` Event (threading.Event.wait
    releases the GIL while blocked), so the worker is provably in-flight but NOT
    holding the GIL when the main thread enters the teardown.
  * A `releaser` daemon thread sets `release` 0.5s AFTER the main thread enters
    the blocking teardown. In a build that RELEASES the GIL around the teardown
    (correct), the releaser runs, the callback re-acquires the GIL, returns, the
    worker drains, and engine_destroy returns -> the child exits 0.
  * In a build that does NOT release (FIXPP_PY_GIL_RELEASE_CANARY, or a regression
    that deletes the bands), the main thread holds the GIL inside the teardown
    wait, so NEITHER the releaser thread NOR the parked callback can make progress
    -> the worker never drains -> engine_destroy never returns -> DEADLOCK. The
    parent's subprocess hard timeout turns that hang into the RED signal.

A bare raising callback with NO concurrent teardown does NOT discriminate (it just
PyErr_Print's and returns, exiting cleanly with OR without the release) — the
concurrent blocking teardown is what pins the 053 fix.

Run as a child process so a hung worker cannot wedge the parent pytest:

    python _gil_staging.py [raise]   # 'raise' => the callback raises (watchdog)

Prints "COMPLETED" and exits 0 when the teardown completes; hangs forever when the
GIL is not released.
"""

import os
import sys
import threading
import time

import fixpp

HOST = "127.0.0.1"
MSG_TYPE = "D"          # NewOrderSingle (msgcat='app' -> routes to the callback)
TAG_CLORDID = 11        # ClOrdID, required STRING scalar of "D"
SENT = "ORDER-PY-002"

BIND_TIMEOUT = 5.0
ESTABLISH_TIMEOUT = 5.0
CB_ENTER_TIMEOUT = 5.0
POLL_INTERVAL = 0.02
# The callback's own safety deadline — long enough that the releaser always wins
# in a correct build; irrelevant in a no-release build (the GIL is never free).
CALLBACK_PARK_TIMEOUT = 30.0


# ── Parent-side runner (used by the test wrappers; NOT by the child __main__) ──
_THIS_FILE = os.path.abspath(__file__)
_FIXPP_DIR = os.path.dirname(os.path.abspath(fixpp.__file__))
DEFAULT_HARD_TIMEOUT = float(os.environ.get("FIXPP_GIL_TIMEOUT", "45"))


def child_env():
    """Env for the staging child: prepend the extension's ABSOLUTE dir so
    `import fixpp` works regardless of the child's cwd or a relative parent
    PYTHONPATH (CI sets an absolute path; this makes the tests robust either
    way). Inherits LD_PRELOAD + *SAN_OPTIONS so the child is instrumented under
    the asan/tsan legs."""
    env = dict(os.environ)
    env["PYTHONPATH"] = _FIXPP_DIR + os.pathsep + env.get("PYTHONPATH", "")
    return env


def run_staging(*extra_args, timeout=DEFAULT_HARD_TIMEOUT):
    """Spawn this staging as a child process (cwd = this file's dir) under a hard
    timeout, so a hung worker cannot wedge the parent pytest. Returns the
    CompletedProcess; raises subprocess.TimeoutExpired on a hang."""
    import subprocess
    return subprocess.run(
        [sys.executable, _THIS_FILE, *extra_args],
        cwd=os.path.dirname(_THIS_FILE), env=child_env(),
        capture_output=True, text=True, timeout=timeout)


def _dict_path():
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    return os.path.join(repo_root, "dictionaries", "FIX44.xml")


def _wait_until(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        v = predicate()
        if v:
            return v
        time.sleep(POLL_INTERVAL)
    raise AssertionError(f"timed out after {timeout}s waiting for: {what}")


def _make_session_config(dict_h, role, sender, target, port):
    sc = fixpp.session_config_create()
    fixpp.session_config_set_role(sc, role)
    fixpp.session_config_set_comp_ids(sc, sender, target)
    fixpp.session_config_set_begin_string(sc, "FIX.4.4")
    fixpp.session_config_set_dictionary(sc, dict_h)
    fixpp.session_config_set_security(
        sc, fixpp.SECURITY_INSECURE_PLAIN_TCP, None, None)
    fixpp.session_config_set_heartbeat_seconds(sc, 30)
    fixpp.session_config_set_reset_on_logon(sc, role == fixpp.ROLE_INITIATOR)
    fixpp.session_config_set_reset_seqnum_policy(
        sc, fixpp.RESET_SEQNUM_BILATERAL_LENIENT)
    fixpp.session_config_set_tcp_endpoint(sc, HOST, port)
    return sc


def run(raise_in_callback):
    """Stage the teardown-vs-in-flight-callback deadlock shape. Returns on a
    correct (GIL-releasing) build; hangs on a no-release build."""
    dict_h = fixpp.dict_load_from_xml(_dict_path())

    cb_entered = threading.Event()
    release = threading.Event()

    def on_message(inbound):
        # Runs on the acceptor worker thread (GIL reacquired by the binding).
        cb_entered.set()
        # Park mid-flight; Event.wait releases the GIL while blocked, so the
        # main thread can enter the teardown. A no-release teardown then strands
        # us here (we can never re-acquire the GIL to return).
        release.wait(timeout=CALLBACK_PARK_TIMEOUT)
        if raise_in_callback:
            raise RuntimeError(
                "watchdog: deliberate raise from the inbound callback (FR-011)")

    eng_a = eng_b = acc = ini = None
    try:
        eca = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(eca)
        eng_a = fixpp.engine_create(eca)
        acc_sc = _make_session_config(
            dict_h, fixpp.ROLE_ACCEPTOR, "ACCEPTOR", "INITIATOR", 0)
        acc = fixpp.session_open(eng_a, acc_sc)
        fixpp.session_register_callback(acc, on_message)
        fixpp.engine_start(eng_a)

        port = _wait_until(
            lambda: fixpp.session_acceptor_bound_endpoint(acc) or None,
            BIND_TIMEOUT, "acceptor to bind an ephemeral port")

        ecb = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(ecb)
        eng_b = fixpp.engine_create(ecb)
        ini_sc = _make_session_config(
            dict_h, fixpp.ROLE_INITIATOR, "INITIATOR", "ACCEPTOR", port)
        ini = fixpp.session_open(eng_b, ini_sc)
        fixpp.engine_start(eng_b)

        _wait_until(lambda: fixpp.session_is_established(acc),
                    ESTABLISH_TIMEOUT, "acceptor to establish")
        _wait_until(lambda: fixpp.session_is_established(ini),
                    ESTABLISH_TIMEOUT, "initiator to establish")

        # Send one app message; the acceptor worker delivers it to on_message.
        m = fixpp.msg_create_outbound(ini, MSG_TYPE)
        fixpp.msg_set_string(m, TAG_CLORDID, SENT)
        payload = fixpp.msg_commit(m)
        fixpp.session_send(ini, payload)
        fixpp.msg_destroy(m)

        # The callback must be provably mid-flight before we tear down.
        _wait_until(cb_entered.is_set, CB_ENTER_TIMEOUT,
                    "recv callback to enter (in-flight)")

        # Set `release` AFTER the main thread is inside the blocking teardown, so
        # the teardown is already waiting when the callback is unparked. In a
        # no-release build this thread cannot run (main holds the GIL) -> deadlock.
        def releaser():
            time.sleep(0.5)
            release.set()
        threading.Thread(target=releaser, daemon=True).start()

        # The blocking teardown that must wait for the acceptor worker to drain.
        # Releases the GIL in a correct build; deadlocks under the canary.
        fixpp.engine_destroy(eng_a)
        eng_a = None
    finally:
        # Best-effort cleanup of the remaining handles (only reached on a correct
        # build; a no-release build never gets here — the parent kills the child).
        release.set()
        if ini is not None:
            fixpp.session_close(ini)
        if acc is not None and eng_a is not None:
            fixpp.session_close(acc)
        if eng_b is not None:
            fixpp.engine_destroy(eng_b)
        if eng_a is not None:
            fixpp.engine_destroy(eng_a)
        fixpp.dict_destroy(dict_h)


if __name__ == "__main__":
    run(raise_in_callback=(len(sys.argv) > 1 and sys.argv[1] == "raise"))
    print("COMPLETED")
    sys.stdout.flush()
