# Phase 1 — Data Model — 007-threading-clock

**Anchor:** `.specify/2d-threading.md` v0.4. Entities distill the design-doc §4/§6/§8 surface; no shape is invented. On conflict the design doc wins.

## Entities

### E1 — `fixpp::core::Clock` (abstract plugin interface)

| Member | Shape | Notes |
|---|---|---|
| `~Clock()` | `virtual` default | polymorphic base |
| `now()` | `[[nodiscard]] virtual utc_time_point now() const noexcept = 0` | wall-clock UTC; **not** monotonic (C-P2-5) |
| `steady_now()` | `[[nodiscard]] virtual steady_time_point steady_now() const noexcept = 0` | monotonic; only elapsed source |
| `sleep_until(...)` | `virtual asio::awaitable<expected_t<void>> sleep_until(steady_time_point) = 0` | completes on the awaiter's bound executor; honours the awaiter's cancellation slot |
| `cancel_sleeps()` | `virtual void cancel_sleeps() noexcept = 0` | signals every in-flight awaiter's slot; idempotent; re-entrant-safe |

**Invariant:** exactly **4** pure-virtual (`[const §XIV.2]` 4/5, within cap). Owned by `EngineConfig` (or per-`SessionConfig` override) via `shared_ptr`; must outlive every referencing session. `utc_time_point = std::chrono::time_point<std::chrono::system_clock>`; `steady_time_point = std::chrono::time_point<std::chrono::steady_clock>`.

### E2 — `fixpp::core::system_clock_source` (default impl)

`now()`→`system_clock::now()`; `steady_now()`→`steady_clock::now()`; `sleep_until()`→ASIO `steady_timer` from a **per-session reusable slot pool keyed by `Session*`**, allocated once from `session_arena` (D-8); `cancel_sleeps()` walks an intrusive in-flight-awaiter list (O(N); v1.0 ≤ O(2×sessions)). `now()`/`steady_now()` thread-safe, non-blocking, `noexcept`. `~system_clock_source` drains its intrusive list (no live waiters in well-formed shutdown — sessions drained heartbeat slots first; D-9 / root cause #5).

### E3 — `fixpp::core::mock_clock` (test impl)

Pimpl over an opaque mutable-state object (D-10; `[const §XI.3]`). Public test header `<fixpp/core/test/mock_clock.hpp>`. `advance(delta)` walks a per-deadline ordered map; wakes every awaiter `deadline ≤ new_steady_now`; deterministic across runs. Test-only; may outlive the engine when held by a fixture `shared_ptr` (`cancel_sleeps()` has no live waiters post-teardown — seam 14 variant).

### E4 — `fixpp::core::EngineConfig` (value type)

Required: `asio::any_io_executor executor`; `std::shared_ptr<Clock> clock` (**`clock_not_set` hard invariant at `Engine::open` regardless of session overrides** — root cause #2). Dictionary: `std::vector<std::shared_ptr<const dict::Dictionary>> dictionaries` → engine builds `dict::version_registry` at `Engine::open` (D-13). Defaultable PMR: `default_message_resource`, `default_session_resource` (`std::pmr::memory_resource*`, default `get_default_resource()`). Observability (null→no-op): `Logger`, `TracerProvider`, `MeterProvider`. Default plugin factories: `MessageStoreFactory`, `cert_source`, `TransportFactory`, `unique_ptr<ControlPlaneFactory>`. `fixpp::otel::trace_context engine_trace_context{}` held by the engine as a `std::atomic<trace_context>` snapshot (seqlock fallback if not lock-free; D-1).

### E5 — `fixpp::session::SessionConfig` (value type, frozen at open)

Threading: `std::optional<asio::any_io_executor> executor_override`; `threading_mode mode = per_session_strand`; `lock_policy locks = mutex`; `bool already_serialized_executor = false`; `std::shared_ptr<Clock> clock_override`. Identity (owned by `005`): `sender_comp_id`/`target_comp_id`/`begin_string`. Plugin overrides (null→inherit): `unique_ptr<MessageStoreFactory> store_factory`, `shared_ptr<cert_source>`, `SecurityProfile security_profile` (no-implicit-default sentinel — N-P2-3). Dictionary: `shared_ptr<const dict::Dictionary> dictionary` (required), `shared_ptr<const dict::DialectOverlay>` (optional). Thresholds (owned by `005`, placeholders): `std::optional<seconds> heartbeat_interval`, `std::optional<ms> test_request_threshold`, `std::optional<ms> sending_time_threshold`, `RejectPolicy reject_policy`. PMR: `message_arena`/`framer_carry_arena`/`session_arena` (`std::pmr::memory_resource*`, null→engine default). Observability: `fixpp::otel::trace_context initial_trace_context{}` (value-typed — C-P2-4, no callable in frozen config), `shared_ptr<log::Sink> log_sink_override`. Tap: `tap::TapConsumer`. Backpressure: `backpressure_mode app_backpressure = block`.

Enums:
- `threading_mode : uint8_t { per_session_strand = 0, direct_executor = 1 }`
- `lock_policy : uint8_t { mutex = 0, spin = 1 }`
- `backpressure_mode : uint8_t { block = 0, disconnect_and_recover = 1 }` — **closed, 2 values; `drop_oldest` unrepresentable** (`[[clang::enum_extensibility(closed)]]` where supported + `static_assert` at every switch; `[const §XV.15]`).

### E6 — `fixpp::core::session_executor` (project wrapper class)

Value-typed; satisfies `asio::execution::is_executor_v`. Holds: inner executor (`asio::strand<asio::any_io_executor>` under `per_session_strand` | bare `asio::any_io_executor` under `direct_executor`) + a typed `Session*`. Public `[[nodiscard]] Session* session_ptr() const noexcept`. Survives `bind_executor`/`make_strand` (D-3). Session-lifetime, value-semantic; held by the `Session` instance; stored in `session_arena` (`[2d §8]`).

### E7 — `fixpp::core::session_local<T>`

`Session`-owned slot: `T value_{}`; `load()`/`store(T)`/`clear()` (`noexcept`); caller MUST be inside the owning session's serialisation domain (debug `Session*` self-check). NOT `thread_local`, NOT executor-property-based (D-3). Populated at `Session::open` from `SessionConfig::initial_trace_context`; cleared at `Session::close` completion.

### E8 — `fixpp::current_trace_context` (free awaitable)

`inline constexpr struct current_trace_context_t { auto operator co_await() const noexcept; } current_trace_context;`. Resolution: read `co_await asio::this_coro::executor`; static-recover as `session_executor`; on hit → `session_ptr()->trace_slot_.load()`; on miss (control plane / listener accept / bootstrap) → engine atomic `engine_trace_context` snapshot. Synchronous in the common case; empty-slot mid-open → default `trace_context` + debug assert.

### E9 — `fixpp::core::cancellable_dispatch`

`template <class Handler> [[nodiscard]] asio::awaitable<expected_t<void>> cancellable_dispatch(session_executor exec, asio::cancellation_slot slot, Handler&& handler);`. Three-case contract: (1) slot signalled **before** pickup → handler reaped (not invoked), completes `expected_t<void>{ unexpect, error::dispatch_aborted }`; (2) slot signalled **during** execution → runs to next checkpoint, then slot honoured; (3) not signalled → `asio::dispatch` + one relaxed-atomic check (≤5 ns), completes `expected_t<void>{}`. Dispatch node from the session PMR resource (D-6). Accepts both threading modes uniformly (E6 wrapper holds either inner shape).

### E10 — `fixpp::session::Session` (minimal real skeleton — D-4)

2d-owned surface only: `asio::awaitable<expected_t<void>> close(close_mode = graceful)` (two-phase — D-9, idempotent: re-call/never-opened/already-closed → `error::session_already_closed`); engine-internal `[[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept` (never null; resolution chain `SessionConfig::session_arena ?: EngineConfig::default_session_resource ?: std::pmr::get_default_resource()`; callable from `fixpp::session/` only — `[arch §2.3]`); `session_local<trace_context> trace_slot_` member; executor→`session_executor` binding at open; callback-dispatch hooks. **No FIX FSM** (`005` extends). `close_mode : uint8_t { graceful = 0, terminal = 1 }` — `partial` excluded (N-P1-3).

### E11 — `fixpp::otel::trace_context` (minimal POD — D-1)

Trivially-copyable 32 B: `std::array<std::byte,16> trace_id` + `std::array<std::byte,8> span_id` + `std::uint8_t flags` + padding. Lock-free-atomic-eligible. `2k` extends; does not redefine.

### E12 — Per-session reusable `steady_timer` slot pool (D-8)

Session-lifetime; lazily allocated at first `sleep_until` from `session_arena`; **keyed by `Session*`** (round 2 root cause #1, not strand handle); reused every cycle (no per-cycle heap; seam 18 both modes).

## Error model — slots 47–55 (additive, non-renumbering, `[const §X.4]`)

Per research D-7. First free on this branch = 47 (43–46 = merged `006` `sync_*`).

| Slot | `fixpp::core::error` | Source | Class | C-ABI group |
|---|---|---|---|---|
| 47 | `executor_already_stopped` | resolved executor joined before open | config/lifetime | `FIXPP_ERR_THREAD_CONFIG` |
| 48 | `executor_not_serialised` | `direct_executor` w/o `already_serialized_executor` | construction | `FIXPP_ERR_THREAD_CONFIG` |
| 49 | `clock_sleeps_cancelled` | `sleep_until` completed via `cancel_sleeps` | cancellation | `FIXPP_ERR_CANCELLED` |
| 50 | `strand_dispatch_failed_oom` | PMR arena exhausted (dispatch / `cancellable_dispatch` node) | config bug; forced disconnect | `FIXPP_ERR_THREAD_RUNTIME` |
| 51 | `session_already_open` | `Session::open()` twice | programmer error | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` |
| 52 | `session_already_closed` | `close()` twice / never-opened | idempotency; non-fatal | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` |
| 53 | `invalid_session_config` | incompatible combo (`direct_executor+spin`; null `executor`/`dictionary`; sentinel `security_profile`) | construction | `FIXPP_ERR_THREAD_CONFIG` |
| 54 | `clock_not_set` | `EngineConfig::clock == nullptr` at `Engine::open` | config bug | `FIXPP_ERR_THREAD_CONFIG` |
| 55 | `dispatch_aborted` | `cancellable_dispatch` slot fired before pickup; handler reaped | cancellation; expected on phase-2 close | `FIXPP_ERR_CANCELLED` |

Final C-ABI coalescing is **2i's** call; 2d documents the grouping (per-doc-prefix `FIXPP_ERR_THREAD_*` discipline). NOT introduced: `trace_context_provider_threw` (C-P2-4), `cancellation_propagation_timeout` (N-P2-1), `version_registry_dictionary_missing` (Opus N2-P2-1 — routes to `[2c §6.7]` slot 28).

## Invariants

- **I-01** `Clock` has exactly 4 pure-virtual (`[const §XIV.2]`).
- **I-02** `now()` not monotonic; `steady_now()` is the sole elapsed/heartbeat/SendingTime-delta source (C-P2-5).
- **I-03** `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock`, resolved once at `Session::open`, bound to session lifetime; `clock_not_set` if `EngineConfig::clock == nullptr` regardless of overrides.
- **I-04** Session-scoped LOG/OBS read `effective_clock` + `clock_scope = session`; engine-scope read `EngineConfig::clock` + `clock_scope = engine`.
- **I-05** Per-session strand (default) serialises `{onLogon,onLogout,toAdmin,fromAdmin,toApp,fromApp,store op,clock wake,transport completion}`; no two overlap within a session; cross-session concurrent.
- **I-06** `direct_executor ⇒ already_serialized_executor == true` (else `executor_not_serialised`); `direct_executor + spin ⇒ invalid_session_config` (even attested); no engine-internal mutex/atomic regime added for `direct_executor`.
- **I-07** Two-phase close: phase-1 child `cancellation_state` below root (Logout + timeout NOT pre-cancelled by root total); phase-2 root `total` only after phase-1 resolves; `terminal` skips phase-1; `partial` not in v1.0.
- **I-08** `cancellable_dispatch` three-case determinism (E9); node from session PMR; no global heap.
- **I-09** Cancellation surfaces as `operation_aborted`/`dispatch_aborted`, never a thrown exception across parse→`fromApp`; maps to `FIXPP_ERR_CANCELLED`.
- **I-10** `close()` idempotent.
- **I-11** `current_trace_context` recovers `Session*` via `session_executor::session_ptr()` (member fn), NOT `any_io_executor::query`, NOT `thread_local`; survives `bind_executor`/`make_strand` + cross-thread resume.
- **I-12** Engine-fallback path reads the engine atomic `engine_trace_context` snapshot (no domain query).
- **I-13** `EngineConfig`/`SessionConfig` value-typed, frozen at open; close-and-reopen only; executor/clock/dictionary axes follow `resolved = override.value_or(engine_anchor)`.
- **I-14** `backpressure_mode` closed 2-value; `drop_oldest` unrepresentable + runtime out-of-range-cast reject.
- **I-15** FIXT.1.1 `ApplVerID(1128)` miss → `[2c §6.7] dict_no_dictionary_for_application_version` (slot 28), not a 2d synonym (dispatch-time **and** `Engine::open` registry-build).
- **I-16** Zero global `new`/`delete` parse→`fromApp` and on the heartbeat path (HALO + PMR fallback; per-`Session*` timer slot allocate-once); guard catches global-heap only.
- **I-17** `~Engine` ordering: close all (terminal) → drain → `cancel_sleeps()` → drain waiter list → clear `EngineConfig::clock` `shared_ptr` **last**; fixture-held `mock_clock` may outlive the engine safely.
- **I-18** `Session::session_arena()` never null (ctor pre-conditions the resolution chain; frozen at open); engine-internal; `core/` never back-edges into `session/` (`[arch §2.3]`).
- **I-19** §6.3 Tier-1 ceilings hold; CI fails on >5% regression; cross-thread dispatch row bench-soft (§10 Q4 follow-up).

## Test-seam ↔ entity/invariant coverage (21 seams)

Each `[2d §9]` seam maps to entities/invariants above; the plan.md Test-seam→file table binds each to a named file. Coverage spans I-01..I-19 (e.g. seam 2→I-05, seam 4/5→I-07/I-08/I-09, seam 6/17/21→I-11, seam 10/14→I-17, seam 13→I-14, seam 18→I-16, seam 19→E6/I-11, seam 20→I-15, seam 11→I-03/I-04). No seam is unmapped; no invariant is unexercised.
