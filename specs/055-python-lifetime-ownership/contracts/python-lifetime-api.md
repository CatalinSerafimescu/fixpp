# Contract — PY-004 Python OO lifetime/ownership API (055)

The user-facing Python OO surface and its behavioral guarantees. The flat substrate (`fixpp.session_send(...)`, etc.) **survives unchanged** (additive); these classes are re-exported from the `fixpp` package as the primary surface. Scope per research D-1 (lifetime layer over the existing C-ABI; no §4.5 6-method director).

## C-1 — Class surface (shapes; lifetime-relevant members)

```python
class Engine:
    def __init__(self, config) -> None: ...      # rejects sub-interpreter -> SubInterpreterRejected (1201)
    def open_session(self, ...) -> "Session": ... # wraps fixpp_session_open; child tracked weakly
    def start(self) -> None: ...
    def close(self) -> None: ...                  # ordered teardown (C-3); idempotent
    def __enter__(self) -> "Engine": ...
    def __exit__(self, *exc) -> None: ...          # calls close()
    # not pickleable (C-5)

class Session:
    def send(self, msg: "Message") -> None: ...    # _dead -> ObjectLifetime; _in_callback -> CallbackReentrantClose
    def create_message(self, msg_type: str) -> "Message": ...  # wraps fixpp_msg_create_outbound; outbound flavour (Python-owned handle, NOT _is_inbound); _dead -> ObjectLifetime
    def register_application(self, app) -> None: ... # owns app; DECREF prior on re-register (C-4)
    def close(self) -> None: ...                   # ordered teardown (C-3); idempotent; _in_callback -> CallbackReentrantClose
    def __enter__(self) -> "Session": ...
    def __exit__(self, *exc) -> None: ...

class Message:
    def get_string(self, tag: int) -> str: ...     # _dead -> ObjectLifetime (all accessors)
    def set_string(self, tag: int, value: str) -> None: ...  # inbound -> CapiError(code=4)
    # (richer accessors get_int/double/decimal/bytes/has_tag/get_group/clone: available in C-ABI,
    #  wrapped as needed; exhaustive wrapping is callback-surface completeness, deferred per D-1/D-8)
    def destroy(self) -> None: ...                 # outbound: fixpp_msg_destroy (idempotent); inbound: no-op

class Application:                                  # v1.0: inbound callback only (D-1)
    def fromApp(self, session: "Session", msg: "Message") -> None: ...

class Dictionary:
    @staticmethod
    def load_xml(path: str) -> "Dictionary": ...
    def destroy(self) -> None: ...
```

## C-2 — Liveness sentinel (the no-UAF guarantee)

- Every method that would call the C-ABI MUST check `self._dead` FIRST. If `_dead` is True → raise `fixpp.ObjectLifetime` (numeric 1202) and DO NOT call the C-ABI.
- An inbound `Message` is `_dead` after the `fromApp` callback returns (armed by the trampoline before GIL release).
- Any wrapper is `_dead` after its owning parent's `close()`.
- **Guarantee**: no use-after-free / segfault is reachable from Python for post-close or post-window access (SC-001; ASan-witnessed via seams #3/#8).

## C-3 — Ordered close-flow (the teardown guarantee)

`Engine.close()`:
1. **Reentrancy preflight (pure-Python, BEFORE any C-ABI or any session close):** walk **all** child sessions in `_sessions` and check each `_in_callback`; if **any** is True, raise `fixpp.CallbackReentrantClose` (1204) immediately — without closing any session or calling any C-ABI (C-6; `[2m §6.5]` enforcement "walk all child sessions checking each `_in_callback`"). This prevents a half-closed engine — a sibling session being torn down (native side effects) before the loop reaches the in-callback session's fail-fast. The per-session `_in_callback` raise inside `Session.close()` (below) is a backstop, not the primary gate, for `engine.close()`.
2. Arm `self._dead = True` **FIRST — before any GIL-releasing teardown**. The 053/054 blocking wrappers release the GIL across both the child `session.close()` calls and `fixpp_engine_destroy`, so the engine close-flow is not GIL-atomic; arming `_dead` first means a concurrent `engine.open_session(...)` / `engine.start()` / any other public engine method racing that window sees `_dead` and raises `fixpp.ObjectLifetime` (1202) rather than entering the C-ABI against a tearing-down engine.
3. For each `Session` in `_sessions` (WeakSet): call a **private unguarded close helper** for that child session. *(The engine's own `_dead` is already armed, so the teardown path must not route through public `_dead`-guarded engine methods; this mirrors `Session.close()` step 4 calling `fixpp_session_close(self._handle)` directly.)*
4. Call a **private unguarded engine-destroy helper** that invokes `fixpp_engine_destroy(self._handle)` directly.
5. `self._was_explicitly_closed = True`.

`Session.close()`:
0. **Reentrancy fail-fast (pure-Python, BEFORE arming `_dead`, walking children, dropping `_application`, or ANY C-ABI call):** `if self._in_callback: raise fixpp.CallbackReentrantClose (1204)`. This is the **per-session backstop**; `Engine.close()`'s all-sessions preflight (above, the primary gate on the engine path) raises before any sibling is touched, so the two compose without double-raise. Without this step a `session.close()` invoked from inside its own callback would arm `_dead`, drop `_application`, and enter the blocking native close = the exact L-054-1 strand deadlock the feature prevents (SC-007). The `_in_callback` check precedes the NEW-P2a `_dead`-first ordering below (the reentrancy check simply comes first; it does not re-order the rest).
1. Arm `self._dead = True` **FIRST — before the native close** (NEW-P2a). The 053/054 blocking wrappers release the GIL across `fixpp_session_close`, so the close-flow is not GIL-atomic; arming `_dead` first means a concurrent `session.send()` racing in that window sees `_dead` and raises `fixpp.ObjectLifetime` rather than entering the C-ABI against a tearing-down session. **Safe** because step 4 calls `fixpp_session_close(self._handle)` **directly**, not via a `_dead`-guarded accessor.
2. For each `Message` in `_messages` (WeakSet): set `_dead = True`. *(BEFORE the native close, so no accessor races the free.)*
3. Drop `self._application` — the **Python** callback ref (this **releases the binding's reference to the registered callable**: the leak fix, FR-011 / SC-002 — once the caller's external strong refs are also dropped, a `weakref` to it is dead after `gc.collect()`). It also breaks the Application↔Session cycle. The native registration's strong ref is to the **`Session`** (`userdata` = the INCREF'd `Session`), **NOT** to the callable, and is NOT released yet.
4. `fixpp_session_close(self._handle)`. *(dispatch into the callback stops here.)*
5. Release the C-ABI registration `userdata` (the INCREF on the **`Session`**) — **only now**, AFTER the native close (releasing it before step 4 would let an in-flight callback dispatch into a finalized `Session` wrapper → UAF; C-4 / research D-7).
6. `self._was_explicitly_closed = True`.

- `close()` is **idempotent** (a second call, including a `with`-exit after an explicit close, is a no-op — guarded by `_dead`/`_was_explicitly_closed`).
- `with Engine(config) as engine:` calls `close()` on `__exit__` deterministically.
- GC-only teardown (no explicit close) emits a `DeprecationWarning` and attempts best-effort cleanup; cross-module `__del__` order at interpreter shutdown is NOT relied upon.

## C-4 — Callback ownership (the no-leak guarantee)

- The trampoline's `userdata` is the **owning Python `Session` wrapper** (INCREF'd at registration so the native side can dispatch), **NOT** the bare callable (research D-2 / D-7). The trampoline receives the `Session` as `userdata` and reaches the callable via `session._application` (and sets `_in_callback`, builds/arms the inbound `Message`).
- **Two distinct references, distinct owners:** (1) the binding holds exactly **one binding-owned strong ref** to the **callable** — the plain Python attribute `Session._application` (the user may also hold their own external refs; the guarantee is over the binding's ref, not global object lifetime); (2) the **`Session` wrapper** is held by the C-ABI registration `userdata` INCREF (a strong ref **from C into Python**).
- **Leak fix:** the callable is released by **dropping `session._application`** (a plain Python attribute) in the close-flow (C-3 step 3) — the matched release the 053/054 hold-until-interpreter-exit retention lacked. The `Session`-`userdata` INCREF is released **AT/AFTER** `fixpp_session_close` (C-3 step 5 / research D-7), once dispatch has stopped.
- **Re-registration** is a plain attribute reassignment: `session._application = new_callable` releases the binding's reference to the prior callable (the binding then retains no further reference to it). The `userdata` (the `Session`) is **unchanged** — no native re-pointing and no `userdata` DECREF is involved.
- **No-UAF sub-guarantee**: the `Session`-`userdata` INCREF is never dropped while the native session can still dispatch (releasing it before the native close would dispatch into a finalized `Session` → UAF). `__del__`'s best-effort close uses the same ordering.
- **No-leak guarantee**: no **binding-owned** reference to the callback outlives its session — the binding retains no reference after close; once the caller's external strong refs are also dropped, a `weakref` to it is dead after `gc.collect()` post-close (via the `_application` drop) (SC-002).

## C-5 — Pickle-ban (the cross-process-safety guarantee)

- `Engine` / `Session` / `Message` / `Application` / `Dictionary` raise `TypeError("fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries")` from `__reduce_ex__`/`__reduce__`.
- No value-typed Python classes are introduced (D-9); their pickleability is deferred.

## C-6 — Reentrancy (the no-deadlock guarantee)

- `session.send` / `session.close` / `engine.close` (→ engine_destroy) called from inside an inbound callback raise `fixpp.CallbackReentrantClose` (numeric 1204) BEFORE entering the C-ABI (via the GIL-protected `session._in_callback` marker).
- **Guarantee**: no blocking-API-from-callback deadlock is reachable (SC-007; watchdog/timeout-witnessed). **Amends `[2m]` §9 seam #4 / §1.3 rule (2)** (which called send-from-callback legal) — Article XX checkpoint, Gate A reviews.

## C-7 — Construction-time guarantee

- `Engine(config)` from a non-main CPython interpreter (PEP 554) raises `fixpp.SubInterpreterRejected` (numeric 1201) before any native engine is created.

## C-8 — Invariants & boundaries

- **No `include/fix/c_api.h` change**; codes 1201/1202/1204 already exist; the `0→1` C-ABI freeze holds.
- All lifetime/reentrancy state is GIL-protected (no `threading.local`, no OS-thread-id assumptions — `[2m §1.3]` rule (4)).
- The flat substrate functions remain available and behave identically (additive; existing tests stay green — SC-004).
