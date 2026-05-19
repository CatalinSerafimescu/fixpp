# Implementation Plan — 007-threading-clock

**Branch**: `007-threading-clock` | **Date**: 2026-05-19 | **Spec**: [spec.md](spec.md)
**Design anchor**: `.specify/2d-threading.md` **v0.4** (Gate-A-converged, round 3, post-cap line-edit pass), **including the cross-doc amendments already recorded in its Appendix C at `2f-async-mutex.md` v1.5 sign-off and `2e-msgstore.md` v0.4 sign-off** (notably the engine-internal `Session::session_arena()` accessor per `[2d §4.5]` / `[2f Appendix D §D.1]`). On conflict the anchor wins; an inconsistency is a defect in this plan.

> **Authority anchor:** This feature realizes the **signed-off Phase-2 design doc `.specify/2d-threading.md` v0.4** as shipped code. Where this plan and the design doc disagree, **the design doc (as amended by the Appendix C cross-doc entries) wins; an inconsistency is a defect in this bundle.** 007 is the **second of three prerequisite features** (`2f-async-mutex [006, merged]` → `2d-threading [007]` → `2e-msgstore`) that the deferred `005-session-establishment-fsm` consumes. `006` is merged to `main`; it was implemented against the *recorded, not re-litigated* `[2d §4.8]` `session_executor` / `[2d §7.4]` executor-compat contract and shipped a **declaration-only** `include/fixpp/session/async_lock_via_session_executor.hpp` — this feature ships the real `session_executor` / `cancellable_dispatch` / `Session::session_arena()` backing those surfaces. Catalogue row owned: **NFR-015** (NEW) — Pluggable Clock interface (design doc §11 / Appendix A; added to `spec/feature-catalogue.md` + `spec/coverage-index.md`, plus the `[arch §11]` row-7 disposition flip and the `[arch §5.4]` / 2k-`clock_scope`-schema drop-ins, **by the orchestrator at sign-off** — not by this feature, per the `[2c App D]` precedent). 005 is a deferred dependent; 2e is unblocked by this PR.

## Normative References

Per `[const §VI.5]`. **No `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies** to the threading contract itself — the design doc records (Appendix B, mirroring `architecture.md` Appendix B) that 2d's drivers are engineering judgment and the locked `[SYN §3.2]` synthesis decisions, not a FIX specification section. The FIX SendingTime/heartbeat *consumers* of the clock seam are owned by the deferred session-module spec (`005`); 2d delivers only the seam. Recorded explicitly per `[const §VI.5]`'s exact-citation rule.

Governing sources:

- `[const §XI.1]`/`[const §XI.2]`/`[const §XI.4]`/`[const §XI.6]`/`[const §XI.7]` — coroutines; ASIO native cancellation slots (no `stop_token`); **per-session-strand application threading default (XI.4 — direct mandate)**; HALO-first frame allocation; threading-affecting features trigger all four mandatory controls.
- `[const §XI.3]`/`[const §XI.5]` — awaitable mutex required / hot-path lock policy (consumed: the merged `006` `async_mutex` is the executor-compat surface 2d locks at `[2d §7.4]`).
- `[const §VIII.5]` — zero allocation between parse and `fromApp`; `[const §X.4]` — forwards-compatible C-ABI error codes; `[const §XIV.2]` — ≤5 pure-virtual on pluggable interfaces (**Clock is a plugin: exactly 4/5**).
- `[const §XIII]`/`[const §XIII.3]` — observability; strand-stored trace context; `thread_local` propagation prohibited.
- `[const §XV.15]` — `drop-oldest` banned on the application/session message path (telemetry/tap exception under `[const §XIII.2]`).
- `[SYN §3.2 Q6a]` (cancellation — ASIO native, DECIDED), `[SYN §3.2 Q6b]` (awaitable-mutex executor-compat surface 2d locks, DECIDED — owned by 2f/`006`), `[SYN §3.2 Q6c]` (application threading contract — option 3, default per-session strand, DECIDED).
- `[arch §1.1]` pluggable-clocks promise (**NFR-015 discharges the clock seam only**); `[arch §4.1]`/`[arch §4.4]` core+session surface & Threading default (locked); `[arch §5.1]`/`[arch §5.3]`/`[arch §5.4]`/`[arch §5.6]` executor model / error model / trace context / frozen config; `[arch §6]` plugin pattern; `[arch §10]` row 2d; `[arch §11]` row 7 (NFR-015 disposition); `[arch §2.3]` leaf rule (`core/` cannot back-edge into `session/`).
- Sibling docs (consumed, not modified): `[2b §6.6]`/`[2b §8]` parser-on-strand + three-arena PMR; `[2c §4.8]`/`[2c §4.9]`/`[2c §6.3]`/`[2c §6.7]` `dict::reify` + `version_registry` + dict-layer error routing (003 merged); `[2f §4.1.1]`/`[2f §4.3.2]`/`[2f §7.4]`/`[2f §6.5]` async_mutex executor-compat (merged as `006`).

## Summary

This feature delivers the **application threading contract and the `fixpp::core::Clock` plugin seam**, realizing `.specify/2d-threading.md` v0.4 as shipped code:

- **`fixpp::core::Clock`** — abstract plugin interface, **exactly 4 pure-virtual** (`now`, `steady_now`, `sleep_until`, `cancel_sleeps`); `include/fixpp/core/clock.hpp` (`[2d §4.1]`).
- **`fixpp::core::system_clock_source`** — default impl over `std::chrono::system_clock` + `std::chrono::steady_clock` + ASIO `steady_timer`, with a per-session reusable `steady_timer` slot pool keyed by `Session*` from `session_arena` (zero-per-cycle heap; `[2d §4.2]` / §6.6 / root cause #5).
- **`fixpp::core::mock_clock`** — deterministic test impl, pimpl'd over an opaque mutable-state object (satisfies `[const §XI.3]`), public test header `<fixpp/core/test/mock_clock.hpp>`; `advance(delta)` (`[2d §4.3]`).
- **`fixpp::core::EngineConfig`** — value-typed engine-level shared resources (executor, `clock`, `dictionaries`→`version_registry`, PMR defaults, observability providers, default plugin factories, atomic `engine_trace_context` snapshot); `clock_not_set` hard invariant at `Engine::open` (`[2d §4.4]`).
- **`fixpp::session::SessionConfig`** — value-typed frozen-at-open knobs as nullable overrides; `threading_mode` / `lock_policy` / closed-2-value `backpressure_mode` enums; engine-anchor + session-override pattern across executor/clock/dictionary axes (`[2d §4.5]`).
- **`fixpp::core::session_executor`** — project-owned value-typed ASIO-executor-concept wrapper holding the resolved executor (strand-wrapped under `per_session_strand`, bare under `direct_executor`) + a typed `Session*`; public `session_ptr()` member accessor that survives `bind_executor`/`make_strand` (round 3 root cause #1; `[2d §4.8]`).
- **`fixpp::core::session_local<T>` + `fixpp::current_trace_context`** — `Session`-owned domain-local slot and the free awaitable resolving it (in-domain via `session_ptr()`, engine-fallback atomic snapshot otherwise); `thread_local` forbidden (`[2d §4.6]` / `[const §XIII.3]`).
- **`fixpp::core::cancellable_dispatch`** — project-owned reaping-aware dispatch primitive returning `asio::awaitable<expected_t<void>>` with the deterministic three-case contract; node allocated from session PMR (`[2d §6.5]`).
- **`fixpp::session::Session` (minimal real skeleton)** — shipped in `fixpp::session`, exposing **only** the 2d-owned surface: two-phase `close(close_mode)`, `session_arena()` accessor (engine-internal, `[2d §4.5]` / `[2f App D §D.1]`), the `session_local<trace_context>` member, executor→`session_executor` binding, callback-dispatch hooks. **No FIX FSM logic** (Clarifications 2026-05-19; `005` extends this type).
- **`fixpp::otel::trace_context`** — minimal 32-byte value type (trace_id 16 B + span_id 8 B + flags 1 B + pad) defined here because `2k` (full OTel surface) is not built; `2k` extends, does not redefine (research D-1).
- **Additive edit** to `include/fixpp/core/error.hpp` — **9** new variants appended at the first free slots **47–55** (non-renumbering per `[const §X.4]`; `[2d §6.7]`).
- **`tests/`** — 21 named test seams (`[2d §9]`, by name not ordinal); **`bench/threading/`** — latency regression bench + baselines (`[2d §6.3]`).

**Not shipped here:** the full session FSM — Logon/gap-fill/ResendRequest/sequence-reset, concrete `heartbeat_interval`/`test_request_threshold`/`sending_time_threshold`/`close_timeout` values (deferred `005`/session-module spec; `std::optional` placeholder fields only); C-ABI symbol shapes & cancellation-token representation (`2i`, `[2d §10]` Q2 — 2d provides the C++ error variants + documents the `FIXPP_ERR_THREAD_*`/`FIXPP_ERR_CANCELLED` coalescing only); the full FIX-TC conformance corpus `tests/conformance/` (TC-001..017, `005`); `2k` log/OTel record schema (2d supplies only the `effective_clock` source + `clock_scope` producer-side commitment); cross-doc Appendix D drop-ins (`[arch §5.4]` storage rename, §11 NFR-015 catalogue/coverage-index/`[arch §11]`-row-7/2k-`clock_scope`-schema) — applied by the orchestrator at sign-off, NOT by this feature (research D-12).

**Consumed-not-built upstream:** `006`/`2f` `async_mutex` (merged) — 2d ships the real `session_executor` backing the already-merged declaration-only `async_lock_via_session_executor.hpp`; `003`/`2c` `dict::version_registry` (merged) consumed via the `[2c §4.9]` API; `001`/`002`/`004` `core`/`wire` baseline (merged).

**C++-only, no C-ABI surface added here** (the C-ABI cancellation/trace shapes are `2i`'s; 2d provides the C++ error variants + the documented coalescing groups).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), ASIO native cancellation slots/states, `std::pmr`, `std::atomic` (incl. `std::atomic<trace_context>` snapshot or `seqlock` fallback if not lock-free — `[2d §4.4]`), `std::expected` (via `core::expected_t`), `std::shared_ptr`, `std::optional`, `std::chrono`, `[[nodiscard]]`, `[[clang::enum_extensibility(closed)]]` where supported. No fallback to earlier standards.

**Primary Dependencies:** ASIO (`asio::awaitable`, `asio::strand`, `asio::make_strand`, `asio::bind_executor`, `asio::any_io_executor`, `asio::steady_timer`, `asio::cancellation_state`/`cancellation_slot`/`cancellation_type`, `asio::this_coro`, `asio::dispatch`), `fixpp::core` (`expected_t`, `error`, `detail::trap_throw`), `fixpp::dict` (`version_registry`, `Dictionary` — 003 merged), `fixpp::sync` (`async_mutex` executor-compat surface — 006 merged). GoogleTest 1.17.0 + GoogleMock, Google Benchmark 1.9.5. **No new Conan row** — `asio/1.36.0` already pinned (added by 006, 2026-05-18); GTest/Benchmark unchanged (`[const §III.2]`).

**Project Type:** C++23 library spanning `core/` (Clock, configs, executor wrapper, session-local, trace-context awaitable, cancellable_dispatch) and `session/` (`SessionConfig`, the minimal `Session` skeleton). Header-dominant where HALO visibility matters (the dispatch hot path, `cancellable_dispatch`, `session_local` access); `mock_clock` is pimpl'd (out-of-line `src/core/test/mock_clock.cpp`); `system_clock_source` and the `Session` skeleton may go out-of-line (`src/core/system_clock_source.cpp`, `src/session/session.cpp`) — compile-time tradeoff, decision deferred to `/implement`. No SWIG, no C-ABI in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release):** per `[2d §6.3]` Tier-1 ceilings. CI fails on >5% regression vs `bench/baselines/threading/` (`[const §VIII.2]`):

| Operation | Workload | Ceiling |
|---|---|---|
| Strand `dispatch` handoff (in-strand, HALO-fired) | parser-completion → `fromApp`, single message | **≤ 25 ns** |
| Strand `dispatch` handoff (cross-thread) | same, executor off-strand thread; OS-scheduler-dependent (bench-soft; §10 Q4 follow-up) | ≤ 250 ns |
| Cross-strand `reify` + dispatch (20-tag) | `fromApp` posts reified `owning_message_t<>` to a foreign executor | ≤ 1.25 µs |
| Cross-strand `reify` + dispatch (200-tag) | large-message workload | ≤ 10.25 µs |
| `Clock::now()` (default impl) | single call | ≤ 25 ns |
| `Clock::steady_now()` (default impl) | single call | ≤ 20 ns |
| Session-domain `trace_context` access | `co_await current_trace_context`, in-domain, slot populated | ≤ 15 ns |
| Engine-fallback `trace_context` access | `co_await current_trace_context`, outside session domain | ≤ 25 ns |

Bench harnesses in `bench/threading/` enforce these via Google Benchmark (`[const §VIII.1]`); ±5% (`[const §VIII.2]`). The cross-thread dispatch row is intentionally generous (OS-jitter-dependent) and bench-soft; `[2d §10]` Q4 is an explicit 2d-implementation bench-spike follow-up (may tighten below 250 ns), not a blocker.

**Constraints:**

- Zero global `new`/`delete` between parse and `fromApp` (`[const §VIII.5]`): HALO-elided awaiter frame; per-awaiter PMR override on `SessionConfig::message_arena` when HALO does not fire; `cancellable_dispatch` node from the session PMR resource; `system_clock_source` per-session `steady_timer` slot allocated **once** per session from `session_arena`, **keyed by `Session*`**, reused every heartbeat cycle (root cause #5 / N-P1-1). `tools/check_alloc.py` under `mallocnesia` verifies global-heap only (PMR-arena allocations expected, not flagged — N-P2-4); Linux/Clang Tier-1 (same caveat as `[2a §9]` seam #6 / `[2b §9]` seam #10).
- No exception across the parse → `fromApp` window (`[arch §5.3]`); cancellation is **not** an exception — `asio::error::operation_aborted` / `expected_t` `dispatch_aborted`; PMR throw routes through `fixpp::core::detail::trap_throw` (`[2a §4.2]`).
- ASIO native cancellation slots end-to-end; **no parallel `stop_token`** (`[const §XI.2]`). Two-phase close: phase-1 Logout under a **child** `asio::cancellation_state` composed below the root (NOT pre-cancelled by the eventual root total); phase-2 fires `cancellation_type::total` on the root (`[2d §4.7]`/§6.5). `partial` excluded from the v1.0 surface (N-P1-3).
- `Clock::now()` is **not** monotonic; `steady_now()` is the only elapsed/heartbeat/SendingTime-delta/S-035 source; `now()` only for wire-formatted + log/OTel timestamps (`[2d §6.6]`, C-P2-5).
- `session_executor` is a project-owned class satisfying `asio::execution::is_executor` that **survives `bind_executor`/`make_strand`** decoration on engine-controlled paths; the typed `Session*` is recovered via the `session_ptr()` **member function**, NOT `asio::any_io_executor::query` (round 3 root cause #1 — the closed `any_io_executor` property set does not forward user-defined queries; the round-2 typed-property formulation was regression-equivalent to the rejected `query(void*)` and is documented as known-bad by seam 21's negative assertion).
- `thread_local` forbidden for trace context (`[const §XIII.3]`); the `session_local<trace_context>` slot is reached through the stable `Session*`, safe across coroutine resume on a different thread.
- `direct_executor` is a **contract, not a delegation**: requires `already_serialized_executor == true` (else `error::executor_not_serialised`); `direct_executor + lock_policy::spin` rejected with `error::invalid_session_config` even when attested; the engine introduces no internal `async_mutex`/atomic regime for `direct_executor` (`[2d §4.5]`/§6.1, root cause #1).
- `drop_oldest` is **unrepresentable** on the app/session path — `backpressure_mode` is a closed 2-value enum + `static_assert` at every switch + a runtime out-of-range-cast reject (`[const §XV.15]` / `[2d §6.4]`).
- **`[arch §2.3]` leaf rule** — `core/` does NOT back-edge into `session/`. `Session::session_arena()` is engine-internal (callable from `fixpp::session/` only); `core::cancellable_dispatch`/`session_local` take the project types by value/pointer, never reach into `session/`.

**Scale/Scope:** ~9 owned `core/` headers + 2 `session/` headers + 1 minimal `Session` skeleton + a 32-byte `otel::trace_context` value + 9 `error.hpp` slots (47–55) + 21 test files + 1 bench dir + baselines. Magnitude domain per `[2d §1.2]` (≤ ~6 in-flight slots/session; ~24–32 B strand/session; O(2×sessions) `cancel_sleeps` walk at 10⁴-session shutdown). Estimate ~2600–3400 LOC (header-dominant; test-heavy for 21 seams incl. TSan/ASan/fuzz). Closes **NFR-015** (NEW).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.* Canonical citation form `[const §Roman.arabic]`. **Mood:** at `/speckit-plan`-stage these rows assert *planned conformance*; delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`. The citation-verification pass at the end of this file was run against `.specify/constitution.md`.

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §I.1]`, `[const §I.3]`, `[const §I.4]` | Identity/mission; catalogue tracker; no silent omission | Owns **NFR-015** (NEW — Pluggable Clock interface). Drop-in language for `feature-catalogue.md` + `coverage-index.md` + `[arch §11]` row-7 flip + `[arch §5.4]`/2k-`clock_scope` is in `[2d §11]`/Appendix D; the **orchestrator** applies it at sign-off (research D-12). Session-FSM boundary explicitly recorded (Clarifications 2026-05-19 / D-4). |
| `[const §II.1]` | C++23, no fallback | Coroutines, ASIO cancellation states, `std::pmr`, `std::atomic`, `std::expected`, `std::chrono`, `[[nodiscard]]`; no fallback. |
| `[const §III.2]` | Conan, pinned deps | **No new Conan row** — `asio/1.36.0` already pinned (added by 006, 2026-05-18); GTest 1.17.0 / Benchmark 1.9.5 unchanged. Verified against `conanfile.py` on this branch. |
| `[const §V.1]`, `[const §V.4]` | AGPL-3.0; vendored attribution | Every new header carries `SPDX-License-Identifier: AGPL-3.0-or-later`. **No vendored algorithm** — `system_clock_source` wraps std/asio; `mock_clock` is original; no BSL-1.0 attribution needed (contrast 006). |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional traceability + Normative References | spec+plan carry the Authority-anchor blockquote + `## Normative References` with exact `[const §...]`/`[arch §...]`/`[SYN §...]`/`[2X §...]` entries. `coverage-index.md` entry added at sign-off. **No `[FIX-SL]`/`[FIXT]`/`[FIXS]` applies** — recorded per `[2d Appendix B]`. |
| `[const §VII.1]`, `[const §VII.3]`, `[const §VII.4]` | GoogleTest + TDD + no untested code | `tasks.md` ordered red-green-refactor; every C++ test target GoogleTest; 21 named seams cover every behavioural contract from `[2d §9]`. |
| `[const §VII.5]` | Conformance corpus (TC-001..TC-017 every PR) | **N/A-with-reason — no `[FIX-TC]` scope applies to 2d.** The FIX Session Layer Test Cases require the session FSM, which is the deferred `005`; 2d delivers only the threading/clock seam (`[2d Appendix B]`; C-P2-6 "clock seam only"). 2d ships its own **2d-scoped deterministic clock-injection corpus** (seam 11, relocated out of `tests/conformance/` per Clarifications 2026-05-19) as the in-feature analogue. Structural non-applicability — **NOT a waiver, NOT a deferred obligation**; 2d claims no FIX-TC discharge so the feature-completeness audit passes without a waiver (contrast `005`). Recorded D-5/D-11. |
| `[const §VII.6]` | Interop test (QuickFIX) | **N/A — no FIX FSM in 2d.** Interop Logon→…→Logout needs the session FSM (`005`). 2d's executor-compat seam (3) drives the *sequence shape* through a scripted test-double FSM to prove strand/clock/cancellation behaviour, not FIX interop. Recorded D-5. |
| `[const §VII.7]` | Fuzzing (parser-touching modules) | **Strictly N/A — 2d is not parser-touching** — but 2d **voluntarily ships** the libFuzzer cancellation-timing harness (seam 12, `tests/fuzz/fuzz_session_cancellation.cpp`) per `[2d §9 seam 12]` Gate-A discretion (`[const §IX.4]` extended to threading-touching code). Recorded D-11. |
| `[const §VIII.1]`, `[const §VIII.2]`, `[const §VIII.5]` | Benchmark + ±5% + zero hot-path alloc | `bench/threading/` + `bench/baselines/threading/` enforce the Technical-Context ceilings; cross-thread row bench-soft (§10 Q4). Zero global `new`/`delete` parse→`fromApp` + on the heartbeat path verified by `tools/check_alloc.py` + `mallocnesia` (seams 7, 18). |
| `[const §IX.1]` | ≥95% line / ≥85% branch on touched modules | Planned: `linux-clang-coverage` measures the owned `core/` headers (+ `.cpp` if out-of-line) + `session/session_config.hpp` + the `Session` skeleton as the Tier-1 gate; enforced at `/speckit-verify` on the lcov DA/BRDA basis (memory `feedback_coverage_gate_lcov_basis`); header-inline/`if constexpr`/templated paths judged on zero-hit DA / not-taken BRDA, not the `llvm-cov report` aggregate. |
| `[const §IX.2]` | Tier-1 sanitizers | ASan + UBSan on every test. **TSan mandatory** — 2d is a threading/concurrency feature; strand-serialisation (seam 2), sleep/cancel race (seam 10), session_local lifetime (seam 17), cancellation (seams 4/5), round-trip (seam 19) run under TSan; race/leak seams additionally ASan-clean. |
| `[const §IX.4]` | Tier-1 static analysis | clang-tidy + clang-format + cppcheck + IWYU on all owned `core/` and `session/` headers/sources. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A — no C-ABI surface added.** C-ABI cancellation/trace shapes are `2i`'s (`[2d §5]`/§7.7/§10 Q2); zero `extern "C"` symbols introduced. 9 new `core::error` variants are C++-internal, appended non-renumbering at unused slots 47–55, pre-publication (`[const §X.4]`). Recorded D-11. |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3 (incl. TSan, alloc-guard, fuzz). Tier 2: Windows manual/nightly. |
| `[const §X.2]`, `[const §X.4]` | No C++ leakage through C ABI; forwards-compat error codes | No C-ABI symbol/type introduced (`[2d §5]`). **9** new `core::error` variants appended at unused slots **47–55**, non-renumbering (planned/pre-publication; pinned at Gate A / `/tasks` per data-model; `[const §X.4]`). C-ABI coalescing targets `FIXPP_ERR_THREAD_CONFIG`/`_SESSION_LIFECYCLE`/`_RUNTIME` + reused `FIXPP_ERR_CANCELLED` documented for 2i (per-doc-prefix discipline; final coalescing 2i's call). |
| `[const §XI.1]`–`[const §XI.6]` | Coroutines; ASIO cancellation; awaitable mutex; threading default; lock policy; HALO | Feature **IS** the application threading contract: per-session-strand default (XI.4 direct mandate); coroutine-composed `asio::awaitable<T>` (XI.1); ASIO native two-phase cancellation, no `stop_token` (XI.2); consumes the merged `006` `async_mutex` as the only coroutine mutex (XI.3) and locks its executor-compat surface `[2d §7.4]`; `lock_policy` per-session with `direct_executor+spin` reject (XI.5); dispatch hot path HALO-eligible, PMR fallback (XI.6). |
| `[const §XI.7]` | Threading-affecting → all four mandatory controls | **All four triggered & satisfied:** `/speckit-clarify` done (2 codebase-reality scoping clarifications; 0 design-doc clarifications — design doc signed-off/Gate-A-converged); Codex Gate A before `/tasks` (both passes); `/speckit-analyze` post-`/tasks`; user `/plan` sign-off. Pipeline order = the canonical `[const §XVI.4]` row below. |
| `[const §XII.5]` | Security profile no-implicit-default | `SessionConfig::security_profile` default-constructs to a sentinel rejected at `Session::open` with `error::invalid_session_config` (sentinel value owned by 2g; 2d records only the rejection invariant — N-P2-3). |
| `[const §XIII.3]` | Strand-stored trace context; no `thread_local` | `session_local<trace_context>` slot owned by `Session`; reached via the stable `Session*` from `session_executor::session_ptr()`; `thread_local` forbidden; survives resume on a different thread (seams 6, 17, 21). |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | **Clock IS a plugin: exactly 4 pure-virtual (4/5 — within cap; no justification paragraph required).** `now`/`steady_now`/`sleep_until`/`cancel_sleeps`. `session_executor`/`session_local`/`cancellable_dispatch`/configs are value types, not plugins (0 pure-virtual). |
| `[const §XV.15]` | Banned `drop-oldest` on app/session path | `backpressure_mode` is a closed 2-value enum (`block`, `disconnect_and_recover`); `drop_oldest` unrepresentable; `[[clang::enum_extensibility(closed)]]` + `static_assert` at every switch + runtime out-of-range-cast reject (seam 13). |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` (threading + error semantics) | `/speckit-clarify` run; **2 questions** — both *codebase-reality scoping* (Session-shell scope; FSM-dependent-seam/corpus realization), **0 design-doc clarifications** (signed-off/Gate-A-converged). Recorded in `spec.md` Clarifications Session 2026-05-19. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | **CANONICAL PIPELINE ORDER (single source of truth — all other mentions in this bundle cross-reference this row):** `/plan` → Gate A → `/tasks` → `/analyze` → `/implement`. `/speckit-analyze` runs **after Gate A converges and after `/speckit-tasks`** (the drift check against constitution ↔ spec ↔ plan ↔ **tasks**). |
| `[const §XVII.1]`, `[const §XVII.2]`, `[const §XVII.3]`, `[const §XVII.7]`, `[const §XVII.8]` | Gate A; Gate B; author≠reviewer; local build gate; `/speckit-verify` mandatory | `gate_a_required: yes` — **Public C++ API + Concurrency/Threading/Cancellation/Executor model + Error semantics + new pluggable interface (Clock)** per Appendix A. Phase-2 design doc signed-off/Gate-A-converged, but the **Phase-4 bundle Gate A is its own review of record**. Both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`. Gate B mandatory pre-merge; author≠reviewer; local build gate precondition; `/speckit-verify` mandatory post-`/implement`. |

**Gates — PASS. Complexity Tracking is EMPTY.** No constitution violation requiring justification. `[const §XIV.2]` satisfied **with a real plugin** (Clock 4/5 — within cap, no justification paragraph required). `[const §VII.5]` / `[const §VII.6]` **N/A-with-reason** (no `[FIX-TC]`/interop scope — structural; FIX-TC corpus is `005`'s; NOT a waiver, NOT a deferred obligation, no completeness-audit waiver needed). `[const §VII.7]` strictly N/A (not parser-touching) but a cancellation fuzz harness is shipped voluntarily per design-doc Gate-A discretion. `[const §IX.5]` N/A (no C-ABI surface). No Article XVII §1 recorded Gate-A-blocker waiver needed.

## Project Structure

### Documentation (this feature)

```text
specs/007-threading-clock/
├── plan.md              # this file (/speckit-plan 2026-05-19)
├── spec.md              # /specify 2026-05-19 (anchored to .specify/2d-threading.md v0.4) + /clarify 2026-05-19
├── research.md          # Phase 0 — design decisions D-1..D-14
├── data-model.md        # Phase 1 — entities E1..E12, invariants, error slots 47–55
├── quickstart.md        # Phase 1 — build / test / TSan(mandatory) / ASan+UBSan / alloc-guard / fuzz / bench / coverage / verify / gate-a / gate-b
├── contracts/
│   ├── clock.hpp                          # shape oracle — fixpp::core::Clock (4 pure-virtual) + time aliases
│   ├── system_clock_source.hpp            # shape oracle — default impl
│   ├── mock_clock.hpp                     # shape oracle — pimpl'd test impl (public test header)
│   ├── engine_config.hpp                  # shape oracle — EngineConfig
│   ├── session_config.hpp                 # shape oracle — SessionConfig + 3 enums (closed backpressure)
│   ├── session_executor.hpp               # shape oracle — project wrapper class + session_ptr()
│   ├── session_local.hpp                  # shape oracle — session_local<T>
│   ├── trace_context.hpp                  # shape oracle — current_trace_context awaitable + otel::trace_context POD
│   ├── cancellable_dispatch.hpp           # shape oracle — primitive signature + 3-case contract
│   ├── session.hpp                        # shape oracle — minimal Session skeleton (2d-owned surface only)
│   └── threading_errors.hpp               # 9 variants (slots 47–55) + FIXPP_ERR_THREAD_* mapping
├── checklists/
│   └── requirements.md  # /specify quality checklist
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (library submodule root)

```text
include/fixpp/core/
├── clock.hpp                  # OWNS: fixpp::core::Clock (4 pure-virtual) + utc_time_point/steady_time_point aliases
├── system_clock_source.hpp    # OWNS: fixpp::core::system_clock_source (default impl) + per-Session* timer slot pool
├── engine_config.hpp          # OWNS: fixpp::core::EngineConfig + clock_not_set invariant surface
├── session_executor.hpp       # OWNS: fixpp::core::session_executor wrapper class + session_ptr() accessor
├── session_local.hpp          # OWNS: fixpp::core::session_local<T>
├── trace_context.hpp          # OWNS: fixpp::current_trace_context awaitable + fixpp::otel::trace_context POD (2k extends)
├── cancellable_dispatch.hpp   # OWNS: fixpp::core::cancellable_dispatch (reaping-aware, expected_t<void>)
├── error.hpp                  # ADDITIVE EDIT: append 9 variants at unused slots 47–55 (non-renumbering, [const §X.4]; D-7)
└── test/
    └── mock_clock.hpp         # OWNS: fixpp::core::mock_clock (pimpl'd; public test header; deterministic advance())

include/fixpp/session/
├── session_config.hpp         # OWNS: fixpp::session::SessionConfig + threading_mode/lock_policy/backpressure_mode enums
└── session.hpp                # OWNS: minimal fixpp::session::Session skeleton (2d-owned surface; NO FIX FSM; 005 extends)

src/core/
├── system_clock_source.cpp    # out-of-line impl if inline budget exceeded (decision deferred to /implement)
└── test/mock_clock.cpp        # pimpl impl over opaque mutable-state object ([const §XI.3])

src/session/
└── session.cpp                # out-of-line Session-skeleton impl if needed (decision deferred to /implement)

tests/core/      tests/session/      tests/alloc_guard/      tests/fuzz/      bench/threading/
  (21 seam files + bench — see Test-seam → file mapping)

bench/baselines/threading/
└── threading_baselines.json   # ±5% baseline JSON ([const §VIII.2])
```

**Structure decision:** spans `core/` (timing/executor/trace primitives) and `session/` (`SessionConfig` + the minimal `Session` skeleton) per `[arch §4.1]`/`[arch §4.4]`; the `[arch §2.3]` leaf rule is preserved (`core/` never includes `session/`; `Session::session_arena()` is engine-internal, callable only from `fixpp::session/`). Header-dominant where the compiler needs HALO visibility (dispatch hot path, `cancellable_dispatch`, `session_local` access); `mock_clock` is necessarily pimpl'd (`[const §XI.3]` — no `std::mutex` in an `asio::awaitable`-including header; opaque mutable-state object). `system_clock_source` / `Session` skeleton out-of-line decision deferred to `/implement` (compile-time tradeoff, not architectural). The 2d-scoped clock-injection corpus (seam 11) is relocated from the design-doc's nominal `tests/conformance/test_corpus_mock_clock.cpp` to `tests/session/test_clock_injection_corpus.cpp` because `tests/conformance/` is reserved for the FIX-TC corpus owned by `005` (Clarifications 2026-05-19; D-5). The alloc-guard seams (7, 18) use the repo's established `tests/alloc_guard/` home rather than the design-doc's nominal `tests/perf/` path (consistent with the `2a`/`2b` alloc-guard precedent the design doc itself cites; D-11).

### Test-seam → file mapping (every seam bound to explicitly named on-disk files — no globs)

| Seam (by name) | File | Design-doc link |
|---|---|---|
| `mock_clock` determinism | `tests/core/test_mock_clock_determinism.cpp` | `[2d §9.1]` |
| Strand-serialisation property (TSan) | `tests/session/test_strand_serialisation.cpp` | `[2d §9.2]` / `[2d §6.1]` |
| Executor-opt-out compatibility (6 combos, scripted FSM) | `tests/session/test_executor_compat.cpp` | `[2d §9.3]` / `[2d §6.1]` |
| Cancellation-slot propagation (parse → `fromApp`) | `tests/session/test_cancellation_parse_to_fromapp.cpp` | `[2d §9.4]` / `[2d §6.5]` |
| Cancellation-slot propagation (`fromApp` → close) | `tests/session/test_cancellation_fromapp_to_close.cpp` | `[2d §9.5]` / `[2d §6.5]` |
| `trace_context` resume-on-different-thread | `tests/core/test_trace_context_resume.cpp` | `[2d §9.6]` / `[2d §4.6]` |
| Allocation guard on dispatch hot path (`mallocnesia`) | `tests/alloc_guard/test_dispatch_alloc_guard.cpp` | `[2d §9.7]` / `[2d §6.2]` |
| Latency regression bench | `bench/threading/bench_threading.cpp` + `bench/baselines/threading/threading_baselines.json` | `[2d §9.8]` / `[2d §6.3]` |
| Heartbeat-window simulation under `mock_clock` (scripted FSM) | `tests/session/test_heartbeat_under_mock_clock.cpp` | `[2d §9.9]` |
| `sleep_until` + `cancel_sleeps` race (TSan+ASan) | `tests/core/test_sleep_cancel_race.cpp` | `[2d §9.10]` / `[2d §6.6]` |
| 2d-scoped clock-injection corpus on injected `mock_clock` | `tests/session/test_clock_injection_corpus.cpp` | `[2d §9.11]` / `[2d §7.9]` (relocated from `tests/conformance/`; D-5) |
| Fuzz harness for cancellation timing (libFuzzer) | `tests/fuzz/fuzz_session_cancellation.cpp` | `[2d §9.12]` / `[const §IX.4]` (Gate-A discretion) |
| Drop-oldest banned-on-app-path enforcement (compile + runtime) | `tests/session/test_backpressure_drop_oldest_banned.cpp` | `[2d §9.13]` / `[const §XV.15]` |
| Engine-shutdown ordering | `tests/core/test_engine_shutdown_order.cpp` | `[2d §9.14]` / root cause #5 |
| Third-party `Clock` conformance | `tests/core/test_third_party_clock_conformance.cpp` | `[2d §9.15]` / `[2d §4.1.1]` |
| `direct_executor` re-entrancy guard | `tests/session/test_direct_executor_reentrancy.cpp` | `[2d §9.16]` / root cause #1 |
| `session_local<T>` lifetime-under-cancellation | `tests/core/test_session_local_lifetime.cpp` | `[2d §9.17]` / `[2d §4.6]` |
| Allocation guard on `Clock::sleep_until` path (`mallocnesia`) | `tests/alloc_guard/test_clock_sleep_alloc_guard.cpp` | `[2d §9.18]` / root cause #5 / N-P1-1 |
| `session_executor` round-trip across both modes | `tests/core/test_session_executor_round_trip.cpp` | `[2d §9.19]` / round 2 root cause #1 |
| `version_registry` dictionary-missing routes through `[2c §6.7]` | `tests/session/test_version_registry_missing_routes_to_dict_layer.cpp` | `[2d §9.20]` / Opus N2-P2-1 |
| `session_executor` typed-accessor survives ASIO erasure (compile+runtime+negative) | `tests/core/test_session_executor_accessor_survives_erasure.cpp` | `[2d §9.21]` / round 3 root cause #1 |

## Complexity Tracking

**Empty.** No constitution violation requiring justification.

- **Clock is a real plugin per `[const §XIV.2]` — exactly 4 pure-virtual (4/5), within the cap; no justification paragraph required.** `session_executor`/`session_local`/`cancellable_dispatch`/`EngineConfig`/`SessionConfig`/`Session` are value/closed types (0 pure-virtual).
- **`[const §VII.5]` / `[const §VII.6]` are N/A-with-reason, NOT waivers.** No `[FIX-TC]`/interop conformance case applies to the threading/clock seam; the FIX session-layer test cases require the session FSM (deferred `005`). 2d ships its own 2d-scoped clock-injection corpus (seam 11). Structural non-applicability — 2d claims no FIX-TC discharge, so the feature-completeness audit passes **without** a waiver (contrast `005`, which required an explicit `[const §XVII.1]` Gate-A-blocker waiver for in-scope deferred TC cases).
- **`[const §VII.7]` strictly N/A** (not parser-touching) — a cancellation fuzz harness (seam 12) is shipped **voluntarily** per `[2d §9 seam 12]` Gate-A discretion; not a waiver of an obligation.
- **`[const §IX.5]` N/A** — no C-ABI surface added (delegated to 2i; `[2d §5]`).

## Gate A

```
gate_a_required: yes
```

**Triggers** (per `[const §XVII.1]` / Appendix A):
1. **Public C++ API** — new `core::Clock`/`EngineConfig`/`session_executor`/`session_local`/`cancellable_dispatch` + `session::SessionConfig`/`Session` surface.
2. **Concurrency / threading / cancellation / executor model** — the entire feature (per-session strand, two-phase close, `cancellable_dispatch`, `direct_executor`).
3. **Session FSM / message-store contract adjacency** — `Session::close()` two-phase semantics, `session_arena()` accessor consumed by the merged `006` helper.
4. **New pluggable interface** — `fixpp::core::Clock` (4 pure-virtual).
5. **Error semantics** — 9 new `core::error` variants (slots 47–55).

The Phase-2 design doc `.specify/2d-threading.md` v0.4 is signed-off and Gate-A-converged (rounds 1–3 + post-cap line-edit pass; Appendix C). **The Phase-4 bundle Gate A is a distinct review of record** — it reviews this Phase-4 bundle (plan.md, research.md, data-model.md, contracts/, spec.md) for internal consistency, completeness, and faithfulness to the design doc. It does **not** re-litigate Phase-2 design decisions; the round-3 root-cause closures (wrapper-class accessor; `session_local` keying axis; single-effective-clock; two-phase close) are authority, not open questions.

**Execution:** both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`; Opus triages/rewrites; reviews → `research/reviews/`. Gate A runs **before `/speckit-tasks`**; blockers resolved/waived before tasks. Pipeline order then follows the **single canonical statement** in the Constitution-Check `[const §XVI.4]` row (`/plan` → Gate A → `/tasks` → `/analyze` → `/implement`).

## Citation verification pass

Constitution articles cited above verified against `.specify/constitution.md`:

- `[const §VII.5]` — `constitution.md:91` — "Every PR must pass them in CI" (FIX-TC TC-001..017) — confirmed. N/A-with-reason correct: the FIX session-layer test cases require the FSM (`005`); 2d delivers only the clock seam (`[2d Appendix B]` / C-P2-6). NOT a waiver; no completeness-audit waiver needed (claims no FIX-TC discharge).
- `[const §VII.6]` — `constitution.md:92` — interop QuickFIX Logon→…→Logout — confirmed N/A (needs FSM = `005`).
- `[const §VII.7]` — `constitution.md:93` — "Fuzzing (parser-touching modules)" — confirmed; 2d not parser-touching; fuzz harness shipped voluntarily (seam 12, Gate-A discretion).
- `[const §IX.5]` — `constitution.md:126` — "ABI check (from the first tagged C ABI release onward)" — confirmed N/A (no C-ABI surface; 2i owns it).
- `[const §XI.4]` — `constitution.md:147` — "Application threading default: per-session strand" — confirmed; **direct mandate** — this feature IS the contract.
- `[const §XI.7]` — `constitution.md:152` — "Threading/concurrency-affecting features trigger all four mandatory controls" — confirmed; all four triggered.
- `[const §XIV.2]` — `constitution.md:199` — "≤5 pure-virtual on pluggable interfaces" — confirmed; Clock = exactly 4 pure-virtual (4/5, within cap, no justification paragraph required).
- `[const §XV.15]` — Banned `drop-oldest` on app/session message path — confirmed; `backpressure_mode` closed 2-value enum.
- `[const §XVII.1]` — `constitution.md:247-255` — Gate A triggers include "Touches the public C++ API", "Touches concurrency / threading / cancellation / executor model", "Any new design document" — confirmed; Appendix A also lists "Error semantics".
- `[const §X.4]` — `constitution.md:138` — "once a numeric value is published in a tagged C ABI release, it never changes meaning" — confirmed; slots 47–55 planned/pre-publication.
- **`error.hpp` occupancy verified on THIS branch (007-threading-clock):** slots 1, 10–13, 20–29, 30–42 occupied by 001–004; **43–46 occupied by 006's `sync_*` (merged)**; **first free = 47**. The 9 threading variants are planned at slots **47–55**.
- **Design-doc error variant count verified:** `[2d §6.7]` lists exactly **9** variants: `executor_already_stopped`, `executor_not_serialised`, `clock_sleeps_cancelled`, `strand_dispatch_failed_oom`, `session_already_open`, `session_already_closed`, `invalid_session_config`, `clock_not_set`, `dispatch_aborted`. (Dropped-in-design variants `trace_context_provider_threw`/`cancellation_propagation_timeout`/`version_registry_dictionary_missing` are NOT introduced — confirmed against `[2d §6.7]`.)
- **Appendix D / cross-doc:** `[2d §11]`/Appendix D drop-ins (`[arch §5.4]` storage rename; §11 NFR-015 catalogue+coverage-index; `[arch §11]` row-7 flip; 2k `clock_scope` schema) and the 2f-requested §D.1/§D.2/§D.3 2d edits are realized as the **shipped 2d surface** (already in the design-doc body, e.g. `Session::session_arena()` at `[2d §4.5]`); the catalogue/architecture text amendments are applied by the **orchestrator at sign-off**, NOT by this feature (research D-12).
- **Consumed-not-built verified:** `006` shipped `include/fixpp/session/async_lock_via_session_executor.hpp` (declaration-only) + `include/fixpp/core/sync/async_mutex.hpp` on `main`; this feature provides the real `session_executor`/`cancellable_dispatch`/`Session::session_arena()` backing. `003` `dict::version_registry` present (merged). No `class Session` and no `fixpp::otel::trace_context` exist yet (verified) — both are created here (skeleton / minimal POD).

## Phase-2 input checklist

- [x] FR/SC from spec.md (US1–US6, SC-001..008) mapped to design-doc §4/§6/§7/§9
- [x] All 21 test seams (per `[2d §9]`) bound to explicitly named on-disk files — no globs
- [x] Error slots 47–55 planned; non-renumbering; verified against `error.hpp` on THIS branch (43–46 = merged 006 `sync_*`)
- [x] Performance ceilings fixed to `[2d §6.3]` Tier-1 rows; cross-thread row bench-soft (§10 Q4 follow-up)
- [x] `[const §VII.5]`/`[const §VII.6]` N/A-with-reason recorded — no `[FIX-TC]`/interop scope; FIX-TC corpus = `005`; NO completeness-audit waiver needed
- [x] `[const §VII.7]` strictly-N/A recorded; cancellation fuzz harness shipped voluntarily (seam 12)
- [x] `[const §IX.5]` N/A recorded — no C-ABI (2i owns the C-ABI shapes)
- [x] Clock = real plugin, exactly 4 pure-virtual (`[const §XIV.2]` 4/5, within cap)
- [x] Clarifications 2026-05-19 (Session-shell scope = minimal real skeleton; FSM-dep seams = scripted test-double FSM; seam-11 relocated) carried into Project Structure + D-4/D-5
- [x] `[arch §2.3]` leaf rule preserved (`core/` ⊄ `session/`; `session_arena()` engine-internal)
- [x] Gate A pending-before-`/tasks`; both Codex passes mandated
- [x] Pipeline order per the canonical `[const §XVI.4]` Constitution-Check row (single source of truth): `/plan` → Gate A → `/tasks` → `/analyze` → `/implement`
- [x] Appendix D 2d edits + NFR-015 drop-in language recorded D-12 — applied at sign-off by the orchestrator, not this feature
