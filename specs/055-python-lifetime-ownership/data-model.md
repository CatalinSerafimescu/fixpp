# Data Model — PY-004 Python lifetime/ownership layer (055)

Entities are pure-Python wrapper objects (no new C-ABI types). Each handle-bearing wrapper holds an opaque native handle (the existing SWIG pointer) + lifetime state. State transitions are GIL-protected.

---

## E-1 — `Engine` (handle-bearing, root)

| Field | Type | Notes |
|---|---|---|
| `_handle` | opaque (`fixpp_engine_t*` via SWIG) | owned; created by `fixpp_engine_create` |
| `_dead` | bool | armed first in `close()`, before any GIL-releasing teardown |
| `_was_explicitly_closed` | bool | read by `__del__` to decide DeprecationWarning |
| `_sessions` | `weakref.WeakSet[Session]` | children, for close-flow walk |

**Relationships**: root of the graph; strong-ref'd UP by each child `Session` (Py_INCREF). Owns no parent ref.
**Validation/invariants**: construction rejects a non-main interpreter → `SubInterpreterRejected` (1201). Every public method checks `_dead` → `ObjectLifetime` (1202). Not pickleable → `TypeError`.
**Lifecycle**: `__init__` (create native + sub-interpreter check) → live → `close()` (ordered: all-sessions `_in_callback` preflight → arm own `_dead` FIRST → close all child sessions via private unguarded close helper → `fixpp_engine_destroy` via private unguarded destroy helper → `_was_explicitly_closed=True`) → dead. `close()` idempotent. `__enter__`/`__exit__`. `__del__` = best-effort + DeprecationWarning if not explicitly closed.

---

## E-2 — `Session` (handle-bearing; engine-owned native handle, explicit-close driver)

| Field | Type | Notes |
|---|---|---|
| `_handle` | opaque (`fixpp_session_t*`) | engine-owned lifetime; wrapper drives `close()` |
| `_dead` | bool | set True by own `close()` or parent `Engine.close()` |
| `_was_explicitly_closed` | bool | DeprecationWarning gate |
| `_engine` | `Engine` | **strong ref UP** (Py_INCREF) — parent cannot be GC'd first |
| `_messages` | `weakref.WeakSet[Message]` | inbound flyweights + outbound, for invalidation walk |
| `_application` | `Application` \| callable \| None | **owned** Python callback ref — the **only binding-owned** ref to the callable (the user may hold their own external refs; the guarantee is over the binding's ref). Dropped in the close-flow (C-3 step 3) to release the binding's reference to the callable (the leak fix). The C-ABI registration `userdata` is the **INCREF'd `Session`** (not the callable); it is released AT/AFTER `fixpp_session_close` (after dispatch stops) — never before, or an in-flight callback UAFs into a finalized `Session`. Re-register: reassign `_application` (old callable released); `userdata` (the `Session`) is unchanged. (D-7) |
| `_in_callback` | bool | GIL-protected reentrancy marker (D-5); set/cleared by the trampoline |

**Relationships**: child of `Engine`; parent of `Message`. `_application` ↔ `Session` is a (collectable) cycle.
**Validation/invariants**: method calls check `_dead` → `ObjectLifetime`. `send` / `close` check `_in_callback` → `CallbackReentrantClose` (1204). Not pickleable.
**Lifecycle**: created via `Engine.open_session(...)` (wraps `fixpp_session_open`) → live → `close()` (**reentrancy fail-fast FIRST**: `if _in_callback: raise CallbackReentrantClose` [1204] — before any state mutation or C-ABI, the per-session backstop against the L-054-1 deadlock; then arm own `_dead` FIRST [NEW-P2a]; walk `_messages` set `_dead`; drop `_application`; `fixpp_session_close`; release `Session`-`userdata` INCREF) → dead. Idempotent. `__enter__`/`__exit__`.

---

## E-3 — `Message` (handle-bearing; two flavours)

| Field | Type | Notes |
|---|---|---|
| `_handle` | opaque (`fixpp_msg_t*`) | inbound: engine-owned (`own=0`); outbound/clone: Python-owned |
| `_dead` | bool | inbound: armed by trampoline at callback return (closes L-053-1); any flavour: armed by `Session.close()` |
| `_is_inbound` | bool | drives `__del__` (no-op inbound vs `fixpp_msg_destroy` outbound) and inbound `set_*` rejection |
| `_session` | `Session` | **strong ref UP** (Py_INCREF) |

**Relationships**: child of `Session` (added to `Session._messages` on construction).
**Validation/invariants**: accessor checks `_dead` → `ObjectLifetime`. `set_*` on an inbound message short-circuits to `CapiError(code=4)` (`_is_inbound` flag, `[2m §1.3]` rule (1)) — distinct path from `_dead`. Not pickleable.
**Lifecycle (inbound flyweight)**: constructed by the trampoline (`own=0`, `_is_inbound=True`, `_dead=False`) → readable inside the `fromApp` window → `_dead=True` armed before GIL release on callback return → any later access raises `ObjectLifetime`; `__del__` no-op. **Lifecycle (outbound/clone)**: `_is_inbound=False`; owner-controlled until `Session.send` / `destroy()` / `__del__` (→ `fixpp_msg_destroy`, idempotent).

---

## E-4 — `Application` (user-owned callback target)

| Field | Type | Notes |
|---|---|---|
| (user subclass) | — | v1.0 scope: the inbound callback (`fromApp`-equivalent) only (D-1) |

**Relationships**: held by `Session._application` (strong) while registered; un-referenced on `Session.close()`.
**Validation/invariants**: a Python exception raised in the callback is captured by the trampoline (`PyErr_Print` → `sys.stderr`), never crosses `extern "C"` (existing 053/054 behavior). Not pickleable.
**Lifecycle**: registered via `Session.register_application(app)` / callback registration → owned by Session → released on close / re-registration (D-7). The 4 extra `[2m §4.5]` director methods (`onLogon`/`onLogout`/`toAdmin`/`fromAdmin`) are OUT of scope (D-1).

---

## E-5 — `Dictionary` (handle-bearing, root)

| Field | Type | Notes |
|---|---|---|
| `_handle` | opaque (`fixpp_dict_t*`) | owned; created by `fixpp_dict_load_from_xml` |
| `_dead` | bool | set by `close()`/`__del__` (`fixpp_dict_destroy`) |

**Relationships**: a root (engine refcounts internally per `[2i §5.3]`).
**Validation/invariants**: method checks `_dead`. Not pickleable.
**Lifecycle**: `load_xml(...)` → live → `destroy()`/`__del__` → dead.

---

## E-6 — Liveness sentinel (cross-cutting behavior, not a class)

The `(_handle, _dead)` pair on E-1..E-3/E-5. **Pre-call rule**: every method that would touch the C-ABI checks `_dead` first; if dead → raise `fixpp.ObjectLifetime` (1202) and return without calling the C-ABI. **Arming**: by the owning parent's close-flow (walks the child WeakSet) or, for inbound `Message`, by the trampoline at `fromApp` return.

---

## E-7 — In-callback marker (cross-cutting behavior)

`Session._in_callback: bool`, GIL-protected (lives on the Python `Session`, not `threading.local`). **Set** True by the trampoline on callback entry (under GIL); **cleared** before GIL release on exit. **Read** by `Session.send` / `Session.close` / `Engine.close` (the latter walks child sessions); if any is True → raise `fixpp.CallbackReentrantClose` (1204) before the C-ABI. Correct under all `[2d §4.5]` threading modes (GIL serialises; survives strand resumption on a different OS thread).

---

## E-8 — Binding error codes (pre-existing; this feature is first to RAISE them)

| Code | Symbol (`error.h`) | Python class (`fixpp.i`) | Raised by |
|---|---|---|---|
| 1201 | `FIXPP_ERR_BINDING_SUBINTERPRETER` | `SubInterpreterRejected` | `Engine.__init__` sub-interpreter check (E-1) |
| 1202 | `FIXPP_ERR_BINDING_OBJECT_LIFETIME` | `ObjectLifetime` | the liveness sentinel (E-6) |
| 1204 | `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE` | `CallbackReentrantClose` | the in-callback marker (E-7) |

All three already in `error.h` (lines 157/159/163) and `fixpp.i` (classes :168/:169/:171; mapping :251/:252/:254) from PY-003/054 → **no new error code; `0→1` freeze unaffected.** These are binding-internal codes raised Python-side (never returned across `extern "C"`).
