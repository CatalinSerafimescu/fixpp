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

- **L-015-2 — An initiator pointed at a DOWN peer IS promptly torn down (RESOLVED
  2026-06-11; both halves fixed in 016, PR #93 `52d4180`).** Historically the
  per-session reconnect FSM used a default `ReconnectPolicy{}` with an **empty schedule**
  (0 backoff) ⇒ an unbounded busy-spin at ~100% CPU on repeated connect failure (**cause #1**);
  and an in-flight `async_connect` was not cancelled by total-cancel, so a
  *mid-connect, never-established* initiator blocked `Engine::stop()` until the 30 s
  connect timeout ran to completion (**cause #2**). **Both are fixed.** Cause #1:
  `resolve_reconnect_policy()` defaults to `ReconnectPolicy::defaults_quickfix_compat`
  (non-zero backoff — `src/session/session.cpp:103-117`). Cause #2: an OUT
  cancellation-state filter maps any accepted cancellation to `terminal` on the in-flight
  connect (`src/transport/asio_tls_transport.cpp:912-927`), so `stop()`'s
  `cancellation_type::total` promptly aborts it. **Proof:** the `DownPeerWatchdog` cell
  (`tests/interop/happy/hp_down_peer_stop_watchdog_test.cpp`, ctest #316) aims an
  initiator at a SYN-black-holed peer (RFC 5737 `192.0.2.1:9`) and asserts `stop()`
  returns in <1.5 s against the 30 s `connect_timeout` — measured ~32 ms.
  **Established** sessions stop cleanly (B-015-2). *(012-transport
  `async_connect`-cancellation; [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]].)*

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

- **L-016-1 — RESOLVED (2026-06-11). The session-only `016` badge did NOT discharge the
  `[const §VII.6]` business-message interop clause; `020` + live validation now do.**
  `Logon → NewOrderSingle → ExecutionReport → Logout` was **not** exercised by `016`
  (v1.0 interop was session-layer only — the open v1.0-GA residual carried in `plan.md`
  R7). It is now **DISCHARGED**: the 019 `Application`-callback layer + 020 typed
  builders are driven live `Logon→NOS→ExecRpt→Logout` vs **both** QuickFIX-J and
  QuickFIX-cpp, both roles, by the 4 gated `BM-*-fix44-nos-execrpt` cells (`status: pass`,
  `matrix_disposition: live`) with banked goldens (`phase-9-harness/golden/BM-*.fix`),
  satisfying 020 SC-003. **Status: resolved** — see catalogue `[const §VII.6]` note +
  `A-001/A-006` (which stay `backlog` only for full-field / all-version codegen coverage).
  *(FR-005/FR-027/SC-008.)*

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

- **L-019-3 — ~~Callbacks are serialized by single-thread engine-executor confinement; a
  multi-threaded `io_context` is NOT supported this slice.~~ LIFTED by 023.** Engine-driven
  session entry points now run on a per-session strand (the whole role loop —
  establish/handshake/read-pump/callbacks/sends/both teardown closes — is `co_spawn`'d on
  `SessionEntry::session_strand`, and the transport I/O object is bound to that strand), and
  all engine-global control-plane state is serialized on a distinct engine **control strand**
  (registry/listeners/endpoints/counters/handle-publication + `stop()`'s teardown reads).
  **A multi-threaded `io_context` is now safe under the default configuration (SC-005).**
  **Status: LIFTED 2026-06-06 by `023-engine-session-strand`.** Earned by the FULL witness
  set passing under a clean ASan ∧ UBSan ∧ TSan matrix (exact set, not a subset —
  `[[feedback_completeness_gate_exact_set_not_subset]]`): **V-1 ∧ V-2 ∧ V-3 ∧ V-8 ∧ V-9 ∧
  V-10 ∧ V-11 ∧ V-12** (per-session teardown serialization, MT business-message round-trip
  acceptance, cross-session parallelism, control-plane public-reader race fixed by the
  D-SNAP snapshot, re-entrant-send no-deadlock + post-stop fast-fail, transport-on-strand at
  all four ctor sites, MT-safe snapshot readers + bounded-handle lease, stop-before-publish).
  TSan full suite 388/388; engine_session_strand + business_messages_roundtrip green ×3
  sanitizers. Fixes the flaky `BIO_ctrl` SEGV/UAF teardown crash
  (`[[project_business_roundtrip_bio_ctrl_segv]]`). *(FR-010/SC-005; research.md D0–D8;
  data-model.md E-0…E-7; contract C-0…C-8 / V-1…V-12.)*

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

<!-- 021-inbound-possdup-origsendingtime -->

- **B-021-1 — Inbound possible-duplicate (`PossDupFlag(43)=Y`) handling is tolerant and
  wire-conformant.** A too-low inbound message (`MsgSeqNum < expected`) bearing `43=Y` and a
  valid `OrigSendingTime(122)` is TOLERATED: the session stays `Active`, the expected inbound
  seqnum is NOT advanced, and the message is not re-applied (Arm A). Independently, any `43=Y`
  non-`SequenceReset(35=4)` inbound (any seqnum, including at-expected) is VALIDATED for
  OrigSendingTime: missing `122` → session `Reject(35=3)` with `RefTagID(371)=122`,
  `SessionRejectReason(373)=1` (RequiredTagMissing), session survives (Arm C); `122` present
  but unparseable → same Arm C disposition (`Reject 371=122/373=1`, session survives —
  an unusable `122` is treated identically to an absent one); `122 > 52` strict →
  `Reject(35=3)` `371=122`, `373=10` (SendingTimeAccuracyProblem) + `Logout` +
  `Disconnected` (Arm D). `122 == 52` is accepted. Validation runs AFTER the too-high arm
  (a forward-gap `43=Y` still issues `ResendRequest`, matching QuickFIX-cpp v1.16.0 +
  QuickFIX-J 3.0.1) and BEFORE the too-low/at-expected disposition. `SequenceReset(35=4)+43=Y`
  is exempt from the `122` requirement (Arm E — routed to the existing reset/gap-fill path).
  **Status: shipped** (021, updated gate-b/r1). *(FR-001..FR-007; data-model.md §1 INV-1/3/4;
  research.md D1/D4/D5/D6; engine-parity placement = user decision 2026-06-04.)*

- **B-021-2 — Guard-3 `SendingTime(52)` MaxLatency validation precedes Stage-1 possdup
  (matches QFJ `verify()` ordering).** Guard-3 (inbound SendingTime accuracy check, 120 s
  default threshold) runs BEFORE the Stage-1 possdup block. A `43=Y` replay carrying a
  stale or unparseable `SendingTime(52)` is therefore killed by Guard-3 — it emits
  `Reject(35=3, 371=52, 373=10)` + `Logout` + `Disconnect` — and never reaches Arm A.
  FR-001's "too-low `43=Y` must not disconnect" guarantee implicitly assumes a
  well-formed, recent `52` (which is the realistic retransmit case: a genuine replay
  re-stamps `52=now`, carrying only the original time in `122`). This ordering is
  byte-faithful to QFJ `Session.java` `isGoodTime`@1821 before `validatePossDup`@1843.
  **Status: shipped** (021, documented gate-b/r1). *(Guard-3 at `session.cpp:1670`;
  Stage-1 at `session.cpp:1860`.)*

- **L-021-1 — App possible-duplicate disposition is configurable (default DROP); admin
  duplicates are ALWAYS ignored.** For a validated too-low possible-duplicate APPLICATION
  message, `SessionConfig::redeliver_poss_dup` (default `false`) governs disposition: `false`
  drops it (no `Application::fromApp`); `true` redelivers it to `fromApp` (the replayed frame
  carries `43=Y`, so the callback sees it flagged possible-duplicate). ADMINISTRATIVE duplicates
  are ignored unconditionally, even when the knob is `true` — this asymmetry is operator-visible.
  Neither disposition advances the seqnum or disconnects. This is a PROTOCOL duplicate-discard
  (the message was already processed once; `MsgSeqNum < expected` proves it) — NOT a
  `[const §XV.15]` backpressure/queue drop; the sequence contract is exactly preserved.
  **Status: shipped** (021, FR-010). *(data-model.md §2; research.md D2; distinguish from
  [const §XV.15].)*

- **L-021-2 — The send-path `AllowPossDup` strip knob (FR-008) is DEFERRED.** This slice is
  INBOUND-ONLY. Stripping caller-supplied `43`/`122` on a plain `send` is NOT a toggle of an
  existing seam: the opaque `send_impl` copies the business body verbatim, so it requires a NEW
  boundary-anchored `43`/`122` excision parser with a delimiter-injection hostile witness
  (same hazard class as 020 RC#1) before it can ship. Intended default = STRIP, auto-resend
  always re-adds. The `allow_poss_dup` `SessionConfig` field is NOT added in this slice (only
  `redeliver_poss_dup` is). **Status: deferred** (FR-008 / research.md D7 — own future
  opaque-send-hardening slice). **SUPERSEDED by 022 (B-022-1): the knob + excision shipped.**

- **L-021-3 — The PossDup-replay live interop cells run against QuickFIX-J only; the
  QuickFIX-cpp half is waived with rationale (SC-004 not claimed fully met).** Witnessing
  fixpp's inbound PossDup tolerance live requires the counterparty to INJECT a too-low `43=Y`
  frame on command. Only QuickFIX-J can: its public `Session.send(message, allowPosDup=true)`
  preserves `PossDupFlag(43)`/`OrigSendingTime(122)`. QuickFIX-cpp v1.16.0 cannot —
  `Session::send()` unconditionally strips `43`/`122`, `sendRaw` is private, there is no
  `AllowPosDup` setting, and a too-low replay is not a behaviour a healthy QuickFIX-cpp session
  ever produces (it resends only the gap ranges it is asked for, never an already-seen frame).
  The four `PD-QFcpp-*` cells are therefore `deferred:qfcpp-no-possdup-injection` (status `n/a`)
  in `tests/interop/cell_results.yaml`. fixpp's tolerance is a RECEIVE-path property — the
  injected bytes are identical regardless of the sending engine — so it is proven LIVE against
  QuickFIX-J 3.0.1 (the four `PD-QFj-*` cells: replay-survives + malformed-dup-rejected ×
  initiator + acceptor, green under `normal` + `asan-ubsan`) and in-process by
  `test_inbound_poss_dup_tolerance.cpp` / `test_inbound_poss_dup_validation.cpp`. Consequently
  **SC-004's QuickFIX-cpp clause is waived-with-rationale, not met**; SC-001/SC-002 are
  satisfied by the QuickFIX-J live cells + the unit suite. **Status: shipped + waived**
  (2026-06-11, Item-1 live sweep). *(021 SC-001/SC-002/SC-004;
  `cell_results.yaml` deferred:qfcpp-no-possdup-injection; QFcpp `Session.cpp:534-537`.)*

## PossResend(97) inbound + AllowPosDup send-path strip (022-possresend-allowpossdup-send)

<!-- 022-possresend-allowpossdup-send — completes catalogue row S-010 (backlog → done) -->

- **B-022-1 — A plain `send` strips caller-supplied `PossDupFlag(43)` / `OrigSendingTime(122)`
  by default; the auto-resend path always re-adds them independently.** `SessionConfig::allow_pos_dup`
  (default `false`, QuickFIX-J `AllowPosDup` config-key parity) governs the plain `Session::send`
  path: `false` (default) STRIPS any caller-supplied `43`/`122` from the opaque application payload
  before framing; `true` RETAINS them verbatim (operator opt-in for callers that manage their own
  duplicate flags). The strip is a no-heap, boundary-anchored field excision behind a 022-owned
  per-field scanner that validates every post-`35=` field is `<non-empty digit-only tag>=<value>\x01`
  and fails the send CLOSED (`app_payload_malformed=131`, no seqnum consumed, no transmit) on the
  FIRST malformed field — a missing `=`, an empty/non-digit tag, or an empty field (cases the 020
  denylist floor admits). Only complete, SOH-boundary-anchored `43=…\x01`/`122=…\x01` fields are
  excised; a literal `43=` inside another field's value (no preceding SOH) is preserved (injection-safe,
  INV-2). The automatic resend/retransmission path (`build_replay_frame`) ALWAYS re-adds `43=Y`+`122`
  regardless of the knob — it never routes through `send_impl` (FR-007, structural). Default-strip is
  an intentional default wire-behavior change matching QuickFIX-cpp (unconditional strip) and
  QuickFIX-J (default strip). **Status: shipped** (022, supersedes L-021-2). *(FR-006..FR-009;
  data-model.md §2 INV-1..5; research.md D1/D2/D5/D6; contracts §C2/§C3; one site in `send_impl`.)*

- **L-022-1 — `PossResend(97)` carries NO session-level handling; it is delivered to the
  application for business-level duplicate determination.** An in-sequence application message bearing
  `PossResend(97)=Y` is processed normally (the expected inbound seqnum advances) and delivered to the
  registered `Application::fromApp` with the full `MessageView` (tag 97 readable); the session never
  rejects, drops, or disconnects it for `97`, and `97` does NOT trigger the `OrigSendingTime(122)`-required
  rule (that keys on `43=Y` only). fixpp adds NO session-level PossResend logic — matching QuickFIX-cpp
  v1.16.0 and QuickFIX-J 3.0.1, which define the field but never read it in their session layers. The
  application must perform business-level de-duplication on its own keys (e.g. `ClOrdID`). **Status:
  shipped as witness-only** (022, zero production code — clarify-confirmed). *(FR-001..FR-005;
  data-model.md §3; research.md D4; contracts §C4.)*

## Per-Session + Control-Plane Strand Binding (023-engine-session-strand)

### Feature Catalogue Rows

- **B-023-1 — The per-session strand binds the whole role loop + transport + both teardown
  closes.** Each engine-managed session runs its entire role loop (accept/connect, TLS
  handshake, read-pump, application callbacks, sends, and BOTH teardown closes — transport
  `close()` and terminal `Session::close()`) on a single `asio::strand` created per session
  (`SessionEntry::session_strand`); the transport I/O object is bound to that strand at every
  construction site (the four `asio_tls_transport` ctors + the listener-build + reconnect
  paths — INV-7/V-10). This serializes a session's TLS state against its in-flight read
  during teardown, fixing the flaky `BIO_ctrl` SEGV/UAF
  (`[[project_business_roundtrip_bio_ctrl_segv]]`). *(FR-005/FR-009; E-1/E-3/E-5; C-1/C-7.)*
- **B-023-2 — Engine-global control-plane state is serialized on a distinct control strand.**
  A single engine **control strand** (distinct from every session strand — INV-0) serializes
  ALL engine-global mutation AND `stop()`'s teardown reads: `registry_`, `listeners_`,
  `listener_endpoints_`, `accept_scope_signals_`, the outstanding/send counters, and the
  awaited handle publication/unpublication. `send` traverses caller→control→session; `stop`
  runs its whole teardown (snapshot/cancel/join/close-dispatch/clear) on the control strand —
  all non-blocking posts, no locks, no deadlock. *(FR-011/FR-012; E-0/E-2/E-4; C-0/C-2/C-6.)*
- **B-023-3 — Public synchronous readers are MT-safe via an atomically-published RCU
  snapshot; `lookup()` returns a bounded handle.** `lookup()` and `acceptor_bound_endpoint()`
  read an atomically-published immutable snapshot (`std::atomic<std::shared_ptr<const
  ReaderSnapshot>>`, standard C++20 — no `std::mutex` in our headers, §XV.9; republished on
  the control strand after every control-plane mutation) — never entering a session/control
  strand, a user-visible lock, or a blocking wait. **NOTE: this atomic is NOT lock-free**
  (`is_lock_free() == false` on the supported libc++/libstdc++ — `atomic<shared_ptr>` is
  implemented with an STL-internal lock pool); the read is wait-free of *engine* locks/strands
  but takes a brief STL-internal lock. The "lock-free" claim from earlier design notes is
  therefore dropped (V-6). Correctness is unaffected (no deadlock, no data race). `Engine::lookup()`
  changes from `Session*` to **`std::shared_ptr<Session>`** (the single recorded ABI change,
  FR-008/SC-004) — a **bounded handle**: the `Engine` must outlive any outstanding handle
  (`~Engine` debug-asserts zero outstanding leases; the lease is a debug-assert + caller
  obligation only, kept strictly separate from the `send_counter_` barrier — R7).
  *(FR-008/FR-014; E-7/INV-9/INV-9a; C-4/C-8.)*

### Limitations

- **L-023-1 — The bounded-handle `lookup()` lease is enforced in DEBUG only.** In debug
  builds `lookup()` returns an aliasing `std::shared_ptr<Session>` whose control block
  increments an engine-owned outstanding-lease counter (`~Engine` asserts it is zero). In
  release builds it is a plain `std::shared_ptr<Session>` with no counter; the
  Engine-outlives-handles precondition is a documented caller obligation, not enforced.
  **Status: by design** (R7 — never a `stop()` drain; draining on app-held leases would hang
  `stop()`). *(INV-9a; C-8; research.md R7.)*

- **L-023-2 — No dedicated `Engine::send` two-hop / establish-churn perf micro-bench
  (V-6 partial evidence).** The two-hop send (`caller→control→session`) and the D-SNAP
  snapshot read/republish are structurally new (no `Engine::send` bench existed pre-023), so
  there is no prior baseline to gate Article VIII ±5% against. A standalone micro-bench would
  be dominated by TLS-loopback setup (the strand hops are µs-scale), giving low signal. What
  IS recorded: the snapshot atomic `is_lock_free() == false` on the supported libc++/libstdc++
  (an STL-internal lock pool — see B-023-3). The binding correctness gate for this concurrency
  feature is TSan (full suite 388/388, exact witness set ×3 sanitizers). **Status: follow-up**
  — a dedicated send/establish-churn bench + baseline is a low-risk bench-only carry-forward
  (cf. the 012 RC#G handshake-bench scaffold precedent). *(V-6; research.md D7/D-SNAP;
  Article VIII.)*

## ResetOn{Logon,Logout,Disconnect} Lifecycle Reset Knobs (024-reset-refresh-on-logon)

### Behaviors

- **B-024-1 — Three `SessionConfig` knobs reset both sequence numbers to 1 at a session
  lifecycle event; default off.** `reset_on_logon` / `reset_on_logout` / `reset_on_disconnect`
  (all default `false`, QuickFIX cfg-key parity) trigger a durable reset to `{1,1}` —
  `SeqnumManager::reset_to_one()` then `MessageStore::reset()` — at, respectively, Logon, a
  Logout teardown (sent OR received), and ANY disconnect (incl. an abnormal drop). The reset
  reuses the `013` reset primitive via a shared `reset_seqnums_to_one_durable(disposition)`
  helper; it adds no new error slot, codegen, or wire field. **The initiator announces a
  ResetOnLogon via `ResetSeqNumFlag(141)=Y`** on its outbound Logon through an OR-of-three
  predicate (`(reset_on_logon || reset_on_logout || reset_on_disconnect) && seqnums=={1,1}`,
  evaluated against post-reset live state — matching QuickFIX-cpp `shouldSendReset()` /
  QuickFIX-J `isResetNeeded()`); a `reset_on_logout`/`reset_on_disconnect` session that reset
  to `{1,1}` at a prior teardown therefore also sets `141=Y` on its NEXT initiator Logon. The
  reset is wired at the **shared** initiator-Logon emission point (`emit_initiator_logon_()`),
  so it fires for both per-session-direct `open()` and engine-managed `drive_reconnect()`
  (initial lazy-connect + reconnect). The store-failure disposition is **cause-keyed**:
  knob-driven Logon = **fatal** (blocks `Active`); the `013`-only received-`141` path is
  **I-07 logged-then-proceed for non-persistent stores but fatal-when-persistent since 030**
  (see B-030-2); teardown = logged.
  The acceptor handles the two reset causes via a **cause-dependent split** (mutually exclusive
  arms): the knob-driven reset (`reset_on_logon==true`) runs **before** `check_inbound`
  (`session.cpp:1559`, fatal disposition) so a fresh peer `34=1` at local-expected>1 is
  admitted; the 013-only received-`141` reset (`peer_sent_reset && !reset_on_logon`) runs
  **after** `check_inbound`. The arms are mutually exclusive → exactly
  one `store_->reset()` per path. A logout+disconnect teardown double-trigger collapses via
  a single-fire guard — each teardown also yields exactly one observable `MessageStore::reset()`
  (`FileStore::reset()` is non-idempotent I/O). *The cause-dependent split is retained for
  admission semantics (the knob-driven reset must precede `check_inbound`). The earlier rationale
  that the split "preserves `next_inbound`==1 byte-identity" for the 013-only arm is **superseded
  by 030**: 030 restores the received-`141` next-expected-**inbound** to 2 (the consumed seq-1
  reset Logon is a surviving advance — QuickFIX reset-then-increment parity), so the inbound
  post-state is now intentionally 2 while only the OUTBOUND reply stays byte-identical at seq 1.
  See B-030-1.*
  **Status: shipped** (024; received-`141` inbound post-state corrected by 030). *(FR-001..FR-010; C2.1–C5.2; data-model disposition table.)*

### Limitations

- **L-024-1 — `RefreshOnLogon` (S-018) is NOT implemented.** *(DISCHARGED by 025 — see S-018
  catalogue row and L-025-1 below for the remaining per-reconnect re-hydrate caveat.
  Historical record preserved below.)*
  fixpp's `SeqnumManager` was never store-seeded at `open()` (it started at 1; the only
  `set_next_inbound` caller was the inbound SequenceReset handler), so there was no
  construction-time store cache to refresh on reconnect. A meaningful `RefreshOnLogon` needed
  a store→manager hydrate-on-open path (an `008`-boundary change) fixpp did not yet have.
  **The `008`-boundary dependency was discharged by 029 (S-042)**; the `RefreshOnLogon` knob
  (`refresh_on_logon=true`) was shipped by 025 (S-018). **Status: discharged** (025, S-018
  → `done`). *(Clarifications Q3; contract C7.1; catalogue S-018.)*

- **L-024-2 — A `reset_on_logon=true` INITIATOR rebases its OUTBOUND seqnum 2→1 on the
  peer's `141=Y` echo (live-found, RESOLVED — 032).** A fixpp
  initiator with `reset_on_logon=true` resets to `{1,1}`, emits `Logon(141=Y, 34=1)`
  (outbound advances 1→2), then receives the peer's Logon ack which — per QuickFIX-cpp
  and QuickFIX-J — echoes `141=Y`. fixpp's initiator Logon-ack handler treats that echo
  as a peer-requested reset (`session.cpp:3185`, `peer_ack_sent_reset_flag` arm) and calls
  `reset_seqnums_to_one_durable()`, which rebases BOTH counters to 1 — so **outbound
  regresses 2→1** and the next outbound frame would carry a duplicate `MsgSeqNum=1` (the
  031 duplicate-seqnum hazard; both real engines would reject it). 030 restored the
  INBOUND twin on this arm (`session.cpp:3360`) but left the outbound twin unfixed; the
  echo of fixpp's *own* reset should not trigger a second reset at all. **Invisible to
  in-process units** — the merged unit `ResetOnLogon_Initiator_ResetsAndEmits141`
  (`test_reset_on_lifecycle.cpp:390`) asserts the correct `peek_outbound()==2` but never
  processes a peer `141=Y` echo; the divergence surfaces only on the first live run
  (`RL-{QFcpp,QFj}-init-fix44-reset-on-logon`, both engines fail identically). The
  ACCEPTOR cells (`RL-*-acc`) are unaffected (030-fixed) and live-green. **Status: RESOLVED
  (032-initiator-reset-outbound-advance).** The initiator `peer_ack_sent_reset_flag` arm now
  restores OUTBOUND to 2 after the echo-confirmed reset (Mechanism A: restore-after-reset — the
  outbound twin of 030's inbound restore, `set_next_outbound(seqnum_min+1)` +
  `persist_outbound_advance_`, fatal-when-persistent) iff fixpp itself emitted the reset Logon at
  seq 1 (latched emit-time fact `own_logon_sent_reset_flag_` AND `reset_before_send`). The
  skip-the-reset alternative this entry originally speculated was **rejected at Gate A** (unsound
  for a fresh `bilateral_strict`-at-`{1,1}` initiator whose only durable reset on the path is this
  ack-arm reset). The harm test is now live:
  `tests/session/test_persistent_seqnum_hydrate.cpp` →
  `ResetOnLogon_Initiator_PeerAck141_OutboundStaysTwo` (asserts `peek_outbound()==2` + the SC-002
  wire witness `34=2`, no duplicate `34=1`); the 2 init interop cells flip from
  `deferred:initiator-141echo-outbound-rebase` to pass. See B-032-1.
  *(`src/session/session.cpp:3185`; sibling of 030/031; found 2026-06-11, fixed 032.)*

## RefreshOnLogon — per-logon re-hydrate knob (025-refresh-on-logon)

### Feature Catalogue Rows

- **S-018** (session) — RefreshOnLogon — reload persisted state on reconnect — `backlog → done`.
  FIX 4.0–5.0SP2, FIXT.1.1.

### Behaviors

*(The re-hydrate-on-logon behavior is described by the S-018 catalogue row; see
`feature-catalogue.md`.)*

### Limitations

- **L-025-1 — A `refresh_on_logon` re-hydrate on an ACTIVE session can transiently set the
  manager's inbound or outbound counter to a value BELOW the previously-seen in-memory high-water
  mark (store-wins DOWN, INV-RoL-4).** This is the design intent for standby topologies where the
  store reflects a primary's authoritative counter; it is NOT a violation of the 029 INV-H1
  lower-bound (which is a store ≤ manager store-side invariant, not a manager monotonicity
  constraint). However, operators using `refresh_on_logon=true` in a configuration where the
  store can lag behind the in-memory counter (e.g. a single-node session reconnecting after a
  partial in-memory-only run) should be aware that the re-hydrate will regress the in-memory
  counter to the store's (lower) value — potentially causing duplicate-seqnum acceptance or
  replay. This is suppressed by `reset_seqnum_policy = bilateral_strict` (INV-RoL-3), which
  prevents the re-hydrate entirely; under `bilateral_strict` the knob is a no-op and the
  managed counter is monotonic. `refresh_on_logon=true` is intended for **standby-only**
  topologies where the authoritative source is the external store (shared with the primary).
  **Status: documented** (research D-RoL-6; data-model.md INV-RoL-3/INV-RoL-4;
  contracts/refresh-knob.md C4). *(catalogue S-018; `session.cpp` `refresh_active_`
  suppression; test W5a INV-RoL-3 witness.)*

- **L-025-2 — The acceptor `force=true` warm re-hydrate path at `session.cpp:1754` is not
  reachable through the current engine and has no reachable test vehicle.** The production engine
  (`engine.cpp:864`) constructs a **fresh** `Session` per accepted connection; `hydrated_` is set
  at first logon and never reset, so every acceptor Logon arrives on a Session with
  `hydrated_==false` (the force latch-bypass at `:565` is never triggered on the acceptor side).
  A 2nd Logon received in `Active` state is dispatched to the dup-Logon-in-Active `Reject` arm,
  not back through the `NotConnected` Logon handler. The acceptor `force` wiring is therefore
  **dead-but-harmless**: it is correctly wired and would function if Session reuse across acceptor
  reconnect is introduced (deferred). FR-002's per-2nd+-logon re-hydrate is witnessed for the
  **initiator** role (W1/W2/W7); the acceptor receives the same re-hydrate semantics on each new
  connection via the 029 cold-hydrate spine (fresh Session → fresh `ensure_hydrated_()` call on
  the first Logon). **Status: documented, acceptor same-connection re-Logon force-bypass deferred
  pending Session reuse.** *(data-model.md W6 scope; catalogue S-018; `session.cpp:1754`.)*

## Nanosecond-resolution SendingTime (026-nanosecond-sendingtime)

### Feature Catalogue Rows

- **S-039** (session) — Configurable SendingTime(52) emit precision incl. nanoseconds + lenient
  inbound UTCTimestamp parse — `backlog → done`.

### Behaviors

- **B-026-1 — A per-session `fix_time_precision` selects `SendingTime(52)` emit precision
  (including nanoseconds); inbound parsing is leniently width-tolerant; `OrigSendingTime(122)`
  is preserved verbatim; default `millis` is a byte-identical no-op.** `SessionConfig::sending_time_precision`
  (`fix_time_precision`, default `millis`) controls the precision of every **newly-stamped
  outbound `SendingTime(52)`**: `nanos` emits the 27-char `YYYYMMDD-HH:MM:SS.sssssssss` form,
  `micros` the 24-char form, `millis` (default) the 21-char FIX 4.x form. The precision threads
  compile-time-exhaustively (non-defaulted parameter) through both stamp helpers
  (`session::stamp_sending_time`, file-local `stamp_sending_time(Clock&)`) and all 21 call sites
  — a missed site is a build error, not a silent wrong-precision frame. The inbound parser
  (`core::fix_string_to_utc_time`) is **lenient**: it accepts a bare length-17 timestamp OR a
  `.` at index 17 followed by any 1–9 sub-second digits (total length 19–27), scaling an N-digit
  fraction to nanoseconds by `10^(9−N)` — so a counterparty's nanos (or any non-standard-width)
  `52=`/`122=` parses instead of being rejected (Postel's law; matches QuickFIX-cpp). Malformed
  fractions reject via `wire_invalid_field_format`: empty fraction (`…SS.`), a non-digit fraction
  char, or >9 digits (caught by an explicit **width/length gate** before any digit parse — a
  10-digit value fits in `int64` and would not trip an arithmetic overflow). `OrigSendingTime(122)`
  on a PossDup resend echoes the **stored original** `52=` bytes verbatim — `build_replay_frame`
  byte-copies them, never re-stamping at the configured precision. MaxLatency (S-019) operates
  correctly on the parsed ns instant with no boundary-logic change. Default `millis` ⇒ every
  outbound `52=` is byte-identical to the pre-feature baseline. No new wire field, error slot,
  codegen, or C-ABI surface (formatter reuses `decimal_buffer_too_small`; parser reuses
  `wire_invalid_field_format`). **Status: shipped** (026). *(FR-001..FR-009; SC-001..SC-005;
  contract C1–C7; data-model E1–E6 / I-NST-1..6.)*

### Limitations

- **L-026-1 — Achieved sub-second resolution is bounded by the platform `system_clock::period`;
  FIXT/version-gating of sub-second precision is deferred to G4.** When `nanos` is selected the
  wire FORMAT is always 9 digits, but the achieved resolution reflects the clock's true tick:
  full nanoseconds on libstdc++ (Tier-1 Linux), coarser on platforms whose `system_clock` ticks
  at ~100 ns (e.g. MSVC, Tier-2) — there the trailing digits are `00`, a documented platform
  nuance, not a defect. fixpp is FIX.4.4-scoped, so the QuickFIX-cpp/J FIX4.2+/FIXT
  `supportsSubSecondTimestamps` version-gate is moot here; it becomes relevant when FIXT.1.1 /
  5.0SP2 land (G4). **Status: documented** — version-gating tracked for G4. *(research D6;
  spec Edge Cases; contract C6.)*

## Per-Session NextExpectedMsgSeqNum(789) fast resume (027-next-expected-msgseqnum)

### Feature Catalogue Rows

- **S-031** (session) — NextExpectedMsgSeqNum(789) in Logon — fast session resume without
  ResendRequest round-trip — `backlog → implementation-parity-4.4`.
  FIX 4.4 parity only; FIXT.1.1 / 5.0SP2 outstanding to G4.

### Behaviors

- **B-027-1 — Per-session `NextExpectedMsgSeqNum(789)`: advertise next-expected-inbound in
  Logon; honor a peer's 789 with a proactive resend that eliminates the ResendRequest
  round-trip; X>N or present-but-invalid 789 ⇒ Logout+disconnect; default off byte-identical;
  FIX 4.4 only.**
  When `SessionConfig::enable_next_expected_msg_seq_num` is `true` (default `false`),
  fixpp appends tag `789=<next_inbound>` to every outbound Logon (both the initiator's opening
  Logon and the acceptor's reply), where `<next_inbound>` is `seqnum_mgr_.next_inbound_unsafe()`
  — plain, no `+1` (the acceptor reply is built post-`check_inbound` which already advanced
  the counter, so the read is already correct; matches research D-3/E-OBO). When an inbound
  Logon carries `789=X`: (a) present-but-invalid X (parse→0, empty, non-digit, overflow) ⇒
  `Logout`+disconnect — evaluated **before** the X<N compare to close the `[1,N-1]`
  full-history-amplification path (research D-10, contract C6); (b) X>N (peer expects more
  than we have sent) ⇒ `Logout("NextExpectedMsgSeqNum too high …")`+disconnect (FR-005);
  (c) X<N ⇒ proactive resend `replay_outbound_range_(X, N-1, through_current=true)` with
  PossDup app frames + GapFill admin frames — no `ResendRequest` round-trip; (d) X==N ⇒
  in sync, no resend. The comparison basis is outbound: N = `seqnum_mgr_.peek_outbound()`
  (I-NEX-11 — never confused with the inbound counter). The acceptor's proactive resend runs
  AFTER the reply `store_then_emit` (RC#4 ordering, `:1766`). Default off (`false`) ⇒ outbound
  Logon byte-identical to pre-feature baseline; inbound `789` ignored; existing `ResendRequest`
  recovery (013) untouched. FIX 4.4 only — no FIXT / 5.0 version-gating this slice (G4).
  **Status: shipped** (027). *(FR-001..FR-009; SC-001..SC-005; contracts C1–C10; data-model
  E1–E3, I-NEX-1..12; `tests/session/test_next_expected_msgseqnum.cpp`;
  `tests/interop/happy/hp_fix44_next_expected_test.cpp`.)*

### Limitations

- **L-027-1 — 789 is both-peers-required; there is NO automatic ResendRequest fallback at
  logon when only one side enables the knob.** When `enable_next_expected_msg_seq_num=true`
  and the peer's inbound Logon carries NO `789`, the at-logon ResendRequest is suppressed
  (FR-004 suppression is unconditional when the knob is on). If fixpp has an at-logon gap
  and the peer does not send `789`, the gap is NOT proactively filled at logon time — it will
  only self-heal when the Active too-high arm emits a `ResendRequest` on the first in-sequence
  frame whose seqnum exposes the gap. Operators MUST enable `789` on BOTH endpoints (QFcpp:
  `EnableNextExpectedMsgSeqNum=Y`; QFJ: `EnableNextExpectedMsgSeqNum=Y`). This matches
  QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1, which likewise have no automatic fallback.
  **Status: by design** (FR-004/FR-009, L-027-1 deliberate divergence from a hypothetical
  mixed-mode). *(research D-7/D-11; contract C5/C9; data-model I-NEX-10.)*

- **L-027-2 — A lost proactive resend self-heals via the Active too-high arm on the next
  inbound frame; a permanent no-recover hole cannot arise from the current codebase.**
  When the behind-side partner sent 789=X and expected to receive `[X, N-1]` proactively but
  the proactive resend was lost (e.g. a transport error after the Logon exchange), the behind
  side's `next_inbound_` is still at X. The first in-sequence active frame from the far side
  (seqnum M > X) hits the `Active` too-high arm (`:1968-2009`), which issues a `ResendRequest`
  for `[X, M-1]` — recovering the gap via the normal recovery path. A true never-recover hole
  would only arise if a future change ALSO suppressed the Active too-high arm when the knob is
  on, which the current code does not do (T017 review comment annotates `:1968` as
  recovery-of-last-resort — stays active regardless of knob state). **Status: documented**
  (research D-11, data-model I-NEX-10). *(contracts C5; `session.cpp:1968`; T017 annotation.)*

## Validation-compat toggles — CheckCompID & ValidateSequenceNumbers (028-validation-compat-toggles)

### Behaviors

- **B-028-1 — `check_comp_id=false` skips the steady-state SenderCompID/TargetCompID match;
  BeginString, Logon-establishment CompID, and 013 authz remain strict; default byte-identical.**
  `SessionConfig::check_comp_id` (default `true`) controls the per-message `49`/`56` equality
  gate in the `LogonReceived/Active` inbound handler. When `false`, a frame whose
  `SenderCompID(49)` or `TargetCompID(56)` does not match the configured pair is **accepted and
  delivered** instead of triggering a disconnect (FR-001/FR-002). Three gates are deliberately
  left strict regardless of the knob: (a) `BeginString(8)` mismatch still disconnects
  (I-VCT-1); (b) the Logon-establishment CompID check in `interpret_logon` is unaffected —
  a Logon whose `49` ≠ configured `target_comp_id` is still refused (steady-state-only scope,
  I-VCT-6, FR-012); (c) the 013 `compid_authorization_policy` allow-list still refuses a
  non-allow-listed principal at Logon time (I-VCT-2). Default `true` ⇒ byte-identical no-op.
  QuickFIX-compat: QFcpp `CheckCompID=N` / QFJ `CheckCompID=N`. *(FR-001/002/003/012;
  data-model I-VCT-1/2/6; research D-2; contracts C1; `tests/session/test_validation_compat_toggles.cpp`.)*

- **B-028-2 — `validate_sequence_numbers=false` tolerates out-of-order inbound: no
  ResendRequest on a forward gap, no disconnect on a too-low; counter advances on exact match
  only; `SequenceReset(35=4) NewSeqNo` not applied; PossDup + `seq==0` + too-low-Heartbeat
  carve-outs retained; default byte-identical.**
  `SessionConfig::validate_sequence_numbers` (default `true`) controls four inbound seqnum
  enforcement sites in the `LogonReceived/Active` handler. When `false`: (1) too-high inbound
  (`seq > next_expected`) does NOT enter AwaitingResend and does NOT emit a `ResendRequest`
  (site S2); the frame falls through to a deliver-without-advance path (site S4). (2) too-low
  inbound (`seq < next_expected`) does NOT disconnect; the frame is delivered to `fromAdmin`
  (admin `MsgType`) or `fromApp` (app `MsgType`) via `parse_and_dispatch_` — counter unchanged,
  session stays `Active` (site S4, FR-004/005). (3) reset-mode `SequenceReset(35=4)` (site S6,
  before the seqnum gate) — the `apply_inbound_sequence_reset` intercept is bypassed; frame
  delivered to `fromAdmin`, counter unchanged (FR-013/I-VCT-11). (4) gapfill-mode
  `SequenceReset(35=4, 123=Y)` (site S7, after the seqnum gate) — same bypass; `NewSeqNo` NOT
  applied; an exact-match gapfill `35=4` that already advanced the counter by +1 via S5 does
  NOT additionally apply `NewSeqNo`. The inbound counter advances on exact match only
  (unchanged S5 path). Four carve-outs are retained regardless of the knob: PossDup Stage-1/
  Stage-2 handling runs on both the exact-match and out-of-order arms (I-VCT-5); `seq==0`
  (unparseable `MsgSeqNum`) remains fatal (I-VCT-10); too-low `Heartbeat(35=0)` is still
  silently dropped pre-gate (N3 carve-out at site S3); Logon-time seqnum checks are unchanged
  (steady-state-only scope, I-VCT-6, FR-012). Default `true` ⇒ byte-identical no-op.
  QuickFIX-compat: QFJ `ValidateSequenceNumbers=N`. *(FR-004/005/006/013/012;
  data-model I-VCT-3/4/5/10/11; research D-3; contracts C2; `tests/session/test_validation_compat_toggles.cpp`.)*

### Limitations

- **L-028-1 — `validate_sequence_numbers=false` disables gap detection — real gaps are silently
  accepted and messages may be processed out of order.** With the knob off, fixpp makes no
  attempt to detect or recover a missing message range: a forward gap simply delivers the
  higher-seqnum frame without issuing a `ResendRequest`, and the missed messages are never
  requested. This means the application layer may receive frames out of order or miss frames
  entirely. This knob is intended ONLY for counterparties that are known to send out-of-order
  frames as a deliberate protocol choice (e.g. a QuickFIX-J peer configured
  `ValidateSequenceNumbers=N`); using it against a conformant FIX peer will hide real gaps.
  **Status: by design** (FR-005/FR-006; research D-0/D-3; L-028-3 is the steady-state-only
  companion). *(data-model I-VCT-3; contracts C2.2.)*

- **L-028-2 — `check_comp_id=false` removes the steady-state mis-routing guard — a message
  addressed to a different CompID pair is accepted; rely on 013 authz + transport binding.**
  With the knob off, an inbound frame bearing any `SenderCompID(49)` / `TargetCompID(56)` pair
  is delivered as long as it passes the strict gates (BeginString, Logon-establishment CompID,
  013 `compid_authorization_policy`). The steady-state mis-routing guard that would normally
  reject a cross-session frame is absent. Operators using this knob should ensure adequate
  security via mTLS transport binding (where the 013 allow-list verifies the TLS identity ↔
  CompID mapping) or by ensuring the network topology is point-to-point. This knob is intended
  for counterparties known to send inconsistent CompIDs (e.g. a QuickFIX counterparty
  configured `CheckCompID=N`). **Status: by design** (FR-002/FR-003; research D-0/D-2;
  L-028-3 is the steady-state-only companion). *(data-model I-VCT-2; contracts C1.2.)*

- **L-028-3 — Both relaxations are steady-state only — Logon establishment is unaffected by
  either knob; a counterparty needing relaxed Logon-time checks is not supported.** The
  `check_comp_id` and `validate_sequence_numbers` knobs apply exclusively to the
  `LogonReceived/Active` inbound handler. The Logon-establishment paths (`NotConnected` /
  `LogonSent`) are deliberately left strict: a Logon with a mismatched `SenderCompID(49)` is
  still refused, and a Logon-time too-high `MsgSeqNum` still disconnects, regardless of either
  knob. This is a deliberate divergence from QuickFIX-J, which routes Logon through the same
  `verify()` method and therefore relaxes at Logon too (`ValidateSequenceNumbers=N` also
  suppresses Logon-time too-high checks in QFJ). The fixpp restriction keeps Logon
  establishment strict for safe session bring-up and avoids entangling the 013/024 reset FSM.
  **Status: by design** (clarify Q3 / D-4; steady-state-only scope). *(data-model I-VCT-6;
  FR-012; plan Summary "Steady-state only".)*

## Persistent seqnum continuity — bidirectional hydrate-on-open (029-persistent-seqnum-hydrate)

### Feature Catalogue Rows

- **S-042** (session) — Persistent inbound seqnum continuity — durable inbound counter +
  bidirectional hydrate-on-open; resume both directions across restart — `backlog → done`.
  FIX 4.4.

### Behaviors

*(The hydrate-on-open and persist-inbound-advance behaviors are described by the S-042 catalogue
row; see `feature-catalogue.md`.)*

### Limitations

- **L-029-1 — Post-GapFill restart yields a bounded redundant ResendRequest when 789/reset is
  available; otherwise the too-high peer Logon fatals on the Logon gate and recovers by
  reconnect; recovery is correct at-least-once in both cases.** The `persist_inbound_advance_()`
  helper uses `+1` per-delivery writes only — there is no absolute counter-set in the
  `MessageStore` interface (preserving the 4-pure-virtual cap). A prior-run
  `SequenceReset`-GapFill absolute jump (`apply_inbound_sequence_reset`) updates the in-memory
  manager but is NOT persisted. On restart the persisted counter is a **monotonic lower bound**
  of the true in-memory value (INV-H1). If the peer's outbound counter advanced past the restart
  point (e.g. it sent messages after the GapFill that the fixpp side accepted), the peer's next
  Logon will carry a `MsgSeqNum(34)` above fixpp's hydrated `next_inbound`. Two outcomes
  depending on knob state: (a) **`enable_next_expected_msg_seq_num=true`** — fixpp advertises
  `789=<hydrated_next_inbound>` in its Logon; the peer proactively resends or fixpp emits a
  `ResendRequest` for the gap; session reaches Active, residual gap recovered; bounded redundant
  resend (at most the untracked GapFill jump range). (b) **knob off** — the Logon gate has no
  ResendRequest arm; a too-high peer `MsgSeqNum(34)` triggers a fatal
  Logout+disconnect at the Logon-path check; the session reconnects and the peer resets to `1`
  (ResetOnLogon) or the gap resolves after a further handshake. Recovery is correct (at-least-once,
  no skip) in both cases; case (b) incurs an extra reconnect cycle. Operators using persistent
  stores and SequenceReset-GapFill recovery should enable `enable_next_expected_msg_seq_num` on
  both sides (QFcpp `EnableNextExpectedMsgSeqNum=Y` / QFJ `EnableNextExpectedMsgSeqNum=Y`) to
  stay on the fast-recover path. **Status: documented** (INV-H1; research D-5; plan §VI delta
  L-029-1; data-model W5/SC-004). *(contracts/seqnum-hydrate.md C2/C3; `session.cpp`
  `apply_inbound_sequence_reset`; `tests/session/test_persistent_seqnum_hydrate.cpp` W5.)*

- **L-029-2 — A swallowed I-07 outbound store-write failure in a prior run leaves the persisted
  outbound counter behind the true last-sent value; hydrate is only as fresh as the last
  successful outbound write.** The existing outbound store write (added in 008/024) is I-07
  logged-then-proceed — an outbound `next_seqnum(outbound,true)` failure is logged but does NOT
  disconnect the session (asymmetric with the inbound path where failure is fatal, per research
  D-3). If this outbound write silently fails mid-session, the FileStore's persisted outbound
  counter is behind the true `next_outbound_`. On restart, `ensure_hydrated_()` reads the stale
  persisted value and loads it into the manager — the restarted session may replay seqnums the
  peer has already seen, causing a too-low reject or unexpected seqnum jump. This is a
  **pre-existing 008/024 property** (the I-07 policy predates 029); 029 adds the hydrate path
  that makes the stale-write scenario observable but does not alter the I-07 policy. The correct
  fix (promote the outbound write failure to fatal-disconnect, matching the inbound treatment) is
  deferred as a separate store-hardening slice. Operators relying on persisted outbound counters
  should ensure the underlying `MessageStore` (e.g. `FileStore`) operates on a reliable
  filesystem. **Status: documented** (research D-3 / plan §VI delta L-029-2; pre-existing
  008/024 I-07 policy; outbound→fatal deferred). *(contracts/seqnum-hydrate.md C3; `file_store.cpp`
  outbound-write path; `tests/session/test_persistent_seqnum_hydrate.cpp` NoHeap witness.)*

- **L-029-3 — Under `reset_seqnum_policy = bilateral_strict` with a non-1 persisted outbound
  counter, the 029 cold-open hydrate seeds the manager at the stored (non-1) outbound value
  and the cold Logon is then emitted with `141=Y` AND `34=<N>` (N > 1), which is malformed per
  FIX spec when a peer validates that `ResetSeqNumFlag(141)=Y` implies `MsgSeqNum(34)=1`.
  This is a property of the `bilateral_strict` cold-open path (029's one-shot `ensure_hydrated_`
  seeds from a non-1 store, then the strict policy adds `141=Y` unconditionally); 025 does NOT
  close this gap — the per-reconnect re-hydrate (025) is suppressed under `bilateral_strict`
  (INV-RoL-3), so 025 introduces no new exposure. QuickFIX-cpp/J peers that enforce the
  `141=Y`→`34=1` invariant will reject the cold Logon; the session will disconnect+reconnect
  until the peer or the store is reset. Operators should use `bilateral_lenient` or
  `unilateral` policy when the persisted outbound counter may be > 1 at cold open.
  **Status: documented, DEFERRED** (Gate A D-RoL-6; data-model.md W5b L-029-3 gap-witness;
  025 INV-RoL-3; NOT closed by 025). *(contracts/refresh-knob.md C4; `session.cpp` strict-policy
  cold-open path; `tests/session/test_refresh_on_logon.cpp` W5b.)*

## Received-reset inbound advance correction (030-received-reset-inbound-advance)

### Feature Catalogue Rows

- Amends **S-017** (received-`141` reset machinery), **S-031** (789 advertisement),
  **S-032** (ResetSeqNumFlag(141)). No new S-row — this is a conformance correction of the
  existing received-`141` path, found via a failed live acceptor interop cell vs QuickFIX-cpp/J.

### Behaviors

- **B-030-1 — A received `Logon(141=Y)` advances next-expected-**inbound** to 2 (not 1) on
  both the acceptor and the initiator arm; the outbound reply stays seq 1.** When a peer
  initiates a sequence reset by sending `Logon(34=1, 141=Y)` and the local `reset_on_logon`
  knob is OFF (the "received-141" path), the consumed seq-1 reset Logon is an in-sequence
  message: after the post-`check_inbound` durable reset, fixpp restores next-expected-inbound
  to `seqnum_min+1` (=2) in BOTH the in-memory `SeqnumManager` (`set_next_inbound`) AND the
  durable store (a `next_seqnum(inbound, true)` write-through → `store == manager == 2`,
  INV-H1 equality). This matches QuickFIX-cpp/J, which **reset-then-increment** (net 2);
  fixpp previously increment-then-reset (net 1), which left next-expected-inbound at 1 and
  emitted a **spurious `ResendRequest`** on the peer's next genuine message at seq 2 (and,
  with 027 enabled, advertised `789=1` instead of `2`). The correction is applied symmetrically
  on the two separate-but-identical code paths: the acceptor `NotConnected` Logon handler and
  the initiator Logon-ack `peer_ack_sent_reset_flag` arm (FR-009). The **OUTBOUND reply Logon
  `MsgSeqNum` stays seq 1** (independent counter; byte-identical); only the reply `789` content
  corrects 1→2 (acceptor-reply-specific, 027-on). The `reset_on_logon=true` knob path is
  unchanged (already produced 2). **Status: shipped** (030). *(FR-001..FR-009; reference oracle
  QFcpp `Session.cpp::nextLogon` reset-then-increment, QFJ `Session.java` lines 2202-2204/2215/
  2303; `tests/session/test_reset_on_lifecycle.cpp` discriminating triple + initiator witnesses;
  `tests/session/test_reset_seqnum_policy_matrix.cpp`, `test_next_expected_msgseqnum.cpp`,
  `test_persistent_seqnum_hydrate.cpp` value-pins.)*

- **B-030-2 — On a persistent store, a received-`141` durable-reset failure now DISCONNECTS
  (was stay-Active); non-persistent stores keep stay-Active.** This amends the 024 FR-001/C2.6
  I-07 "logged-then-proceed" contract for the persistent received-`141` sub-case. Rationale: a
  swallowed (`logged`) store-reset failure on a persistent store would leave the durable counter
  stale, and the B-030-1 persist-to-2 write-through would then advance the **stale** store
  (N→N+1) — for any session that had received messages this yields `store > manager` (INV-H1
  violation → silent inbound skip on restart, the 029 over-persist harm). Making the reset
  **fatal when the store is persistent** (`store_is_persistent_ ? fatal : logged`, on both arms)
  guarantees the reset succeeded before persist-to-2 runs, so `store == manager == 2` truly holds;
  a reset failure disconnects, the session re-opens, re-hydrates the store at its last-good value
  N (a valid INV-H1 lower bound), and the peer re-drives the reset — resuming with nothing skipped.
  Non-persistent
  stores are unaffected (the reset cannot meaningfully fail; INV-H4 makes persist-to-2 a no-op).
  Aligns with 029 D-3 ("inbound-correctness failures are fatal") and the existing fatal knob-reset
  sites. **Status: shipped** (030). *(FR-010; amends B-024-1; `tests/session/test_reset_on_lifecycle.cpp`
  fault-injection witnesses + the persistent-Disconnect / non-persistent-stay-Active contract split.)*

- **B-031-1 — As an acceptor, fixpp honors a peer initiator's `NextExpectedMsgSeqNum(789)`
  against its PRE-reply next-outbound (`N_pre`), so an in-sync peer triggers no resend and the
  session establishes with no duplicate-sequence frame.** With the `789` knob on, the acceptor
  emits its reply Logon first and then honors the peer's advertised `789` (the deliberate 027
  RC#4 ordering). Because the reply Logon consumes a sequence number, the live next-outbound at
  honor time is the **post-reply** `N_post = N_pre+1`. fixpp previously compared the peer's `789`
  against `N_post`; a conformant in-sync peer advertises `789 = N_pre` (its expected target, no
  `+1`), so `N_pre < N_post` mis-classified the peer as behind-by-one and emitted a spurious
  `SequenceReset-GapFill` at the sequence number the reply Logon had just used — a duplicate-
  `MsgSeqNum` violation that QuickFIX-cpp/J reject with a "MsgSeqNum too low" `Logout`, so the
  session never established (live-found, invisible to in-process 027 unit tests; parallels 030).
  The honor now compares against `N_pre` (captured before the reply consumes a seq; parameterized
  `honor_peer_next_expected_(…, next_outbound_ref)`) for all three arms: in-sync `X==N_pre` ⇒ no
  resend; too-high `X>N_pre` ⇒ Logout (so `X==N_pre+1` in the peer's initial Logon is correctly
  too-high, not in-sync); genuine-gap `X<N_pre` ⇒ proactive resend `[X, N_pre]` (range unchanged,
  reads live `peek_outbound()-1`). The **initiator** honor is byte-identical (its peer-reply
  `789 = target+1` already matches fixpp's post-own-Logon outbound; it passes the current
  `peek_outbound()`). Reference-engine-conformant (QFcpp `Session.cpp:228/277/687/709`; QFJ
  `Session.java:2250/2312/2334` evaluate the decision against the pre-reply sender counter).
  **Status: shipped** (031). *(FR-001..FR-009; `tests/session/test_next_expected_msgseqnum.cpp`
  W1 `Acceptor_XeqNpre_NoResend_Establishes` + W3 `Acceptor_XeqNprePlus1_TooHigh_Logout`; live
  close-out via the `NE-*-acc` interop cell vs QFcpp/QFJ.)*

- **B-032-1 — As an initiator, fixpp restores its OUTBOUND seqnum to 2 (not 1) when the peer
  echoes fixpp's own `141=Y`, so it carries one post-logon frame at `34=2` with no duplicate
  `34=1`.** A `reset_on_logon`/`reset_on_logout`/`reset_on_disconnect` initiator that reset before
  sending emits `Logon(141=Y, 34=1)` (outbound 1→2); a conformant peer (QFcpp/QFJ) echoes `141=Y`
  in its Logon ack. fixpp's `peer_ack_sent_reset_flag` arm reset-rewinds both counters to 1; 030
  restored the inbound twin but outbound regressed 2→1 → the next frame duplicated `34=1` →
  QFcpp/QFJ reject "MsgSeqNum too low" (L-024-2, live-found). The arm now restores outbound to 2
  (`set_next_outbound(seqnum_min+1)` + `persist_outbound_advance_`, manager-first/store-second,
  fatal-when-persistent — the outbound twin of B-030-1) gated on BOTH a latched emit-time fact
  (`own_logon_sent_reset_flag_` = fixpp actually emitted `141=Y`, which carries the inbound-at-1
  conjunct) AND `reset_before_send` (fixpp's Logon went at post-reset seq 1). The reset-event
  `by_peer_request` now keys on the latch ALONE — correcting the prior `bilateral_strict`-only
  classification for non-strict reset-knob initiators. Restore (latch && reset-before-send) and
  label (latch alone) are DISTINCT gates that diverge on `bilateral_strict`-at-N (latch true, no
  restore). Covers all reset knobs via the emit-time latch; acceptor / knob-off / peer-spontaneous
  / `bilateral_strict`-at-N outbound unchanged (byte-identical). Reference-engine-conformant
  (QuickFIX reset-then-increment). **Status: shipped** (032). *(FR-001/FR-003/FR-005/FR-006/FR-007;
  `tests/session/test_persistent_seqnum_hydrate.cpp` W1 + W5/W6/W8,
  `test_reset_seqnum_policy_matrix.cpp` W2/W3/W4b/W7, `test_refresh_on_logon.cpp` cross-reconnect
  latch witness; live close-out via the `RL-*-init` interop cell vs QFcpp/QFJ.)*

### Limitations

- None specific to 030/031 (both conformance corrections; no new deferred surface). The pre-existing
  L-029-1 (post-GapFill bounded redundant resend) and L-029-3 (`bilateral_strict` non-1 cold-open
  malformed Logon) are unchanged.
