# Contract: Python module surface (PY-001)

The thin `fixpp` Python surface PY-001 exposes. SWIG generates flat function wrappers from the C-ABI
(Pythonic classes are a later, separate concern). Names are the SWIG-wrapped C-ABI functions unless noted.
Every function returns/raises per the error model in §Error below.

## Loaded as

```python
import fixpp
```

## Functions in scope (the round-trip surface)

| Python call | Wraps | Returns (typemapped) |
|---|---|---|
| `fixpp.dict_load_from_xml(path)` | `fixpp_dict_load_from_xml` | Dictionary proxy (raises on bad path) |
| `fixpp.dict_destroy(d)` | `fixpp_dict_destroy` | None |
| `fixpp.engine_config_create()` / `engine_config_set_worker_threads(c,n)` / `engine_config_set_realtime_clock(c)` / `engine_config_destroy(c)` | `fixpp_engine_config_*` | EngineConfig proxy / None |
| `fixpp.engine_create(cfg)` / `engine_start(e)` / `engine_destroy(e)` | `fixpp_engine_*` | Engine proxy / None (`engine_create(cfg)` is a thin wrapper over the real 4-arg `fixpp_engine_create(cfg, consumer_major, consumer_minor, &out)` — it injects `FIXPP_C_ABI_VERSION_MAJOR`/`_MINOR`) |
| `fixpp.session_config_create()` | `fixpp_session_config_create` | SessionConfig proxy |
| `fixpp.session_config_set_comp_ids(c, sender, target)` | `..set_comp_ids` | None |
| `fixpp.session_config_set_begin_string(c, "FIX.4.4")` | `..set_begin_string` | None |
| `fixpp.session_config_set_role(c, role)` | `..set_role` | None |
| `fixpp.session_config_set_dictionary(c, d)` | `..set_dictionary` | None |
| `fixpp.session_config_set_security(c, kind, cert, key)` | `..set_security` | None (FR-004a; `SECURITY_INSECURE_PLAIN_TCP`, `cert`/`key` = `None` for plaintext) |
| `fixpp.session_config_set_reset_on_logon(c, flag)` | `..set_reset_on_logon` | None (FR-004a; per-role: True initiator / False acceptor) |
| `fixpp.session_config_set_heartbeat_seconds(c, n)` | `..set_heartbeat_seconds` | None (FR-004a) |
| `fixpp.session_config_set_reset_seqnum_policy(c, policy)` | `..set_reset_seqnum_policy` | None |
| `fixpp.session_config_set_tcp_endpoint(c, host, port)` | `..set_tcp_endpoint` | None |
| `fixpp.session_open(engine, cfg)` | `fixpp_session_open` | Session proxy (consumes cfg) |
| `fixpp.session_is_established(s)` | `fixpp_session_is_established` | `bool` |
| `fixpp.session_acceptor_bound_endpoint(s)` | `fixpp_session_acceptor_bound_endpoint` | `int` (0 = not yet bound) |
| `fixpp.session_register_callback(s, callable)` | `fixpp_session_register_callback` | None (callable kept alive) |
| `fixpp.session_send(s, frame_bytes)` | `fixpp_session_send` | None |
| `fixpp.session_close(s)` | `fixpp_session_close` | None (invalidates s) |
| `fixpp.msg_create_outbound(s, msg_type)` | `fixpp_msg_create_outbound` | OutboundMsg proxy |
| `fixpp.msg_set_string(m, tag, value)` | `fixpp_msg_set_string` | None |
| `fixpp.msg_commit(m)` | `fixpp_msg_commit` | `bytes` (the app payload) |
| `fixpp.msg_destroy(m)` | `fixpp_msg_destroy` | None |
| `fixpp.msg_get_string(m, tag)` | `fixpp_msg_get_string` | `str` |
| `fixpp.version_string()` | `fixpp_version_string` | `str` (existing) |

Role / policy / security enum values (`fixpp.ROLE_ACCEPTOR` / `ROLE_INITIATOR`, reset-seqnum policy
constants, and `fixpp.SECURITY_INSECURE_PLAIN_TCP`) are exposed as wrapped C-ABI enum constants — exact names
taken from `session.h` at implement time.

## Callback contract

```python
def on_message(inbound_msg):        # invoked from an engine worker thread (GIL reacquired by the binding)
    value = fixpp.msg_get_string(inbound_msg, TAG)   # MUST read inside the call; inbound_msg is borrowed
    ...                                              # do NOT store inbound_msg; do NOT call blocking fixpp.* here
fixpp.session_register_callback(acceptor_session, on_message)   # MUST be before engine_start
```

- The binding `Py_INCREF`s `on_message` (the load-bearing half) and holds it until interpreter exit for the
  thin single-callback test; DECREF-on-reregister/deregister/teardown (a session-keyed registry) is deferred
  to PY-004 (FR-013).
- `inbound_msg` is a **non-owning** view valid only for the call (FR-014).
- No `session_send` / `session_close` from inside the callback (FR-013a — deadlock). FR-013a (as-built 050 blocking send, `session.h:256-260`) **supersedes** `[2m §6.5]` / `2m-pybind.md:89` ("send-from-`fromApp` is legal"); the formal `[2m]` amendment is deferred to PY-002. See `data-model.md` E-4 provenance note.

## Error model (thin — FR-008)

- A non-OK `fixpp_error_t` from any wrapped call raises a single `fixpp.Error` whose message is the
  `fixpp_strerror` text for that code. (The typed exception hierarchy is PY-003.)
- Out-params are returned as Python values (above); the raw `fixpp_error_t` is consumed by the typemap, not
  surfaced as a return code, except where a poll legitimately returns a value (`is_established` bool,
  `acceptor_bound_endpoint` 0-until-bound).

## Out of scope (named, deferred)

Repeating-group accessors/builders (`fixpp_msg_get_group`, `fixpp_msg_group_begin`, …), `int/double/decimal`
field getters/setters, `register_send_callback` (toApp), `msg_clone`, `field_count`/`field_at` iteration,
typed exceptions (PY-003), full GIL discipline (PY-002), lifetime hardening (PY-004), wheel/pip/abi3 (PY-005).
