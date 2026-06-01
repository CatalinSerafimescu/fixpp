# Behaviors & Limitations

Cross-feature catalogue of **non-obvious intended behaviors** and **known limitations**
of the fixpp library. This is the **source of truth** that the operator/reference
documentation harvests into a consolidated *"Behaviors & Limitations"* section at
doc-build time (sibling to [`feature-catalogue.md`](./feature-catalogue.md); `prebuild.py`
whitelists `spec/*.md` into the mdBook `docs/src/`).

Scope and conventions:

- **Behavior (B-*)** = a deliberate, shipped behavior that is surprising, divergent from
  a naïve expectation, or divergent from another FIX engine — something a user/operator
  must know but would not guess. Each cites its origin feature + anchor.
- **Limitation (L-*)** = a known gap or sharp edge in shipped code. Carries a **Status**:
  `deferred` (intentional, tracked), `follow-up` (in the deferred-work registry), or
  `wontfix` (a documented divergence we stand behind). Limitations with a backlog item
  link to the **Deferred-work registry** in `CLAUDE.md`.
- Entries are added as each feature ships (Polish / catalogue step). **Prior features
  (001–014) are not yet back-filled** — do that during the first operator-doc build by
  sweeping each `specs/<id>/contracts/realized-behavior.md` + spec "Out of scope".

---

## Runtime engine (015-runtime-engine)

### Behaviors

- **B-015-1 — Initiator emits its Logon AFTER connect (connect-then-Logon).**
  An engine-managed initiator does **not** emit the initial Logon at `open()`; the Logon
  is sent only once the transport is live (post-connect, post-handshake), over the
  rebound outbound sink. This matches QuickFIX-cpp (`setResponder()` → `generateLogon()`)
  and Fix8 (`connect()` → `send(generate_logon())`); fixpp's pre-015 per-session-direct
  model (emit-at-open) was the outlier. *(FR-003; data-model E-1a; gated by
  `SessionConfig::engine_managed`, default `false` so non-engine sessions are unchanged.)*

- **B-015-2 — `Engine::stop()` closes live transport sockets, and is mandatory before
  destruction.** `stop()` emits total-cancellation **and** closes each session's live
  transport socket, because total-cancel alone does **not** break a blocked idle
  `async_read_some` on an established TLS session (no peer EOF). `~Engine()` is a strict
  `assert(stopped())` — you **must** `co_await stop()` before destroying an `Engine`,
  **even if it was never started**. *(FR-011; data-model E-7; T018; see
  `[[feedback_engine_stop_must_close_transports_total_cancel_insufficient]]`.)*

- **B-015-3 — `lookup()` returns null for a registered-but-not-yet-open session.**
  Sessions are constructed **lazily** inside their accept/connect loop, not at
  `register_session` or `start()`. So immediately after `start()`, and for an acceptor
  with no peer yet, `lookup(id)` legitimately returns `nullptr` — null is not an error.
  *(data-model E-7 "open() sequencing"; Gate A New-3.)*

- **B-015-4 — Acceptors resolve sessions by reversed CompID against a static registry.**
  An inbound Logon is matched by reversing its `SenderCompID(49)`/`TargetCompID(56)`
  against the registered `SessionId`s (mirrors QuickFIX `lookupSession(..., true)` /
  QuickFIX/J `getReverseSessionID()`). No dynamic session provider. *(FR-005/006;
  data-model E-2; R2/R4.)*

### Limitations

- **L-015-1 — One connect+pump per initiator; multi-cycle reconnect-respin is not
  implemented.** The engine drives a single connect → handshake → Logon → read-pump per
  initiator session. `Session::close(close_mode::terminal)` is permanent
  (`lifecycle::closed_drained`), so the same `Session` cannot reconnect; a transport drop
  on an established session is session-fatal (→ `Disconnected`), not auto-respun.
  **Status: deferred** (needs fresh-Session-per-reconnect or a lifecycle re-open redesign
  — its own future feature). *(data-model E-1a; Clarifications 2026-05-31.)*

- **L-015-2 — An initiator pointed at a DOWN peer is not promptly torn down.** The
  per-session reconnect FSM uses a default `ReconnectPolicy{}` with an **empty schedule**
  (0 backoff) ⇒ an unbounded busy-spin at ~100% CPU on repeated connect failure; and an
  in-flight `async_connect` is not promptly cancelled by total-cancel (~30 s, a single
  connect timeout). A *mid-connect, never-established* initiator therefore does not stop
  promptly. **Established** sessions stop cleanly (B-015-2). **Status: follow-up** —
  Deferred-work registry "015 down-peer initiator teardown"; fix = a `SessionConfig`
  reconnect-policy field wired in `open()` + a bounded/cancellable engine connect.
  *(`src/session/session.cpp:93`; a 012-transport `async_connect`-cancellation concern.)*

- **L-015-3 — Bounded below the Phase-5 service wrapper (scope).** No config-file
  parsing, no `Application` user-callback ecosystem, no store/log factory abstractions,
  no C-ABI / control-plane / observability / pybind, and no user sink for inbound
  *application* messages (the read-pump delivers every frame to the admin/session layer).
  **Status: wontfix for 015** (intentional scope bound). *(FR-013; spec "Scope guard".)*

- **L-015-4 — A `Session` must outlive all work dispatched on its executor (lifetime
  contract; not enforced at `~Session`).** `Session::dispatch_app_callback` posts a
  handler that captures `this`, and in debug/sanitizer builds the re-entrancy
  `dispatch_guard` dtor stores to `in_dispatch_` *after* the user callback returns.
  Destroying the `Session` while a dispatched callback is queued/running (incl. that
  trailing store) is a use-after-scope / data race. **Production is safe today:**
  `dispatch_app_callback` has no production callers (the `Application` app-callback path is
  Phase-5, L-015-3), and the Engine drains every session on teardown (`stop()` →
  `close(terminal)` + join-before-registry-clear; `~Engine()` asserts `stop()`). The hazard
  is only reachable by **bypassing the Engine** — constructing a raw `Session`, dispatching
  on it, and destroying it without draining `exec_` (the seam tests; fixed in
  `test_executor_compat.cpp run_combo` by a guard-less post onto `executor()` + wait).
  Unlike `~Engine()`, `~Session()` does **not** assert a drained precondition. **Status:
  follow-up — Phase-5 app-callback wiring MUST drain dispatched app work on session
  teardown** (a shared keepalive, cf. the 014 detached-write fix), and should consider a
  debug `~Session` guard once the precise "no in-flight executor work" invariant is
  trackable. *(`include/fixpp/session/session.hpp dispatch_app_callback`; CI-TSan, 2026-06-01.)*
