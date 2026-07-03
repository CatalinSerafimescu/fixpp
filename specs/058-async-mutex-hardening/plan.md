# Implementation Plan: async_mutex hardening (Cluster-4)

**Branch**: `058-async-mutex-hardening` | **Date**: 2026-07-02 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/058-async-mutex-hardening/spec.md`

## Summary

Close seven Phase-0-verified defects (AM-P1..AM-P3) plus the test-validity gaps (T-1..T-7) in the
coroutine `async_mutex` primitive, hardening it across its full **supported cross-thread envelope**
(latent-in-production only because all shipped consumers are strand-confined). The load-bearing fix
is AM-P1: replace the tagless Treiber free-list head with a **generation-tagged packed head** and an
**atomic free-link**, defeating both the ABA and the plain-`next_` data race — the pool is
index-addressable (512 slots), so the head packs `{generation, slot_index}` into the *existing*
8-byte `waiter_pool_free_` atom (no new `async_mutex` member → the `sizeof==131120` layout golden is
preserved). Ordering (AM-P2-1) tightens to release/acquire with a documented strand-local drain
intent; the destructor guard (AM-P2-2) gains an in-flight/holder check; the exhaustion counter
(AM-P2-3) becomes bounded; AM-P3-1/2/3 become asserts / a disarm / a documented OOM disposition.
Every fix ships a discriminating, mutation-tested witness; the AM-P1 witness is a **deterministic
interleaving harness** (a blind stress test runs green against the nanosecond ABA window — it would
fail the RED-against-current bar).

## Technical Context

**Language/Version**: C++20 (coroutines), Clang 22 local == CI
**Primary Dependencies**: standalone asio 1.36.0 (`asio::awaitable`, executors, cancellation slots); header-only primitive
**Storage**: N/A (in-process synchronization primitive; 512-slot inline waiter pool + PMR fallback)
**Testing**: GoogleTest under `tests/sync/` (~40 files); ctest; sanitizer matrix ASan/UBSan/TSan; lcov coverage lane
**Target Platform**: Linux (Clang/GCC Tier-1), Windows/MSVC (Tier-2, layout-golden skip-and-report); native ARM64 weak-memory HW unavailable in CI
**Project Type**: single C++ library (header-only sync primitive inside the fixpp library submodule)
**Performance Goals**: no regression to the uncontended fast path; the tagged-CAS pop adds one atomic load + a packed-CAS on the *contended* pool path only; strand-confined consumers behaviorally unchanged
**Constraints**: `sizeof(async_mutex)==131120` / `alignof==16` layout golden MUST hold; `sizeof(waiter_record)<=256` static-assert (`:619`) MUST hold; no public-API change; `no-std-mutex` gate green; AM-P1 is TSan-invisible → correctness argued by deterministic interleaving + reasoning, not TSan-green
**Scale/Scope**: one header (`include/fixpp/core/sync/async_mutex.hpp`, 1221 lines) + `tests/sync/` additions/conversions; no consumer call-site changes

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Article XI (Concurrency & Coroutines)** — §3 async_mutex is the only allowed coroutine mutex; all
  fixes stay inside `async_mutex.hpp` and add NO `std::mutex` (Article XV §9). §6 HALO-first / awaiter
  ≤96 B budget: unaffected (no awaiter member change). **§7 — this is a concurrency feature → all four
  mandatory controls: `/clarify` (✓ done), `/analyze` (✓ this step), Codex Gate A (✓ **DONE** —
  dual Fable+Codex CONVERGED, record `.specify/decisions/058-async-mutex-hardening-gatea.md`), user
  `/plan` sign-off (✓ done).** PASS.
- **Article IX (Coverage/Sanitizers/Static analysis)** — §1 ≥95% line/≥85% branch on touched modules;
  this feature commits to the STRICTER 100%-of-reachable-branch bar for `async_mutex.hpp` (SC-004),
  every unreachable branch waived with a written proof in `.specify/decisions/058-async-mutex-hardening-verify.md`.
  §2 ASan/UBSan/TSan every PR — all run; NOTE AM-P1 is TSan-invisible so TSan-green is necessary-not-sufficient
  (FR-013). §4 clang-tidy/format/cppcheck/iwyu clean. PASS.
- **Article XV (Banned patterns)** — §9 no `std::mutex` in coroutine context: preserved (the
  `no-std-mutex` CI gate stays). PASS.
- **Article XVI §3 / Appendix A** — `/clarify` mandatory before `/plan` for threading features: done.
  PASS.
- **Article XVII (Gates)** — Gate A (design) triggered by §1 "touches concurrency/threading/executor
  model" → REQUIRED before `/tasks`. Independence (§3): Fable = finder → CANNOT design/implement;
  Opus/Codex design+implement; dual reviewers both gates. Gate B before merge. Verify gate (§8)
  after `/implement`. PASS (scheduled).

**Layout-golden gate (feature-specific):** `test_async_mutex_layout_golden` (`sizeof==131120`,
`alignof==16`) is a compile-time `static_assert` — it is the hard safety net for the AM-P1 packing
approach. Design keeps all tagging inside the existing 8-byte pool-head atom + any new `waiter_record`
field within the 256-byte slot cap, so the golden holds. If a fix would change it, that is called out
explicitly per FR-011 (none currently planned).

*Initial Constitution Check: PASS. Re-check after Phase 1 design (below).*

## Project Structure

### Documentation (this feature)

```text
specs/058-async-mutex-hardening/
├── plan.md              # This file
├── research.md          # Phase 0 — the AM-P1 free-list redesign + test-seam decision (Gate-A critical)
├── data-model.md        # Phase 1 — field/state deltas (pool head, free-link, guard, counter)
├── quickstart.md        # Phase 1 — how to build/run the async_mutex suite + the deterministic AM-P1 witness
├── contracts/
│   └── async_mutex-contract-delta.md  # drain/destructor doc tightening + OOM disposition
├── checklists/
│   └── requirements.md  # spec quality checklist (from /specify)
└── tasks.md             # Phase 2 — /speckit-tasks (NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/core/sync/
└── async_mutex.hpp      # the ONLY production file touched — all seven fixes land here

tests/sync/
├── test_async_mutex_aba_interleave.cpp   # NEW — deterministic AM-P1 witness (test-seam-driven)
├── test_pool_exhaustion_reuse.cpp        # NEW — AM-P2-3 + free-list reuse (T-3)
├── test_drain_destroy_inflight_mt.cpp    # NEW — AM-P2-1/P2-2 genuinely-MT teardown witnesses
├── test_fifo_across_cycles.cpp           # NEW — T-7 FIFO-across-cycles
├── test_race_cancel_during_resume.cpp    # CONVERT → genuinely MT (T-1)
├── test_race_multi_cancel.cpp            # CONVERT → genuinely MT (T-1)
├── test_race_cancel_pre_drain.cpp        # CONVERT → genuinely MT (T-1)
├── test_result_write_race.cpp            # CONVERT → genuinely MT (T-2)
├── test_cancellation_mid_wait.cpp        # CONVERT → genuinely MT (T-1)
├── test_destructor_release_death.cpp     # EXTEND — the P2-2 cancel-then-destroy shape (T-4)
├── test_async_mutex_layout_golden.cpp    # PRESERVE (load-bearing)
└── CMakeLists.txt                        # register new tests
```

**Structure Decision**: Single header + `tests/sync/` deltas. One feature / one PR — the seven fixes
are interlocking edits to shared state in one file; splitting creates ordering hazards. The US1..US5
priority slices are TASK ordering within this PR, not separate PRs.

## Complexity Tracking

*No Constitution Check violations. The one notable design cost — a test seam in the production header
for the deterministic AM-P1 witness — is justified below and gated to zero prod cost.*

| Decision | Why Needed | Simpler Alternative Rejected Because |
|----------|------------|--------------------------------------|
| Compile-gated **two-phase** test seam in `async_mutex.hpp` for the AM-P1 interleaves, in a **standalone** test target | AM-P1's window is a few instructions + TSan-invisible; a blind stress test runs GREEN against the buggy code → non-discriminating, fails the FR-008/SC-007 RED-against-current bar. Deterministic interleaves need hooks: `pop_pre_cas` (part 1 ABA) and `pop_pre_link_load` (part 2 reuse race). The seam TU MUST be standalone (ODR — header-only inline bodies differ under the macro). | A pure stress harness (non-discriminating). A single seam phase (misses AM-P1 part 2 — Gate-A: Fable). A model-checker (CDSChecker/GenMC) on an extracted model (heavier tooling, separate lane, model-drift) — kept as a Gate-A alternative, not primary. |
| Restructure `waiter_pool_slot` to host a persistent `free_link` atomic (slot metadata) | The free-list link CANNOT be a `waiter_record` member: `release_ref` destroys the record before the push and placement-new reinitializes it → member-atomic lifetime churn = UB (Gate-A BLOCKER, both reviewers). | A `waiter_record` member (the BLOCKER). A parallel `array<atomic<uint32>,512>` async_mutex member (+2048 B → breaks the 131120 golden). The slot-tail approach keeps `sizeof(slot)==256` → golden preserved. |

## Post-Design Constitution Re-Check

*Completed after Phase 1 artifacts + Gate A.* PASS — no new violations; layout golden preserved by
construction (packing in the existing pool-head atom + persistent slot-tail link, `sizeof(slot)==256`);
no public-API or ABI surface touched. Gate A CONVERGED (dual Fable+Codex; the free-link-lifetime
BLOCKER + 4 majors fixed in-bundle) — see `.specify/decisions/058-async-mutex-hardening-gatea.md`. All
four Article XI §7 controls satisfied through `/analyze`.
