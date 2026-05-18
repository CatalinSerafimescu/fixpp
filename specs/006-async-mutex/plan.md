# Implementation Plan — 006-async-mutex

**Branch**: `006-async-mutex` | **Date**: 2026-05-18 | **Spec**: [spec.md](spec.md)
**Design anchor**: `.specify/2f-async-mutex.md` **v1.5** (Gate-A-converged through v1.5). On conflict the anchor wins; an inconsistency is a defect in this plan.

> **Authority anchor:** This feature realizes the **signed-off Phase-2 design doc `.specify/2f-async-mutex.md` v1.5** as shipped code. Where this plan and the design doc disagree, **the design doc wins; an inconsistency is a defect in this bundle.** 006 is the **first of three prerequisite features** (`2f-async-mutex [006]` → `2d-threading` → `2e-msgstore`) that the deferred `005-session-establishment-fsm` consumes; 2f is the lowest-level standalone concurrency primitive (zero upstream code dependency beyond the merged 001–004 `core`/ASIO baseline). Catalogue row owned: **NFR-016** (NEW) — Awaitable mutex `fixpp::sync::async_mutex` (design doc §11 / Appendix A; added to `spec/feature-catalogue.md` + `spec/coverage-index.md` at sign-off). 005 is a deferred dependent; 2d and 2e are unblocked sequentially by this PR.

## Normative References

Per `[const §VI.5]`. **No `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies.** The design doc records (Appendix B, mirroring `architecture.md` Appendix B and `[2d Appendix B] §B.2`) that 2f's primary drivers are engineering judgment, not a FIX specification section. The awaitable-mutex primitive is not described in any FIX session-layer, FIXT, or FIXS spec section. This is recorded explicitly per `[const §VI.5]`'s exact-citation rule.

Governing sources:

- `[const §XI.3]` — awaitable mutex required in coroutine context; `fixpp::sync::async_mutex` is the only allowed mutex shape; **direct mandate** for this feature.
- `[const §XV.9]` — plain `std::mutex` banned in any header that includes `asio::awaitable<...>`; the CI enforcement mechanism (`tools/check_no_std_mutex_in_awaitable_headers.sh`) is 2f's deliverable.
- `[const §XI.5]` — hot-path lock policy; the store-write path always uses mutex regardless of `SessionConfig::lock_policy`; binds 2f to zero-hot-path-alloc discipline on the contended path.
- `[const §XI.6]` — HALO-first frame allocation; the awaiter is HALO-eligible by construction (≤ 96 B per `[2f §1.1]`); PMR fallback via the explicit `mr` parameter.
- `[const §VIII.5]` — allocator policy; zero global `new`/`delete` on the v1.0 hot path.
- `[const §XIV.2]` — pluggable interfaces; `async_mutex` is **NOT a plugin** (zero pure-virtual; the ≤ 5 pure-virtual rule does not bind — see Constitution Check).
- `[SYN §3.2 Q6a]` — cancellation propagation (DECIDED — ASIO native cancellation slots end-to-end; honour `cancellation_type::total`).
- `[SYN §3.2 Q6b]` — awaitable mutex (DECIDED — own implementation, BSL-1.0 algorithm attribution to avast/asio-mutex / cppcoro / Lewis-Baker); the six-item design list is the operating spec.
- `[arch §3]` / `[arch §4.1]` / `[arch §2.3]` — `fixpp::sync` lives physically under `core/`; class header `include/fixpp/core/sync/async_mutex.hpp`; session-side helper lives downstream in `session/`.
- `[2d §7.4]` — locked executor-compat contract surface 2f must satisfy.
- `[2e §3.1]` / `[2e §6.4]` — 2e's `MemoryStore` writer-mutex contract; **2f sign-off is the named hard hand-off gate for 2e**.
- `[2a §4.2]` — `trap_throw` pattern; `[2b §6.4]` — flyweight lifetime contract; per-doc-prefix discipline → `FIXPP_ERR_SYNC_*`.

## Summary

This feature delivers the **`fixpp::sync::async_mutex` value type** and associated surface, realizing `.specify/2f-async-mutex.md` v1.5 as shipped code:

- **`fixpp::sync::async_mutex`** — the awaitable mutex class (`include/fixpp/core/sync/async_mutex.hpp`), with the six-item design list from `[SYN §3.2 Q6b]`: (1) waiter embedded in the awaiter object (HALO-eligible, ≤ 96 B); (2) explicit-`mr` PMR fallback overload; (3) ASIO `cancellation_type::total` CAS-arbitration contract; (4) per-mutex `dispatch`/`post` completion policy; (5) `std::terminate()` destructor precondition + `cancel_and_drain()` drain primitive; (6) full test seam coverage (29 seams, `[2f §9]`).
- **`fixpp::sync::async_lock_guard`** — RAII guard with `[[clang::lifetimebound]]` and destructive move-assign (RC#1 / N-P1-3 close; `[2f §4.4]`).
- **`fixpp::sync::detail::async_mutex_awaiter`** — per-waiter three-state `waiter_phase` atomic machine (`{ queued, granted, cancelled }`, RC-A v1.1; `[2f §4.2]`).
- **`fixpp::sync::detail::drain_latch_state`** — lazy-constructed event-state object allocated as `std::shared_ptr<detail::drain_latch_state>` inside `cancel_and_drain`'s coroutine frame (RC-β v1.3; `[2f §4.7.2/§4.7.3]`).
- **`fixpp::sync::detail::slot_allocator`** — three-case storage allocator (RC-C v1.1; `[2f §4.3.4]`): 32-byte inline buffer / null-resource / PMR.
- **`fixpp::sync::completion_policy`** — per-mutex `dispatch`/`post` enum (`[2f §4.1]`).
- **Declaration** of `fixpp::session::async_lock_via_session_executor` (`include/fixpp/session/async_lock_via_session_executor.hpp`) — declared by 2f per `[2f §4.3.2]`, **implemented by the later session-module spec** (RC#2 layering boundary; `[arch §2.3]`).
- **Additive edit** to `include/fixpp/core/error.hpp` — four `sync_*` error variants appended at the first free slots 43–46 (planned, non-renumbering per `[const §X.4]`; `[2f §6.5]`).
- **`tools/check_no_std_mutex_in_awaitable_headers.sh`** — the `[const §XV.9]` grep-gate CI enforcement tool (post-preprocessing scope, `[2f §6.6]`); corpus of labelled fixture headers under `tests/sync/fixtures/`.
- **`tests/sync/`** — 29 named test files (§9 seams, see Project Structure).
- **`bench/sync/`** — 2 named benchmark files + baselines (`[2f §6.3]` Tier-1 ceilings).

**Not shipped here:** C ABI (`async_mutex` is C++ only; delegated to 2i per `[2f §5]`); `async_lock_via_session_executor` implementation (session-module spec owns it); consuming sites (seqnum counter = 005; store writer mutex = 2e; pinset rotation = 2g); cross-doc 2d Appendix D drop-ins (§D.1/§D.2/§D.3 requested by 2f; applied when 2d ships — 2f does NOT edit 2d here, per D-12).

**Consumed-not-built upstream:** none — 2f is the lowest-level primitive; 2d and 2e are downstream of 2f.

**C++-only, no C-ABI surface.**

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), `std::atomic`, `std::pmr`, `std::expected` (via `core::expected_t`), `std::shared_ptr`, `std::coroutine_handle<>`, `[[clang::lifetimebound]]`, `[[nodiscard]]`. No fallback to earlier standards.

**Primary Dependencies:** ASIO (coroutines, `asio::awaitable`, native cancellation slots, `asio::experimental::concurrent_channel`, `asio::bind_allocator`), `fixpp::core` (`expected_t`, `error`, `detail::trap_throw`). GoogleTest 1.17.0 + GoogleMock, Google Benchmark 1.9.5 — pinned via Conan from Phase 3 CI. **No new Conan row** (`[const §III.2]`).

**Project Type:** C++23 library, `core/sync/` sub-module. The mutex is largely header-inline (the compiler needs visibility into `await_ready`/`await_suspend` for HALO analysis per `[2f §6.4]`). `cancel_and_drain()` coroutine body may go out-of-line if the body exceeds reasonable inline budget (decision deferred to `/implement`). The session-side helper declaration is a thin header-only file. No build-only tool, no SWIG, no C-ABI in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release `-O2`):** per `[2f §6.3]` Tier-1 ceilings. CI fails on >5% regression vs `bench/baselines/` (`[const §VIII.2]`):

| Operation | Workload | Ceiling |
|---|---|---|
| `async_lock` uncontended | single CAS fast path; v1.3 RC-α acquirer/holder counters included `[2f §4.2.1]` | **≤ 20–25 ns** |
| `async_lock` contended (enqueue) | LIFO push + cancellation slot bind; awaiter ≤ 96 B HALO-embedded | ≤ 80 ns |
| `unlock` uncontended | empty LIFO; exchange + close-out CAS; v1.3 holder-counter decrement | **≤ 15 ns** |
| `unlock` contended drain (same-strand dispatch, per waiter handoff) | drain-side cost only; excludes resumed coroutine's own work `[2f §6.3 footer]` | ≤ 30 ns + ≤ 50 ns/waiter (bench-harness-soft) |
| `cancel_and_drain()` per N waiters | full drain epoch; lazy `drain_latch_state` alloc; RC-α stable-loop | ≤ 120 ns + ≤ 80 ns/waiter (bench-harness-soft) |

Bench harnesses in `bench/sync/` enforce these via Google Benchmark (`[const §VIII.1]`); ±5% (`[const §VIII.2]`). Drain-cost rows are bench-harness-soft (per `[2e §6.6]` precedent for long-tail rows).

**Constraints:**

- Zero global `new`/`delete` on the v1.0 hot path (`[const §VIII.5]` / `[const §XI.5]`): waiter embedded in caller's coroutine frame (HALO-eligible, ≤ 96 B per `[2f §1.1]`); cancellation slot handler closure storage via the awaiter's 32-byte inline `slot_storage_` buffer (`detail::slot_allocator` three-case table per `[2f §4.3.4]`). `tools/check_alloc.py` under `mallocnesia` verifies (seams #7, #8, #10).
- All public methods `noexcept`; PMR throw routes through `fixpp::core::detail::trap_throw` per `[2a §4.2]`.
- ASIO native cancellation slots end-to-end; **no parallel `stop_token`** (`[const §XI.2]`); `cancellation_type::total` triggers the per-waiter phase CAS-arbitration (`[2f §4.5]`).
- `std::terminate()` hard precondition on `~async_mutex()` if waiters present or mutex held; callers MUST drain before destruction (`[2f §4.7]` RC#3 fix).
- No `std::mutex` in any header that includes `asio::awaitable<...>` (`[const §XV.9]`); enforced by `tools/check_no_std_mutex_in_awaitable_headers.sh` with post-preprocessing scope.
- Memory-ordering static_asserts for lock-freedom on `state_`, `next_drain_head_` atomics; ARM64 weak-memory correctness verified by TSan (`[2f §6.2.2]`, seam #18).
- `async_mutex` is `constexpr`-default-constructible and executor-free per `[arch §5.5]`; the `drain_latch_state` is constructed lazily inside `cancel_and_drain`'s coroutine frame (RC-β v1.3 — the v1.2 by-value `asio::steady_timer` member was non-implementable per Opus C-R3-P2-1).
- **RC#2 layering boundary** — `core::async_mutex` does NOT reach into `session/` or an engine handle for memory. The explicit `async_lock(mr)` overload is the sole PMR fallback path; the session-side helper lives in `session/`, implemented by the session-module spec.

**Scale/Scope:** 1 public class header (`async_mutex.hpp`), 1 session-side declaration header, 4 `sync_*` error slots (43–46), 1 CI tool script + labelled fixture corpus, 29 test files, 2 bench files + baselines. Estimate ~1800–2400 LOC (header-dominant; test-heavy for 29 seams). Closes NFR-016 (NEW).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.* Canonical citation form `[const §Roman.arabic]` per `constitution.md:5`. **Mood:** at this `/speckit-plan`-stage the rows assert *planned conformance*; delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`, not here. The citation-verification pass at the end of this file was actually run against `.specify/constitution.md`.

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §I.1]`, `[const §I.3]`, `[const §I.4]` | Identity/mission; catalogue tracker; no silent omission | Owns **NFR-016** (NEW — Awaitable mutex). Drop-in language for `feature-catalogue.md` + `coverage-index.md` is in `[2f §11]`; the orchestrator applies the amendment at sign-off. Session-side helper implementation boundary explicitly recorded (RC#2 / D-10). |
| `[const §II.1]` | C++23, no fallback | Coroutines, `std::atomic`, `std::pmr`, `std::expected`, `[[clang::lifetimebound]]`, `[[nodiscard]]`; no fallback. |
| `[const §III.2]` | Conan, pinned deps | **No new Conan row.** Reuses ASIO + GTest/Benchmark already pinned. |
| `[const §V.1]`, `[const §V.4]` | AGPL-3.0 dual; vendored attribution | Every new header carries `SPDX-License-Identifier: AGPL-3.0-or-later`. BSL-1.0 algorithm attribution to avast/asio-mutex / cppcoro / Lewis-Baker at the file level per `[const §V.4]`. |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional traceability + Normative References | spec + plan carry the Authority-anchor blockquote + a `## Normative References` section with exact `[const §...]`/`[arch §...]`/`[SYN §...]`/`[2X §...]` entries. `coverage-index.md` entry added at sign-off. **No `[FIX-SL]`/`[FIXT]`/`[FIXS]` reference applies** — explicitly recorded per `[2f Appendix B]` and this plan's Normative References section. |
| `[const §VII.1]`, `[const §VII.3]` | GoogleTest + TDD | `tasks.md` ordered red-green-refactor; every C++ test target is GoogleTest; 29 named seams cover every behavioural contract from `[2f §9]`. |
| `[const §VII.5]` | Conformance corpus (TC-001..TC-017 every PR) | **N/A-with-reason — no `[FIX-TC]` scope applies to this feature.** 2f is an engineering-judgment-driven concurrency primitive; Appendix B of the design doc explicitly records that no `[FIX-SL]`/`[FIXT]`/`[FIXS]` section bears on the awaitable-mutex algorithm. There is no FIX session-layer test case for a mutex implementation. **This is NOT a waiver of `[const §VII.5]`**: the article mandates the conformance corpus for features that have FIX-TC scope; 2f has none. The absence of a `[FIX-TC]` case is a structural fact, not a deferred obligation. Complexity Tracking is EMPTY precisely because there is no obligation being waived (contrast with 005, which had in-scope TC cases and required an explicit `[const §XVII.1]` Gate-A-blocker waiver for the deferred ones). |
| `[const §VII.7]` | Fuzzing on parser-touching modules | **N/A — 2f is not parser-touching.** `async_mutex` is a concurrency primitive that does not parse, frame, or decode any byte stream. Recorded for explicit non-applicability (research D-11). |
| `[const §VIII.1]`, `[const §VIII.2]`, `[const §VIII.5]` | Benchmark + ±5% + zero hot-path alloc | `bench/sync/bench_async_mutex_uncontended.cpp` + `bench_async_mutex_contended.cpp` + baselines enforce Technical-Context ceilings. Zero global `new`/`delete` on the v1.0 hot path verified by `tools/check_alloc.py` + `mallocnesia` (seams #7, #8, #10). |
| `[const §IX.1]` | ≥95% line / ≥85% branch on touched modules | Planned: `linux-clang-coverage` measures `include/fixpp/core/sync/async_mutex.hpp` (and `src/core/sync/async_mutex.cpp` if out-of-line) + `include/fixpp/session/async_lock_via_session_executor.hpp` as the Tier-1 gate; threshold asserted/enforced at `/speckit-verify` per the lcov DA/BRDA basis (memory `feedback_coverage_gate_lcov_basis`). Header-inline coverage under HALO is judged on zero-hit DA/not-taken BRDA, not the `llvm-cov report` aggregate. |
| `[const §IX.2]` | Tier-1 sanitizers | ASan + UBSan on every sync test. **TSan mandatory** — this is a threading/concurrency feature; every seam tests atomics, cancellation races, or cross-strand behaviour. Seam #18 runs on Linux-ARM64 under TSan to catch ARM64-specific weak-memory bugs. |
| `[const §IX.4]` | Tier-1 static analysis | clang-tidy + clang-format + cppcheck + IWYU on all `core/sync/` and `session/` headers/sources. The `[const §XV.9]` grep gate (`tools/check_no_std_mutex_in_awaitable_headers.sh`) is a first-class Tier-1 CI step verified by seam #14. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A — no C-ABI surface.** `async_mutex` is C++ only per `[2f §5]`; zero `extern "C"` symbols introduced. Recorded for explicit non-applicability (research D-11). |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3. Tier 2: Windows manual/nightly. |
| `[const §X.2]`, `[const §X.4]` | No C++ leakage through C ABI; forwards-compat error codes | No C-ABI symbol or type introduced (`[2f §5]`). Four new `sync_*` `core::error` variants appended at unused slots **43–46**, non-renumbering (planned, not yet published; pinned at Gate A / `/tasks` per data-model; `[const §X.4]`). C-ABI coalescing target `FIXPP_ERR_SYNC_*` documented for 2i per per-doc-prefix discipline. |
| `[const §XI.1]`, `[const §XI.2]`, `[const §XI.3]`, `[const §XI.5]`, `[const §XI.6]` | Coroutines; ASIO cancellation; async_mutex; hot-path lock policy; HALO | Feature IS the awaitable mutex (XI.3 direct mandate); coroutine-composed (`asio::awaitable<T>`, XI.1); ASIO native slots, no `stop_token` (XI.2); store-write callsite cap always mutex per XI.5; awaiter HALO-eligible ≤ 96 B (XI.6). |
| `[const §XI.7]` | Threading-affecting features trigger all four mandatory controls | **Gate A mandatory — Threading/Concurrency + Error semantics triggers** per Appendix A. `/clarify` done (0 questions — design doc signed-off/Gate-A-converged); `/analyze` post-`/tasks`; Codex Gate A before `/tasks`; user `/plan` sign-off. |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | **NOT a plugin — zero pure-virtual; the ≤ 5 cap is trivially satisfied (0/5) and the cap does not bind.** Per `[2f §2]` and `[arch §6]`, `async_mutex` is a closed, value-typed, non-virtual class. Recorded explicitly once here per `[2f §3]` "records this once" rule. |
| `[const §XV.9]` | Banned: `std::mutex` in coroutine context | 2f IS the deliverable enforcement mechanism: `tools/check_no_std_mutex_in_awaitable_headers.sh` (grep gate, post-preprocessing scope) is the `[const §XV.9]` CI gate. `async_mutex` itself contains no `std::mutex`. |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` (threading + error semantics) | `/speckit-clarify` run; **0 questions** — the design doc is signed-off and Gate-A-converged through v1.5; all decisions are fixed in `[2f §4]`/`[2f §6]`/`[SYN §3.2 Q6a/Q6b]`. No residual ambiguity. |
| `[const §XVI.4]` | `/analyze` mandatory post-`/plan` | Canonical order: `/plan` → Gate A → `/tasks` → `/analyze` → `/implement`. `/speckit-analyze` runs **after Gate A converges and after `/speckit-tasks`**, before `/speckit-implement`. |
| `[const §XVII.1]`, `[const §XVII.2]`, `[const §XVII.3]`, `[const §XVII.7]`, `[const §XVII.8]` | Gate A mandatory; Gate B; author≠reviewer; local build gate; /speckit-verify mandatory | `gate_a_required: yes` — **Concurrency/Threading + Error semantics** per Appendix A. Phase-2 design doc is signed-off/Gate-A-converged, but the **Phase-4 bundle Gate A is its own review of record.** Both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`. Gate B mandatory pre-merge; author≠reviewer; local build gate precondition; `/speckit-verify` mandatory post-`/implement`. |

**Gates — PASS. Complexity Tracking is EMPTY.** No constitution violation requiring justification. `[const §VII.5]` is **N/A-with-reason** (no `[FIX-TC]` scope — structural non-applicability, NOT a waiver, NOT a deferred obligation). `[const §XIV.2]` trivially satisfied (0 pure-virtual; not-a-plugin). `[const §VII.7]` N/A (not parser-touching). `[const §IX.5]` N/A (no C-ABI). No new pluggable interface. No Article XVII §1 recorded Gate-A-blocker waiver needed.

## Project Structure

### Documentation (this feature)

```text
specs/006-async-mutex/
├── plan.md              # this file (/speckit-plan 2026-05-18)
├── spec.md              # /specify 2026-05-18 (anchored to .specify/2f-async-mutex.md v1.5)
├── research.md          # Phase 0 — design decisions D-1..D-12
├── data-model.md        # Phase 1 — entities E1..E7, memory-ordering invariants, error slot allocation
├── quickstart.md        # Phase 1 — build / test / bench / sanitizer(TSan) / coverage / verify / gate-a / gate-b
├── contracts/
│   ├── async_mutex.hpp                         # shape oracle (NOT the build header)
│   ├── async_lock_guard.hpp                    # shape oracle
│   ├── async_mutex_awaiter.hpp                 # shape oracle (detail::)
│   ├── drain_latch_state.hpp                   # shape oracle (detail::)
│   ├── completion_policy.hpp                   # shape oracle
│   ├── async_lock_via_session_executor.hpp     # declaration-only oracle, session/ namespace
│   └── sync_errors.hpp                         # 4 sync_* variants + FIXPP_ERR_SYNC_* mapping
├── checklists/
│   └── requirements.md  # /specify quality checklist
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (library submodule root)

```text
include/fixpp/core/sync/
└── async_mutex.hpp          # OWNS: fixpp::sync::async_mutex + async_lock_guard +
                             #   detail::async_mutex_awaiter + detail::slot_allocator +
                             #   detail::drain_latch_state (forward-decl) +
                             #   completion_policy enum; BSL-1.0 algorithm attribution;
                             #   static_asserts; SPDX-License-Identifier: AGPL-3.0-or-later

include/fixpp/session/
└── async_lock_via_session_executor.hpp   # DECLARES: fixpp::session::async_lock_via_session_executor
                                          #   (session/ namespace; RC#2 layering — impl by session-module spec)
                                          #   SPDX-License-Identifier: AGPL-3.0-or-later

include/fixpp/core/
└── error.hpp                # ADDITIVE EDIT: append 4 sync_* variants at unused slots 43–46
                             #   (non-renumbering, [const §X.4]; D-7)

tools/
└── check_no_std_mutex_in_awaitable_headers.sh   # OWNED/FINALIZED HERE ([const §XV.9] grep gate;
                                                 #   post-preprocessing scope per [2f §6.6] / Codex C-P2-10)

tests/sync/
├── CMakeLists.txt
├── fixtures/
│   ├── header_with_std_mutex_and_awaitable.hpp  # deliberate-violation fixture (seam #14)
│   └── header_without_violation.hpp             # non-violating fixture (seam #14)
├── test_uncontended_latency.cpp                 # seam #1
├── test_contended_latency.cpp                   # seam #2
├── test_fifo_fairness.cpp                       # seam #3
├── test_cancellation_mid_wait.cpp               # seam #4
├── test_destructor_release_death.cpp            # seam #5
├── test_contention_stress.cpp                   # seam #6
├── test_tsan_clean.cpp                          # seam #7
├── test_asan_clean.cpp                          # seam #8
├── test_halo_firing.cpp                         # seam #9
├── test_pmr_fallback.cpp                        # seam #10
├── test_executor_compat.cpp                     # seam #11
├── test_dispatch_vs_post.cpp                    # seam #12
├── test_cross_strand_acquire.cpp                # seam #13
├── test_no_std_mutex_ci_gate.cpp               # seam #14
├── test_race_cancel_pre_drain.cpp               # seam #15
├── test_race_multi_cancel.cpp                   # seam #16
├── test_race_cancel_during_resume.cpp           # seam #17
├── test_arm64_weak_memory.cpp                   # seam #18
├── test_cancel_and_drain.cpp                    # seam #19
├── test_guard_destructive_move.cpp              # seam #20
├── test_slot_allocator_storage.cpp              # seam #21
├── test_residual_cancel_graceful.cpp            # seam #22
├── test_cancel_and_drain_concurrent.cpp         # seam #23
├── test_drain_latch_holder_lifecycle.cpp        # seam #24
├── test_in_flight_acquirer_coverage.cpp         # seam #25
├── test_drain_awaitable_cancellation.cpp        # seam #26
├── test_unlock_reaper_splice.cpp                # seam #27
├── test_result_write_race.cpp                   # seam #28
└── test_drain_reaper_abort_subscribers.cpp      # seam #29

bench/sync/
├── bench_async_mutex_uncontended.cpp   # seams #1/#2 ceilings (uncontended ≤ 20–25 ns, unlock ≤ 15 ns)
└── bench_async_mutex_contended.cpp     # seam #2 ceilings (contended ≤ 80 ns; drain rows bench-harness-soft)

bench/baselines/sync/
└── async_mutex_baselines.json          # ±5% baseline JSON ([const §VIII.2])
```

**Structure decision:** `async_mutex` is header-dominant — the compiler needs inline visibility of `await_ready`/`await_suspend` for HALO analysis (`[2f §6.4]`). `cancel_and_drain()` coroutine body may go out-of-line to `src/core/sync/async_mutex.cpp` if needed (decision deferred to `/implement`). The session-side helper declaration is a thin header-only file. No out-of-line `.cpp` is architecturally required — it is a compile-time tradeoff.

### Test-seam → file mapping (every seam bound to explicitly named on-disk files)

| Seam | File | Design-doc link |
|---|---|---|
| #1 Uncontended-acquire latency Tier 1 | `tests/sync/test_uncontended_latency.cpp` + `bench/sync/bench_async_mutex_uncontended.cpp` | `[2f §9 seam #1]` / `[2f §6.3 row 1]` |
| #2 Contended-enqueue latency Tier 1 | `tests/sync/test_contended_latency.cpp` + `bench/sync/bench_async_mutex_contended.cpp` | `[2f §9 seam #2]` / `[2f §6.3 rows 2–5]` |
| #3 FIFO fairness across drain cycles | `tests/sync/test_fifo_fairness.cpp` | `[2f §9 seam #3]` / `[2f §4.5.2]` |
| #4 Cancellation mid-wait | `tests/sync/test_cancellation_mid_wait.cpp` | `[2f §9 seam #4]` / `[2f §4.5]` |
| #5 Destructor-with-waiters fires `std::terminate()` | `tests/sync/test_destructor_release_death.cpp` | `[2f §9 seam #5]` / `[2f §4.7]` |
| #6 Contention stress (≥ 10⁴ coroutines) | `tests/sync/test_contention_stress.cpp` | `[2f §9 seam #6]` / `[2f §1.1]` |
| #7 TSan clean under stress | `tests/sync/test_tsan_clean.cpp` | `[2f §9 seam #7]` / `[const §IX.2]` |
| #8 ASan/UBSan clean under stress | `tests/sync/test_asan_clean.cpp` | `[2f §9 seam #8]` / `[const §IX.2]` |
| #9 HALO firing across compiler matrix | `tests/sync/test_halo_firing.cpp` | `[2f §9 seam #9]` / `[2f §6.4]` / `[arch §11 row 2]` |
| #10 PMR fallback exercise | `tests/sync/test_pmr_fallback.cpp` | `[2f §9 seam #10]` / `[2f §4.3]` |
| #11 Executor-compat: completion on bound executor | `tests/sync/test_executor_compat.cpp` | `[2f §9 seam #11]` / `[2d §7.4]` |
| #12 Dispatch vs post policy | `tests/sync/test_dispatch_vs_post.cpp` | `[2f §9 seam #12]` / `[2f §4.6]` |
| #13 Cross-strand acquire (FIFO-fair drain) | `tests/sync/test_cross_strand_acquire.cpp` | `[2f §9 seam #13]` / `[2f §6.1.3]` |
| #14 `std::mutex`-in-coroutine-context CI gate | `tests/sync/test_no_std_mutex_ci_gate.cpp` + `tests/sync/fixtures/` | `[2f §9 seam #14]` / `[const §XV.9]` / `[2f §6.6]` |
| #15 Cancel-after-detach-pre-drain race | `tests/sync/test_race_cancel_pre_drain.cpp` | `[2f §9 seam #15]` / RC#1 / `[2f §4.5]` |
| #16 Multi-cancel-same-list race | `tests/sync/test_race_multi_cancel.cpp` | `[2f §9 seam #16]` / RC#1 |
| #17 Cancel-during-await_resume race | `tests/sync/test_race_cancel_during_resume.cpp` | `[2f §9 seam #17]` / RC#1 |
| #18 ARM64 weak-memory contention stress | `tests/sync/test_arm64_weak_memory.cpp` | `[2f §9 seam #18]` / RC#5 / `[2f §6.2.2]` |
| #19 `cancel_and_drain()` reaps every in-flight waiter | `tests/sync/test_cancel_and_drain.cpp` | `[2f §9 seam #19]` / RC#3 / `[2f §4.7]` |
| #20 `async_lock_guard` destructive move-assign | `tests/sync/test_guard_destructive_move.cpp` | `[2f §9 seam #20]` / RC#1 / N-P1-3 |
| #21 Slot-allocator storage cases (3 cases) | `tests/sync/test_slot_allocator_storage.cpp` | `[2f §9 seam #21]` / RC-C / `[2f §4.3.4]` |
| #22 Residual-chain cancellation under graceful close | `tests/sync/test_residual_cancel_graceful.cpp` | `[2f §9 seam #22]` / RC-A |
| #23 Concurrent `cancel_and_drain` is serialised | `tests/sync/test_cancel_and_drain_concurrent.cpp` | `[2f §9 seam #23]` / RC-B |
| #24 Drain-latch lazy state + pre-drain holder lifecycle | `tests/sync/test_drain_latch_holder_lifecycle.cpp` | `[2f §9 seam #24]` / RC-β |
| #25 In-flight acquirer coverage | `tests/sync/test_in_flight_acquirer_coverage.cpp` | `[2f §9 seam #25]` / RC-α / Opus C-R3-P1-2 |
| #26 `cancel_and_drain` awaitable cancellation propagation | `tests/sync/test_drain_awaitable_cancellation.cpp` | `[2f §9 seam #26]` / RC-β |
| #27 Unlock-vs-reaper splice race closure | `tests/sync/test_unlock_reaper_splice.cpp` | `[2f §9 seam #27]` / RC-α / Opus C-R3-P1-3 |
| #28 `*result_` CAS-then-publish arbitration | `tests/sync/test_result_write_race.cpp` | `[2f §9 seam #28]` / v1.4 |
| #29 Reaper cancellation wakes subscribers | `tests/sync/test_drain_reaper_abort_subscribers.cpp` | `[2f §9 seam #29]` / v1.4 |

## Complexity Tracking

**Empty.** No constitution violation requiring justification.

- **Not a plugin per `[const §XIV.2]`** — zero pure-virtual; closed value-typed class. Cap trivially satisfied (0/5). No new pluggable interface introduced.
- **`[const §VII.5]` is N/A-with-reason, NOT a waiver.** There is no `[FIX-TC]` conformance case for a concurrency primitive. 2f's design is engineering-judgment-driven per `[2f Appendix B]`. No FIX session-layer test case applies. This is a structural non-applicability, not a missing obligation being deferred (contrast with 005 which required an explicit `[const §XVII.1]` Gate-A-blocker waiver for deferred TC cases).
- **`[const §VII.7]` N/A** — not parser-touching.
- **`[const §IX.5]` N/A** — no C-ABI surface.

## Gate A

```
gate_a_required: yes
```

**Triggers** (per `[const §XVII.1]` / Appendix A):
1. **Threading / concurrency** — new awaitable, atomic state machine, lock policy, cancellation CAS-arbitration, strand discipline.
2. **Error semantics** — 4 new `sync_*` `core::error` variants (`sync_lock_aborted`, `sync_lock_alloc_failed`, `sync_lock_outside_session`, `sync_lock_drained`).

The Phase-2 design doc `.specify/2f-async-mutex.md` v1.5 is signed-off and Gate-A-converged. **The Phase-4 bundle Gate A is a distinct review of record** — it reviews this Phase-4 bundle (plan.md, research.md, data-model.md, contracts/, spec.md) for internal consistency, completeness, and faithfulness to the design doc. It does not re-litigate Phase-2 design decisions.

**Execution:** both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`; Opus triages/rewrites; reviews → `research/reviews/`. Gate A runs **before `/speckit-tasks`**; blockers resolved/waived before tasks. Then `/speckit-analyze` (drift detection), then `/speckit-tasks`, then `/speckit-implement`.

## Citation verification pass

Constitution articles cited above verified against `.specify/constitution.md` v0.1:

- `[const §VII.5]` — `constitution.md:91` — "Every PR must pass them in CI" — confirmed. N/A-with-reason is correct: no `[FIX-TC]` scope applies to an awaitable-mutex primitive.
- `[const §VII.7]` — `constitution.md:93` — "Fuzzing (parser-touching modules)" — confirmed; 2f is not parser-touching.
- `[const §IX.5]` — `constitution.md:126` — "ABI check (from the first tagged C ABI release onward)" — confirmed; N/A because no C-ABI surface.
- `[const §XIV.2]` — `constitution.md:199` — "≤5 pure-virtual on pluggable interfaces" — confirmed; 2f has zero pure-virtual (not-a-plugin).
- `[const §XV.9]` — `constitution.md:217` — "`std::mutex` in coroutine context" — confirmed; 2f IS the enforcement mechanism.
- `[const §XVII.1]` — `constitution.md:247-255` — Gate A triggers include "Touches concurrency / threading / cancellation / executor model" — confirmed. Appendix A also lists "Error semantics" as a trigger.
- `[const §XI.7]` — `constitution.md:152` — "Threading/concurrency-affecting features trigger all four mandatory controls" — confirmed.
- `[const §X.4]` — `constitution.md:138` — "once a numeric value is published in a tagged C ABI release, it never changes meaning" — confirmed; `sync_*` slots are planned/pre-publication.
- **Error.hpp occupancy verified on THIS branch (006-async-mutex):** slots 1, 10–13, 20–29, 30–42 occupied; **first free = 43**. The four `sync_*` variants are planned at slots **43, 44, 45, 46**.
- **Design-doc error variant count verified:** `[2f §6.5]` lists exactly 4 variants: `sync_lock_aborted`, `sync_lock_alloc_failed`, `sync_lock_outside_session`, `sync_lock_drained` (4th added RC-B v1.1).
- **Appendix D 2d edits:** `[2f §3.1]` requests §D.1/§D.2/§D.3 edits to 2d. These are NOT applied here; applied when 2d ships (research D-12).
- **`[2e §6.6]` envelope check:** `[2f §6.3 row 1]` uncontended ceiling ≤ 20–25 ns; `[2e §6.6]` `MemoryStore::store` 200 ns budget; headroom ≥ 175 ns — confirmed compatible.

## Phase-2 input checklist

- [x] FR/SC from spec.md (US1–US4, SC rows) mapped to design-doc §4/§6/§9
- [x] All 29 test seams (per `[2f §9]`) bound to explicitly named on-disk files — no globs
- [x] Error slots 43–46 planned; non-renumbering; verified against `error.hpp` on THIS branch
- [x] Performance ceilings fixed to `[2f §6.3]` Tier-1 rows (v1.3/v1.4 updated values)
- [x] Fuzz N/A recorded — not parser-touching (research D-11)
- [x] abidiff N/A recorded — no C-ABI (research D-11)
- [x] `[const §VII.5]` N/A-with-reason recorded — no `[FIX-TC]` scope (NOT a waiver)
- [x] Gate A pending-before-`/tasks`; both Codex passes mandated
- [x] `/analyze` post-`/tasks`
- [x] RC#2 layering boundary crisp: session-side helper declared here, implemented by session-module spec
- [x] Appendix D 2d edits recorded as D-12 (applied when 2d ships — 2f does not edit 2d here)
- [x] NFR-016 drop-in language from `[2f §11]` — applied at sign-off by the orchestrator
