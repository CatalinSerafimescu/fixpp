# 006-async-mutex — T074 Completeness Audit

`/gate-b` precondition per `[const §XVII.8]` / `feedback_feature_completeness_gate`.
Maps every FR-001..FR-016 and SC-001..SC-010 to its covering implementation +
seam/evidence. Status: **100% covered** (no waivers). Updated gate-b/r1
2026-05-19: F-1 catalogue closure + F-2 abort-reentrant UAF fix + F-3-residual
seams + F-4 metadata re-anchor to v1.6.

## Functional Requirements

| FR | Covered by | Evidence |
|---|---|---|
| FR-001 value-typed non-virtual non-movable `async_mutex` | T012/T026 impl | seam #20 + `sync_consumer_contract_compile` (T078) static_asserts |
| FR-002 single-CAS fast path + contended path | US1 T026-T031 | seams #1 #2 #6 (`sync_contention_stress` 10⁴, overlap==0) |
| FR-003 success ⇒ `async_lock_guard` | US1 | seam #20 `sync_guard_destructive_move`, #28 result-write |
| FR-004 release grants next waiter | US1 unlock() | seams #3 `sync_fifo_fairness`, #27 unlock-reaper-splice |
| FR-005 `cancellation_type::total` honoured | US2 T033-T040 | seams #4 #15 #16 #17 #22 (cancel mid-wait / multi / vs-drain) |
| FR-006 `cancel_and_drain()` awaitable | US3 T048-T049 | seams #19 #23 #24 #26 #29 |
| FR-007 post-drain fast-fail `sync_lock_drained` | US3 T051 | seam #19/#24 post-drain assertions |
| FR-008 destruct-with-waiters ⇒ `std::terminate()` | US3 T050 | seam #5 `sync_destructor_release_death` GREEN 3/3 release |
| FR-009 embedded path zero global new/delete | US4 T058-T061 | mallocnesia `alloc_guard_sync_embedded` PASS (E-4 steady-state) |
| FR-010 explicit `async_lock(mr)` PMR overload | US4 T059 | seam #10 `sync_pmr_fallback` (all-from-resource + exhaustion) |
| FR-011 declare `async_lock_via_session_executor` | T014 + T078 | `sync_consumer_contract_compile` signature static_assert |
| FR-012 failures via `expected_t` + `sync_*` slots | all US | seams across US1-US3; error slots 43-46 |
| FR-013 publish memory-ordering spec + static_asserts | T069 | I-01..I-31 audit; FR-013 static_asserts compile clang-22+gcc-13; seam #18 |
| FR-014 `[const §XV.9]` CI grep gate | US5 T062-T067 | seam #14 + `check_no_std_mutex_corpus` Tier-1 ctest, zero FN/FP |
| FR-015 out-of-scope surface absent (U1) | T078 negative asserts | `sync_consumer_contract_compile` (no try_lock/alias/engaged-ctor/hook) |
| FR-016 NFR-016 catalogue row closed at sign-off | T075 + gate-b/r1 F-1 fix | `spec/feature-catalogue.md:228` NFR-016 Status=done, /specify=v1.6, PR=#73, Tests=32 seams, Verified=GREEN; `spec/coverage-index.md:462` v1.6 sign-off with E-3/E-4 semantics |

## Success Criteria

| SC | Covered by | Evidence |
|---|---|---|
| SC-001 mutual exclusion 100% stress | seam #6 | `sync_contention_stress` 10⁴ coroutines, overlap==0; matrix 30/30 |
| SC-002 cancellation correctness incl. cancel-vs-drain | US2/US3 seams | #4 #15-17 #22 #26 #29 GREEN incl. TSan |
| SC-003 `cancel_and_drain` completes each waiter once | seams #19 #23 #29 | GREEN debug+TSan |
| SC-004 zero global new/delete embedded | mallocnesia harness | `alloc_guard_sync_embedded` PASS |
| SC-005 latency Tier-1 ±5% (SOFT, `[const §VIII.2]`) | T071 | real benches measured; baseline committed; bench_compare ±5% self-consistent |
| SC-006 grep gate flags `std::mutex`-in-awaitable, no FP | T067 | seam #14 22 assertions + real-corpus ctest zero FN/FP |
| SC-007 ordering static_asserts compile + weak-memory | T069/seam #18 | clang-22+gcc-13 compile; `sync_arm64_weak_memory` GREEN incl. TSan; native ARM64 host-unavailable (x86_64) — recorded |
| SC-008 consumer compile/link check | T078 | `sync_consumer_contract_compile` GREEN across full matrix |
| SC-009 clean under merged sanitizer/static matrix | T070/T073 | TSan 30/30 (mandatory) + ASan + UBSan + gcc-release 30/30; clang-tidy zero correctness defects; coverage residue via `[const §IX.1]` justification note |
| SC-010 NFR-016 catalogue + coverage-index closed | T075 + gate-b/r1 F-1 fix | both closed: catalogue Status=done/v1.6/PR/Tests/Verified populated; coverage-index v1.6 narrative with E-3 always-post semantics (see FR-016) |

## Tasks ↔ phases

All 78 tasks (T001-T078) marked `[X]` in `tasks.md` with evidence; T072 closed
via the user-accepted `[const §IX.1]` written coverage-justification note
(`coverage-justification.md`); ARM64-native execution of seam #18 recorded
host-unavailable (x86_64 build host) per the FR-013/SC-007 fallback clause.

**Result: 100% FR/SC coverage, 0 waivers, 0 gaps.** Gate-B precondition satisfied
(pending `/speckit-verify` GREEN — T076).
