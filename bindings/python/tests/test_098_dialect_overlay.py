"""bindings/python/tests/test_098_dialect_overlay.py — 098 W-10 (Python).

098-dialect-overlay, contracts/witnesses.md W-10, in-tree only (no wheel twin: the
overlay fixtures are not in the wheel):

- inbound read: fixpp.dict_load_from_xml_with_overlay(FIX44.xml, the W-2 FIX44
  overlay) set on an initiator session config. A raw TCP peer written here
  accepts the connection, reads the Logon, answers it and writes the C-ABI live
  arm's frame (tests/capi/dict_overlay_capi_live_test.cpp): the overlay's custom
  pair 5010 (LENGTH) / 5011 (DATA), whose Data value holds an SOH followed by
  text shaped as a field. The callback copies msg_get_string(5011) out, and the
  row asserts it is the full counted value;
- cap row: the entry-cap overlay raises ValidatorError with .code 201. The
  class alone is not the assertion: ValidatorError covers every code 200-299.

No row skips: a missing extension, fixture or bind fails it.

Run: PYTHONPATH=<build>/lib pytest bindings/python/tests/test_098_dialect_overlay.py -v -rs
"""

import os
import socket
import threading
import time

import pytest

import fixpp

HOST = "127.0.0.1"
ENGINE_COMP_ID = "INI-098"
PEER_COMP_ID = "ACC-098"
HEARTBEAT_S = 30
STEP_TIMEOUT = 5.0
POLL_INTERVAL = 0.01
DICT_LIMIT_EXCEEDED = 201  # FIXPP_ERR_DICT_LIMIT_EXCEEDED, include/fix/c_api/error.h
LENGTH_TAG = 5010
DATA_TAG = 5011
DATA_VALUE = "AB\x0158=CD"  # an SOH, then text shaped as a field; 58 is not otherwise in the frame


def _repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", ".."))


def _base_path():
    return os.path.join(_repo_root(), "dictionaries", "FIX44.xml")


def _fixture(*parts):
    return os.path.join(_repo_root(), "tests", "dictionary", "fixtures", "098", *parts)


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


def _custom_data_frame(seq):
    return _frame44(b"35=D\x01" + _peer_header(seq) +
                    _fields((11, "ORD098"), (54, "1"), (60, _sending_time()), (40, "1"),
                            (LENGTH_TAG, str(len(DATA_VALUE.encode()))),
                            (DATA_TAG, DATA_VALUE)))


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


def _is_logon(frame):
    return b"\x0135=A\x01" in frame


# ── Inbound read ────────────────────────────────────────────────────────────

def test_custom_data_value_reads_intact():
    base, overlay = _base_path(), _fixture("real", "FIX44_overlay.xml")
    assert os.path.isfile(base), base
    assert os.path.isfile(overlay), overlay
    dict_h = fixpp.dict_load_from_xml_with_overlay(base, overlay)

    received = {}
    got = threading.Event()

    def on_message(inbound):
        # Runs on the engine worker with the GIL; inbound is valid only here.
        received["value"] = fixpp.msg_get_string(inbound, DATA_TAG)
        got.set()

    peer = _Peer()
    eng = None
    try:
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
        fixpp.session_config_set_tcp_endpoint(sc, HOST, peer.port)
        sess = fixpp.session_open(eng, sc)
        fixpp.session_register_callback(sess, on_message)  # before start
        fixpp.engine_start(eng)

        peer.accept()
        assert peer.read_until(_is_logon) is not None, "no Logon from the initiator"
        peer.write(_logon_reply())
        _wait_until(lambda: fixpp.session_is_established(sess), STEP_TIMEOUT,
                    "the initiator to establish")

        peer.write(_custom_data_frame(2))
        assert got.wait(timeout=STEP_TIMEOUT), "the frame did not reach the callback"
        assert received.get("value") == DATA_VALUE, \
            f"5011 must read the full counted value; got {received.get('value')!r}"
    finally:
        if eng is not None:
            fixpp.engine_destroy(eng)
        peer.close()
        fixpp.dict_destroy(dict_h)


# ── Cap row ─────────────────────────────────────────────────────────────────

def test_entry_cap_overlay_raises_validator_error_201():
    base, overlay = _base_path(), _fixture("capi", "FIX44_entry_cap_overlay.xml")
    assert os.path.isfile(base), base
    assert os.path.isfile(overlay), overlay
    with pytest.raises(fixpp.ValidatorError) as ei:
        fixpp.dict_load_from_xml_with_overlay(base, overlay)
    assert ei.value.code == DICT_LIMIT_EXCEEDED
