"""Shared GIL-release discriminating staging (data-model E-4/E-5, research D-2).

The teardown-vs-in-flight-recv-callback scenario reused by BOTH the GIL-release
canary (test_gil_release_canary.py, PY-002 FR-004) and the raising-callback
watchdog (test_callback_raise_watchdog.py, PY-002/PY-003 FR-011). It is the exact
053 deadlock shape: a blocking operation on the main thread that can only complete
if the engine worker can (re)acquire the GIL to run the in-flight recv callback.

The staging is parameterised over the decisive blocking operation — one of the
THREE blocking C-ABI wrappers that release the GIL (FR-001 audit table):

  op="engine_destroy"  — fixpp.engine_destroy(eng_a)  [the original shape]
  op="session_close"   — fixpp.session_close(acc)      [closes acc on the parked engine]
  op="session_send"    — fixpp.session_send(acc, p)    [sends from acc on the parked engine]

All three ops target acc/eng_a — the engine whose SINGLE worker is parked mid-
callback in release.wait(). Pinning eng_a to 1 worker is LOAD-BEARING for the
session_close and session_send ops: with >1 worker a free worker could run the
co_spawn'd coroutine without the main thread releasing the GIL, masking the
discriminator. eng_a is always constructed with worker_threads=1.

Discrimination — why each op pins the GIL release:
  * The recv callback parks mid-flight on a `release` Event (threading.Event.wait
    releases the GIL while blocked), so the worker is provably in-flight but NOT
    holding the GIL when the main thread enters the decisive op.
  * A `releaser` daemon thread sets `release` 0.5s AFTER the main thread enters
    the blocking op. In a build that RELEASES the GIL around the op (correct),
    the releaser runs, the callback re-acquires the GIL, returns, the worker
    drains, and the decisive op returns -> the child exits 0.
  * In a build that does NOT release (FIXPP_PY_GIL_RELEASE_CANARY, or a regression
    that deletes one of the three bands), the main thread holds the GIL inside the
    blocking op wait, so NEITHER the releaser thread NOR the parked callback can
    make progress -> the worker never drains -> the decisive op never returns ->
    DEADLOCK. The parent's subprocess hard timeout turns that hang into the RED
    signal.

A bare raising callback with NO concurrent blocking op does NOT discriminate (it
just PyErr_Print's and returns, exiting cleanly with OR without the release) — the
concurrent blocking op is what pins the 053 fix.

Run as a child process so a hung worker cannot wedge the parent pytest:

    python _gil_staging.py [op] [raise]
      op   : engine_destroy | session_close | session_send  (default: engine_destroy)
      raise: literal string "raise" => the callback also raises (watchdog mode)

Prints "COMPLETED" and exits 0 when the op completes; hangs forever when the GIL
is not released.
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


def run_staging(op="engine_destroy", *, raise_in_callback=False,
                timeout=DEFAULT_HARD_TIMEOUT):
    """Spawn this staging as a child process (cwd = this file's dir) under a hard
    timeout, so a hung worker cannot wedge the parent pytest. Returns the
    CompletedProcess; raises subprocess.TimeoutExpired on a hang.

    op: which blocking wrapper is the decisive deadlocking call — one of
        "engine_destroy", "session_close", "session_send".
    raise_in_callback: if True the staging callback also raises (watchdog mode).
    """
    import subprocess
    args = [op]
    if raise_in_callback:
        args.append("raise")
    return subprocess.run(
        [sys.executable, _THIS_FILE, *args],
        cwd=os.path.dirname(_THIS_FILE), env=child_env(),
        capture_output=True, text=True, timeout=timeout)


def _dict_path():
    # Wheel-suite port (T012): resolve via the installed locator, not a repo path.
    import _wheeldict
    return _wheeldict.resolve("FIX44")


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


def run(raise_in_callback, op="engine_destroy"):
    """Stage the teardown-vs-in-flight-callback deadlock shape. Returns on a
    correct (GIL-releasing) build; hangs on a no-release build.

    op must be one of "engine_destroy", "session_close", "session_send". All
    three target acc/eng_a (the engine whose single worker is parked mid-callback),
    so each is a valid discriminating witness for its GIL-release band.
    """
    if op not in ("engine_destroy", "session_close", "session_send"):
        raise ValueError(
            f"unknown op {op!r}; expected one of: engine_destroy, session_close, session_send")

    dict_h = fixpp.dict_load_from_xml(_dict_path())

    cb_entered = threading.Event()
    release = threading.Event()

    def on_message(inbound):
        # Runs on the acceptor worker thread (GIL reacquired by the binding).
        cb_entered.set()
        # Park mid-flight; Event.wait releases the GIL while blocked, so the
        # main thread can enter the blocking op. A no-release op then strands
        # us here (we can never re-acquire the GIL to return).
        release.wait(timeout=CALLBACK_PARK_TIMEOUT)
        if raise_in_callback:
            raise RuntimeError(
                "watchdog: deliberate raise from the inbound callback (FR-011)")

    eng_a = eng_b = acc = ini = None
    acc_m2 = None  # outbound reply pre-built for the session_send op
    try:
        eca = fixpp.engine_config_create()
        fixpp.engine_config_set_realtime_clock(eca)
        # Pin eng_a to exactly 1 worker — LOAD-BEARING for the session_close and
        # session_send ops: the decisive co_spawn must queue on the SAME parked
        # worker so the main thread is forced to release the GIL to unblock it.
        # With >1 worker a free worker could run the coroutine without the GIL
        # ever being released, masking the discriminator.
        fixpp.engine_config_set_worker_threads(eca, 1)
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

        # For the session_send op: prebuild an outbound reply from acc NOW, while
        # the session is established and the worker is still free. The blocking
        # session_send call comes AFTER the worker is parked in the callback.
        if op == "session_send":
            acc_m2 = fixpp.msg_create_outbound(acc, MSG_TYPE)
            fixpp.msg_set_string(acc_m2, TAG_CLORDID, "REPLY")
            acc_p2 = fixpp.msg_commit(acc_m2)

        # Send one app message; the acceptor worker delivers it to on_message.
        m = fixpp.msg_create_outbound(ini, MSG_TYPE)
        fixpp.msg_set_string(m, TAG_CLORDID, SENT)
        payload = fixpp.msg_commit(m)
        fixpp.session_send(ini, payload)
        fixpp.msg_destroy(m)

        # The callback must be provably mid-flight before we issue the decisive op.
        _wait_until(cb_entered.is_set, CB_ENTER_TIMEOUT,
                    "recv callback to enter (in-flight)")

        # Set `release` AFTER the main thread is inside the blocking op, so the
        # op is already waiting when the callback is unparked. In a no-release
        # build this thread cannot run (main holds the GIL) -> deadlock.
        def releaser():
            time.sleep(0.5)
            release.set()
        threading.Thread(target=releaser, daemon=True).start()

        # The decisive blocking op — targeting acc/eng_a, whose single worker is
        # parked mid-callback. The op co-spawns work on eng_a's ioc_ and blocks
        # (fut.get()) until the worker drains. Releases the GIL in a correct build;
        # deadlocks under the canary (FIXPP_PY_GIL_RELEASE_CANARY).
        if op == "engine_destroy":
            fixpp.engine_destroy(eng_a)
            eng_a = None
        elif op == "session_close":
            fixpp.session_close(acc)
            acc = None  # nulled: guard against double-close in finally
        elif op == "session_send":
            fixpp.session_send(acc, acc_p2)
            fixpp.msg_destroy(acc_m2)
            acc_m2 = None
        else:
            raise ValueError(f"unknown op {op!r}")
    finally:
        # Best-effort cleanup of the remaining handles (only reached on a correct
        # build; a no-release build never gets here — the parent kills the child).
        release.set()
        if acc_m2 is not None:
            fixpp.msg_destroy(acc_m2)
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
    op = sys.argv[1] if len(sys.argv) > 1 else "engine_destroy"
    raise_in_callback = len(sys.argv) > 2 and sys.argv[2] == "raise"
    run(raise_in_callback=raise_in_callback, op=op)
    print("COMPLETED")
    sys.stdout.flush()
