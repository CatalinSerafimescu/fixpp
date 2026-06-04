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

## Interop harness (016-interop-harness)

### Behaviors

- **B-016-1 — The thorny corpus reconciles to the FIX spec, not to a reference
  engine; a fixpp-vs-engine divergence is encoded against the spec mandate (FR-018).**
  Where a reference engine special-cases behavior fixpp does not, the corpus encodes
  fixpp's actual spec-defensible behavior and documents the divergence (disposition
  `pass`, not a known-limitation). Worked example: an inbound **Logout carrying a
  too-high MsgSeqNum** does **not** disconnect on fixpp (as QuickFIX-J#750 chose) —
  fixpp's uniform FIX-SL §4.5.3 gap-recovery takes precedence (ResendRequest, stays
  Active), recovering the gap before the Logout is dispatched; only a too-low Logout
  disconnects. *(`tests/interop/thorny/recovery/qfj-750-logout-seqnum-mismatch_test.cpp`;
  `tests/interop/thorny/CORPUS-INDEX.md` C-004; FR-018.)*

### Limitations

- **L-016-1 — The session-only interop badge does NOT discharge the `[const §VII.6]`
  business-message interop clause.** `Logon → NewOrderSingle → ExecutionReport →
  Logout` is **not** exercised by `016`; v1.0 interop is session-layer only. This is an
  **open v1.0-GA residual** (the one Gate-A adjudication carried in `plan.md`, R7) — the
  business-message matrix cells are present as `deferred:app-messages` (`status: n/a`)
  and **zero** `016` artifact claims the business flow ran. **Status: open** —
  forward-pointer to catalogue **A-001/A-006**; revisit with the application-message
  layer. *(FR-005/FR-027/SC-008; `tests/interop/KNOWN-LIMITATIONS.md`;
  `tests/interop/happy/MATRIX.md` deferred rows.)*

- **L-016-2 — Live interop is all-TLS with a server-auth `one_way_ca` baseline;
  mutual-certificate mTLS is deferred to v1.1.** fixpp ships TLS-only (no plaintext
  transport), so every live cell runs over TLS; the v1.0 baseline trusts a
  counterparty server cert (`one_way_ca`). App-layer client-cert ↔ CompID mutual mTLS
  (`mtls_ca`) is `deferred:v1.1-mtls`. **Status: wontfix for v1.0** (intentional scope
  bound). *(FR-025; `tests/interop/happy/MATRIX.md`.)*

---

## Async Logger + OTel Observability (017-log-otel)

### Feature Catalogue Rows (done)

| Row | Title | Status | /specify | PR | Tests |
|---|---|---|---|---|---|
| LOG-001 | Zero-alloc async MPSC logger — producer/consumer ring, `Level`/`Category` filtering, drain thread, `Logger::enqueue()` | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_compile_cutoff_zero_alloc.cpp` (TS-1: dual-gate zero-alloc, fill 10/50/95%), `tests/log/test_overflow_drop_newest.cpp` (TS-2: drop_newest + TSan), `tests/log/test_block_overflow_raw_thread.cpp` (TS-3: block mode raw thread), `bench/log/log_enqueue.cpp` (TS-9: mean ≤ 50 ns gate + p99/p999) |
| LOG-002 | `Sink` interface (4 pure-virtual: `open`/`emit`/`flush`/`close`) + `FileSink` (rotation+fsync) + `SyslogSink` | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_file_sink_rotation.cpp` (TS-4: rotation + archived-only keep-count + TSan), `tests/log/test_file_sink_async_fsync.cpp` (TS-5: fsync on drain thread) |
| LOG-003 | Trace-correlated log records — `trace_id`/`span_id` carried per `Record`; `FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0` macros (no `thread_local`) | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_trace_correlation.cpp` (TS-6: SLOG/ELOG/LOG0 macro tier verification; SlogTimestampIsWallClock + ElogTimestampFromMockClock — ELOG mock-clock / SLOG wall-clock) |
| LOG-004 | Compile-time level cutoff (`FIXPP_LOG_MIN_LEVEL` + `if constexpr`) + runtime category bitmask filter | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_level_and_category_filter.cpp` (TS-8: combined compile+runtime filter → `filter_count()==1`, `drop_count()==0`) |
| OBS-001 | `SessionSpans` RAII helper — lifecycle span + `ParseSpan`/`StoreSpan`/`DispatchSpan` children with explicit-parent OTel context (no `Scope`/`thread_local`) | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/otel/test_session_spans.cpp` (TS-12: session+parse spans, explicit parenting, cross-thread span_id) |
| OBS-002 | `TracerProvider`/`MeterProvider` RAII wrappers + `PrometheusExporter`/`OtlpMetricExporter` dual-reader | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/otel/test_dual_metric_export.cpp` (TS-11: counter readable via `:9464` + OTLP push), `tests/otel/test_engine_close_teardown.cpp` (provider init/shutdown/no-op fallback) |
| OBS-003 | `OtlpLogSink` — `Sink` impl translating `Record → opentelemetry::logs::LogRecord` via `BatchLogRecordProcessor` (non-blocking; no double-write; capped retries) | **done** | `017-log-otel` | #98 (squash 09a9ae1) | `tests/log/test_otlp_log_sink.cpp` (TS-10: single-write path, severity/trace/body match) |

### Behaviors

- **B-017-1 — `overflow_policy::drop_newest` preserves the oldest in-flight record.**
  When the MPSC ring is full, the producer detects the full ring and drops the record it
  is *about to enqueue* (newest = just-arriving), not an older in-flight slot. This means
  the oldest records are always preserved with an exact `drop_count()`. The stale-read
  of `read_sequence_` under `relaxed` ordering can only cause an *early* drop — safe for
  `drop_newest`. *(FR-003/FR-004; data-model §overflow_policy; TS-2.)*

- **B-017-2 — `block` overflow mode is prohibited from session-strand coroutines.**
  `overflow_policy::block` makes the producer spin-yield until a ring slot is available.
  This pins the executor OS thread at the enqueue site, which is equivalent to holding
  `std::mutex` inside a coroutine — explicitly prohibited by `[const §XI.3]`. A debug
  `FIXPP_ASSERT` fires if `block` is used from a detected session-executor thread.
  `block` is safe only from dedicated non-coroutine producer threads (e.g. background
  control-plane threads not sharing the session executor). *(FR-004; contracts/log-core.md;
  data-model §overflow_policy.)*

### Limitations

- **L-017-1 — The three log macros (`FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0`) take an
  explicit `logger_ptr` first argument — a deliberate deviation from FR-013's no-logger
  signature.** FR-013 specifies the three context-tier macros with no explicit logger
  parameter; the implementation instead requires an explicit `Logger*` first arg because
  loggers are per-engine (there is no global logger per `[const §XIII.1]`), and a
  no-arg form would require a `thread_local` or implicit injection mechanism that violates
  `[const §XIII.3]`. The explicit-logger form is the public API for v1.0; operators must
  hold and pass the logger pointer from their engine/session context.
  **Status: wontfix for v1.0** (deliberate; `thread_local` banned). *(FR-013; [const §XIII.3];
  data-model §Trace-correlation-macros.)*

- **L-017-2 — The MPSC ring advances `read_sequence_` AFTER the drain copies the record
  out of the slot, not before.** This means a slot is held for the full duration of the
  drain's `Sink::emit()` fan-out, costing at most 1 of 65,536 slots per in-flight drain
  iteration. The record is fully copied before the slot is released, so there is no
  use-after-free risk. The alternative (Disruptor copy-then-free) advances the sequence
  before fan-out; our variant is simpler and the 1/65,536 overhead is negligible.
  **Status: wontfix** (defensible design choice; single-consumer ring). *(logger.cpp drain
  loop; data-model §Logger ring invariants.)*

- **L-017-3 — `OtlpLogSink` lives in a separate `fixpp_log_otlp` target so the base
  `fixpp_log` library stays OTel-free.** Users who only need file/syslog logging do not
  pull in the OpenTelemetry C++ SDK. `fixpp_log_otlp` is an opt-in link target.
  **Status: wontfix for v1.0** (intentional layering). *(FR-018; CMakeLists.txt; [arch §4.7].)*

- **L-017-4 — TS-13 backend-selection disposition is PROVISIONAL; the quill comparison
  is deferred behind `FIXPP_LOG_SPIKE_QUILL=ON`.** The own lock-free MPSC ring is the
  v1.0 shipping candidate (`[arch §9.3]`; `[2k §1.2]`). TS-13 was executed and recorded:
  on WSL2 debug (Clang debug build) at 50% fill with 4 producers over 10M records, the
  own-ring p99 is ~1,062 ns and p999 is ~1,793 ns; Criterion A (zero-alloc under
  mallocnesia, 10% and 50% fill) passes (exit 0). The Criterion-B comparison (p99 ≤ 50 ns
  vs quill 11.x at 50% fill on reference CI hardware) is deferred — it is a **recorded,
  non-blocking metric** that does NOT gate v1.0 delivery. The backend is swappable behind
  the identical `Logger` facade without any public-API change.
  **Status: deferred** (Criterion B comparison, non-blocking). *(FR-021; [2k §1.2]; [arch §9.3];
  `.specify/decisions/017-log-otel-verify.md` TS-13 record; `bench/log/log_spike.cpp`.)*

- **L-017-5 — `SessionSpans` is a standalone helper; live session-FSM wiring is deferred
  to the future session-module feature.** 017 ships `SessionSpans` + parse/store/dispatch
  child-span types in the `otel` module, verified by TS-12 against a test/mock session.
  Constructing `SessionSpans` in the real session-FSM open path and emitting spans from
  the live message-processing coroutine is **out of scope for 017** (clarified boundary,
  scope question 1). The hand-off point is anchor §11 of `.specify/2k-log-otel.md`.
  **Status: deferred** (future session-module feature). *(FR-016; spec.md Clarification 1;
  [2k §11]; `tests/otel/test_session_spans.cpp`.)*

- **L-017-6 — `overflow_policy::drop_newest` is the only supported overflow mode for
  session-strand producers; `block` mode is prohibited from coroutine contexts.** The
  `block` policy spins until a ring slot opens, which pins the executor thread. This
  violates `[const §XI.3]` (no mutex/spin in coroutine context) and is equivalent to
  holding a `std::mutex` inside a `co_await` chain. A debug `FIXPP_ASSERT` fires if
  `block` is used from a detected session-executor thread. Operators who need guaranteed
  delivery (no drops) from a non-coroutine thread may use `block` on a dedicated
  non-session OS thread.
  **Status: wontfix** (constitutional constraint `[const §XI.3]`). *(FR-004;
  data-model §overflow_policy; B-017-2 is the positive-behavior counterpart.)*

- **L-017-8 — The `overflow_policy::block` session-strand debug guard is not implemented
  (T033/T034 deferral).** `contracts/log-core.md` and `logger.hpp` specify that a debug
  `FIXPP_ASSERT` fires if `block` is used from a session-executor thread. This guard is
  **not yet implemented** because `Logger` is intentionally session/engine-ref-free
  (`[2k §4.3]`): it holds no session executor reference, so there is no cheap hook to
  detect "am I on a session-executor thread". The raw-thread path (the only production
  use today) is correct. Session-strand misuse of `block` is a documented **caller
  obligation**, not a runtime-enforced invariant in v1.0. T033/T034 track a follow-up
  approach (e.g. a `LoggerConfig` flag or an injected `is_session_thread` predicate from
  the caller) that keeps Logger ref-free while enabling the guard.
  **Status: deferred** (T033/T034). *(FR-004; `[2k §4.3]`; `src/log/logger.cpp` block
  path comment; L-017-6 is the caller-obligation counterpart.)*

- **L-017-7 — `FIXPP_SLOG` uses `system_clock::now()` (wall-clock) for its timestamp;
  deterministic mock-clock control applies only to `FIXPP_ELOG`.** FR-006's "effective
  clock" determinism is scoped to the Engine-tier macro `FIXPP_ELOG`, which reads
  `engine.clock()->now()` (the injected `EngineConfig::clock`). `FIXPP_SLOG` carries
  only the caller's `trace_context` (trace_id + span_id) — there is no clock field in
  `trace_context` because adding one would widen the SLOG call-site API (`[2k §4.3]`
  locked surface). `FIXPP_LOG0` (Tier 3, zero context) is wall-clock by design. In
  practice, wall-clock timestamps are monotone-ish for log ordering; the gap only
  matters for test determinism, not production correctness. Threading the effective
  clock into SLOG is a future extension (see T033/T034).
  **Status: deferred** (T033/T034; would require macro-signature change and a clock
  field in `trace_context`; non-blocking for v1.0). *(FR-006; `[2k §4.3]`;
  `contracts/log-core.md` LOG-003 macro contract; `logger.hpp` FIXPP_SLOG comment;
  `tests/log/test_trace_correlation.cpp` `SlogTimestampIsWallClock`.)*

## Application callback layer (019-app-callbacks)

### Feature Catalogue Rows (done)

| Row | Title | Status | /specify | PR | Tests |
|---|---|---|---|---|---|
| APP-001 | Application callback interface (`onCreate`/`onLogon`/`onLogout`, `fromAdmin`/`fromApp`, `toAdmin`/`toApp`) + any-thread `Engine::send` | **done** | `019-app-callbacks` | (Gate B pending) | `tests/session/test_application_{inbound,business_reject,outbound,lifecycle,strand,throw}.cpp` + `test_019_g2_enablement_witness.cpp` |
| OSS-005 | QuickFIX-style Application callback interface (return-value reject/veto divergence) | **done** | `019-app-callbacks` | (Gate B pending) | see APP-001 |
| A-014 | `BusinessMessageReject(35=j)` builder (`build_business_message_reject`, emitted on `fromApp`-reject) | **done** | `019-app-callbacks` | (Gate B pending) | `tests/session/test_application_business_reject.cpp` |

### Behaviors

- **B-019-1 — Reject/veto is signalled by return value, never by an exception.** A
  `fromApp`/`fromAdmin` callback returns `unexpected(error)` to reject (→
  `BusinessMessageReject(35=j)` / session `Reject(35=3)` respectively); a `toApp` callback
  returns `unexpected(error::app_do_not_send)` to veto an outbound app message (DoNotSend),
  or another `error` to abort the send with that error surfaced to the `Engine::send`
  caller. `toAdmin` is inspect-only (admin messages are always sent). This is a deliberate
  divergence from QuickFIX's exception-based callback API to fit the fixpp no-throw house
  style (`[const §XV.9]`). *(FR-005/007/008/015; data-model reject/veto table; research D1/D2.)*

- **B-019-2 — A throwing user callback is a fatal user-contract violation → terminal
  session close.** Because every normal outcome (accept/reject/veto) is a return value, an
  exception escaping any of the 7 callbacks is unexpected: the engine catches it at the
  dispatch boundary, clears the re-entrancy guard, terminal-closes the session, and records
  `error::app_callback_threw`. The exception never reaches engine internals. *(FR-011;
  research D5; `tests/session/test_application_throw.cpp`.)*

- **B-019-3 — `Engine::send` is any-thread-safe and posts onto the per-session strand.**
  `co_await engine.send(id, payload)` looks the session up (capturing a `shared_ptr<Session>`
  keepalive that outlives the post — the 014 detached-write UAF class), posts onto the
  session's `exec_`, runs `toApp`, then the durable-before-transmit `Session::send` path.
  The awaited result carries the veto/store/write outcome (natural backpressure — no
  silent-drop queue, `[const §XV.15]`). A re-entrant `send` from inside an on-strand
  callback is enqueued behind the current dispatch (no deadlock). *(FR-006; research D3/D6;
  `tests/session/test_application_strand.cpp`.)*

### Limitations

- **L-019-1 — Outbound interception is inspect + veto only; in-place outbound message
  MODIFICATION is deferred to a later Phase-5 slice.** `toApp` may inspect the outbound
  `MessageView` and veto it (`app_do_not_send`); `toAdmin` may inspect it. Neither can
  MODIFY/stamp the outbound message in place this slice — a mutable outbound builder/view
  is a large, separable design (it would expose the `Writer`/builder mid-emit) and is not
  required for the G2 round-trip (the originator builds the full payload passed to
  `Engine::send`). The user stamps fields by constructing the payload before `send`.
  **Status: deferred** (mutable outbound interception, a later Phase-5 slice). *(FR-007/008;
  research D1; spec.md §FR-007/008 forward-pointer; contracts/application-interface.md
  §Out of contract.)*

- **L-019-2 — A single `Application` is registered per `Engine` (no per-session override).**
  `EngineConfig::application` holds one `Application` invoked for all of the engine's
  sessions, with the `SessionId` passed per call (the QuickFIX-C++/J + Fix8 model). A
  per-session `Application` override is out of scope for this slice.
  **Status: deferred** (per-session override, a later Phase-5 slice). *(FR-002;
  Clarifications 2026-06-03 Q2; data-model §EngineConfig::application.)*

- **L-019-3 — Callbacks are serialized by single-thread engine-executor confinement; a
  multi-threaded `io_context` is NOT supported this slice.** Engine-driven session entry
  points (`on_inbound_frame`, `open()`, `close()`, the admin-emit sites) are NOT hopped
  onto the per-session strand before invoking callbacks — `Session::open()` sets
  `this->exec_ = make_strand(...)` but the running engine coroutine is not dispatched
  onto it. The no-concurrent-callback invariant (FR-010 / INV-2) holds because the 015
  engine is single-executor-confined: the injected `exec_` is always a single-threaded
  `io_context` (consistent with 015 E-5 / `engine.hpp:156–160`). Promoting engine-driven
  entry points to true per-session strand-confinement (hopping each entry point onto
  `Session::executor()`) is a future Phase-5 slice; it would re-trigger Gate A.
  **Status: deferred** (true strand-confinement on engine-driven paths). *(INV-2 correction;
  gate-b/r1 FIX-4; research.md D3 clarification; data-model.md INV-2.)*

## G2 Business Messages (020-g2-business-messages)

### Feature Catalogue Rows

No new catalogue rows. A-001 (NewOrderSingle 35=D) and A-006 (ExecutionReport 35=8)
**stay `backlog`** with a partial-G2-interop-evidence gap-note (NOT a closure) — see the
`## Application Messages — Order Management` blockquote in `spec/feature-catalogue.md` and
the A-001/A-006 gap-notes in `spec/coverage-index.md`. Mints `fixpp::core::error`
enumerator `app_payload_malformed = 131` (`tests/core/test_019_error_completeness.cpp`
forward-boundary now at slot 132; exact-SET ownership of 131 by the 020 completeness gate).

### Behaviors

- **B-020-1 — Application sends now place MsgType(35) at wire field-3 with a digit-only
  BodyLength.** `Session::send_impl` (the path under `Engine::send` and `Session::send`)
  re-frames the outbound application frame so the first three wire fields are
  `8=BeginString`, `9=BodyLength`, `35=MsgType` in that order, and `9=` is digit-only /
  unpadded (`.specify/2b-wire.md`). Previously MsgType landed 7th and `9=` was zero-padded
  (`9=000045`) — accepted by fixpp's lenient parser but rejected by QuickFIX/J/cpp + Fix8.
  This corrects 019's latent opaque-path framing for ALL app sends, not just the typed
  builders. *(FR-004a; research.md D1; data-model.md INV-1; B-020 send-path framing.)*

- **B-020-2 — Application send payloads are validated fail-closed before any seqnum/transmit.**
  `Engine::send` / `Session::send` copy arbitrary opaque app bytes; `send_impl` now validates
  the payload BEFORE stamping SendingTime, peeking/assigning a seqnum, or storing/transmitting:
  it must lead with exactly one `35=` MsgType field and carry no embedded session header/trailer
  tag (`8/9/34/49/52/56/10`); empty, no-leading-`35=`, duplicate-`35=`, or embedded-tag payloads
  are rejected with `error::app_payload_malformed` (131) and consume NO seqnum. Two additional
  defensive floors are enforced (gate-b/r1 RC#2): (a) the payload must **end with SOH**
  (`pv.back() == '\x01'`) so the final field is terminated before checksum append; (b) the
  MsgType value must be **non-empty** (`first_soh > 3`, i.e. at least one byte between `35=`
  and the first SOH). Both return `app_payload_malformed` with no seqnum consumed. *(FR-016;
  data-model.md INV-8; research.md D1 opaque-payload validation.)*

- **B-020-3 — Typed minimal builders for NewOrderSingle / ExecutionReport.**
  `build_new_order_single` (Limit-only, OrdType=2) and `build_execution_report` (fully-filled
  reply: ExecType='F'/OrdStatus='2'/LeavesQty=0/CumQty=OrderQty/AvgPx=Price) are `noexcept`,
  allocation-free (stack-scratch-then-copy, INV-4 atomicity), emit the app body only (no
  session header/trailer tags), and serialize numerics via `decimal_t::format` (canonical,
  locale-independent). The READ side consumes the already-generated `fixpp::v44` flyweights.
  *(FR-001..008; data-model.md E1/E2; INV-2/3/4; `[const §VIII.5]`.)*

### Limitations

- **L-020-1 — Minimal field set only; full FIX 4.4 field/group coverage is deferred.**
  The 020 builders emit only the minimal NOS/ExecRpt fields (NOS: 11/55/54/38/40/44/60;
  ExecRpt: 37/17/150/39/55/54/151/14/6). NewOrderSingle is **Limit-only** (OrdType fixed to
  `2`, Price always required); Market orders, optional fields, and repeating groups are not
  supported. The full-coverage path is the codegen *writer-emitter* (which would emit writers
  for the entire message set). **Status: deferred** (FR-015a — codegen writer-emitter).
  *(FR-015a; research.md D3/D5 + "Forward obligations"; Deferred-work registry in CLAUDE.md.)*

- **L-020-2 — FIX 4.4 only; all-protocol-version coverage (4.2 / 5.0SP2 / FIXT.1.1) is
  scheduled post-v1.0.** The typed builders + live interop cells negotiate FIX 4.4 only
  (matching 016/018 and the generated `fixpp::v44` flyweights). NewOrderSingle/ExecutionReport
  over 4.2 / 5.0SP2 / FIXT.1.1 (interop roadmap G4 axis) are not covered. **Status: deferred**
  (FR-015b — scheduled post-v1.0). *(FR-015b; research.md D8 + "Forward obligations";
  Deferred-work registry in CLAUDE.md.)*

- **L-020-3 — ExecType/OrdStatus enum values are not validated against the FIX 4.4 enum set;
  only printable, non-control ASCII (0x20–0x7E) is enforced.** The builders accept any
  printable char for `exec_type`/`ord_status` (e.g. `'Z'` succeeds). Caller-supplied chars
  are printable-floor-checked only; full FIX 4.4 enum-set validation (e.g. restricting
  `exec_type` to `'0'/'1'/'2'/'3'/'4'/'5'/'6'/'7'/'8'/'B'/'C'/'D'/'E'/'F'/'G'/'H'/'I'/'J'`)
  is deferred (FR-015a). The `exec_type='F'` / `ord_status='2'` (fully-filled) contract is a
  caller/harness obligation (data-model.md E2/E3), not a builder precondition.
  **Status: deferred** (FR-015a). *(gate-b/r1 RC#4; contracts/business-messages.md §Conventions.)*
