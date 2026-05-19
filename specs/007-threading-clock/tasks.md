---
description: "Task list — 007-threading-clock"
---

# Tasks: Application Threading Contract & `fixpp::core::Clock`

**Input**: Design documents from `/specs/007-threading-clock/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-14), data-model.md (E1..E12 / I-01..I-19 / slots 47–55), contracts/ (11 shape oracles), quickstart.md
**Design anchor**: `.specify/2d-threading.md` **v0.4** (Gate-A-converged, round 3 + post-cap line-edit pass; incl. Appendix C cross-doc entries from `2f` v1.5 / `2e` v0.4). On conflict the design doc wins; an inconsistency is a defect in this bundle.

**Tests**: REQUIRED — TDD mandated (`[const §VII.1]`/`[const §VII.3]`/`[const §VII.4]`); the 21 named `[2d §9]` test seams are the behavioural contract. Each seam file is written to FAIL first, then made green.

**Organization**: Phase 2 ships the shared contract surface every story binds against (large, by design — a single tightly-coupled library feature). Phases 3–8 are the six user stories in priority order (US1/US2/US3 = P1; US4/US5 = P2; US6 = P3). Phase 9 is the cross-cutting sanitizer/coverage/verify/audit gate.

**Scope guards (carry into every task):**
- FSM-shaped seams (3, 9, 15, 16) + clock-injection corpus (11) are driven by a **deterministic scripted test-double FSM** in `tests/support/`; assert only 2d-owned properties (strand serialisation, `mock_clock` determinism, two-phase cancellation, alloc/latency) — **never FIX FSM correctness** (`005`-owned; D-5).
- `core/` MUST NOT back-edge into `session/` (`[arch §2.3]`); `Session::session_arena()` is engine-internal (`fixpp::session/` callers only).
- `error.hpp` edit is **additive, non-renumbering** at slots **47–55** only (first free verified on this branch; D-7).
- This feature does NOT edit `architecture.md` / `feature-catalogue.md` / `coverage-index.md` (already landed at 2d v0.4 sign-off; only the Gate-B catalogue Status-field promotion is residual orchestrator work — D-12).

## Path Conventions

Library submodule root: `research/G19-fix-fpml-iso20022/library/`. All paths below are relative to that root. Headers under `include/fixpp/{core,session}/`, sources under `src/{core,session}/`, tests under `tests/{core,session,alloc_guard,fuzz,support}/`, bench under `bench/threading/` + `bench/baselines/threading/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Pre-`/implement` probes (quickstart §0) and build scaffold.

- [X] T001 Run the `std::atomic<fixpp::otel::trace_context>` lock-freedom probe on every Tier-1 STL; record the result and whether the `seqlock` fallback path is selected (quickstart §0.1; research D-1; `[2d §4.4]`). Non-lock-free is NOT a blocker but MUST select the fallback, not silently degrade. — DONE: `is_always_lock_free=false` on libstdc++ AND libc++ (32 B > 16 B CAS) → **seqlock fallback REQUIRED** (not a blocker); evidence `.specify/decisions/007-threading-clock-probes.md`.
- [X] T002 Run the `session_executor` is-an-executor toolchain probe: confirm `asio::execution::is_executor_v<fixpp::core::session_executor>` and `bind_executor(session_executor, awaitable)` bound-executor type is recoverable (quickstart §0.2; seam 21). A failure here is a HARD pre-`/implement` blocker. — DONE: **PASS** — `is_executor_v` true + `bind_executor` recovers `session_executor` (clang 22 / asio 1.36.0); hard blocker CLEARED.
- [X] T003 [P] Confirm `tools/mallocnesia/libmallocnesia.so` is present and note the per-thread warm-up caveat for the alloc guards (quickstart §0.3; memory `reference_mallocnesia_path`; seams 7/18). — DONE: present (16600 B, +x); warm-up caveat noted.
- [X] T004 [P] Verify no new Conan row needed — `asio/1.36.0` already pinned (added by 006), GTest 1.17.0 / Benchmark 1.9.5 unchanged — against `conanfile.py` on this branch (`[const §III.2]`). — DONE: confirmed, no new row.
- [X] T005 Create the source/test directory scaffold and CMake target wiring stubs for `include/fixpp/core/`, `include/fixpp/session/`, `src/core/`, `src/session/`, `tests/{core,session,alloc_guard,fuzz,support}/`, `bench/threading/`, `bench/baselines/threading/` per plan.md Project Structure (empty/declared targets only; bodies land in later phases). — DONE: created `include/fixpp/core/test/`, `src/core/test/`, `bench/threading/` (declared-empty CMake stub wired into `bench/CMakeLists.txt`), `bench/baselines/threading/`; existing dirs reused.

**Checkpoint**: Probes recorded; build skeleton compiles empty.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The shared contract surface (value types + the `Clock` plugin interface + the `Session` bind target + the test-double FSM) every user story's seams compile and link against. Realizes the `contracts/` shape oracles verbatim (authored from the design-doc §4.1/§4.2/§4.3/§4.7 frozen API blocks, NOT prose — round-1 root cause A).

**⚠️ CRITICAL**: No user-story phase can begin until this phase is complete.

- [X] T006 Append the **9** threading error variants at slots **47–55** in `include/fixpp/core/error.hpp` in design-doc table order (`executor_already_stopped` 47, `executor_not_serialised` 48, `clock_sleeps_cancelled` 49, `strand_dispatch_failed_oom` 50, `session_already_open` 51, `session_already_closed` 52, `invalid_session_config` 53, `clock_not_set` 54, `dispatch_aborted` 55) — additive, non-renumbering; mirror the `006` `sync_errors.hpp` additive-extension style (no standalone enum type) per `contracts/threading_errors.hpp`; document the `FIXPP_ERR_THREAD_{CONFIG,SESSION_LIFECYCLE,RUNTIME}` / `FIXPP_ERR_CANCELLED` coalescing groups (data-model slots table; D-2/D-7; `[2d §6.7]`; `[const §X.4]`).
- [X] T007 [P] Implement `fixpp::otel::trace_context` minimal POD in `include/fixpp/core/trace_context.hpp` — `std::array<std::byte,16> trace_id` + `std::array<std::byte,8> span_id` + `std::uint8_t flags` + `std::array<std::byte,7> _pad`, with the in-header `static_assert(sizeof==32 && std::is_trivially_copyable_v && std::is_standard_layout_v)` (E11; D-1; `contracts/trace_context.hpp`). POD only — the `current_trace_context` awaitable is US4.
- [X] T008 [P] Implement the `fixpp::core::Clock` 4-pure-virtual interface + `utc_time_point`/`steady_time_point` aliases in `include/fixpp/core/clock.hpp` exactly per `contracts/clock.hpp` — `now()`/`steady_now()` `[[nodiscard]] noexcept`, `sleep_until(steady_time_point) -> [[nodiscard]] asio::awaitable<void>` (NO `expected_t` return — cancellation via `operation_aborted`), `cancel_sleeps() noexcept`; assert exactly 4 pure-virtual (E1; I-01; `[const §XIV.2]`).
- [X] T009 [P] Implement `fixpp::session::SessionConfig` + the three enums in `include/fixpp/session/session_config.hpp` per `contracts/session_config.hpp` — namespace-scoped `threading_mode`/`lock_policy`, the **nested closed 2-value** `SessionConfig::backpressure_mode` (`[[clang::enum_extensibility(closed)]]` where supported) with the frozen field named `SessionConfig::app_backpressure`; all nullable-override fields; `heartbeat_interval`/`test_request_threshold`/`sending_time_threshold` as `std::optional` placeholders (owned by `005`); NO `close_timeout` field (D-9) (E5; I-13/I-14).
- [X] T010 Implement `fixpp::core::EngineConfig` in `include/fixpp/core/engine_config.hpp` per `contracts/engine_config.hpp` — required `executor` + `std::shared_ptr<Clock> clock`; `dictionaries` vector; defaultable PMR resources; null-able observability/plugin factories; `engine_trace_context` held as `std::atomic<trace_context>` snapshot with the seqlock fallback selected per T001 (E4; depends on T007/T008).
- [X] T010a Create the minimal abstract-interface stubs `fixpp::session::MessageStoreFactory` (`include/fixpp/session/message_store_factory.hpp`) and `fixpp::service::ControlPlaneFactory` (`include/fixpp/service/control_plane_factory.hpp`) — pure-virtual base + virtual dtor + deleted copy/move; `TransportFactory` stays a fwd-decl (shared_ptr member). **Prerequisite for T009/T010**: their `std::unique_ptr<…Factory>` members make the `SessionConfig`/`EngineConfig` value-typed destructors ill-formed unless these types are complete (D-15). Concrete factories owned by `005`/`2j`; 007 ships only the polymorphic bind target (D-1/D-4 minimal-skeleton precedent). *Retro-tracked: created reactively during the Phase-2 build, not originally scheduled at `/speckit-tasks` nor caught by the step-9 `gate.md` audit — see research D-15 provenance note.*
- [X] T011 Implement the `fixpp::core::session_executor` wrapper-class **shape** in `include/fixpp/core/session_executor.hpp` per `contracts/session_executor.hpp` — private `asio::any_io_executor inner_` + `Session* session_` + `bool strand_wrapped_`; public default ctor, construction ctor `session_executor(asio::any_io_executor, Session*, bool) noexcept`, `[[nodiscard]] Session* session_ptr() const noexcept`, `[[nodiscard]] bool is_strand_wrapped() const noexcept`; satisfies `asio::execution::is_executor_v`; copyable but NOT trivially copyable (do not assert trivial copyability) (E6; D-3). The `make_session_executor` enforcement behavior is US1 (T020).
- [X] T012 Implement the minimal real `fixpp::session::Session` **skeleton shape** in `include/fixpp/session/session.hpp` (+ `src/session/session.cpp` if out-of-line) per `contracts/session.hpp` — 2d-owned surface only: `[[nodiscard]] asio::awaitable<expected_t<void>> open() noexcept`, `[[nodiscard]] asio::awaitable<expected_t<void>> close(close_mode = graceful) noexcept`, engine-internal `[[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept` (never-null resolution chain, `fixpp::session/`-only), `session_local<trace_context> trace_slot_` member, `close_mode : uint8_t { graceful=0, terminal=1 }` (NO `partial`); ctor pre-conditions a non-null `session_arena`. **No FIX FSM** — `005` extends (E10; D-4; `[arch §2.3]`). Per-story behavior (executor binding, two-phase close, slot population) wired in US phases.
- [X] T013 [P] Implement the deterministic scripted test-double FSM in `tests/support/` — replays Logon→NewOrderSingle→ExecutionReport→Logout-shaped label sequences and heartbeat windows deterministically; injectable `Clock`; consumed by the FSM-shaped seams (3, 9, 15, 16) and the clock-injection corpus (11); asserts only 2d-owned properties (D-5). Picks values 2d does not pin (e.g. heartbeat interval).
- [X] T014 Wire all Phase-2 headers/sources into the CMake library target + the GoogleTest/Benchmark test targets declared in T005; confirm the library + an empty test binary build clean on `linux-clang-debug`.

**Checkpoint**: Shared contract surface compiles; all 21 seam files can `#include` and link the contracts. User-story phases may begin.

---

## Phase 3: User Story 1 — Per-session strand & executor opt-out (Priority: P1) 🎯 MVP

**Goal**: Every `Session` runs on a serialisation domain derived from `executor_override.value_or(EngineConfig::executor)`; default `per_session_strand` wraps it in `asio::make_strand` and serialises all app callbacks (no two overlap within a session, cross-session concurrent, never on the recv thread); `direct_executor` opt-out requires attestation, enforced at the single `make_session_executor(...)` point.

**Independent Test**: Seam 2 (strand serialisation, TSan) + seam 3 (6-combo executor-compat via scripted FSM) + seam 16 (`direct_executor` reentrancy) + seam 19 (round-trip both modes incl. the `executor_not_serialised` enforcement-point + N5 open-path arm) all green; FR-007/FR-008/FR-009 satisfied.

### Tests for User Story 1 (write FIRST, ensure they FAIL) ⚠️

- [X] T015 [P] [US1] Seam 2 — strand-serialisation property (TSan-mandatory) in `tests/session/test_strand_serialisation.cpp`: N user threads drive `Session::send(...)` (test-double label); assert `fromApp`/`toApp` strictly non-overlapping single-threaded ordering, never on recv thread, cross-session concurrent (`[2d §9.2]`/§6.1; I-05).
- [X] T016 [P] [US1] Seam 3 — executor-opt-out compatibility, 6 combos via the scripted FSM (T013) in `tests/session/test_executor_compat.cpp`: `{thread_pool, system_executor, 1-thread io_context} × {per_session_strand, direct_executor(attested)}` each completes the Logon→…→Logout label sequence correctly (`[2d §9.3]`/§6.1).
- [X] T017 [P] [US1] Seam 16 — `direct_executor` re-entrancy guard in `tests/session/test_direct_executor_reentrancy.cpp` using `session_executor::is_strand_wrapped()`; debug-build strand-invariant assert on detected concurrent FSM entry (`[2d §9.16]`/root cause #1).
- [X] T018 [P] [US1] Seam 19 — `session_executor` round-trip across both modes via `make_session_executor(...)` in `tests/core/test_session_executor_round_trip.cpp`, INCLUDING the `executor_not_serialised` enforcement-point assertion (`[2d §4.8]:996`) and the N5 open-path arm: second `Session::open()` → `session_already_open` (slot 51), open-time `effective_clock` resolution (I-03), `session_local` slot population (FR-014) (`[2d §9.19]`/round-2 RC#1/N5).

### Implementation for User Story 1

- [X] T019 [US1] Implement `fixpp::core::make_session_executor(resolved_exec, mode, already_serialized_executor, session) -> expected_t<session_executor>` in `include/fixpp/core/session_executor.hpp` — the SINGLE `error::executor_not_serialised` (slot 48) enforcement point: `per_session_strand` wraps `resolved_exec` in `asio::make_strand` (strand lives inside `inner_`), `direct_executor` carries the bare attested executor, `direct_executor && !already_serialized_executor` → `executor_not_serialised` (FR-009; I-06; `[2d §4.8]:983-987,996`).
- [X] T020 [US1] Wire `Session::open()` executor resolution into `include/fixpp/session/session.hpp` / `src/session/session.cpp`: resolve `SessionConfig::executor_override.value_or(EngineConfig::executor)` → `make_session_executor(...)`; reject `direct_executor + lock_policy::spin` with `error::invalid_session_config` (slot 53) even when attested; reject second `open()` with `error::session_already_open` (slot 51); reject null `EngineConfig::executor` with `invalid_session_config` (FR-018; I-06; depends on T019).
- [X] T021 [US1] Implement the per-session strand callback-dispatch path so `{onLogon,onLogout,toAdmin,fromAdmin,toApp,fromApp,store op,clock wake,transport completion}` serialise on the resolved domain — `fromApp(N+1)` never begins before `fromApp(N)` returns; the engine never picks a concrete executor (FR-007/FR-008; I-05; `[2d §6.1]`).

**Checkpoint**: US1 seams (2/3/16/19) green incl. TSan; strand contract independently testable. **MVP boundary.**

---

## Phase 4: User Story 2 — Pluggable Clock seam (NFR-015) (Priority: P1)

**Goal**: `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` resolved once at `Session::open`, feeds every session-scoped consumer; `system_clock_source` default + pimpl'd `mock_clock`; `Engine::open` rejects null `EngineConfig::clock`; `cancel_sleeps()` is concurrent/re-entrant-safe; `now()` non-monotonic, `steady_now()` sole elapsed source.

**Independent Test**: Seam 1 (`mock_clock` determinism) + seam 9 (heartbeat-window via scripted FSM) + seam 10 (sleep/cancel race, TSan+ASan) + seam 11 (2d-scoped clock-injection corpus vs golden) + seam 14 (engine-shutdown ordering) + seam 15 (third-party `Clock` conformance / SC-006 via scripted FSM) green; FR-001..006, SC-002, SC-006 satisfied.

### Tests for User Story 2 (write FIRST, ensure they FAIL) ⚠️

- [X] T022 [P] [US2] Seam 1 — `mock_clock` determinism in `tests/core/test_mock_clock_determinism.cpp`: two identically-seeded instances + same `advance` sequence → identical `now`/`steady_now`/wake-up order; cover `step_to`/`set_utc_skew` (`[2d §9.1]`; E3).
- [X] T023 [P] [US2] Seam 9 — heartbeat-window simulation under `mock_clock` via the scripted FSM (T013) in `tests/session/test_heartbeat_under_mock_clock.cpp` (`[2d §9.9]`).
- [X] T024 [P] [US2] Seam 10 — `sleep_until` + `cancel_sleeps` race (TSan+ASan-clean) in `tests/core/test_sleep_cancel_race.cpp`: N in-flight waiters, concurrent `cancel_sleeps()`, every awaiter completes exactly once (deadline | `operation_aborted`), no leak/double-completion (`[2d §9.10]`/§6.6; I-17).
- [X] T025 [P] [US2] Seam 11 — 2d-scoped deterministic clock-injection corpus in `tests/session/test_clock_injection_corpus.cpp` (relocated from `tests/conformance/`; D-5): `mock_clock` via `clock_override`, diff against golden expectation; proves zero direct `std::chrono::system_clock::now()` leak on any session-scoped path; engine-scope records unaffected (`[2d §9.11]`/§7.9; SC-002; I-03/I-04).
- [X] T026 [P] [US2] Seam 14 — engine-shutdown ordering in `tests/core/test_engine_shutdown_order.cpp`: `~Engine` → close all (terminal) → drain → `cancel_sleeps()` → drain waiter list → clear `EngineConfig::clock` `shared_ptr` LAST; fixture-held `mock_clock` outlives the engine safely (`[2d §9.14]`/root cause #5; I-17).
- [X] T027 [P] [US2] Seam 15 — third-party `Clock` conformance / SC-006 via the scripted FSM (T013) in `tests/core/test_third_party_clock_conformance.cpp`: a non-`system_clock_source`/non-`mock_clock` derivative drives "Logon→NewOrderSingle→cancel" (test-double labels); assert `sleep_until` completes on the awaiter's bound executor + root-`total`/child-state cancellation honoured + alloc-guard clean — NOT FIX FSM correctness (`[2d §9.15]`/§4.1.1; SC-006; D-5).

### Implementation for User Story 2

- [X] T028 [P] [US2] Implement `fixpp::core::system_clock_source` in `include/fixpp/core/system_clock_source.hpp` (+ `src/core/system_clock_source.cpp` if out-of-line) per `contracts/system_clock_source.hpp` — ctor takes the engine-level `asio::any_io_executor`; `now()`→`system_clock::now()`, `steady_now()`→`steady_clock::now()`, `sleep_until()`→ASIO `steady_timer`; `cancel_sleeps()` walks an intrusive in-flight-awaiter list O(N); `~system_clock_source` drains the list (E2; FR-002/FR-003; the per-`Session*` timer-slot reuse is T046 / US6).
- [X] T029 [P] [US2] Implement `fixpp::core::mock_clock` in `include/fixpp/core/test/mock_clock.hpp` + `src/core/test/mock_clock.cpp` per `contracts/mock_clock.hpp` — pimpl'd over an opaque mutable-state object (`[const §XI.3]`), seeded ctor, deterministic `advance(delta)` (per-deadline ordered map, wakes `deadline ≤ new_steady_now`), `step_to`/`set_utc_skew` (E3; D-10; FR-002).
- [X] T029a [US2] Implement `Clock::sleep_until` completion landing on the awaiter's bound executor and `cancel_sleeps()` signalling every in-flight waiter's slot (idempotent, concurrent- and re-entrant-safe, O(N) / ≤O(2×sessions)) for both impls (FR-003; `[2d §4.1]`/§6.6).
- [X] T030 [US2] Implement the single-`effective_clock` resolution in `Session::open` (`SessionConfig::clock_override ?: EngineConfig::clock`, resolved once, bound to session lifetime) and route every session-scoped consumer (heartbeat, SendingTime, S-035, session-scoped LOG/OBS) through it; engine-scope records read `EngineConfig::clock` with `clock_scope = engine` (FR-005; I-03/I-04; `[2d §7.9]`; depends on T020).
- [X] T031 [US2] Enforce the `error::clock_not_set` (slot 54) hard invariant at `Engine::open` when `EngineConfig::clock == nullptr` regardless of any session `clock_override` (FR-006; root cause #2; `[2d §4.4]`).
- [X] T032 [US2] Enforce `now()` non-monotonic / `steady_now()`-only for all elapsed/heartbeat/SendingTime-delta/S-035 measurements; `now()` used only for wire-formatted + log/OTel timestamps; a benign NTP backward step never trips a SendingTime reject (FR-004; I-02; `[2d §6.6]`).

**Checkpoint**: US2 seams (1/9/10/11/14/15) green incl. TSan+ASan; clock seam (NFR-015) independently testable.

---

## Phase 5: User Story 3 — Two-phase close cancels in-flight work (Priority: P1)

**Goal**: `close(graceful)` runs phase 1 (last `store(...)` resumes → `FileStore::flush_for_session_close()` hook once → Logout exchange + `Clock::sleep_until` timeout under a CHILD `cancellation_state`) then phase 2 (`cancellation_type::total` on the root); `close(terminal)` skips phase 1 (hook NOT invoked); `partial` absent; cancellation surfaces as `operation_aborted`/`dispatch_aborted` never an exception; `close()` idempotent with the signed-off three-state model; `cancellable_dispatch` three-case determinism.

**Independent Test**: Seam 4 (cancellation parse→`fromApp`) + seam 5 (cancellation `fromApp`→close) + seam 12 (libFuzzer cancellation-injection) green; FR-011/FR-012/FR-013, SC-007 satisfied.

### Tests for User Story 3 (write FIRST, ensure they FAIL) ⚠️

- [X] T033 [P] [US3] Seam 4 — cancellation-slot propagation parse→`fromApp` in `tests/session/test_cancellation_parse_to_fromapp.cpp`: `close(graceful)` while `fromApp` pending dispatch → reaped via `cancellable_dispatch`, awaitable completes `expected_t<void>{unexpect, error::dispatch_aborted}` (`[2d §9.4]`/§6.5; I-08/I-09).
- [X] T034 [P] [US3] Seam 5 — cancellation-slot propagation `fromApp`→close in `tests/session/test_cancellation_fromapp_to_close.cpp`: block inside `fromApp` on a cancelled `sleep_until`, `close(terminal)` drains it with `operation_aborted`; phase-1/phase-2 ordering + child-state non-pre-cancellation; `FileStore::flush_for_session_close()` invoked once under graceful, NOT under terminal (`[2d §9.5]`/§6.5; I-07).
- [X] T035 [P] [US3] Seam 12 — libFuzzer cancellation-timing harness in `tests/fuzz/fuzz_session_cancellation.cpp` (voluntary per `[2d §9 seam 12]` Gate-A discretion; NOT a `[const §VII.7]` obligation — 2d not parser-touching): fire `close(graceful)`/`close(terminal)` at every `co_await` checkpoint; assert no deadlock / double-free / PMR leak (`[2d §9.12]`; SC-007; D-11).

### Implementation for User Story 3

- [X] T036 [US3] Implement `fixpp::core::cancellable_dispatch(session_executor, asio::cancellation_slot, Handler&&) -> asio::awaitable<expected_t<void>>` in `include/fixpp/core/cancellable_dispatch.hpp` per `contracts/cancellable_dispatch.hpp` — the deterministic three-case contract (slot-before-pickup → reaped, `error::dispatch_aborted` slot 55; slot-during → run to next checkpoint then honour; not-signalled → `asio::dispatch` + ≤5 ns relaxed-atomic check → `expected_t<void>{}`); dispatch node from the session PMR via `exec.session_ptr()->session_arena()`; global heap on this path → `error::strand_dispatch_failed_oom` (slot 50), not a silent fallback (E9; FR-012; I-08; `[2d §6.5]:1153-1154`).
- [X] T037 [US3] Implement two-phase `Session::close(close_mode)` in `include/fixpp/session/session.hpp` / `src/session/session.cpp` — phase 1: after the FSM's last in-flight `store(...)` resumes and BEFORE the Logout `async_write`, invoke the engine-internal `FileStore::flush_for_session_close()` hook exactly once (idempotent; reached via the session's `unique_ptr<MessageStore>` friend mechanism; on mid-flush `fdatasync`/`FlushFileBuffers` error → `expected_t::unexpected{store_io_failure}` logged then proceed; 007 wires only the call site — concrete `FileStore` is `2e`'s); Logout exchange + `Clock::sleep_until` timeout (`deadline = effective_clock.steady_now() + close_timeout`, value supplied mechanically by `005`, NOT a `SessionConfig` field) under a CHILD `asio::cancellation_state` below the root; phase 2: fire `cancellation_type::total` on the root only after phase 1 resolves (peer ACK | child timeout | child cancelled). `terminal` skips phase 1 (hook NOT invoked); `partial` excluded (FR-011; I-07; D-9; `[2d §4.7]` as amended at `2e` v0.4). **D-16:** 007 ships NO `MessageStore`/`FileStore` type; the phase-1 hook is realized as an injectable engine-internal `set_close_flush_hook(...)` seam the seam-5 scripted test-double sets — "wire only the call site, assert the 2d-owned ordering property" (I-07 / D-5). Realizability resolved proactively by the hardened §9 audit rule, not a build-time fix.
- [X] T038 [US3] Implement `close()` idempotency three-state model + cancellation surfacing: already-closing → return the SAME in-flight awaitable, no error, no side effects; never-opened or already-closed(drained) → `error::session_already_closed` (slot 52); cancellation surfaces as `operation_aborted`/`dispatch_aborted` and never a thrown exception across the parse→`fromApp` window; maps to `FIXPP_ERR_CANCELLED` at the C ABI (documented grouping only — 2i owns symbols) (FR-013; I-09/I-10; `[2d §4.7]:830-832,863`).
- [X] T039 [US3] Propagate phase-2 root `cancellation_type::total` to transport read/write, heartbeat sleep, awaitable-mutex acquire (the merged `006` 2f surface), application-callback dispatch (via `cancellable_dispatch`), and the parser→`fromApp` chain (FR-011; I-07; `[2d §6.5]`).

**Checkpoint**: US3 seams (4/5/12) green incl. fuzz campaign clean; two-phase cancellation independently testable. **All P1 stories complete.**

---

## Phase 6: User Story 4 — Session-domain trace context, never `thread_local` (Priority: P2)

**Goal**: `co_await fixpp::current_trace_context` recovers the typed `Session*` via `session_executor::session_ptr()` (NOT `any_io_executor::query`, NOT `thread_local`), reads the `session_local<trace_context>` slot in-domain; engine-fallback to the atomic `EngineConfig::engine_trace_context` outside any session domain; survives `bind_executor`/`make_strand` + cross-thread resume.

**Independent Test**: Seam 6 (resume-on-different-thread) + seam 17 (`session_local` lifetime-under-cancellation, TSan) + seam 21 (accessor-survives-ASIO-erasure, compile+runtime+negative) green; FR-014/FR-015 satisfied.

### Tests for User Story 4 (write FIRST, ensure they FAIL) ⚠️

- [X] T040 [P] [US4] Seam 6 — `trace_context` resume-on-different-thread in `tests/core/test_trace_context_resume.cpp`: bind a session over a 4-thread pool, capture `current_trace_context` in `fromApp`, `co_await` a sleep resuming on a different thread (assert thread-id changed), re-read, assert byte-equality (`[2d §9.6]`/§4.6; I-11).
- [X] T041 [P] [US4] Seam 17 — `session_local<T>` lifetime-under-cancellation (TSan-clean) in `tests/core/test_session_local_lifetime.cpp`: `close(graceful)` mid-`fromApp`, slot stays valid until close completes, never read through a destroyed slot (`[2d §9.17]`/§4.6; I-11; I-17).
- [X] T042 [P] [US4] Seam 21 — `session_executor` typed-accessor survives ASIO erasure in `tests/core/test_session_executor_accessor_survives_erasure.cpp`: `static_assert` ASIO executor concept + survives `bind_executor`/`make_strand`; runtime recovery; NEGATIVE assertion that an `any_io_executor` cast does NOT expose a `Session*` property query (the rejected v0.3 shape, documented known-bad) (`[2d §9.21]`/round-3 RC#1; I-11).

### Implementation for User Story 4

- [X] T043 [P] [US4] Implement `fixpp::core::session_local<T>` in `include/fixpp/core/session_local.hpp` per `contracts/session_local.hpp` — `Session`-owned `T value_{}`; `load()`/`store(T)`/`clear()` `noexcept`; debug `Session*` self-check that the caller is inside the owning session's serialisation domain; NOT `thread_local`, NOT executor-property-based (E7; D-3).
- [X] T044 [US4] Implement the `fixpp::current_trace_context` free awaitable in `include/fixpp/core/trace_context.hpp` per `contracts/trace_context.hpp` — read `co_await asio::this_coro::executor`, recover `Session*` via the typed `session_executor::session_ptr()` (NOT `any_io_executor::query`); hit → `session_ptr()->trace_slot_.load()`; miss → engine atomic `engine_trace_context` snapshot; empty-slot mid-open → default `trace_context` + debug assert (E8; FR-015; I-11/I-12; depends on T007/T011/T043).
- [X] T045 [US4] Wire slot population/teardown into `Session::open`/`Session::close`: populate `trace_slot_` from `SessionConfig::initial_trace_context` at open, clear at close completion (FR-014; depends on T020/T037/T043).

**Checkpoint**: US4 seams (6/17/21) green incl. TSan; trace-context domain binding independently testable.

---

## Phase 7: User Story 5 — Frozen engine-anchor + session-override config split (Priority: P2)

**Goal**: `EngineConfig`/`SessionConfig` value-typed, frozen at open, uniform `resolved = override.value_or(engine_anchor)` across executor/clock/dictionary axes; illegal states unrepresentable or rejected at open; `drop_oldest` unrepresentable; FIXT.1.1 `ApplVerID(1128)` miss routes through the existing `[2c §6.7]` dict-layer error, not a 2d synonym.

**Independent Test**: Seam 13 (drop-oldest banned, compile+runtime) + seam 20 (`version_registry` missing → `[2c §6.7]`) green; FR-010/FR-016/FR-017/FR-018 satisfied.

### Tests for User Story 5 (write FIRST, ensure they FAIL) ⚠️

- [ ] T046 [P] [US5] Seam 13 — drop-oldest-banned-on-app-path enforcement in `tests/session/test_backpressure_drop_oldest_banned.cpp`: compile-time enum-exhaustiveness `static_assert` + runtime out-of-range cast (from int) rejected at construction (`[2d §9.13]`/`[const §XV.15]`; I-14).
- [ ] T047 [P] [US5] Seam 20 — `version_registry` dictionary-missing routes through `[2c §6.7]` in `tests/session/test_version_registry_missing_routes_to_dict_layer.cpp`: omit a FIXT.1.1 version from `EngineConfig::dictionaries`, drive an `ApplVerID(1128)` override frame; failure surfaces as `[2c §6.7] dict_no_dictionary_for_application_version` (slot 28 → `FIXPP_ERR_DICT_CONFIG`), NOT a 2d synonym — both dispatch-time AND `Engine::open` registry-build paths (`[2d §9.20]`/Opus N2-P2-1; I-15).

### Implementation for User Story 5

- [ ] T048 [US5] Enforce the closed `SessionConfig::backpressure_mode` (2 values, `drop_oldest` unrepresentable) with a `static_assert` at every switch site and a runtime out-of-range-cast reject at construction (FR-010; I-14; `[const §XV.15]`; `[2d §6.4]`).
- [ ] T049 [US5] Build `dict::version_registry` from `EngineConfig::dictionaries` at `Engine::open` via the merged-`003` `[2c §4.9]` API; route a FIXT.1.1 `ApplVerID(1128)` miss through the existing `[2c §6.7] dict_no_dictionary_for_application_version` (slot 28) — no 2d-layer synonym introduced — at BOTH dispatch-time and registry-build (FR-017; I-15; D-13).
- [ ] T050 [US5] Complete `Session::open` config-validation rejections: null `dictionary`, default-constructed `security_profile` sentinel, or any incompatible combination → `error::invalid_session_config` (slot 53); confirm the uniform `resolved = override.value_or(engine_anchor)` pattern holds across the executor/clock/dictionary axes and that all frozen fields are close-and-reopen-only (FR-016/FR-018; I-13; `[arch §5.6]`; depends on T020/T030).

**Checkpoint**: US5 seams (13/20) green; config split + dict-layer routing independently testable.

---

## Phase 8: User Story 6 — Zero-global-alloc, exception-free, latency-ceilinged hot path (Priority: P3)

**Goal**: parse→`fromApp` touches zero global heap (HALO-elided awaiter frame; PMR fallback on `message_arena`); the `system_clock_source` per-session `steady_timer` slot is allocated exactly once on the first `sleep_until` for that session from `session_arena`, keyed by `Session*`, reused every cycle; no exception crosses the window; §6.3 Tier-1 ceilings hold, CI fails on >5% regression.

**Independent Test**: Seam 7 (dispatch alloc guard) + seam 18 (clock-sleep alloc guard) + seam 8 (latency regression bench) green; FR-020/FR-021, SC-003/SC-004 satisfied.

### Tests for User Story 6 (write FIRST, ensure they FAIL) ⚠️

- [ ] T051 [P] [US6] Seam 7 — dispatch-hot-path allocation guard (`mallocnesia`, Linux/Clang) in `tests/alloc_guard/test_dispatch_alloc_guard.cpp`: zero GLOBAL-heap `new`/`delete`/`malloc` between parse and `fromApp` over a 10⁴-message corpus (PMR-arena allocations expected, NOT flagged — N-P2-4) (`[2d §9.7]`/§6.2; SC-003; I-16).
- [ ] T052 [P] [US6] Seam 18 — `Clock::sleep_until`-path allocation guard in `tests/alloc_guard/test_clock_sleep_alloc_guard.cpp`: zero global-heap after the first heartbeat cycle over 10⁴ cycles, BOTH threading modes; an idle session that never sleeps allocates no slot (`[2d §9.18]`/root cause #5 / N-P1-1; SC-003; I-16).
- [ ] T053 [P] [US6] Seam 8 — latency regression bench in `bench/threading/bench_threading.cpp` + baseline `bench/baselines/threading/threading_baselines.json`: assert the §6.3 ceilings (in-strand ≤25 ns, cross-thread ≤250 ns [bench-soft, §10 Q4], cross-strand reify+dispatch 20-tag ≤1.25 µs / 200-tag ≤10.25 µs, `now()` ≤25 ns, `steady_now()` ≤20 ns, in-domain trace ≤15 ns, engine-fallback ≤25 ns); CI fails on >5% regression vs the previous tagged release (`[2d §9.8]`/§6.3; SC-004; I-19).

### Implementation for User Story 6

- [ ] T054 [US6] Make the parse→`fromApp` dispatch chain HALO-elision-eligible (single strand-local invocation chain, header-visible) with a per-awaiter PMR override constructing the promise on `SessionConfig::message_arena` when HALO does not fire; ensure no exception crosses the window (PMR throw routes through `fixpp::core::detail::trap_throw`) (FR-020/FR-021; I-16; D-6; `[const §VIII.5]`/`[const §XI.6]`).
- [ ] T055 [US6] Implement the per-session reusable `steady_timer` slot pool in `system_clock_source` — lazily allocated once at the FIRST `sleep_until` for that session from `session_arena`, keyed by `Session*` (NOT strand handle), `expires_at`+`async_wait` reset with no allocation thereafter, under both threading modes (FR-020; E12; D-8; round-2 RC#1; extends T028).
- [ ] T056 [US6] Capture the bench baseline `threading_baselines.json` from a tagged release on `linux-clang-release` and wire the ±5% CI regression check; record the cross-thread row as bench-soft (`[2d §10]` Q4 follow-up, not a blocker) (FR-021; I-19; `[const §VIII.1]`/`[const §VIII.2]`).

**Checkpoint**: US6 seams (7/8/18) green; hot-path NFR guarantees independently verifiable.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Tier-1 sanitizer/coverage/static-analysis matrix, mandatory `/speckit-verify`, the feature-completeness audit, and the Gate-B handoff bookkeeping. (Pipeline order: `/implement` → `/simplify` → these → `/speckit-verify` → `/gate-b`; memory `feedback_speckit_simplify_before_verify`.)

- [ ] T057 Run the full Tier-1 sanitizer matrix (`[const §IX.2]`): TSan **mandatory** on seams 2/4/5/10/17/19; ASan+UBSan on race/leak seams 10/14/17; all green. Re-run feature binaries fresh (memory `feedback_coverage_profraw_staleness`).
- [ ] T058 [P] Run the alloc-guard preset (`ctest --preset linux-clang-asan -R 'dispatch_alloc_guard|clock_sleep_alloc_guard'`) and confirm global-heap-only semantics (PMR-arena activity not flagged — N-P2-4); run the libFuzzer cancellation campaign (≥10 min) clean (SC-007).
- [ ] T059 [P] Coverage on touched modules (`[const §IX.1]` ≥95% line / ≥85% branch) judged on the **lcov DA/BRDA basis** not the `llvm-cov report` aggregate (memory `feedback_coverage_gate_lcov_basis`); record `error.hpp` slots 47–55 as **coverage-exempt-by-inspection** (enumerator append → zero instrumentable lines/branches; Article IX §1 recorded-non-assessable-touch rule; plan.md `[const §IX.1]` row).
- [ ] T060 [P] Tier-1 static analysis (`[const §IX.4]`): clang-tidy + clang-format + cppcheck + IWYU clean on all owned `core/`/`session/` headers/sources.
- [ ] T061 Run `/speckit-verify` (mandatory post-`/implement`, `[const §XVII.8]`); produce `.specify/decisions/007-threading-clock-verify.md` (non-RED required `/gate-b` evidence) — verify each polish task actually fires: TSan preset, alloc guards, coverage (lcov basis), static analysis, fuzz, bench vs baselines.
- [ ] T062 Run the feature-completeness audit — tasks ↔ FR-001..022 / SC-001..008 ↔ NFR-015 catalogue row: confirm 100% mapping with NO waiver (2d claims no FIX-TC discharge — `[const §VII.5]`/`[const §VII.6]` N/A-with-reason, `[const §VII.7]` strictly-N/A-voluntary, `[const §IX.5]` N/A; D-5/D-11). Hard `/gate-b` precondition alongside non-RED `/speckit-verify` (memory `feedback_feature_completeness_gate`; `[const §XVII.8]`).
- [ ] T063 Record the residual Gate-B orchestrator bookkeeping (do NOT perform here): at this feature's Gate-B merge the `feature-catalogue.md:227` NFR-015 **Status field** flips `backlog → done` + PR/Tests/Verified linkage — the row text / `coverage-index.md:460` / `architecture.md:602` `[arch §11]` row-7 `DONE` / `architecture.md:395` `session_local` rename are ALREADY present (2d v0.4 sign-off 2026-05-08); this feature does NOT edit those files (research D-12; `[2c App D]` precedent).
- [ ] T064 Run quickstart.md end-to-end (build → test → TSan → ASan+UBSan → alloc-guard → fuzz → bench → coverage) on `linux-clang-debug`/`-release` and confirm every step matches the documented commands.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies; T002 is a hard pre-`/implement` blocker.
- **Foundational (Phase 2)**: depends on Setup; **BLOCKS all user stories**. T006/T007/T008/T013 are independent ([P]); **T010a (factory stubs) precedes T009/T010** (their unique_ptr members need the complete types — D-15); T009 depends on T010a; T010 depends on T007/T008/T010a; T011 standalone; T012 depends on T011; T014 depends on T006–T013.
- **User Stories (Phases 3–8)**: all depend on Foundational completion.
  - US1 (P1) → US2 (P1) → US3 (P1) is the recommended order (US2's `effective_clock` resolution T030 depends on US1's `Session::open` executor wiring T020; US3's slot/close wiring depends on US1/US2).
  - US4 (P2) depends on US1 (T020) + Foundational T007/T011; US5 (P2) depends on US1 (T020) + US2 (T030); US6 (P3) extends US2's `system_clock_source` (T028→T055) and US1's dispatch path.
- **Polish (Phase 9)**: depends on all targeted user stories complete.

### Cross-story implementation dependencies (the few real edges)

- T030 (US2 effective_clock) → after T020 (US1 `Session::open` executor wiring).
- T037/T038/T039 (US3 close) → after T020 (US1) + T030 (US2 clock) + T036 (cancellable_dispatch).
- T044/T045 (US4 trace awaitable + slot wiring) → after T007/T011/T043 + T020/T037.
- T050 (US5 open validation) → after T020/T030.
- T055 (US6 timer-slot pool) → extends T028 (US2 `system_clock_source`).

### Within Each User Story

- Seam tests written and FAILING before implementation (TDD; `[const §VII.3]`).
- Contracts/value types before behavior; behavior before cross-cutting NFR.
- Story complete + its seams green before moving to the next priority.

### Parallel Opportunities

- Setup: T003/T004 [P].
- Foundational: T006/T007/T008/T009/T013 [P] (distinct files, no deps).
- Every story's seam-test tasks are [P] (distinct files); independent impl tasks (T028/T029, T043) are [P].
- After Foundational, US1→US2→US3 should be sequential (real edges above); US4/US5/US6 can overlap once their US1/US2 prerequisites land.

---

## Parallel Example: User Story 1 seam tests

```bash
# Write all US1 seam tests first (they must FAIL before T019–T021):
Task: "Seam 2 strand-serialisation (TSan) in tests/session/test_strand_serialisation.cpp"
Task: "Seam 3 executor-compat 6-combo in tests/session/test_executor_compat.cpp"
Task: "Seam 16 direct_executor reentrancy in tests/session/test_direct_executor_reentrancy.cpp"
Task: "Seam 19 session_executor round-trip in tests/core/test_session_executor_round_trip.cpp"
```

---

## Implementation Strategy

### MVP (User Story 1 — the single locked promise)

1. Phase 1 Setup (incl. the hard T002 executor probe).
2. Phase 2 Foundational (CRITICAL — blocks all stories; ships the contract surface).
3. Phase 3 US1 → seams 2/3/16/19 green incl. TSan.
4. **STOP and VALIDATE**: strand contract independently testable; downstream modules now have a defined execution domain.

### Incremental delivery (all three P1 stories before any P2)

1. Setup + Foundational → contract surface ready.
2. US1 (strand/executor) → MVP.
3. US2 (Clock/NFR-015) → clock seam testable.
4. US3 (two-phase close) → cancellation testable. **All P1 done — feature is downstream-consumable.**
5. US4 (trace context, P2) → observability slice.
6. US5 (config split, P2) → config hardening.
7. US6 (hot-path NFR, P3) → latency/alloc gates.
8. Phase 9 → sanitizer/coverage/verify/audit gate → `/gate-b`.

---

## Notes

- [P] = different files, no incomplete-task dependency. [Story] maps each task to its US for SC-008 traceability.
- 21 named seams ↔ 22 seam-test tasks (T015–T018, T022–T027, T033–T035, T040–T042, T046–T047, T051–T053) bound to explicitly named on-disk files — no globs (plan.md Test-seam→file table; matches D-5 scoping).
- TDD: every seam file FAILS before its implementation task; TSan-mandatory seams (2/4/5/10/17/19) gate the merge.
- `error.hpp` touch is the only edit to a merged file — additive, non-renumbering, slots 47–55 only.
- Commit after each task or logical group; stop at any checkpoint to validate the story independently.
- Avoid: re-litigating signed-off `2d` v0.4 design decisions (round-3 closures are authority, not open questions); editing `architecture.md`/`feature-catalogue.md`/`coverage-index.md` (orchestrator-only at Gate-B merge).
