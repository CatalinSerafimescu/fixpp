# Phase 0 — Research & Decisions — 006-async-mutex

**Anchor:** `.specify/2f-async-mutex.md` v1.5 (Gate-A-converged). On conflict the anchor wins. All decisions below distill fixed design choices from the design doc; no design choice is invented here.

---

### D-1 — Algorithm, state encoding, and mutex-owned residual FIFO chain (RC-A v1.1)

**Decision:** Own implementation of the cppcoro / Lewis-Baker lock-free awaitable-mutex algorithm, with BSL-1.0 algorithm attribution to avast/asio-mutex. State encoding in `std::atomic<uintptr_t> state_`:
- `not_locked = 1` (low bit set; distinguishable from any 8-byte-aligned pointer)
- `locked_no_waiters = 0`
- `<pointer-to-awaiter>` — the LIFO head

**RC-A v1.1 fix:** the mutex additionally owns `std::atomic<async_mutex_awaiter*> next_drain_head_` — the residual FIFO chain after an `unlock()` drain grants the first waiter. `unlock()` walks `next_drain_head_` FIRST, then falls back to the LIFO chain anchored at `state_`. The residual list was moved from the awaiter (v1.0 design defect — the granted awaiter "owned" the FIFO but `unlock()` had no way to reach it from `async_mutex*` alone; UAF on holder-cancellation mid-critical-section per Codex C-P1-1/Opus C-P1-2/Opus N-P1-1) to the mutex.

The three-state `waiter_phase` machine (`{ queued, granted, cancelled }`, RC-A v1.1 — collapsed from v1.0's four-state `{ queued, draining, cancelling, completed }`) per awaiter drives CAS-arbitration between `unlock()` drain and the cancellation handler. Both potential writers first win a per-waiter `phase_` CAS from `queued` to their terminal state; the winner alone writes `*result_` (v1.4 CAS-then-publish).

**Rationale:** avast/asio-mutex / cppcoro / Lewis-Baker algorithm is the established reference for ASIO-compatible awaitable mutexes. Moving the residual list to the mutex closes the v1.0 UAF defect class (RC-A). The three-state machine suffices because the drain CAS is atomic with ownership transfer — no intermediate "draining" or "cancelling" phase is needed under the mutex-owned-residual shape.

**Anchor:** `[2f §4.1]` / `[2f §1.2]` / `[2f §4.2]` / `[2f §6.2]` / `[2f Appendix C]` RC-A close.

---

### D-2 — Awaiter shape, the three-state phase machine, and the ≤ 96 B HALO budget

**Decision:** `fixpp::sync::detail::async_mutex_awaiter` is `alignas(8)`, ≤ 96 B total (v1.1 budget per `[2f §1.1]`). Member layout:

| Member | Size | Role |
|---|---|---|
| `async_mutex* mutex_` | 8 B | back-pointer to the owning mutex |
| `async_mutex_awaiter* next_` | 8 B | intrusive link (reused by LIFO chain on `state_` AND by `next_drain_head_` FIFO chain — RC-A) |
| `std::atomic<waiter_phase> phase_` | 4 B (with padding) | 3-state machine; CAS-arbitration point |
| `asio::cancellation_slot slot_` | ≈ 16 B | bound at `await_suspend`; ASIO implementation-defined size |
| `std::coroutine_handle<> coro_` | 8 B | continuation |
| `expected_t<async_lock_guard>* result_` | 8 B | result sink; written AFTER phase_ CAS (v1.4 CAS-then-publish) |
| `std::array<std::byte, 32> slot_storage_` | 32 B | RC-C inline buffer for cancellation handler closure storage |
| Total raw | 84 B; padded ≈ 88 B; ceiling 96 B (8 B ASIO headroom) |

**HALO eligibility:** ≤ 96 B allows the awaiter to embed in the caller's coroutine frame without heap allocation when HALO fires. The `alignas(8)` ensures the low-bit `not_locked` sentinel distinguishes real waiter pointers. The 32-byte `slot_storage_` inline buffer feeds `detail::slot_allocator` so the cancellation handler closure does not touch the global heap (RC-C, `[2f §4.3.4]` case 1).

**`result_` validity and CAS-then-publish (v1.4):** `result_` points into the suspended coroutine's frame slot. It is valid from `await_suspend(h)` entry through `await_resume()` return. **Only the CAS winner writes `*result_`** — the winner first wins `phase_.compare_exchange(queued → terminal, acq_rel)` then writes `*result_` and schedules resumption. CAS losers do not touch `*result_`. `await_resume` acquire-loads `phase_` before reading `*result_`.

**Anchor:** `[2f §1.1]` / `[2f §4.2]` / `[2f §4.2.1]` / `[2f §4.2.2]` / `[2f §4.2.3]` / `[2f §6.4]`.

---

### D-3 — Cancellation CAS-arbitration contract (§4.5, `cancellation_type::total` → `sync_lock_aborted`)

**Decision:** `cancellation_type::total` (and `terminal`, treated as `total`; `partial`, treated as `total` per `[2d §4.7]`) triggers a per-waiter CAS from `phase_ queued → cancelled`. The arbitration race with `unlock()`'s drain CAS (`queued → granted`) is resolved atomically: exactly one wins, the loser observes terminal phase and does not write `*result_`. The winner writes `*result_ = unexpected{sync_lock_aborted}` (on cancel) or `*result_ = engaged-guard` (on grant) and schedules the resumption.

`await_resume` returns `expected_t::unexpected{error::sync_lock_aborted}` when `phase_ == cancelled`. The 2f-boundary outcome joins `[2d §6.7]`'s `dispatch_aborted` and `clock_sleeps_cancelled` in the `FIXPP_ERR_CANCELLED` group at the C ABI per `[2d §4.7]` Appendix D §D.2 (applied when 2d ships — D-12).

The cancellation slot is registered exactly once at `await_suspend` (via `asio::bind_allocator(detail::slot_allocator{this, mr})`) and cleared once in `await_resume`. Cancellation-after-resume is a safe no-op: the CAS observes `granted` or `cancelled` (terminal) and returns without dereferencing freed state.

**Anchor:** `[2f §4.5]` / `[2f §4.5.1]` / `[2f §6.1.4]` / `[SYN §3.2 Q6a]` / `[2d §7.4]`.

---

### D-4 — `cancel_and_drain()` + lazy `std::shared_ptr<drain_latch_state>` shape + `std::terminate()` destructor precondition (RC#3 / RC-β v1.3)

**Decision:** RC#3 fix — the destructor fires `std::terminate()` (both debug and release) if waiters are present or the mutex is held. No release-mode UB, no silent discard. Callers MUST drain before destruction via `cancel_and_drain()`.

`cancel_and_drain()` is an `awaitable<expected_t<void>>` mutex-owned reaper. v1.3 mechanism (post-cap, RC-α + RC-β):
1. Allocates `std::shared_ptr<detail::drain_latch_state>` inside the reaper's coroutine frame via `std::make_shared`; atomic-stores it into `drain_latch_ptr_` with release semantics BEFORE setting `draining_ = true` (v1.4 deterministic publication order).
2. Sets `draining_ = true`; subsequent `async_lock(...)` callers observe this in `await_ready` BEFORE the fast-path CAS and fast-fail with `sync_lock_drained` (RC-B).
3. Atomic-exchanges `state_` and `next_drain_head_`; walks both lists; CAS'es every `queued` waiter to `cancelled` (winner-only writes `*result_`); schedules resumptions.
4. Re-walks BOTH lists in a stable loop until both observe nullptr in a single iteration (closes the unlock-vs-reaper splice race per RC-α / Opus C-R3-P1-3).
5. Waits for `active_holders_count_ == 0` AND `active_acquirers_count_ == 0` (RC-α epoch counters) AND `in_flight_resumptions_ == 0` via `co_await drain_latch_state::wait()`.
6. On normal completion: `signal_release()`, clear `drain_latch_ptr_`, return `expected_t<void>{}`.
7. On cancellation propagation (caller's parent state fires `total` mid-drain): `signal_abort()`, return `unexpected{sync_lock_aborted}`.

`drain_latch_state` (RC-β v1.3) owns: `std::atomic<bool> released_`, `std::atomic<bool> aborted_`, `std::atomic<uint32_t> in_flight_resumptions_`, and a multi-waiter latch surface implemented as `asio::experimental::concurrent_channel<void()>` (or a project-internal fallback subscriber-list). The mutex stays `constexpr`-default-constructible because the state object is lazily heap-allocated inside the reaper's frame (the v1.2 by-value `asio::steady_timer` member was non-implementable per Opus C-R3-P2-1).

**Anchor:** `[2f §4.7]` / `[2f §4.7.2]` / `[2f §4.7.3]` / `[2f §4.7.4]` / `[arch §5.5]`.

---

### D-5 — Explicit-`mr` PMR fallback + `slot_allocator` three-case storage (RC-C / RC#2)

**Decision:** RC#2 fix — `core::async_mutex` does NOT reach into `session/` or an engine handle for memory. The explicit `async_lock(std::pmr::memory_resource* mr = nullptr) noexcept` overload is the sole PMR fallback path.

When `mr == nullptr` (default hot path): the awaiter is embedded in the caller's coroutine frame (HALO-friendly); the cancellation slot's handler closure storage draws from the awaiter's 32-byte `slot_storage_` inline buffer via `detail::slot_allocator{this, nullptr}` → `null_memory_resource()` fallback (RC-C).

When `mr != nullptr` (PMR fallback path): the awaiter is allocated from `mr`; the cancellation slot's storage is `std::pmr::polymorphic_allocator<void>{mr}`.

Three-case storage table (`[2f §4.3.4]`):
- Case 1 — embedded + HALO firing: zero global-heap touch.
- Case 2 — embedded + HALO not firing: global-heap touch observable; non-fatal iff seam #10 passes.
- Case 3 — PMR fallback: all allocations from caller-supplied `mr`; zero global-heap.

The session-side helper `async_lock_via_session_executor` (declared by 2f, implemented by the session-module spec) recovers the per-session resource via `Session::session_arena()` (Appendix D §D.1 of 2f, applied when 2d ships) and forwards into `async_lock(mr)`. This helper lives in `session/` downstream of `core/` per `[arch §2.3]`'s leaf rule (RC#2 layering fix).

**Anchor:** `[2f §4.3]` / `[2f §4.3.1]` / `[2f §4.3.2]` / `[2f §4.3.4]` / `[2f §6.1.1]` / `[2f §8]`.

---

### D-6 — Memory-ordering specification + compile-time invariants + ARM64 correctness (RC#5)

**Decision:** Full memory-ordering sub-table per `[2f §6.2.2]` — every atomic operation on `state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `active_holders_count_`, `active_acquirers_count_`, `drain_latch_ptr_`, and per-`drain_latch_state` fields is pinned to a specific ordering with its rationale and pairing partner. ARM64 weak-memory is the load-bearing target (x86 TSO masks several of the required orderings).

Compile-time invariants (`static_assert`s per RC#5 / `[2f §4.1]`):
- `sizeof(uintptr_t) >= sizeof(void*)` — state encoding prerequisite.
- `std::atomic<uintptr_t>::is_always_lock_free` — wait-freedom claim requires lock-free atomic.
- `std::atomic<async_mutex_awaiter*>::is_always_lock_free` — `next_drain_head_` exchange requires lock-free.
- `alignof(async_mutex_awaiter) >= 8` — low-bit `not_locked` sentinel distinguishability (placed after awaiter's complete-type definition per Codex C-P3-7 order-validity close).

TSan seams #7 / #18 (ARM64 Graviton) verify no data races on the hot-path LIFO push / unlock exchange / FIFO drain / cancellation-handler / reaper interactions.

**Anchor:** `[2f §6.2]` / `[2f §6.2.1]` / `[2f §6.2.2]` / `[2f §4.1]` (static_asserts).

---

### D-7 — The four `sync_*` error variants + `FIXPP_ERR_SYNC_*` prefix + planned slot allocation

**Decision:** four new `fixpp::core::error` variants, per-doc-prefix `FIXPP_ERR_SYNC_*`, appended at the first free slots on THIS branch:

`error.hpp` occupancy verified on `006-async-mutex` branch: slots 1, 10–13, 20–29, 30–42 occupied → **first free = 43**.

| Variant | Planned slot | Source | C-ABI group |
|---|---|---|---|
| `sync_lock_aborted` | 43 | `[2f §4.5]` — cancellation won CAS-arbitration | `FIXPP_ERR_CANCELLED` (per `[2d §6.7]` Appendix D §D.2) |
| `sync_lock_alloc_failed` | 44 | `[2f §4.3]` — PMR `allocate(...)` threw `std::bad_alloc` (trapped) | `FIXPP_ERR_SYNC_RUNTIME` |
| `sync_lock_outside_session` | 45 | `[2f §4.3.2]` — session-side helper called outside a session | `FIXPP_ERR_SYNC_RUNTIME` |
| `sync_lock_drained` | 46 | `[2f §6.5]` RC-B v1.1 — `draining_ == true`; post-drain fast-fail | `FIXPP_ERR_SYNC_RUNTIME` |

This is a **planned, pre-publication allocation** — no tagged C-ABI release has occurred. Slots are pinned at Gate A / `/speckit-tasks` before any freeze under `[const §X.4]`. 2i owns the final C-ABI coalescing; the `FIXPP_ERR_SYNC_*` prefix is recorded here per the per-doc-prefix discipline.

**No `async_mutex_destroyed_with_waiters` variant** — the destructor fires `std::terminate()`, not an `expected_t` form (`[2f §6.5]` note, RC#3 fix).

**Anchor:** `[2f §6.5]` / `[2f §5]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]` (per-doc-prefix precedent).

---

### D-8 — Performance Tier-1 ceilings (`[2f §6.3]`)

**Decision:** per `[2f §6.3]` (v1.3/v1.4 updated values — raised from v1.2's lower ceilings to accommodate the RC-α acquirer/holder counter RMWs):

| Operation | Ceiling | Note |
|---|---|---|
| `async_lock` uncontended | ≤ 20–25 ns | Includes 2× `acq_rel` counter RMWs (RC-α). Hard CI gate. |
| `async_lock` contended | ≤ 80 ns | Contended enqueue ceiling. Hard CI gate. |
| `unlock` uncontended | ≤ 15 ns | Raised from ≤ 10 ns (RC-α holder-counter decrement RMW). Hard. |
| `unlock` contended drain (per waiter, same-strand) | ≤ 30 ns + ≤ 50 ns/waiter | bench-harness-soft |
| `cancel_and_drain()` per N waiters | ≤ 120 ns + ≤ 80 ns/waiter | bench-harness-soft |

The `[2e §6.6]` `MemoryStore::store` 200 ns envelope is satisfied: uncontended ceiling ≤ 25 ns leaves ≥ 175 ns headroom. `bench/sync/` harnesses enforce via Google Benchmark; ±5% vs `bench/baselines/sync/async_mutex_baselines.json` (`[const §VIII.2]`).

**Anchor:** `[2f §6.3]` / `[2e §6.6]` / `[const §VIII.1]` / `[const §VIII.2]`.

---

### D-9 — The `[const §XV.9]` CI grep-gate ownership and labelled corpus

**Decision:** `tools/check_no_std_mutex_in_awaitable_headers.sh` is **owned and finalized** by this feature. Scaffolded by 005 Phase 1, delivered and tested here. The tool runs after preprocessing (post-include-expansion) per Codex C-P2-10 close, scanning every header under `include/fixpp/...` and `src/` for the conjunction of `<mutex>` / `std::mutex` AND `asio::awaitable` / `<asio/awaitable.hpp>`. Hits fail the build with documentation pointers.

The test corpus (seam #14) provides:
- `tests/sync/fixtures/header_with_std_mutex_and_awaitable.hpp` — deliberate-violation fixture; the grep gate MUST fire on it.
- `tests/sync/fixtures/header_without_violation.hpp` — non-violating fixture; the grep gate MUST NOT fire.
- A transitive-include variant (a header that pulls in `asio/awaitable.hpp` only via a transitive include) — post-preprocessing scope catches it.

Post-v1: clang-tidy custom check `fixpp-no-std-mutex-in-coroutine-context` (per `[2f §6.6]` §10 Q3 disposition). Not shipped here — v1.0 ships the grep gate.

**Anchor:** `[2f §6.6]` / `[const §XV.9]` / `[const §XI.3]` / `[2f §9 seam #14]`.

---

### D-10 — Session-side helper declaration-only layering boundary (RC#2)

**Decision:** `fixpp::session::async_lock_via_session_executor` is **declared** in `include/fixpp/session/async_lock_via_session_executor.hpp` by this feature. Its **implementation is owned by the later session-module spec** (the Phase-4 `005-session-establishment-fsm` feature or a successor). This preserves the RC#2 layering fix: `core::async_mutex` does NOT reach into `session/` or an engine handle.

The declaration (per `[2f §4.3.2]`):
```cpp
namespace fixpp::session {
[[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>>
    async_lock_via_session_executor(fixpp::sync::async_mutex& m) noexcept;
}
```

At 2f sign-off, Appendix D §D.1 of the design doc publishes `Session::session_arena() noexcept -> std::pmr::memory_resource*` as an engine-internal accessor in 2d. This is a requested cross-doc edit; it is NOT applied here (2f does not edit 2d). It is recorded as a 2d-to-do at 2f sign-off (the orchestrator applies it when 2d ships).

If a non-session caller (engine bootstrap, listener accept) needs a mutex, they pass the resource directly to `async_lock(mr)`. No v1.0 hot path takes the session-helper-outside-session branch (`sync_lock_outside_session` fast-fail in the helper).

**Anchor:** `[2f §4.3.2]` / `[2f §4.3.3]` / `[arch §2.3]` / `[2d §4.5]` / `[2d §4.8]`.

---

### D-11 — Fuzz N/A + abidiff N/A + `[const §VII.5]` N/A-with-reason (non-applicability recorded)

**Decision:**
- **Fuzz N/A:** `async_mutex` is not parser-touching. It does not parse, frame, or decode any byte stream. `[const §VII.7]` does not bind. Recorded for explicit non-applicability so `/speckit-verify` marks it `SKIPPED-with-reason`.
- **abidiff N/A:** no C-ABI surface is added. `async_mutex` is C++ only per `[2f §5]`. `[const §IX.5]` does not bind. Recorded for explicit non-applicability.
- **`[const §VII.5]` N/A-with-reason (NOT a waiver):** the article mandates TC-001..TC-017 for features that have `[FIX-TC]` conformance scope. 2f has no `[FIX-TC]` scope — it is an engineering-judgment-driven concurrency primitive with no FIX session-layer test case applicable. Appendix B of the design doc records this structural fact. This is a **non-applicability**, not a missing obligation being deferred. Complexity Tracking is empty because there is nothing being waived (contrast with 005 which carried an explicit `[const §XVII.1]` Gate-A-blocker waiver for deferred TC cases that had `[FIX-TC]` scope).

**Anchor:** `[2f Appendix B]` / `[const §VII.5]` / `[const §VII.7]` / `[const §IX.5]`.

---

### D-12 — Appendix D cross-doc 2d edits — 2f requests, NOT applies (recorded for tracking)

**Decision:** `[2f §3.1]` Appendix D requests three edits to `2d`:
- **§D.1** — `[2d §4.5]` publishes `Session::session_arena() noexcept -> std::pmr::memory_resource*` as an engine-internal accessor; used by the session-side helper (RC#2 fix; `fixpp::session/` scope only).
- **§D.2** — `[2d §4.7]` per-mode effect table's `async_mutex::lock` row rewritten to surface `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED`; RC#4 fix).
- **§D.3** — `[2d §7.4]` locked-contract-surface bullet for `async_mutex::lock` rewritten to match §D.2's wording (RC-D close; closes sibling-doc inconsistency per Codex C-P2-5 / Opus N-P1-1).

**2f does NOT edit 2d here.** Per `[2c App D]` / `[2d §11]` / `[2e App D]` precedent, these drop-ins are applied by the orchestrator at 2f sign-off, or carried forward to the 2d feature as a prerequisite edit. This is recorded here so the tracking survives the feature boundary.

**Anchor:** `[2f §3.1]` Appendix D / `[2f §11]`.
