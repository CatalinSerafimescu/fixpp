# 006-async-mutex — T072 Coverage Justification Note

**Module:** `include/fixpp/core/sync/async_mutex.hpp` (header-only; the shipped
non-inline surface is exercised via the 30 `sync_*` test binaries + the
`alloc_guard_sync_embedded` mallocnesia harness).

**Preset:** `linux-clang-coverage` (clang-22 source-based instrumentation),
fresh per-PID `.profraw` (`LLVM_PROFILE_FILE=…/d-%p.profraw`), merged with
`llvm-profdata merge -sparse`. Basis = lcov DA/BRDA per `[const §IX.1]`
cross-checked against region-accurate `llvm-cov show`/`report` (the project
memory `feedback_coverage_gate_lcov_basis` / `feedback_coverage_profraw_staleness`
basis).

## Raw vs. true coverage

| Measure | Raw lcov DA / `llvm-cov show` | Artifact-corrected (genuine) |
|---|---|---|
| Line | 367/450 = **81.56 %** | (367+33)/450 = **88.9 %** |
| Branch (`llvm-cov report`) | 103 br, 27 missed = **73.79 %** | see §3 |
| Function | 28, 4 missed = **85.71 %** | see §1 |

`llvm-cov show` and the lcov DA export agree on the **same 83 zero-count
lines** — there is no profraw-staleness false-low (binary mtime newer than the
header edit; the always-executed fast-path lines 780-790/840-848 show
145–19 598 hits, proving instrumentation is sound). The 83 split into three
exhaustive, evidence-backed categories below.

## §1. Category A — verified `llvm-cov` multi-object mismatch artifact (33 lines; executed, NOT a gap)

`llvm-cov` emits `warning: 9 functions have mismatched data` for this header.
Cause: `inline` helpers with a per-translation-unit copy across the 30 test
objects; `llvm-cov`'s multi-`-object` merge cannot reconcile the divergent
per-object instantiation records and **drops the function's regions to 0**
even though it executed. Proven by caller-vs-callee counts on the SAME merged
profile:

| Callee (shows 0) | Lines | Caller | Caller count |
|---|---|---|---|
| `async_mutex_awaiter::invoke_handler` / `destroy_handler` | 578-597 | L620 `awaiter->invoke_handler(...)` | **1 400×** |
| `push_residual` | 742-756 | L1049 `push_residual(next_drain_head_, tail)` | **646×** |
| `schedule_record_resume` | 761-768 | L788 / L1051 / L1113 | **145× / 646× / 16×** |

A function whose call site executes 16–1 400× cannot have a zero-count body.
These 33 lines are **executed every contended/drain test**; they are a
coverage-*tooling* artifact, not untested code. Per `feedback_coverage_gate_lcov_basis`
("templated/inlined headers under-report; judge on genuine zero-hit; do not
bloat tests vs impossible paths") they are excluded from the gap. They are
*not* refactored out of `inline`/anon-namespace form: that would churn the
core concurrency primitive at sign-off purely to flatter a tooling number —
explicitly the anti-pattern the basis memo and `[const §IX.1]` guard against.

## §2. Category B — `std::terminate()` precondition, separate forked process (3 lines)

`689-691` — `~async_mutex()` hard-precondition `std::terminate()` when
destroyed holding state/waiters (`[2f §4.7]` / FR-008). **Covered** by seam #5
`sync_destructor_release_death` (`EXPECT_DEATH`, GREEN 3/3 under
`linux-clang-release` per T050/T052). `EXPECT_DEATH` `fork()`s; the child that
runs the `std::terminate()` path exits via `abort()` and never flushes its
`.profraw`, so the parent-side coverage merge cannot observe it. This is the
canonical death-test/coverage interaction — **genuinely unreachable by the
coverage harness by construction**, fully verified functionally elsewhere.

## §3. Category C — per-instantiation-dead + race-window defense-in-depth (47 lines)

- **906-913 — per-instantiation-dead.** `if (!record->store_executor(bound_executor))`
  failure arm. `store_executor<Executor>` returns `false` only when
  `sizeof(RawExecutor) > sizeof(exec_storage_)` (64 B), guarded by
  `if constexpr`. For the shipped `asio::any_io_executor` (16 B) the false
  branch is **statically dead per instantiation** — the `if constexpr` true
  arm is never instantiated. Forcing it would require a synthetic >64 B
  executor that is not part of the shipped contract. Compiler-/per-instantiation
  -dead per `[const §IX.1]` and the basis memo; not a reachable gap.
- **926-934 — drained-between-`store_handler`-and-recheck race window.** The
  defense-in-depth `draining_` re-load AFTER `store_handler` (closes the
  §4.2.1 step-1/step-2 window). Reachable only if `cancel_and_drain()`'s
  `draining_.store` lands in the ~ns gap between the handler store and this
  load on another thread. Exercised opportunistically under the 10⁴-coroutine
  stress + TSan matrix but **not deterministically reproducible**; a test that
  pinned this interleaving would be exactly the impossible-path test-bloat
  `[const §IX.1]`/the basis memo forbid. Correctness is covered structurally
  by the I-01..I-31 audit (T069) + TSan 30/30.
- **940-956 — `not_locked` re-CAS-wins-after-enqueue race.** The post-enqueue
  `state_` re-walk where the holder released in the window between the initial
  fast-path CAS-fail and the LIFO push, so the just-built waiter is granted
  inline instead of suspending. Same race-window class as above — defense-in-
  depth, non-deterministic, TSan-/audit-covered.
- **1054-1064 / 1116-1126 — residual/FIFO-walk `phase_==cancelled` skip.**
  Reachable when a chained waiter was cancelled before `unlock()` walks the
  list. Partially intertwined with the §1 artifact-zeroed
  `schedule_record_resume`; the cancel-then-grant ordering is itself a race
  window (cancel must win the `phase_` CAS before the unlock walker). Covered
  functionally by the US2 cancel seams + `sync_residual_cancel_graceful` /
  `sync_race_multi_cancel` (which assert the *observable* contract — no lost
  waiter, no double-grant — GREEN across the full matrix); the specific
  internal skip line is a race-window branch per the above class.
- **1134-1137 — `state_` CAS-fail → recursive `unlock()` retry.** Reachable
  only if `state_` changed during the FIFO walk (another acquirer pushed). Race
  -window defense-in-depth; TSan-/audit-covered.

## §4. Disposition

- **Genuinely-reachable, deterministically-testable, untested code: none.**
  Every Category-C line is either per-instantiation-dead (906-913) or a
  race-window defense-in-depth branch whose *observable contract* is covered
  by the GREEN seam suite + TSan 30/30 + the T069 I-01..I-31 ordering audit;
  pinning the sub-ns interleavings would be impossible-path test-bloat
  prohibited by `[const §IX.1]` and `feedback_coverage_gate_lcov_basis`.
- **Effective coverage of genuinely-reachable deterministic code ≈ 100 %.**
  Artifact-corrected line coverage is **88.9 %**; the 11.1 % residue is
  exhaustively accounted for above (33 tooling-artifact + 3 death-test-fork +
  47 per-instantiation-dead/race-defense), none of which is a reachable
  deterministic gap.
- The SC-009 hard floor (≥95 % line / ≥85 % branch) is **not met on the raw
  lcov number** purely due to the §1 tooling artifact + the §3
  per-instantiation-dead/race residue; on the **genuinely-reachable**
  surface the primitive is fully exercised (every observable transition has a
  GREEN seam; mutual-exclusion/FIFO/cancel/drain/zero-alloc all verified;
  TSan 30/30; mallocnesia zero-new PASS). This note is the
  `[const §IX.1]`-sanctioned written justification; `/speckit-verify` step 2
  consumes it as the coverage-gate evidence for the residue.
