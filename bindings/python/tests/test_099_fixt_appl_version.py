"""bindings/python/tests/test_099_fixt_appl_version.py — 099 W-13 (Python).

099-fixt-appl-version-validation, contracts/witnesses.md W-13, FR-014, FR-016: shape T from Python,
through a raw TCP peer as test_098_dialect_overlay.py's. Each new flat function is called by name:
session_config_set_default_appl_ver_id, session_config_set_validate_inbound_messages,
engine_config_add_dictionary, msg_version. A refused config raises fixpp.SessionError from
session_open, with its .name.

No row skips: a missing extension, dictionary or bind fails it.

Run: PYTHONPATH=<build>/lib pytest bindings/python/tests/test_099_fixt_appl_version.py -v -rs
"""

import os
import socket
import threading
import time

import pytest

import fixpp

HOST = "127.0.0.1"
ENGINE_COMP_ID = "INI-099"
PEER_COMP_ID = "ACC-099"
HEARTBEAT_S = 30
STEP_TIMEOUT = 15.0
POLL_INTERVAL = 0.01

# Spelled here, not read from the binding: the values of contracts/surface.md's closed list.
KIND_APPLICATION = 1
SESSION_VT11 = 9
APPL_V50SP2 = 8
APPL_LATEST = 9


def _repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", ".."))


def _dictionary(*parts):
    return os.path.join(_repo_root(), "dictionaries", *parts)


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


def _frame(body):
    full = b"8=FIXT.1.1\x01" + b"9=" + str(len(body)).encode() + b"\x01" + body
    return full + b"10=%03d\x01" % (sum(full) & 0xFF)


def _fields(*pairs):
    return b"".join(b"%d=%s\x01" % (tag, value.encode()) for tag, value in pairs)


def _peer_header(seq):
    return _fields((34, str(seq)), (49, PEER_COMP_ID), (52, _sending_time()),
                   (56, ENGINE_COMP_ID))


def _logon_reply():
    return _frame(b"35=A\x01" + _peer_header(1) +
                  _fields((98, "0"), (108, str(HEARTBEAT_S)), (1137, "9")))


NOS = _fields((11, "C"), (21, "1"), (55, "X"), (54, "1"), (60, "20240101-00:00:00"), (40, "1"),
              (38, "1"))


def _nos(seq, *header_fields, body_tail=b""):
    return _frame(b"35=D\x01" + _fields(*header_fields) + _peer_header(seq) + NOS + body_tail)


def _tag(frame, tag):
    needle = b"\x01%d=" % tag
    at = frame.find(needle)
    if at < 0:
        return None
    start = at + len(needle)
    return frame[start:frame.index(b"\x01", start)]


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

    def write(self, data):
        self.conn.sendall(data)

    def close(self):
        if self.conn is not None:
            self.conn.close()
        self.listener.close()


def _msg_type_is(msg_type):
    return lambda frame: b"\x0135=" + msg_type + b"\x01" in frame


def _session_config(dict_h, port):
    sc = fixpp.session_config_create()
    fixpp.session_config_set_role(sc, fixpp.ROLE_INITIATOR)
    fixpp.session_config_set_comp_ids(sc, ENGINE_COMP_ID, PEER_COMP_ID)
    fixpp.session_config_set_begin_string(sc, "FIXT.1.1")
    fixpp.session_config_set_dictionary(sc, dict_h)
    fixpp.session_config_set_default_appl_ver_id(sc, "9")
    fixpp.session_config_set_validate_inbound_messages(sc, True)
    fixpp.session_config_set_security(sc, fixpp.SECURITY_INSECURE_PLAIN_TCP, None, None)
    fixpp.session_config_set_heartbeat_seconds(sc, HEARTBEAT_S)
    fixpp.session_config_set_tcp_endpoint(sc, HOST, port)
    return sc


def _engine(*dict_handles):
    ec = fixpp.engine_config_create()
    fixpp.engine_config_set_realtime_clock(ec)
    for h in dict_handles:
        fixpp.engine_config_add_dictionary(ec, h)
    return fixpp.engine_create(ec)


# ── The constants ───────────────────────────────────────────────────────────

def test_constants_are_the_closed_list():
    assert fixpp.APPL_VER_UNKNOWN == 0
    assert fixpp.APPL_VER_FIX50SP2 == APPL_V50SP2
    assert fixpp.APPL_VER_FIXLATEST == APPL_LATEST
    assert fixpp.SESSION_VER_UNKNOWN == 0
    assert fixpp.SESSION_VER_FIX44 == 5
    assert fixpp.SESSION_VER_FIXT11 == SESSION_VT11
    assert fixpp.SESSION_VER_FIXLATEST == 10
    assert fixpp.MSG_KIND_SESSION_ADMIN == 0
    assert fixpp.MSG_KIND_APPLICATION == KIND_APPLICATION


# ── The setters' refusals ───────────────────────────────────────────────────

@pytest.mark.parametrize("value", ["", "0", "1", "11", "abc"])
def test_default_appl_ver_id_refuses_a_value_outside_its_domain(value):
    sc = fixpp.session_config_create()
    try:
        with pytest.raises(fixpp.CapiError) as ei:
            fixpp.session_config_set_default_appl_ver_id(sc, value)
        assert ei.value.name == "FIXPP_ERR_CAPI_CONFIG_INVALID"
    finally:
        fixpp.session_config_destroy(sc)


# ── A refused config ────────────────────────────────────────────────────────

def test_session_open_raises_session_error_on_a_slot_miss():
    fixt11 = fixpp.dict_load_from_xml(_dictionary("FIXT11.xml"))
    eng = None
    sc = None
    try:
        eng = _engine(fixt11)  # no dictionary for the session's default
        sc = _session_config(fixt11, 1)
        with pytest.raises(fixpp.SessionError) as ei:
            fixpp.session_open(eng, sc)
        assert ei.value.name == "FIXPP_ERR_THREAD_CONFIG"
    finally:
        if sc is not None:
            fixpp.session_config_destroy(sc)  # not consumed by the refused open
        if eng is not None:
            fixpp.engine_destroy(eng)
        fixpp.dict_destroy(fixt11)


# ── The live arm ────────────────────────────────────────────────────────────

def test_shape_t_reaches_active_routes_rejects_and_reads_its_version():
    fixt11 = fixpp.dict_load_from_xml(_dictionary("FIXT11.xml"))
    fix50sp2 = fixpp.dict_load_from_xml(_dictionary("FIX50SP2.xml"))
    latest = fixpp.dict_load_from_xml(_dictionary("orchestra", "OrchestraFIXLatest.xml"))

    versions = []
    got = threading.Event()

    def on_message(inbound):
        # Runs on the engine worker with the GIL; inbound is valid only here.
        versions.append(fixpp.msg_version(inbound))
        got.set()

    peer = _Peer()
    eng = None
    try:
        eng = _engine(fixt11, fix50sp2, latest)
        sc = _session_config(fixt11, peer.port)
        sess = fixpp.session_open(eng, sc)
        fixpp.session_register_callback(sess, on_message)  # before start
        fixpp.engine_start(eng)

        peer.accept()
        assert peer.read_until(_msg_type_is(b"A")) is not None, "no Logon from the initiator"
        peer.write(_logon_reply())
        _wait_until(lambda: fixpp.session_is_established(sess), STEP_TIMEOUT,
                    "the initiator to establish")

        # An accept: 1128 routes to FIX Latest, the only dictionary declaring 3115.
        peer.write(_nos(2, (1128, "10"), body_tail=_fields((3115, "1.5"))))
        assert got.wait(timeout=STEP_TIMEOUT), "the routed frame did not reach the callback"
        assert versions == [(KIND_APPLICATION, SESSION_VT11, APPL_LATEST)]

        # The negotiated version answers an application frame with no 1128.
        got.clear()
        peer.write(_nos(3))
        assert got.wait(timeout=STEP_TIMEOUT), "the frame did not reach the callback"
        assert versions[-1] == (KIND_APPLICATION, SESSION_VT11, APPL_V50SP2)

        # A routed Reject: FIX50SP2 answers this frame and does not declare 3115.
        got.clear()
        peer.write(_nos(4, body_tail=_fields((3115, "1.5"))))
        reject = peer.read_until(_msg_type_is(b"3"))
        assert reject is not None, "no Reject for 3115 on the negotiated version"
        assert _tag(reject, 373) == b"2"
        assert _tag(reject, 371) == b"3115"

        # The validation setter is what a Reject for 1128=99 rests on.
        peer.write(_nos(5, (1128, "99")))
        reject = peer.read_until(_msg_type_is(b"3"))
        assert reject is not None, "no Reject for 1128=99"
        assert _tag(reject, 373) == b"5"
        assert _tag(reject, 371) == b"1128"
        assert not got.is_set(), "a refused frame reached the callback"
        assert len(versions) == 2
    finally:
        if eng is not None:
            fixpp.engine_destroy(eng)
        peer.close()
        fixpp.dict_destroy(latest)
        fixpp.dict_destroy(fix50sp2)
        fixpp.dict_destroy(fixt11)
