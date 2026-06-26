# Quickstart: PY-001 loopback round-trip

Illustrative end-to-end flow the `tests/test_roundtrip.py` pytest encodes. Exact enum names, msg-type, and
tag are finalized at implement time (D-3/D-7). Every wait uses a **bounded deadline that fails the test**
(D-2) — never an unbounded loop.

```python
import time, threading
import fixpp

DICT = "dictionaries/FIX44.xml"
HOST = "127.0.0.1"
MSG_TYPE = "<app msg type, no required group>"   # D-7
TAG = <app scalar string tag>                    # D-7
SENT = "hello-pyfixpp"

def wait_until(predicate, timeout=5.0, interval=0.02):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        v = predicate()
        if v:
            return v
        time.sleep(interval)
    raise AssertionError("timed out")            # FAILS the test — never hangs CI

received = {}
got = threading.Event()

def on_message(msg):                              # runs on an engine worker thread; GIL reacquired by binding
    received["value"] = fixpp.msg_get_string(msg, TAG)   # read INSIDE the callback (msg is borrowed)
    got.set()

dict_h = fixpp.dict_load_from_xml(DICT)

# ── Acceptor engine (A): open before start, bind ephemeral port, read it after start ──
eca = fixpp.engine_config_create(); fixpp.engine_config_set_realtime_clock(eca)
eng_a = fixpp.engine_create(eca)
sca = fixpp.session_config_create()
fixpp.session_config_set_role(sca, fixpp.ROLE_ACCEPTOR)
fixpp.session_config_set_comp_ids(sca, "ACCEPTOR", "INITIATOR")
fixpp.session_config_set_begin_string(sca, "FIX.4.4")
fixpp.session_config_set_dictionary(sca, dict_h)
fixpp.session_config_set_reset_seqnum_policy(sca, fixpp.RESET_SEQNUM_BILATERAL_LENIENT)
fixpp.session_config_set_tcp_endpoint(sca, HOST, 0)        # ephemeral
acc = fixpp.session_open(eng_a, sca)
fixpp.session_register_callback(acc, on_message)           # BEFORE start
fixpp.engine_start(eng_a)
port = wait_until(lambda: fixpp.session_acceptor_bound_endpoint(acc) or None)

# ── Initiator engine (B): now that the port is known ──
ecb = fixpp.engine_config_create(); fixpp.engine_config_set_realtime_clock(ecb)
eng_b = fixpp.engine_create(ecb)
scb = fixpp.session_config_create()
fixpp.session_config_set_role(scb, fixpp.ROLE_INITIATOR)
fixpp.session_config_set_comp_ids(scb, "INITIATOR", "ACCEPTOR")   # reversed
fixpp.session_config_set_begin_string(scb, "FIX.4.4")
fixpp.session_config_set_dictionary(scb, dict_h)
fixpp.session_config_set_reset_seqnum_policy(scb, fixpp.RESET_SEQNUM_BILATERAL_LENIENT)
fixpp.session_config_set_tcp_endpoint(scb, HOST, port)
ini = fixpp.session_open(eng_b, scb)
fixpp.engine_start(eng_b)

wait_until(lambda: fixpp.session_is_established(acc))
wait_until(lambda: fixpp.session_is_established(ini))

# ── Send one app message, build via the outbound C-ABI surface (D-8) ──
m = fixpp.msg_create_outbound(ini, MSG_TYPE)
fixpp.msg_set_string(m, TAG, SENT)
payload = fixpp.msg_commit(m)          # -> bytes
fixpp.session_send(ini, payload)
fixpp.msg_destroy(m)

assert got.wait(timeout=5.0), "no message received"
assert received["value"] == SENT       # SC-003: correct round-trip, not just 'arrived'

# ── Teardown (D-2 order): close sessions, destroy engines, destroy dict ──
fixpp.session_close(ini); fixpp.session_close(acc)
fixpp.engine_destroy(eng_b); fixpp.engine_destroy(eng_a)
fixpp.dict_destroy(dict_h)
```

## SC-004 (sanitizer) local run

```bash
# Build the extension with ASan, then run the same test under it (local pre-PR gate).
cmake --preset linux-clang-asan -DFIXPP_BUILD_PYTHON=ON -B build/asan-py ...
cmake --build build/asan-py --target fixpp_py
ASAN_OPTIONS=detect_leaks=0 \
  PYTHONPATH=build/asan-py/lib \
  LD_PRELOAD=$(clang -print-file-name=libclang_rt.asan-x86_64.so) \
  pytest bindings/python/tests/test_roundtrip.py -v
```

## Definition of done (PY-001)

- `pytest bindings/python/tests/` passes in the Tier-1 `python-bindings` job (round-trip + asserted field).
- The round-trip passes once under the ASan build (SC-004).
- No `include/fix/c_api.h` change; freeze held.
