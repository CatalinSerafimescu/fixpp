# Phase 1 Data Model: Thin End-to-End Python Binding (PY-001)

This is a binding, not a data feature — the "entities" are the handle proxies SWIG exposes to Python, the
trampoline state, and the lifetime/ownership rules between them. No persistent storage.

## E-1 — Opaque handle proxies (SWIG `SWIGTYPE_p_*`)

Each C-ABI opaque handle becomes a non-introspectable Python proxy wrapping the raw pointer. Ownership of
the underlying native object stays with the C-ABI (the binding never `free`s these directly; it calls the
matching `*_destroy`/`*_close`).

| Proxy | Wraps | Created by | Released by | Ownership note |
|---|---|---|---|---|
| Dictionary | `fixpp_dict_t*` | `fixpp_dict_load_from_xml` | `fixpp_dict_destroy` | refcounted; session keeps its own ref (set_dictionary copies the shared_ptr) |
| EngineConfig | `fixpp_engine_config_t*` | `fixpp_engine_config_create` | consumed by `fixpp_engine_create` (else `_destroy`) | one-shot builder |
| Engine | `fixpp_engine_t*` | `fixpp_engine_create` | `fixpp_engine_destroy` | owns the io_context + workers + its sessions |
| SessionConfig | `fixpp_session_config_t*` | `fixpp_session_config_create` | consumed by `fixpp_session_open` (else `_destroy`) | one-shot builder; copied by value on open |
| Session | `fixpp_session_t*` | `fixpp_session_open` | `fixpp_session_close` (invalidates handle) | **non-owning** — keyed by SessionId; engine owns lifetime |
| OutboundMsg | `fixpp_msg_t*` | `fixpp_msg_create_outbound` | `fixpp_msg_destroy` (single-destroy) | per-message arena; tied to its session's liveness |
| InboundMsg (view) | `const fixpp_msg_t*` | trampoline (callback param) | **not released by Python** | **non-owning, dispatch-window only** (E-3) |

## E-2 — Lifetime & destroy ordering (the test's explicit contract)

PY-001 does **not** add lifetime-guard hardening (that's PY-004); instead the test uses correct, explicit
ordering, and the binding adds only the two lifetime guarantees the callback path forces (FR-013/FR-014).

Teardown order (reverse of construction), per engine:

```
close session(s)  →  engine_destroy  →  (after both engines) dict_destroy
config builders:  destroyed iff NOT consumed by create/open
outbound msg:     destroyed right after commit+send, before the next round
callable:         Py_DECREF on deregister / engine teardown (E-4)
```

State transitions a Session proxy observes (polled, never assumed):
`opened → (engine_start) → connecting → established → closed/invalidated`. The binding exposes
`is_established` (bool poll) and `acceptor_bound_endpoint` (uint16 poll, 0 = not-yet-bound); both are read
with a **bounded deadline** (D-2).

## E-3 — Inbound message view (borrowed; the FR-014 rule)

- The `const fixpp_msg_t*` handed to the trampoline is valid **only within that callback invocation**.
- SWIG proxy is created **non-owning** (`SWIG_NewPointerObj(..., own=0)`): Python must NOT call
  `fixpp_msg_destroy` on it, and reading it after the callback returns is a UAF.
- The thin test reads its scalar field (`fixpp_msg_get_string`) **inside** the callback and stores the
  resulting Python `str` (a copy) — not the proxy. Escape would require `fixpp_msg_clone` (out of scope).

## E-4 — Trampoline state (the registered-callback record)

The `register_callback` typemap binds a Python callable into the native callback slot:

| Field | Type | Rule |
|---|---|---|
| `cb` | `fixpp_recv_cb` (C fn ptr) | the fixed trampoline `void(const fixpp_msg_t*, void*)` |
| `userdata` | `void*` → `PyObject*` | the Python callable; **`Py_INCREF` on register**, `Py_DECREF` on deregister/teardown (FR-013) |
| GIL | `PyGILState_STATE` | acquired (`Ensure`) at trampoline entry, released (`Release`) at exit (FR-007) |

Invariants:
- Register MUST occur **before** `engine_start` (session.h:268; post-start → `CAPI_CONFIG_INVALID`).
- The trampoline makes **no blocking C-ABI call** (`fixpp_session_send` / `fixpp_session_close` from inside
  the callback deadlock — FR-013a). The thin test only reads a field in the callback.

## E-5 — Outbound message payload (the send unit)

- Built via `create_outbound(session, msg_type, len)` → `set_string(tag, value, len)` → `commit(&payload,
  &len)`. `commit` yields a byte span (`35=<type>\x01<tag>=<value>\x01…`, no framing tags), aliasing the
  per-message arena until the next mutation/destroy.
- The binding copies that span into a Python `bytes`; `fixpp_session_send(session, frame, len)` re-borrows
  it (engine deep-copies). Then `fixpp_msg_destroy`.
- Framing tags `8/9/34/49/52/56/10` are forbidden in the body (`set_string` → `MSG_FRAMING_TAG_FORBIDDEN`);
  the test sets only a plain application scalar tag.
