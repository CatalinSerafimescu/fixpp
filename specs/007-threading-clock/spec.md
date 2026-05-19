# Feature Specification: Application Threading Contract & `fixpp::core::Clock`

**Feature Branch**: `007-threading-clock`
**Created**: 2026-05-19
**Status**: Draft
**Input**: User description: "2d-threading: Application Threading Contract and fixpp::core::Clock plugin interface — realize the signed-off Phase-2 design doc `.specify/2d-threading.md` v0.4 as shipped code. Second of three prerequisites (2f → 2d → 2e) the deferred `005-session-establishment-fsm` depends on; 2f (`006-async-mutex`) is merged."

> **Authority anchor:** This feature realizes the **signed-off Phase-2 design doc `.specify/2d-threading.md` v0.4** (Gate-A-converged, round 3, post-cap line-edit pass) as shipped code, **including the cross-doc amendments already applied to it at `2f-async-mutex.md` v1.5 sign-off and `2e-msgstore.md` v0.4 sign-off** (Appendix C cross-doc entries — notably the engine-internal `Session::session_arena()` accessor per `[2d §4.5]` / `[2f Appendix D §D.1]`). Where this spec and the design doc disagree, **the design doc wins; an inconsistency is a defect in this spec.** This is the **second** of three prerequisite features (`2f-async-mutex` → `2d-threading` → `2e-msgstore`) that the deferred `005-session-establishment-fsm` consumes. `2f` (`006-async-mutex`) is merged to `main`; it was implemented against the *recorded, not re-litigated* `[2d §4.8]` `session_executor` / `[2d §7.4]` executor-compat contract — this feature ships the real backing for those surfaces. Catalogue row owned: **NFR-015** (NEW) — Pluggable Clock interface (design doc Appendix A / §11; the row plus its `coverage-index.md` entry and the `[arch §11]` row-7 / `[arch §5.4]` drop-ins are applied by the orchestrator at sign-off, **not** by this feature, per the `[2c App D]` precedent).

## Normative References

Per `[const §VI.5]`: the design doc records (Appendix B) that **2d's primary drivers are engineering judgment and the locked synthesis decisions, not a FIX specification section** — **no `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies** to the threading contract itself (the FIX SendingTime / heartbeat *consumers* of the clock seam are owned by the session-module Phase-4 spec). The governing sources are:

- `[const §XI.1]` Coroutines — `asio::awaitable<T>` is the session/transport composition primitive.
- `[const §XI.2]` Cancellation — ASIO native cancellation slots end-to-end; no parallel `stop_token`; `fixpp_session_close()` signals the slot.
- `[const §XI.3]` Awaitable mutex required; `[const §XI.4]` Application threading default = per-session strand (**direct mandate** for this feature); `[const §XI.5]` hot-path lock policy; `[const §XI.6]` HALO-first frame allocation.
- `[const §VIII.5]` Zero allocation between parse and `fromApp`; `[const §X.4]` out-of-range C-ABI code mapping; `[const §XIV.2]` ≤5 pure-virtual on plugin interfaces.
- `[const §XIII]` / `[const §XIII.3]` Observability — strand-stored trace context, `thread_local` propagation prohibited.
- `[const §XV.15]` `drop-oldest` banned on the application/session message path (telemetry/tap exception under `[const §XIII.2]`).
- `[SYN §3.2 Q6a]` Cancellation (DECIDED — ASIO native), `[SYN §3.2 Q6b]` Awaitable mutex (DECIDED — executor-compat surface this feature locks for 2f), `[SYN §3.2 Q6c]` Application threading contract (DECIDED — option 3 with default per-session strand).
- `[arch §1.1]` pluggable-clocks promise (NFR-015 discharges the *clock seam*), `[arch §4.1]` core surface, `[arch §4.4]` session surface + **Threading default (locked)**, `[arch §5.1]` executor model, `[arch §5.3]` error model, `[arch §5.4]` trace context, `[arch §5.6]` frozen config, `[arch §6]` plugin pattern, `[arch §10]` row 2d, `[arch §11]` row 7 (NFR-015 disposition).
- Sibling docs (consumed, not modified): `[2b §6.6]`/`[2b §8]` parser-on-strand + three-arena PMR; `[2c §4.8]`/`[2c §4.9]`/`[2c §6.3]`/`[2c §6.7]` `dict::reify` + `version_registry` + dict-layer error routing; `[2f §4.1.1]`/`[2f §4.3.2]`/`[2f §6.5]` async_mutex executor-compat (merged as `006`).

## Clarifications

*No design-doc decision required clarification.* The design doc is signed-off and Gate-A-converged through v0.4 (rounds 1–3 plus the post-cap line-edit pass; convergence log Appendix C). Every decision this feature realizes is fixed there: the 4-method `Clock` interface, the two-phase close model and `partial`-dropped surface, the `session_executor` wrapper-class shape (round 3 root cause #1), the `session_local<T>` storage axis (round 2 root cause #1), the single-`effective_clock` rule (root cause #2), the engine-anchor + session-override config pattern, the `cancellable_dispatch` reaping contract, and the §10 open-question dispositions (Q3/Q5/Q6 CLOSED; Q1→2f, Q2→2i, Q4/Q7 = 2d follow-up). The clarifications below resolve **codebase-reality scoping** (not design-doc decisions): the design doc assumes a `Session` type and a FIX-TC conformance corpus that do not yet exist because the session FSM (`005`) is deferred and blocked on this feature.

### Session 2026-05-19

- Q: What `Session` shell does feature 007 build, given no `Session` type exists and the full FIX FSM is the deferred `005`? → A: A **minimal real `Session` skeleton** shipped in `fixpp::session`, exposing only the 2d-owned surface (two-phase `close()`, `session_arena()`, the `session_local<trace_context>` member, executor→`session_executor` binding, callback dispatch) with **no FIX FSM logic**; `005` later extends this same type.
- Q: How are seams 3/9/16 and seam 11 / SC-002 (the "17-scenario FIX-TC conformance corpus") realized when the session FSM (`005`) and `tests/conformance/` do not exist? → A: A **deterministic scripted test-double FSM** in the 2d test fixture drives the message/heartbeat sequences; the seams assert only **2d-owned properties** (strand serialisation, `mock_clock` determinism, two-phase cancellation, alloc/latency), not FIX FSM correctness. Seam 11 / SC-002 is realized as a **2d-scoped deterministic clock-injection corpus**, not the full FIX-TC corpus (which is created with `005`); the bounded claim matches the design doc's "clock seam only" scoping (C-P2-6) and is recorded for the feature-completeness audit.

## User Scenarios & Testing *(mandatory)*

The "users" of this feature are **(a) the application developer embedding the engine** (who supplies an `asio::any_io_executor` and writes `fromApp`/`toApp`/`onLogon` callbacks), and **(b) downstream library modules** that program against the threading contract: the deferred `005` session FSM, `2e` MessageStore, `2g` TLS, `2h` Transport, `2i` C ABI, `2j` control plane, `2k` log+OTel, `2l` tap, `2m` Python. None of them pick a concrete executor or clock — the contract this feature ships is what they all depend on.

### User Story 1 - Application callbacks are serialised per session on a user-supplied executor (Priority: P1)

The application supplies one `asio::any_io_executor` (typically a thread pool) and says nothing else. The engine derives one strand per session from it and dispatches every application callback (`onLogon`, `onLogout`, `toAdmin`, `fromAdmin`, `toApp`, `fromApp`) onto that strand. Two callbacks for the same session never overlap and never run on the I/O recv thread; callbacks for different sessions may run concurrently. An expert who owns their own per-thread fan-out may opt out via `threading_mode::direct_executor` *and* attest `already_serialized_executor = true`.

**Why this priority**: This is the single locked promise of the entire threading model (`[const §XI.4]`, `[SYN §3.2 Q6c]`, `[arch §4.4]` Threading default). Without correct per-session serialisation no downstream module (session FSM, store, TLS, transport) has a defined execution domain — every other story builds on it.

**Independent Test**: Construct a `Session` in default `per_session_strand` mode; concurrently drive `Session::send(...)` from N user threads and verify `fromApp`/`toApp` observe strictly non-overlapping single-threaded ordering (TSan-clean); build the same Logon→NewOrderSingle→ExecutionReport→Logout sequence against `asio::thread_pool::executor_type`, `asio::system_executor`, and a 1-thread `asio::io_context::executor_type` under both `per_session_strand` and `direct_executor` (attested) modes and verify each completes correctly.

**Acceptance Scenarios**:

1. **Given** a session in `per_session_strand` mode under a multi-thread executor, **When** messages arrive faster than the application processes them, **Then** `fromApp` for message N+1 never begins before `fromApp` for message N has returned, and no callback runs on the transport recv thread.
2. **Given** two sessions on the same engine executor, **When** both receive traffic, **Then** their callbacks may run concurrently across sessions but never overlap within a session.
3. **Given** `mode == direct_executor` with `already_serialized_executor == true` over a genuinely serialised executor, **When** the engine runs the session, **Then** no extra `make_strand` wrap is applied and the session behaves identically to the strand mode.
4. **Given** `mode == direct_executor` with `already_serialized_executor == false`, **When** the session is opened, **Then** construction is rejected with `error::executor_not_serialised`; **and** `direct_executor + lock_policy::spin` is rejected with `error::invalid_session_config` even when attested.

---

### User Story 2 - Pluggable Clock seam drives all session-scoped timing (NFR-015) (Priority: P1)

Timing is sourced from a pluggable `fixpp::core::Clock` (exactly 4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`, optionally overridden per `SessionConfig`. The resolved `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` feeds heartbeat, SendingTime checks, S-035 scheduling, and **session-scoped** LOG/OBS timestamps; engine-scope records read `EngineConfig::clock` directly. The default `system_clock_source` wraps system/steady clocks + ASIO `steady_timer`; the `mock_clock` test impl steps every session-scoped consumer deterministically so the conformance corpus runs without real wall-clock time.

**Why this priority**: This is the catalogue row this feature **owns** (NFR-015) and the `[arch §1.1]` pluggable-clocks promise the whole project deferred to 2d. The session FSM, log/OTel, and conformance corpus cannot be deterministically tested without it.

**Independent Test**: Run two identically-seeded `mock_clock` instances through the same `advance` sequence and assert identical `now`/`steady_now`/wake-up order; run the 17-scenario FIX-TC conformance corpus with `mock_clock` injected via `clock_override` and diff against the golden log (catches any code path calling `std::chrono::system_clock::now()` directly); implement a minimal third-party `Clock` and drive a session through it verifying completion lands on the awaiter's bound executor and cancellation is honoured.

**Acceptance Scenarios**:

1. **Given** `EngineConfig::clock == nullptr` at `Engine::open`, **When** the engine opens — even if every session supplies `clock_override` — **Then** it is rejected with `error::clock_not_set`.
2. **Given** a `mock_clock` set as `SessionConfig::clock_override`, **When** the test advances time, **Then** heartbeat timers, S-035 schedule, SendingTime elapsed deltas, and session-scoped LOG/OBS timestamps all step from the override, while engine-scope records still read `EngineConfig::clock`.
3. **Given** `Clock::now()` (wall clock) under an NTP backward step, **When** elapsed time is measured, **Then** heartbeat/SendingTime elapsed deltas use `steady_now()` only and no benign NTP step trips a SendingTime-threshold reject; `now()` is consulted only for wire-formatted and log/OTel timestamps and is not promised monotonic.
4. **Given** N in-flight `sleep_until` waiters, **When** `cancel_sleeps()` fires concurrently from another thread, **Then** every awaiter completes exactly once (deadline-reached or `operation_aborted`), with no leak, no double-completion (TSan+ASan-clean).
5. **Given** an engine with open sessions at teardown, **When** `~Engine` runs, **Then** every session is closed (`terminal`) and drained, `cancel_sleeps()` fires, the waiter list drains, and the `EngineConfig::clock` `shared_ptr` is cleared **last**; a `mock_clock` held outside the engine outlives it safely.

---

### User Story 3 - Session shutdown cancels in-flight work deterministically via two-phase close (Priority: P1)

`Session::close(graceful)` opens a *child* `asio::cancellation_state` below the root, attempts a FIX `Logout` exchange under that child state with a `Clock::sleep_until` timeout, and only then (phase 2) fires `cancellation_type::total` on the root — propagating to transport read/write, heartbeat sleep, awaitable-mutex acquire (2f surface), application-callback dispatch (via the project-owned `cancellable_dispatch` primitive), and the parser→`fromApp` chain. `Session::close(terminal)` skips phase 1. The `partial` mode is not in the v1.0 surface. Cancellation is reported as `asio::error::operation_aborted` / `expected_t` `dispatch_aborted`, never as an exception, and `close()` is idempotent.

**Why this priority**: Clean cancellable shutdown is mandated by `[SYN §3.2 Q6a]` / `[const §XI.2]` and is the hard correctness obligation of the threading contract; the C ABI's `fixpp_session_close()` and every downstream module's shutdown path depend on it.

**Independent Test**: With a `mock_clock` whose `sleep_until` parks indefinitely, issue `close(graceful)` while a `fromApp` is pending dispatch and verify the pending dispatch is reaped before invocation via `cancellable_dispatch` (awaitable completes with `expected_t<void>{unexpect, error::dispatch_aborted}`) per the deterministic three-case answer; block inside `fromApp` on a cancelled `sleep_until` and verify `close(terminal)` drains it with `operation_aborted`; run the libFuzzer cancellation-injection harness firing `close` at every `co_await` checkpoint and verify no deadlock/double-free/PMR leak.

**Acceptance Scenarios**:

1. **Given** a session with an in-flight transport read/write, heartbeat sleep, and pending `fromApp`, **When** `close(graceful)` is called, **Then** phase 1's Logout `async_write` + timeout `sleep_until` run under the child state (not pre-cancelled by the eventual root total), and phase 2 fires root `total` only after phase 1 resolves (peer ACK | child timeout | child cancelled).
2. **Given** a dispatch posted but not yet picked up by the session executor when cancellation lands, **When** `cancellable_dispatch` reaches the hand-off, **Then** `fromApp` is **not** invoked and the awaitable completes with `error::dispatch_aborted`; if the executor had already begun invoking `fromApp`, it runs to completion and the next FSM checkpoint observes the slot.
3. **Given** `close()` already called, **When** it is called again (or on a never-opened session), **Then** it returns idempotently with `error::session_already_closed` and no side effects.
4. **Given** any cancellation, **When** it propagates, **Then** it surfaces as `operation_aborted` / `dispatch_aborted` (never a thrown exception across the parse→`fromApp` window) and maps to `FIXPP_ERR_CANCELLED` at the C ABI.

---

### User Story 4 - Trace context follows the session serialisation domain, never `thread_local` (Priority: P2)

`co_await fixpp::current_trace_context` returns the `fixpp::otel::trace_context` bound to the current session serialisation domain. Storage is a `fixpp::core::session_local<trace_context>` slot owned by the `Session` object, populated at open from `SessionConfig::initial_trace_context` and cleared at close. The awaiter recovers a typed `Session*` via the `fixpp::core::session_executor` wrapper's public `session_ptr()` member-function accessor — **not** an `asio::any_io_executor::query` and **not** `thread_local` — so it survives coroutine resume on a different thread and `bind_executor`/`make_strand` decoration. Outside any session scope (control plane, listener accept) the awaiter falls back to a per-engine atomic snapshot of `EngineConfig::engine_trace_context`.

**Why this priority**: Required by `[const §XIII.3]` / `[arch §5.4]` for log/OTel correlation, but it is a correctness-and-observability slice layered on the US1 domain — it can be built and tested after the strand contract exists.

**Independent Test**: Bind a session over a 4-thread pool; in `fromApp` capture `current_trace_context`, `co_await` a sleep that resumes on a different thread (assert thread-id changed), re-read, and assert byte-equality; trigger `close(graceful)` mid-`fromApp` and verify the slot stays valid until close completes and is never read through a destroyed slot; `static_assert` that `session_executor` satisfies the ASIO executor concept and survives `bind_executor`/`make_strand`, and that a cast to `any_io_executor` does **not** expose a `Session*` property query (the rejected v0.3 shape, documented as known-bad).

**Acceptance Scenarios**:

1. **Given** a coroutine inside a session domain that suspends and resumes on a different pool thread, **When** it re-reads `current_trace_context`, **Then** the value is byte-identical (no `thread_local` regression).
2. **Given** an awaiter bound to a `session_executor` wrapper under either threading mode, **When** it resolves `current_trace_context`, **Then** it recovers the typed `Session*` via `session_ptr()` and reads the session's slot; **and** the accessor remains reachable after `bind_executor`/`make_strand` over the wrapper.
3. **Given** an awaiter outside any session domain (control-plane handler, listener accept), **When** it resolves `current_trace_context`, **Then** wrapper recovery misses and it reads the engine's atomic `engine_trace_context` snapshot.

---

### User Story 5 - Engine/Session config is a frozen engine-anchor + session-override split (Priority: P2)

`EngineConfig` carries engine-level shared resources (executor, `Clock`, dictionary list → `version_registry`, PMR defaults, observability providers, default plugin factories, engine fallback trace context). `SessionConfig` carries session-level frozen-at-open knobs as nullable overrides following one uniform pattern across the executor, clock, and dictionary axes (`resolved = override.value_or(engine_anchor)`). Illegal states are unrepresentable or rejected at open: `backpressure_mode` has only `block`/`disconnect_and_recover` (no `drop_oldest`); a default-constructed `security_profile` sentinel, a null `dictionary`, a null `EngineConfig::executor`, or an incompatible combination is rejected with `error::invalid_session_config`. A FIXT.1.1 per-message version miss routes through the **`2c`** dict-layer error, not a 2d-layer synonym.

**Why this priority**: The config surface is the contract every other module is constructed from, but it is shaped by the design doc's uniform pattern and is testable independently once US1/US2 exist; it is hardening around the P1 core rather than the core itself.

**Independent Test**: Construct sessions under both threading modes and exercise the four primitives re-typed against `session_executor` (round-trip both modes); attempt to construct an out-of-range `backpressure_mode` (cast from int) and verify rejection plus the compile-time enum-exhaustiveness `static_assert`; omit a FIXT.1.1 version from `EngineConfig::dictionaries`, drive a per-message override frame, and verify the failure surfaces as `[2c §6.7] dict_no_dictionary_for_application_version` (→ `FIXPP_ERR_DICT_CONFIG`), not a 2d-layer variant.

**Acceptance Scenarios**:

1. **Given** any frozen field (`executor_override`, `mode`, `locks`, `dictionary`, `clock_override`, `initial_trace_context`, …), **When** mid-session reconfiguration is attempted, **Then** it is not supported; the only path is close-and-reopen (`[arch §5.6]`).
2. **Given** a `SessionConfig` with a null `dictionary`, default-sentinel `security_profile`, or an incompatible combination, **When** `Session::open` runs, **Then** it is rejected with `error::invalid_session_config`.
3. **Given** the `backpressure_mode` enum, **When** code switches on it, **Then** a `drop_oldest` value is unrepresentable (closed enum + `static_assert`) and a runtime out-of-range cast is rejected.
4. **Given** an `EngineConfig::dictionaries` list missing a version a FIXT.1.1 session reaches via `ApplVerID(1128)`, **When** that override frame arrives, **Then** the failure routes through the 2c dict-layer error/C-ABI group, not a 2d synonym.

---

### User Story 6 - The threading hot path is zero-global-alloc, exception-free, and within latency ceilings (Priority: P3)

The parse→`fromApp` dispatch path touches zero global heap (HALO elides the awaiter frame; PMR fallback on `SessionConfig::message_arena` per `[const §XI.6]` when it does not); the per-session reusable `steady_timer` slot is allocated once per session from `session_arena`, keyed by `Session*`, and reused every heartbeat cycle. No exception crosses the parse→`fromApp` window. The §6.3 Tier-1 latency ceilings hold and CI fails on >5% regression.

**Why this priority**: A cross-cutting non-functional guarantee that hardens the P1 paths; it is verified by dedicated guards/benches and gates release quality, but the contract is usable for downstream integration before the bench bars are tuned (§10 Q4 is an explicit 2d follow-up).

**Independent Test**: Run a 10⁴-message corpus under `mallocnesia` and assert zero global-heap `new`/`delete`/`malloc` between parse and `fromApp` (PMR-arena allocations expected, not flagged); run 10⁴ heartbeat cycles and assert no global-heap allocation after the first cycle (timer slot allocated once, keyed by `Session*`, both modes); run the Google Benchmark suite against the §6.3 ceilings and fail on >5% regression vs the tagged baseline.

**Acceptance Scenarios**:

1. **Given** a 10⁴-message Logon→NewOrderSingle→ExecutionReport→Logout corpus, **When** it is dispatched, **Then** the allocation guard reports zero global-heap allocation between parse and `fromApp`.
2. **Given** 10⁴ heartbeat cycles under both threading modes, **When** the clock-sleep allocation guard runs, **Then** the per-session timer slot is allocated exactly once (at session open) and reused with no per-cycle heap touch.
3. **Given** the latency bench (in-strand dispatch ≤25 ns; cross-thread ≤250 ns; cross-strand reify+dispatch 20-tag ≤1.25 µs / 200-tag ≤10.25 µs; `now()` ≤25 ns; `steady_now()` ≤20 ns; in-domain trace access ≤15 ns; engine-fallback ≤25 ns), **When** CI runs it, **Then** any >5% regression vs the previous tagged release fails the build.

---

### Edge Cases

- **NTP / admin clock step backward**: `now()` is not monotonic; elapsed measurements use `steady_now()` only; a benign step never trips a SendingTime reject (§6.6).
- **Cancellation racing release/drain**: handled by the deterministic three-case `cancellable_dispatch` answer (§6.5); exactly one of granted/cancelled per waiter.
- **`cancel_sleeps()` called from inside a `sleep_until` completion handler**: idempotent, no re-entry (§6.6).
- **Engine torn down while a session has an in-flight heartbeat sleep or `fromApp`**: `~Engine` ordering — close all (terminal) → drain → `cancel_sleeps` → drain waiter list → clear `clock` `shared_ptr` last (root cause #5).
- **`mock_clock` held outside the engine** (test fixture): outlives the engine safely; no live waiters after teardown.
- **PMR arena exhausted on the dispatch / `cancellable_dispatch` node**: `error::strand_dispatch_failed_oom`, forced disconnect.
- **Slot empty mid-`Session::open()`** (coroutine runs before slot populated — should not happen under v1.0 FSM ordering): awaiter returns default-constructed `trace_context` + debug assertion.
- **Coroutine resumes on a different thread than it suspended on**: trace context still correct (slot reached via stable `Session*`, never `thread_local`).
- **`direct_executor` attested over a genuinely non-serialised executor**: debug builds trip the strand-invariant assert on detected concurrent FSM entry; release builds are documented user-contract-violation UB.

## Requirements *(mandatory)*

### Functional Requirements

**Clock interface & implementations (NFR-015)**

- **FR-001**: The system MUST expose `fixpp::core::Clock` as a pure-virtual interface with **exactly 4** pure-virtual methods — `now()` (UTC, `noexcept`), `steady_now()` (monotonic, `noexcept`), `sleep_until(...)` (awaitable), `cancel_sleeps()` — within the `[const §XIV.2]` ≤5 cap (`[2d §4.1]`).
- **FR-002**: The system MUST ship `fixpp::core::system_clock_source` (default impl over `std::chrono::system_clock` + `std::chrono::steady_clock` + ASIO `steady_timer`) and `fixpp::core::mock_clock` (test impl, pimpl'd, in public header `<fixpp/core/test/mock_clock.hpp>`, deterministic `advance(...)`) (`[2d §4.2]`, `[2d §4.3]`).
- **FR-003**: `Clock::sleep_until` completion MUST land on the awaiter's bound executor; `cancel_sleeps()` MUST signal every in-flight waiter's cancellation slot (O(N) walk, v1.0 worst case O(2×sessions)) and be safe to call concurrently and re-entrantly (`[2d §4.1]`, `[2d §6.6]`).
- **FR-004**: `now()` MUST NOT be promised monotonic; `steady_now()` MUST be the only source for elapsed/heartbeat/SendingTime-delta/S-035 measurements; `now()` MUST be used only for wire-formatted and log/OTel timestamps (`[2d §6.6]`).
- **FR-005**: The system MUST resolve `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` once at `Session::open`, bound to session lifetime; every session-scoped consumer (heartbeat, SendingTime, S-035, session-scoped LOG/OBS) MUST read `effective_clock`; engine-scope LOG/OBS MUST read `EngineConfig::clock` and carry `clock_scope = engine` (`[2d §7.9]`).
- **FR-006**: `Engine::open` MUST reject `EngineConfig::clock == nullptr` with `error::clock_not_set` regardless of session `clock_override`s (`[2d §4.4]`, root cause #2).

**Per-session strand & executor opt-out**

- **FR-007**: Every `Session` MUST run on a serialisation domain derived from the resolved executor (`SessionConfig::executor_override.value_or(EngineConfig::executor)`); in default `threading_mode::per_session_strand` the engine MUST wrap it in `asio::make_strand` and dispatch all application callbacks onto it; the engine MUST never pick a concrete executor (`[2d §6.1]`, `[const §XI.4]`).
- **FR-008**: The strand MUST serialise `{onLogon, onLogout, toAdmin, fromAdmin, toApp, fromApp, MessageStore op, Clock sleep wake-up, transport completion}` so that no two run concurrently within a session; cross-session concurrency MUST be permitted; `fromApp(N+1)` MUST NOT begin before `fromApp(N)` returns (`[2d §6.1]`).
- **FR-009**: `threading_mode::direct_executor` MUST skip the `make_strand` wrap and require `already_serialized_executor == true`; the engine MUST reject `direct_executor && !already_serialized_executor` with `error::executor_not_serialised`, and `direct_executor + lock_policy::spin` with `error::invalid_session_config` (even when attested) (`[2d §4.5]`, `[2d §6.1]`, root cause #1).
- **FR-010**: Backpressure on the app/session message path MUST support only `block` (default) and `disconnect_and_recover`; `drop_oldest` MUST be unrepresentable (closed `backpressure_mode` enum + `static_assert` at every switch) and an out-of-range cast MUST be rejected at construction (`[2d §6.4]`, `[const §XV.15]`).

**Two-phase cancellation**

- **FR-011**: `Session::close(graceful)` MUST execute phase 1 (FIX `Logout` exchange + `Clock::sleep_until` timeout under a *child* `asio::cancellation_state` composed below the root, NOT pre-cancelled by the eventual root total) then phase 2 (fire `cancellation_type::total` on the root, propagating to transport read/write, heartbeat sleep, awaitable-mutex acquire, application-callback dispatch, parser→`fromApp`); `Session::close(terminal)` MUST skip phase 1; `partial` MUST NOT be in the v1.0 surface (`[2d §4.7]`, `[2d §6.5]`, root cause #1).
- **FR-012**: The system MUST provide the project-owned `fixpp::core::cancellable_dispatch(session_executor, slot, handler)` primitive returning `asio::awaitable<expected_t<void>>` with the deterministic three-case contract: slot-before-pickup → handler reaped, completes `expected_t<void>{unexpect, error::dispatch_aborted}`; slot-during-execution → runs to next checkpoint then honours slot; slot-not-signalled → equivalent to `asio::dispatch` plus a ≤5 ns relaxed-atomic check, completes `expected_t<void>{}`; the dispatch node MUST allocate from the session PMR resource (`[2d §6.5]`).
- **FR-013**: Cancellation MUST surface as `asio::error::operation_aborted` / `expected_t` `dispatch_aborted`, never as a thrown exception across the parse→`fromApp` window, and MUST map to `FIXPP_ERR_CANCELLED` at the C ABI; `Session::close(...)` MUST be idempotent (second/never-opened/already-closed call → `error::session_already_closed`, no side effects) (`[2d §6.5]`, `[2d §6.2]`, `[const §X.4]`).

**Session-domain trace context**

- **FR-014**: The system MUST provide `fixpp::core::session_local<T>` (a `Session`-owned, value-typed, non-`thread_local`, non-executor-property slot) and `fixpp::current_trace_context` (a free awaitable) per `[2d §4.6]`; the slot MUST be populated at `Session::open` from `SessionConfig::initial_trace_context` and cleared at `Session::close` completion.
- **FR-015**: `current_trace_context` MUST recover the typed `Session*` via `fixpp::core::session_executor::session_ptr()` (a public member-function accessor on the project-owned wrapper class — NOT an `asio::any_io_executor` property query, NOT `thread_local`) and read the session slot in-domain; outside any session domain it MUST fall back to the engine's atomic `EngineConfig::engine_trace_context` snapshot; recovery MUST survive `bind_executor`/`make_strand` decoration and coroutine resume on a different thread (`[2d §4.6]`, `[2d §4.8]`, `[2d §7.8]`, round 3 root cause #1).

**Config split & wiring**

- **FR-016**: `EngineConfig` and `SessionConfig` MUST be value-typed and frozen at open per `[arch §5.6]`; the executor, clock, and dictionary axes MUST follow one uniform engine-anchor + nullable-session-override pattern (`resolved = override.value_or(engine_anchor)`); the supported reconfiguration path MUST be close-and-reopen only (`[2d §4.4]`, `[2d §4.5]`).
- **FR-017**: The engine MUST construct `dict::version_registry` from `EngineConfig::dictionaries` at `Engine::open`; a FIXT.1.1 per-message `ApplVerID(1128)` miss MUST route through the existing `[2c §6.7] dict_no_dictionary_for_application_version` (→ `FIXPP_ERR_DICT_CONFIG`), NOT a 2d-layer synonym (`[2d §4.4]`, `[2d §6.7]`, round 2).
- **FR-018**: `Session::open` MUST reject a null `dictionary`, null `EngineConfig::executor`, default-constructed `security_profile` sentinel, or incompatible combination with `error::invalid_session_config`; the error set introduced by this design (`executor_already_stopped`, `executor_not_serialised`, `clock_sleeps_cancelled`, `strand_dispatch_failed_oom`, `session_already_open`, `session_already_closed`, `invalid_session_config`, `clock_not_set`, `dispatch_aborted`) MUST coalesce at the C ABI into `FIXPP_ERR_THREAD_CONFIG` / `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` / `FIXPP_ERR_THREAD_RUNTIME` / `FIXPP_ERR_CANCELLED` per `[2d §6.7]` (final coalescing is 2i's call; this feature provides the C++ variants and the documented grouping).
- **FR-019**: The system MUST expose the engine-internal `Session::session_arena()` accessor (`noexcept`, never null, resolution chain `SessionConfig::session_arena ?: EngineConfig::default_session_resource ?: std::pmr::get_default_resource()`, callable only from `fixpp::session/`) as already amended into `[2d §4.5]` at `2f` sign-off; `core/` MUST NOT back-edge into `session/` per `[arch §2.3]`.

**Hot-path non-functional**

- **FR-020**: The parse→`fromApp` path MUST allocate zero global heap (HALO-elided awaiter frame; per-awaiter PMR fallback on `message_arena` when HALO does not fire); the `system_clock_source` per-session `steady_timer` slot MUST be allocated once per session from `session_arena`, keyed by `Session*`, and reused every cycle with no per-cycle heap touch, under both threading modes (`[2d §6.2]`, `[2d §6.6]`, root cause #5 / N-P1-1).
- **FR-021**: No exception MUST cross the parse→`fromApp` window; the §6.3 Tier-1 latency ceilings MUST be enforced by a Google Benchmark regression bench that fails CI on >5% regression vs the previous tagged release (`[2d §6.2]`, `[2d §6.3]`, `[arch §5.3]`).
- **FR-022**: The feature MUST ship the **21 named test seams** of `[2d §9]` (referenced by name, not ordinal), including TSan-mandatory strand-serialisation, ASan+TSan-clean sleep/cancel race, the libFuzzer cancellation-injection harness, the Linux/Clang `mallocnesia` allocation guards, and the `session_executor`-accessor-survives-erasure compile+runtime+negative assertions. Seams that exercise FSM-shaped behavior (executor-compat message sequences, heartbeat-window simulation, `direct_executor` reentrancy, the clock-injection corpus) MUST be driven by a **deterministic scripted test-double FSM** in the 2d fixture and MUST assert only 2d-owned properties (strand serialisation, `mock_clock` determinism, two-phase cancellation, alloc/latency) — never FIX FSM correctness, which is `005`'s.

### Key Entities

- **`fixpp::core::Clock`**: 4-method pure-virtual timing seam; instances are `shared_ptr`-owned by `EngineConfig` (or per-session `clock_override`), must outlive every referencing session.
- **`fixpp::core::system_clock_source` / `mock_clock`**: default and deterministic-test `Clock` impls; `mock_clock` is pimpl'd test-only with `advance(delta)`.
- **`fixpp::core::EngineConfig`**: value-typed engine-level shared resources (executor, `clock`, `dictionaries`→`version_registry`, PMR defaults, observability providers, default plugin factories, `engine_trace_context` atomic snapshot, control-plane factory).
- **`fixpp::session::SessionConfig`**: value-typed, frozen-at-open session knobs as nullable overrides (`executor_override`, `mode`, `locks`, `already_serialized_executor`, `clock_override`, `dictionary`, plugin overrides, PMR arenas, `initial_trace_context`, `backpressure_mode` [closed enum]).
- **`fixpp::core::session_executor`**: project-owned value-typed executor-concept wrapper holding the resolved executor (strand-wrapped under `per_session_strand`, bare under `direct_executor`) plus a typed `Session*`; publishes `session_ptr()` member accessor; survives `bind_executor`/`make_strand`.
- **`fixpp::core::session_local<T>` + `fixpp::current_trace_context`**: `Session`-owned domain-local slot and the free awaitable that resolves it (in-domain via `session_ptr()`, engine-fallback otherwise).
- **`fixpp::core::cancellable_dispatch`**: project-owned reaping-aware dispatch primitive returning `asio::awaitable<expected_t<void>>`.
- **`close_mode` / two-phase close state**: `graceful` (child cancellation_state → root total) vs `terminal` (root total only); `partial` excluded from v1.0.
- **Per-session reusable `steady_timer` slot pool**: session-lifetime, `session_arena`-allocated, `Session*`-keyed, reused per heartbeat cycle.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All **21 named `[2d §9]` test seams** are implemented and pass on Linux/Clang Tier 1, with the strand-serialisation, sleep/cancel-race, `session_local`-lifetime, and cancellation seams **TSan-clean** and the race/leak seams additionally **ASan-clean**.
- **SC-002**: The **2d-scoped deterministic clock-injection corpus** (driven by the scripted test-double FSM with `mock_clock` injected via `clock_override`; **not** the full FIX-TC corpus, which lands with `005`) produces output byte-identical to its golden expectation, proving zero direct `std::chrono::system_clock::now()` leaks on any session-scoped path; engine-scope records are unaffected.
- **SC-003**: The `mallocnesia` allocation guards report **zero global-heap allocation** between parse and `fromApp` over a 10⁴-message corpus, and **zero global-heap allocation after the first cycle** over 10⁴ heartbeat cycles under both threading modes.
- **SC-004**: The Google Benchmark suite meets every §6.3 ceiling (in-strand dispatch ≤25 ns, cross-thread ≤250 ns, cross-strand reify+dispatch 20-tag ≤1.25 µs / 200-tag ≤10.25 µs, `now()` ≤25 ns, `steady_now()` ≤20 ns, in-domain trace access ≤15 ns, engine-fallback ≤25 ns) and CI fails on any >5% regression vs the previous tagged release.
- **SC-005**: 100% of the design doc's enumerated decisions are realized: `Clock` has exactly 4 pure-virtual methods; the `backpressure_mode` enum is closed at 2 values; `partial` is absent from `close_mode`; `current_trace_context` resolves with **no** `asio::any_io_executor` `Session*` property query and **no** `thread_local`; every §6.7 error variant is present and grouped as documented.
- **SC-006**: A third-party `Clock` derivative (not `system_clock_source`/`mock_clock`) drives a full Logon→NewOrderSingle→cancel session with `sleep_until` completing on the awaiter's bound executor and both root-`total` and child-state cancellation honoured.
- **SC-007**: The libFuzzer cancellation-injection harness runs the configured campaign with **zero** deadlocks, double-frees, or PMR leaks.
- **SC-008**: `/speckit-verify` is non-RED and the feature-completeness audit (tasks ↔ FR/SC ↔ catalogue) is non-failing before `/gate-b`; **NFR-015** plus its `coverage-index.md` entry, the `[arch §11]` row-7 disposition flip, and the `[arch §5.4]`/`2k`-schema drop-ins are recorded for the orchestrator to apply at sign-off (this feature does not edit `architecture.md` / `feature-catalogue.md` / `coverage-index.md` directly).

## Assumptions

- **Design doc is authoritative.** This is a Phase-4 realization of signed-off `.specify/2d-threading.md` v0.4; on any conflict the design doc (as amended by the `2f`/`2e` sign-off cross-doc entries in its Appendix C) wins. No `/speckit-specify`-time clarifications — consistent with the `006`/`004` precedent for signed-off design docs.
- **`Session` shell scope** (Clarifications 2026-05-19). The full session FSM (Logon, gap-fill, ResendRequest, sequence reset, concrete heartbeat/SendingTime/close-timeout values) is owned by the deferred `005-session-establishment-fsm` / session-module Phase-4 spec, **not** this feature. This feature ships a **minimal real `Session` skeleton in `fixpp::session`** exposing only the 2d-owned surface (two-phase `close()`, `session_arena()` accessor, the `session_local<trace_context>` member, executor→`session_executor` binding, callback dispatch hooks) with **no FIX FSM logic**; `005` extends this same type. The `[2d §9]` seams that exercise FSM-shaped behavior (3 executor-compat sequences, 9 heartbeat-window, 16 `direct_executor` reentrancy) are driven by a **deterministic scripted test-double FSM** in the 2d test fixture and assert only 2d-owned properties; the fixture picks values 2d does not pin (e.g., heartbeat interval).
- **Conformance corpus scope** (Clarifications 2026-05-19). `tests/conformance/` and the full FIX-TC corpus do not exist and are created with `005`. Seam 11 / SC-002 is realized as a **2d-scoped deterministic clock-injection corpus** verifying no session-scoped path bypasses `effective_clock` — the bounded "clock seam only" claim per the design doc's C-P2-6. No full-FIX-corpus discharge is claimed by 007; this is recorded for the feature-completeness audit so the audit passes without a waiver.
- **Prerequisite ordering.** `2f` (`006-async-mutex`) is merged to `main`; this feature is prerequisite #2 (`2f → 2d → 2e`). `006` was implemented against the *recorded, not re-litigated* `[2d §4.8]`/`[2d §7.4]` contract; this feature ships the real `session_executor` / `cancellable_dispatch` / executor-compat backing those surfaces. `005` implementation stays blocked until this feature's Gate B merges and `2e` lands.
- **C-ABI symbol shapes deferred to `2i`.** This feature provides the C++ error variants and the documented `FIXPP_ERR_THREAD_*` / `FIXPP_ERR_CANCELLED` coalescing groups; the final C-ABI symbol shapes and cancellation-token representation are `2i`'s call (`[2d §10]` Q2).
- **`[2d §10]` follow-ups.** Q4 (tighten the cross-thread dispatch ceiling below 250 ns) is an explicit 2d-implementation bench-spike follow-up, not a blocker; Q7 defaults to "no `set_engine_trace_context` mutator in v1.0" unless a consumer surfaces.
- **PMR three-arena model** from `[2b §6.6]`/`[2b §8]` is consumed as-is; `framer_carry_arena` is recorded for cross-doc consistency but unused by 2d directly.
- **Linux/Clang Tier 1** is the gating platform for the `mallocnesia` allocation guards and latency benches (same caveat as `[2a §9]`/`[2b §9]`); ARM64 and other tiers run the functional/sanitizer subset per the project quickstart convention.
