# Phase 1 — Data Model — 007-threading-clock

**Anchor:** `.specify/2d-threading.md` v0.4. Entities distill the design-doc §4/§6/§8 surface; no shape is invented. On conflict the design doc wins.

## Entities

### E1 — `fixpp::core::Clock` (abstract plugin interface)

| Member | Shape | Notes |
|---|---|---|
| `~Clock()` | `virtual` default | polymorphic base |
| `now()` | `[[nodiscard]] virtual utc_time_point now() const noexcept = 0` | wall-clock UTC; **not** monotonic (C-P2-5) |
| `steady_now()` | `[[nodiscard]] virtual steady_time_point steady_now() const noexcept = 0` | monotonic; only elapsed source |
| `sleep_until(...)` | `[[nodiscard]] virtual asio::awaitable<void> sleep_until(steady_time_point) = 0` | completes on the awaiter's bound executor; honours the awaiter's cancellation slot. **No `expected_t<T>` here** (`[2d §4.1]` note) — cancellation via `asio::error::operation_aborted` (`[const §XI.2]`), NOT a returned `expected_t`; `error::clock_sleeps_cancelled` is the optional `expected_t` projection for callers that prefer it (`[2d §6.7]`), not the return type |
| `cancel_sleeps()` | `virtual void cancel_sleeps() noexcept = 0` | signals every in-flight awaiter's slot; idempotent; re-entrant-safe |

**Invariant:** exactly **4** pure-virtual (`[const §XIV.2]` 4/5, within cap). Owned by `EngineConfig` (or per-`SessionConfig` override) via `shared_ptr`; must outlive every referencing session. `utc_time_point = std::chrono::time_point<std::chrono::system_clock>`; `steady_time_point = std::chrono::time_point<std::chrono::steady_clock>`.

### E2 — `fixpp::core::system_clock_source` (default impl)

Ctor `explicit system_clock_source(asio::any_io_executor exec) noexcept` — takes the **engine-level executor** (`EngineConfig::executor`), NOT the session strand (`[2d §4.2]` note). `now()`→`system_clock::now()`; `steady_now()`→`steady_clock::now()`; `sleep_until()`→ASIO `steady_timer` from a **per-session reusable slot pool keyed by `Session*`**, allocated once from `session_arena` (D-8), `[[nodiscard]] asio::awaitable<void>` (no `expected_t` — cancellation via `operation_aborted`, E1); `cancel_sleeps()` walks an intrusive in-flight-awaiter list (O(N); v1.0 ≤ O(2×sessions)). `now()`/`steady_now()` thread-safe, non-blocking, `noexcept`. `~system_clock_source` drains its intrusive list (no live waiters in well-formed shutdown — sessions drained heartbeat slots first; D-9 / root cause #5).

### E3 — `fixpp::core::mock_clock` (test impl)

Ctor `mock_clock(utc_time_point initial_utc, steady_time_point initial_steady, asio::any_io_executor exec)` — seeded construction (`[2d §4.3]`); both clocks step independently. Pimpl over an opaque mutable-state object (D-10; `[const §XI.3]`). Public test header `<fixpp/core/test/mock_clock.hpp>`. `sleep_until()` is `[[nodiscard]] asio::awaitable<void>` (no `expected_t` — cancellation via `operation_aborted`, E1). Test-only API: `advance(delta)` walks a per-deadline ordered map and wakes every awaiter `deadline ≤ new_steady_now`, deterministic across runs; `step_to(point)` forces monotonic time (fast-forward); `set_utc_skew(skew)` sets a wall-clock-only delta that does **not** affect `steady_now()` (NTP-step / SendingTime-threshold simulation — US2 AC-3 / seam 1). Test-only; may outlive the engine when held by a fixture `shared_ptr` (`cancel_sleeps()` has no live waiters post-teardown — seam 14 variant).

### E4 — `fixpp::core::EngineConfig` (value type)

Required: `asio::any_io_executor executor`; `std::shared_ptr<Clock> clock` (**`clock_not_set` hard invariant at `Engine::open` regardless of session overrides** — root cause #2). Dictionary: `std::vector<std::shared_ptr<const dict::Dictionary>> dictionaries` → engine builds `dict::version_registry` at `Engine::open` (D-13). Defaultable PMR: `default_message_resource`, `default_session_resource` (`std::pmr::memory_resource*`, default `get_default_resource()`). Observability (null→no-op): `Logger`, `TracerProvider`, `MeterProvider`. Default plugin factories: `MessageStoreFactory`, `cert_source`, `TransportFactory`, `unique_ptr<ControlPlaneFactory>`. `fixpp::otel::trace_context engine_trace_context{}` held by the engine as a `std::atomic<trace_context>` snapshot (seqlock fallback if not lock-free; D-1).

### E5 — `fixpp::session::SessionConfig` (value type, frozen at open)

Threading: `std::optional<asio::any_io_executor> executor_override`; `threading_mode mode = per_session_strand`; `lock_policy locks = mutex`; `bool already_serialized_executor = false`; `std::shared_ptr<Clock> clock_override`. Identity (owned by `005`): `sender_comp_id`/`target_comp_id`/`begin_string`. Plugin overrides (null→inherit): `unique_ptr<MessageStoreFactory> store_factory`, `shared_ptr<cert_source>`, `SecurityProfile security_profile` (no-implicit-default sentinel — N-P2-3). Dictionary: `shared_ptr<const dict::Dictionary> dictionary` (required), `shared_ptr<const dict::DialectOverlay>` (optional). Thresholds (owned by `005`, placeholders): `std::optional<seconds> heartbeat_interval`, `std::optional<ms> test_request_threshold`, `std::optional<ms> sending_time_threshold`, `RejectPolicy reject_policy`. PMR: `message_arena`/`framer_carry_arena`/`session_arena` (`std::pmr::memory_resource*`, null→engine default). Observability: `fixpp::otel::trace_context initial_trace_context{}` (value-typed — C-P2-4, no callable in frozen config), `shared_ptr<log::Sink> log_sink_override`. Tap: `tap::TapConsumer`. Backpressure: nested `SessionConfig::backpressure_mode app_backpressure = SessionConfig::backpressure_mode::block`.

Enums:
- `threading_mode : uint8_t { per_session_strand = 0, direct_executor = 1 }`
- `lock_policy : uint8_t { mutex = 0, spin = 1 }`
- `SessionConfig::backpressure_mode : uint8_t { block = 0, disconnect_and_recover = 1 }` — **nested inside `SessionConfig`; closed, 2 values; `drop_oldest` unrepresentable** (`[[clang::enum_extensibility(closed)]]` where supported + `static_assert` at every switch; `[const §XV.15]`).

### E6 — `fixpp::core::session_executor` (project wrapper class)

Value-typed; satisfies `asio::execution::is_executor_v`. Private layout (`[2d §4.8]`): `asio::any_io_executor inner_` (the strand-wrapped executor under `per_session_strand` | the bare attested executor under `direct_executor` — the strand wrapping lives **inside** `inner_`) + `fixpp::session::Session* session_` + `bool strand_wrapped_`. Public surface (`[2d §4.8]`): default ctor; the construction ctor `session_executor(asio::any_io_executor inner, Session* session, bool strand_wrapped) noexcept`; `[[nodiscard]] Session* session_ptr() const noexcept`; the `[[nodiscard]] bool is_strand_wrapped() const noexcept` discriminator (debug asserts + the seam-16 re-entrancy guard test). **Copyable but NOT trivially copyable** — `asio::any_io_executor` is a type-erased polymorphic handle, so the wrapper is value-typed/copyable (the `bench` cheap-copy assumption is "cheap" not "trivial") but `std::is_trivially_copyable_v<session_executor>` is **false**; do not assert or imply trivial copyability. Survives `bind_executor`/`make_strand` (D-3). Session-lifetime; held by the `Session` instance; stored in `session_arena` (`[2d §8]`). Constructed only via the free helper `[[nodiscard]] expected_t<session_executor> make_session_executor(asio::any_io_executor resolved_exec, threading_mode mode, bool already_serialized_executor, Session* session) noexcept` (`[2d §4.8]`) — the **single enforcement point** for `error::executor_not_serialised` (slot 48; `mode==direct_executor && !already_serialized_executor`; `[2d §4.8]` / FR-009 / I-06). Called from `Session::open()` with `SessionConfig::executor_override.value_or(EngineConfig::executor)` (`[2d §4.8]`).

### E7 — `fixpp::core::session_local<T>`

`Session`-owned slot: `T value_{}`; `load()`/`store(T)`/`clear()` (`noexcept`); caller MUST be inside the owning session's serialisation domain (debug `Session*` self-check). NOT `thread_local`, NOT executor-property-based (D-3). Populated at `Session::open` from `SessionConfig::initial_trace_context`; cleared at `Session::close` completion.

### E8 — `fixpp::current_trace_context` (free awaitable)

`inline constexpr struct current_trace_context_t { auto operator co_await() const noexcept; } current_trace_context;`. Resolution: read `co_await asio::this_coro::executor`; resolve via the typed `session_executor` accessor, NOT `any_io_executor::query`; on hit → `session_ptr()->trace_slot_.load()`; on miss (control plane / listener accept / bootstrap) → engine atomic `engine_trace_context` snapshot. Synchronous in the common case; empty-slot mid-open → default `trace_context` + debug assert.

### E9 — `fixpp::core::cancellable_dispatch`

`template <class Handler> [[nodiscard]] asio::awaitable<expected_t<void>> cancellable_dispatch(session_executor exec, asio::cancellation_slot slot, Handler&& handler);`. Three-case contract: (1) slot signalled **before** pickup → handler reaped (not invoked), completes `expected_t<void>{ unexpect, error::dispatch_aborted }`; (2) slot signalled **during** execution → runs to next checkpoint, then slot honoured; (3) not signalled → `asio::dispatch` + one relaxed-atomic check (≤5 ns), completes `expected_t<void>{}`. Dispatch node from the session PMR resource — derived as `exec.session_ptr()->session_arena()` (`[2d §6.5]:1153-1154`; never-null `[2d §4.5]` chain); global heap on the dispatch path is a contract violation surfaced as `strand_dispatch_failed_oom` (slot 50), not a silent fallback — seam 7 alloc guard enforces (D-6). Accepts both threading modes uniformly (E6 wrapper holds either inner shape).

### E10 — `fixpp::session::Session` (minimal real skeleton — D-4)

2d-owned surface only: `[[nodiscard]] asio::awaitable<expected_t<void>> close(close_mode = graceful) noexcept` (signed-off `[2d §4.7]:833-834` shape — `[[nodiscard]]` + `noexcept`; two-phase — D-9, idempotent three-state model (`[2d §4.7]:830-832,863` / `[2d §6.5]:1172`): a re-call on an **already-closing** session returns the **same in-flight awaitable** with **no error** (no-op for the second caller); a call on a **never-opened** or **already-closed** (drained) session returns `error::session_already_closed`; no side effects in any case; under `graceful` the engine-internal `FileStore::flush_for_session_close()` hook runs once in phase 1 after the last in-flight `store(...)` and before the Logout `async_write`, not invoked under `terminal` — I-07); minimal 2d-owned `[[nodiscard]] asio::awaitable<expected_t<void>> open() noexcept` (the FIX FSM is `005`'s; the 2d-owned obligations bound to it: resolve the session executor `SessionConfig::executor_override.value_or(EngineConfig::executor)` → `fixpp::core::make_session_executor(...)` — the single `executor_not_serialised` enforcement point, `[2d §4.8]:994`; resolve `effective_clock` once — I-03; populate `trace_slot_` from `SessionConfig::initial_trace_context` — FR-014; reject null `dictionary`/null `EngineConfig::executor`/sentinel `security_profile`/incompatible combo → `invalid_session_config` and a second open → `session_already_open` slot 51 — FR-018; the design doc freezes no `open()` signature, only the §4.8:994 resolution rule, so this is the minimal real-skeleton shape pinning the testable 2d obligations); engine-internal `[[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept` (never null; resolution chain `SessionConfig::session_arena ?: EngineConfig::default_session_resource ?: std::pmr::get_default_resource()`; callable from `fixpp::session/` only — `[arch §2.3]`); `session_local<trace_context> trace_slot_` member; executor→`session_executor` binding at open; callback-dispatch hooks. **No FIX FSM** (`005` extends). `close_mode : uint8_t { graceful = 0, terminal = 1 }` — `partial` excluded (N-P1-3).

### E11 — `fixpp::otel::trace_context` (minimal POD — D-1)

Trivially-copyable, standard-layout, **exactly 32 B**: `std::array<std::byte,16> trace_id` + `std::array<std::byte,8> span_id` + `std::uint8_t flags` + `std::array<std::byte,7> _pad`. The size/trivial-copyability/standard-layout is **pinned by a `static_assert(sizeof == 32 && std::is_trivially_copyable_v && std::is_standard_layout_v)`** in `contracts/trace_context.hpp` (not a prose comment) — it is the contract the `std::atomic<trace_context>` snapshot `is_always_lock_free` probe + seqlock memcpy fallback rest on (D-1 / `quickstart.md:7`). Lock-free-atomic-eligible. `2k` extends; does not redefine.

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
| 51 | `session_already_open` | second `Session::open()` on the same handle (open contract: E10 / `contracts/session.hpp` / FR-018) | programmer error | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` |
| 52 | `session_already_closed` | `close()` on a never-opened or already-closed (drained) session — **NOT** on an already-closing one (that returns the same in-flight awaitable, no error — `[2d §4.7]:863`) | idempotency; non-fatal | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` |
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
- **I-06** `direct_executor ⇒ already_serialized_executor == true` (else `executor_not_serialised`, enforced at the single point `make_session_executor(...)` `[2d §4.8]:996`); `direct_executor + spin ⇒ invalid_session_config` (even attested; rejected at `Session::open`); no engine-internal mutex/atomic regime added for `direct_executor`.
- **I-07** Two-phase close: phase-1 child `cancellation_state` below root (Logout + timeout NOT pre-cancelled by root total); the engine-internal `FileStore::flush_for_session_close()` hook (non-virtual on the concrete `FileStore`, NOT on `MessageStore`'s pure-virtual interface — `[2e §4.1.1]`; reached via the session's `unique_ptr<MessageStore>` friend mechanism) runs **once during phase 1, after the FSM's last in-flight `store(...)` awaitable has resumed and before the Logout `async_write` is issued**; it is **idempotent** and is **NOT invoked under `terminal`** (terminal fires root cancellation immediately; the in-flight `MessageStore::write` row governs that state) — `[2d §4.7]:853,857` as amended at `2e` v0.4 sign-off (`[2d]:1594-1602`). The concrete `FileStore` is owned by `2e`; 007 wires only the phase-1 call site and the seam asserts the 2d-owned ordering property only (D-5 scripted-test-double scoping). phase-2 root `total` only after phase-1 resolves; `terminal` skips phase-1; `partial` not in v1.0.
- **I-08** `cancellable_dispatch` three-case determinism (E9); node from session PMR; no global heap.
- **I-09** Cancellation surfaces as `operation_aborted`/`dispatch_aborted`, never a thrown exception across parse→`fromApp`; maps to `FIXPP_ERR_CANCELLED`.
- **I-10** `close()` idempotent — three-state (`[2d §4.7]:830-832,863` / `[2d §6.5]:1172`): already-closing → same in-flight awaitable, no error, no side effects; never-opened or already-closed (drained) → `error::session_already_closed`.
- **I-11** `current_trace_context` recovers `Session*` via `session_executor::session_ptr()` (member fn), NOT `any_io_executor::query`, NOT `thread_local`; survives `bind_executor`/`make_strand` + cross-thread resume.
- **I-12** Engine-fallback path reads the engine atomic `engine_trace_context` snapshot (no domain query).
- **I-13** `EngineConfig`/`SessionConfig` value-typed, frozen at open; close-and-reopen only; executor/clock/dictionary axes follow `resolved = override.value_or(engine_anchor)`.
- **I-14** closed nested type `SessionConfig::backpressure_mode` (the frozen field is `SessionConfig::app_backpressure`) closed 2-value; `drop_oldest` unrepresentable + runtime out-of-range-cast reject.
- **I-15** FIXT.1.1 `ApplVerID(1128)` miss → `[2c §6.7] dict_no_dictionary_for_application_version` (slot 28), not a 2d synonym (dispatch-time **and** `Engine::open` registry-build).
- **I-16** Zero global `new`/`delete` parse→`fromApp` and on the heartbeat path (HALO + PMR fallback; per-`Session*` timer slot allocate-once); guard catches global-heap only.
- **I-17** `~Engine` ordering: close all (terminal) → drain → `cancel_sleeps()` → drain waiter list → clear `EngineConfig::clock` `shared_ptr` **last**; fixture-held `mock_clock` may outlive the engine safely.
- **I-18** `Session::session_arena()` never null (ctor pre-conditions the resolution chain; frozen at open); engine-internal; `core/` never back-edges into `session/` (`[arch §2.3]`).
- **I-19** §6.3 Tier-1 ceilings hold; CI fails on >5% regression; cross-thread dispatch row bench-soft (§10 Q4 follow-up).

## Test-seam ↔ entity/invariant coverage (21 seams)

Each `[2d §9]` seam maps to entities/invariants above; the plan.md Test-seam→file table binds each to a named file. Coverage spans I-01..I-19 (e.g. seam 2→I-05, seam 4/5→I-07/I-08/I-09, seam 6/17/21→I-11, seam 10/14→I-17, seam 13→I-14, seam 15→E1/I-07/I-09 (third-party-`Clock` conformance / SC-006 — scripted test-double FSM, D-5), seam 18→I-16, seam 19→E6/I-11, seam 20→I-15, seam 11→I-03/I-04). No seam is unmapped; no invariant is unexercised.

**Open-path / slot-51 binding (N5 close; round-2 N-C/N-D close):** the design doc gives `session_already_open` (slot 51) no dedicated `[2d §9]` seam, and `[2d §9.19]`'s own anchor is *"`session_executor` round-trip across both threading modes"* — the open-path arm is therefore a within-seam assertion arm, not a seam-count change (the design-doc 21-seam map is unchanged). The open-time obligations it asserts are now pinned by an on-disk contract: the minimal 2d-owned `Session::open()` shape (E10 / `contracts/session.hpp` / FR-018) freezes `session_already_open` rejection on a double `Session::open()`, the open-time `effective_clock = clock_override ?: EngineConfig::clock` resolution (I-03), and the `session_local<trace_context>` slot population from `SessionConfig::initial_trace_context` (FR-014). The arm is added to `tests/core/test_session_executor_round_trip.cpp` (`[2d §9.19]`), so slot 51 / I-03 / the FR-014 open-population obligation are bound to a named file for the SC-008 feature-completeness audit (a within-seam assertion arm flagged for `/tasks`, now well-formed because the open contract exists).
