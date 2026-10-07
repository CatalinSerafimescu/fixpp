"""bindings/python/tests/test_093_inbound_frame_dispositions.py — 093 Python arms.

093-inbound-frame-dispositions, contract C-7 rows 7 and 8 (quickstart Q-30) and
Q-16's Python arm (tasks.md T092):

- session_config_set_logon_timeout_ms: zero raises the typed exception for
  FIXPP_ERR_CAPI_CONFIG_INVALID; a positive value is accepted;
- session_garbled_frame_count: returns an int, 0 before the session exists and
  the count after a garble;
- Q-16: a timeout set through the binding is honoured at T.

The C ABI has only a real-time clock, so Q-16's arm is a timing band on wall
time, the same band and derivation as the C arm
(tests/capi/inbound_frame_dispositions_capi_test.cpp): T = 500 ms, an initiator
whose peer never answers the Logon, the close at an elapsed time >= T from a
stamp taken before the engine starts and < 5 s, half the 10 s default, so an
ignored setter closes at the default and fails.

The peer is a raw TCP listener written here: it accepts the initiator's
connection, reads its Logon and writes hand-built FIX 4.4 frames.

Wheel-suite port (093, plan OD-27): ``_dict_path`` resolves through the installed
locator (``_wheeldict``) instead of a repo-relative path.
"""

import socket
import time

import pytest

import fixpp

import _wheeldict

HOST = "127.0.0.1"
ENGINE_COMP_ID = "INI-93"
PEER_COMP_ID = "ACC-93"
HEARTBEAT_S = 30
STEP_TIMEOUT = 5.0
POLL_INTERVAL = 0.01
CAPI_CONFIG_INVALID = 10  # FIXPP_ERR_CAPI_CONFIG_INVALID, include/fix/c_api/error.h


def _dict_path():
    return _wheeldict.resolve("FIX44")


def _wait_until(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        v = predicate()
        if v:
            return v
        time.sleep(POLL_INTERVAL)
    raise AssertionError(f"timed out after {timeout}s waiting for: {what}")


# ── Frames ──────────────────────────────────────────────────────────────────

def _sending_time():
    now = time.time()
    return time.strftime("%Y%m%d-%H:%M:%S", time.gmtime(now)) + ".%03d" % int((now % 1) * 1000)


def _frame44(body):
    full = b"8=FIX.4.4\x01" + b"9=" + str(len(body)).encode() + b"\x01" + body
    return full + b"10=%03d\x01" % (sum(full) & 0xFF)


def _fields(*pairs):
    return b"".join(b"%d=%s\x01" % (tag, value.encode()) for tag, value in pairs)


def _peer_header(seq):
    return _fields((34, str(seq)), (49, PEER_COMP_ID), (52, _sending_time()),
                   (56, ENGINE_COMP_ID))


def _logon_reply():
    return _frame44(b"35=A\x01" + _peer_header(1) +
                    _fields((98, "0"), (108, str(HEARTBEAT_S)), (141, "Y")))


def _heartbeat(seq):
    return _frame44(b"35=0\x01" + _peer_header(seq))


def _test_request(seq, test_req_id):
    return _frame44(b"35=1\x01" + _peer_header(seq) + _fields((112, test_req_id)))


class _Peer:
    """A raw acceptor: one connection, whole frames read with a deadline."""

    def __init__(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind((HOST, 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.conn = None
        self.buf = b""
        self.eof = False

    def accept(self, timeout=STEP_TIMEOUT):
        self.listener.settimeout(timeout)
        self.conn, _ = self.listener.accept()
        self.listener.close()

    def _read_more(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0 or self.eof:
            return False
        self.conn.settimeout(remaining)
        try:
            chunk = self.conn.recv(8192)
        except socket.timeout:
            return False
        if not chunk:
            self.eof = True
            return False
        self.buf += chunk
        return True

    def _pop_frame(self):
        at = self.buf.find(b"\x0110=")
        if at < 0 or len(self.buf) < at + 8 or self.buf[at + 7:at + 8] != b"\x01":
            return None
        end = at + 8
        frame, self.buf = self.buf[:end], self.buf[end:]
        return frame

    def read_until(self, pred, timeout=STEP_TIMEOUT):
        deadline = time.monotonic() + timeout
        while True:
            frame = self._pop_frame()
            while frame is not None:
                if pred(frame):
                    return frame
                frame = self._pop_frame()
            if not self._read_more(deadline):
                return None

    def wait_eof(self, deadline):
        """Reads and discards until the engine closes; True if it did by `deadline`."""
        while not self.eof:
            if not self._read_more(deadline):
                break
        return self.eof

    def write(self, data):
        self.conn.sendall(data)

    def close(self):
        if self.conn is not None:
            self.conn.close()
        self.listener.close()


def _make_initiator(dict_h, port, logon_timeout_ms=None):
    ec = fixpp.engine_config_create()
    fixpp.engine_config_set_realtime_clock(ec)
    eng = fixpp.engine_create(ec)
    sc = fixpp.session_config_create()
    fixpp.session_config_set_role(sc, fixpp.ROLE_INITIATOR)
    fixpp.session_config_set_comp_ids(sc, ENGINE_COMP_ID, PEER_COMP_ID)
    fixpp.session_config_set_begin_string(sc, "FIX.4.4")
    fixpp.session_config_set_dictionary(sc, dict_h)
    fixpp.session_config_set_security(sc, fixpp.SECURITY_INSECURE_PLAIN_TCP, None, None)
    fixpp.session_config_set_heartbeat_seconds(sc, HEARTBEAT_S)
    fixpp.session_config_set_reset_on_logon(sc, True)
    fixpp.session_config_set_reset_seqnum_policy(sc, fixpp.RESET_SEQNUM_BILATERAL_LENIENT)
    fixpp.session_config_set_tcp_endpoint(sc, HOST, port)
    if logon_timeout_ms is not None:
        fixpp.session_config_set_logon_timeout_ms(sc, logon_timeout_ms)
    sess = fixpp.session_open(eng, sc)
    return eng, sess


def _is_logon(frame):
    return b"\x0135=A\x01" in frame


# ── Q-30: the setter ────────────────────────────────────────────────────────

def test_set_logon_timeout_refuses_zero():
    sc = fixpp.session_config_create()
    try:
        with pytest.raises(fixpp.CapiError) as ei:
            fixpp.session_config_set_logon_timeout_ms(sc, 0)
        assert ei.value.code == CAPI_CONFIG_INVALID
        assert fixpp.session_config_set_logon_timeout_ms(sc, 500) is None
    finally:
        fixpp.session_config_destroy(sc)


# ── Q-30: the getter ────────────────────────────────────────────────────────

def test_garbled_frame_count_is_zero_before_the_session_exists():
    dict_h = fixpp.dict_load_from_xml(_dict_path())
    peer = _Peer()
    eng = None
    try:
        eng, sess = _make_initiator(dict_h, peer.port)
        count = fixpp.session_garbled_frame_count(sess)  # engine not started
        assert type(count) is int
        assert count == 0
    finally:
        if eng is not None:
            fixpp.engine_destroy(eng)
        peer.close()
        fixpp.dict_destroy(dict_h)


def test_garbled_frame_count_after_a_garble():
    dict_h = fixpp.dict_load_from_xml(_dict_path())
    peer = _Peer()
    eng = None
    try:
        eng, sess = _make_initiator(dict_h, peer.port)
        fixpp.engine_start(eng)
        peer.accept()
        assert peer.read_until(_is_logon) is not None, "no Logon from the initiator"
        peer.write(_logon_reply())
        _wait_until(lambda: fixpp.session_is_established(sess), STEP_TIMEOUT,
                    "the initiator to establish")
        assert fixpp.session_garbled_frame_count(sess) == 0

        # Junk before a frame start is one garbled region; the TestRequest's answer
        # shows the session has processed everything written before it.
        peer.write(b"GARBLE" + _heartbeat(2) + _test_request(3, "PY93"))
        answered = peer.read_until(lambda f: b"\x0135=0\x01" in f and b"\x01112=PY93\x01" in f)
        assert answered is not None, "the session did not answer the TestRequest"

        count = fixpp.session_garbled_frame_count(sess)
        assert type(count) is int
        assert count == 1
        assert fixpp.session_is_established(sess)
    finally:
        if eng is not None:
            fixpp.engine_destroy(eng)
        peer.close()
        fixpp.dict_destroy(dict_h)


# ── Q-16's Python arm ───────────────────────────────────────────────────────

def test_a_timeout_set_through_the_binding_is_honoured_at_t():
    t_s = 0.5
    default_s = 10.0          # SessionConfig's default T
    upper_s = default_s / 2
    dict_h = fixpp.dict_load_from_xml(_dict_path())
    peer = _Peer()
    eng = None
    try:
        eng, sess = _make_initiator(dict_h, peer.port, logon_timeout_ms=int(t_s * 1000))
        t0 = time.monotonic()
        fixpp.engine_start(eng)
        peer.accept()
        assert peer.read_until(_is_logon) is not None, "no Logon from the initiator"
        # The peer never answers the Logon.
        closed = peer.wait_eof(t0 + upper_s)
        elapsed = time.monotonic() - t0
        assert closed, f"no close within half the default timeout: elapsed {elapsed:.3f} s"
        assert elapsed >= t_s, f"closed before T: elapsed {elapsed:.3f} s"
        assert elapsed < upper_s
        assert not fixpp.session_is_established(sess)
    finally:
        if eng is not None:
            fixpp.engine_destroy(eng)
        peer.close()
        fixpp.dict_destroy(dict_h)
