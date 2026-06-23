# Contract — Send + receive callback (CA-006 / CA-007)

```c
/* THREAD: REQUIRES_SESSION_LOCK (Gate-A may bless THREAD_SAFE, research D-7)
   THUNK: steady-state (any escaping exception → fatal-log + abort, NOT translated) */
fixpp_error_t fixpp_session_send(fixpp_session_t* session,
                                 const uint8_t* frame, size_t len);

/* the receive callback type — invoked on the session strand, on a fixpp-owned worker thread */
typedef void (*fixpp_recv_cb)(const fixpp_msg_t* inbound, void* userdata);

/* THREAD: SINGLE_THREAD · THUNK: construction-time · MUST be called before fixpp_engine_start */
fixpp_error_t fixpp_session_register_callback(fixpp_session_t* session,
                                              fixpp_recv_cb cb, void* userdata);
```

**Send (CA-006)**
- Maps to `Engine::send(SessionId, std::span<const std::byte>{frame, len})` (`engine.hpp:266`). `frame` is a **committed wire frame** (the consumer obtains it from Feature C's `fixpp_msg_commit`, or hand-rolls/golden it — `[2i §10]`). The engine path: `toApp` veto → `Session::send` → `store_then_emit` (durable-before-transmit, FR-009, honoured by reference).
- Returns (translated): `FIXPP_ERR_OK`; `FIXPP_ERR_APP_DO_NOT_SEND` (toApp veto, 129); `FIXPP_ERR_APP_CALLBACK_THREW`; `FIXPP_ERR_SESSION_INVALID_STATE` (id registered, not Active, 77); `FIXPP_ERR_SESSION_INVALID_ARGUMENT` (id not registered, 119); `FIXPP_ERR_CANCELLED` (total-cancel, uniform `[2i §4.9]`).
- **Steady-state**: an exception escaping the thunk is an invariant violation → fatal-log + `std::abort` (FR-008/016/019); verified by the §5.2 5b fault-injection fixture (SIGABRT trap, SC-006).
- The `frame` buffer is **borrowed** (read-only during the call; the engine deep-copies into the per-session store path); the caller may free/reuse on return.

**Receive (CA-007)**
- `fixpp_session_register_callback` populates the `CapiApplication` `SessionId → {cb, userdata}` map (E-5). Must precede `fixpp_engine_start` (v1.0; no concurrent map mutation vs on-strand read — D-4). Re-registration overwrites; `cb == NULL` clears.
- On an inbound application message, `CapiApplication::fromApp` (on the session strand) wraps the borrowed `MessageView<Index>` in a **stack** `fixpp_msg_t` (no heap, D-6) and invokes `cb(&inbound, userdata)`. Admin messages (`fromAdmin`) are NOT delivered to the app callback (v1.0).
- **Inbound `fixpp_msg_t` lifetime = the callback invocation only** (`[2i §4.6]`, FR-012). The consumer must NOT retain `inbound` or destroy it; after `cb` returns the handle is invalidated (ASan-caught use-after-return, SC-008). The consumer copies out anything it needs before returning.
- The callback runs on a **fixpp-owned worker thread**, not the consumer's — the consumer's callback body must be thread-safe w.r.t. its own state (documented; quickstart).
- v1.0 callback is **void** → `fromApp` returns `{}` (always accept). App-level reject is a documented follow-up (D-4).

> ⚠️ **DEADLOCK CONTRACT (D-10) — the callback MUST NOT make a blocking C-ABI call on its own engine/session** (`fixpp_session_send`, `fixpp_session_close`, `fixpp_engine_destroy`). The callback runs ON the session strand (the io worker thread); a blocking thunk posts onto that same strand and waits → the strand can't run the posted work until the callback returns → **deadlock** (strand serialises regardless of worker count). The "receive → send reply" pattern is expressed as **copy-out-then-send-from-another-thread** (after `fixpp_session_is_established`, or via a drain thread). NB: `Engine::send`'s own "re-entrant on-strand calls enqueued, no deadlock" applies to the *awaitable*; the blocking C wrapper reintroduces the hazard. A callback-safe non-blocking `fixpp_session_send_async` is a v1.x follow-up (L-050), NOT v1.0.
