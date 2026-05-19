# Phase 0 — Research & Decisions — 006-async-mutex

**Anchor:** `.specify/2f-async-mutex.md` **v1.5 + v1.6 errata E-1..E-4** (v1.5 Gate-A-converged; E-1/E-2/E-3/E-4 recorded post-sign-off at `/implement`, source-verified + user-authorized — they re-touch 006 Gate A scope and are propagated through this bundle). On conflict the anchor (as amended by the errata) wins. All decisions below distill fixed design choices from the design doc; no design choice is invented here.

---

### D-1 — Algorithm, state encoding, and mutex-owned residual FIFO chain (RC-A v1.1)

**Decision:** Own implementation of the cppcoro / Lewis-Baker lock-free awaitable-mutex algorithm, with BSL-1.0 algorithm attribution to avast/asio-mutex. State encoding in `std::atomic<uintptr_t> state_`:
- `not_locked = 1` (low bit set; distinguishable from any 8-byte-aligned pointer)
- `locked_no_waiters = 0`
- `<pointer-to-waiter-record>` — the LIFO head. **E-2 (v1.6, `[2f §4.2]` Erratum E-2):** this is a `detail::waiter_record*` (the stable intrusive node, E2a) — **not** the frame-local awaiter. `alignof(waiter_record) >= 8` keeps the low-bit sentinel distinguishable.

**RC-A v1.1 fix (E-2-retyped):** the mutex additionally owns `std::atomic<detail::waiter_record*> next_drain_head_` (**E-2:** retyped from `async_mutex_awaiter*` to `waiter_record*`) — the residual FIFO chain after an `unlock()` drain grants the first waiter. `unlock()` walks `next_drain_head_` FIRST, then falls back to the LIFO chain anchored at `state_`. The residual list was moved from the awaiter (v1.0 design defect — the granted awaiter "owned" the FIFO but `unlock()` had no way to reach it from `async_mutex*` alone; UAF on holder-cancellation mid-critical-section per Codex C-P1-1/Opus C-P1-2/Opus N-P1-1) to the mutex.

> **E-2 split (v1.6 — the frame-local awaiter is NOT the intrusive node).** E-1 made `async_mutex_awaiter` frame-local while §4.2 kept it as the intrusive node linked through `state_`/`next_drain_head_` — a guaranteed use-after-free under prompt cancel-resume of an interior waiter (the frame, hence its `next_`/`phase_`/result, is destroyed while `state_`/`next_drain_head_` still point at it). E-2 splits the waiter into two objects: `detail::async_mutex_awaiter` stays frame-local + HALO-eligible and keeps only its E-1 composed-op machinery + a `detail::waiter_record* record_` attachment; `detail::waiter_record` (E2a) is the **stable intrusive node** linked through `state_`/`next_drain_head_`, carrying `next_`, the `phase_` CAS atom, the owned terminal `result_`, `attached_awaiter_`, inline `exec_storage_`, and the `refcount_` reclamation token (I-32). The contended `mr == nullptr` zero-global-`new` path runs through the per-mutex `waiter_pool_` (E-2), not an embedded waiter node. Reclamation is single-shot via `refcount_` (I-32), sound *because of* the single-drainer invariant — no hazard pointers / epoch GC. Every other §4.2/§4.5/§4.7.2 clause and every I-01..I-31 ordering is unchanged; they now apply to `waiter_record::phase_`/`waiter_record::result_`.

The three-state `waiter_phase` machine (`{ queued, granted, cancelled }`, RC-A v1.1 — collapsed from v1.0's four-state `{ queued, draining, cancelling, completed }`) drives CAS-arbitration between `unlock()` drain and the cancellation handler. **E-2:** it lives on `waiter_record::phase_` (not the awaiter). Both potential writers first win a per-`waiter_record` `phase_` CAS from `queued` to their terminal state; the winner alone writes `record->result_` (v1.4 CAS-then-publish).

**Rationale:** avast/asio-mutex / cppcoro / Lewis-Baker algorithm is the established reference for ASIO-compatible awaitable mutexes. Moving the residual list to the mutex closes the v1.0 UAF defect class (RC-A). The two-object split (E-2) closes the E-1-introduced cancel/lifetime UAF while keeping the frame-local awaiter HALO-eligible. The three-state machine suffices because the drain CAS is atomic with ownership transfer — no intermediate "draining" or "cancelling" phase is needed under the mutex-owned-residual shape.

**Anchor:** `[2f §4.1]` / `[2f §1.2]` / `[2f §4.2]` Erratum E-2 (v1.6) / `[2f §6.2]` / `[2f Appendix C]` RC-A close.

---

### D-2 — Awaiter shape, the three-state phase machine, and the ≤ 96 B HALO budget

**Decision (E-2 split, v1.6).** The waiter is two objects, not one:

**`detail::async_mutex_awaiter`** — frame-local + HALO-eligible, `alignas(8)`, ≤ 96 B (v1.1 budget per `[2f §1.1]`); it is **no longer the intrusive node**. Member layout (E-2-amended; mirrors data-model.md E2):

| Member | Size | Role |
|---|---|---|
| `async_mutex* mutex_` | 8 B | back-pointer to the owning mutex |
| `detail::waiter_record* record_` | 8 B | **E-2 (new):** the stable node (E2a) this awaiter is attached to on the contended path; `nullptr` on the uncontended fast path. Intrusive `next_`/`phase_`/terminal `result_` now live on `*record_`. |
| `asio::cancellation_slot slot_` | ≈ 16 B | bound at `await_suspend`; ASIO implementation-defined size |
| `std::array<std::byte, 32> slot_storage_` | 32 B | RC-C / **E-1:** inline buffer for the asio **completion** handler placement-new'd here — **replaces** the design's `coro_` stored continuation (there is **no `coro_` field**); E-4: it does *not* back the cancellation closure |
| `invoke_fn_t invoke_fn_` | 8 B | **E-1:** type-erased invoke trampoline into `slot_storage_` (replaces the retired `coro_.resume()`) |
| `destroy_fn_t destroy_fn_` | 8 B | **E-1:** type-erased destroy trampoline for the in-buffer completion handler |

**E-2 relocation:** `next_`, `phase_`, and `result_` are **removed from the awaiter** and re-homed on `detail::waiter_record` (E2a). On the uncontended fast path no `waiter_record` is created and `record_ == nullptr`.

**`detail::waiter_record`** — the **stable intrusive node** linked through `state_`/`next_drain_head_`, `alignas(8)`, created only on the contended path; outlives the coroutine frame. Carries: `mutex_`; `next_` (intrusive link reused by the LIFO chain on `state_` AND the `next_drain_head_` FIFO chain — RC-A); `std::atomic<waiter_phase> phase_` (3-state machine; CAS-arbitration point); **owned** `expected_t<async_lock_guard> result_` (not a pointer — written by the `phase_` CAS winner, read by `await_resume` via `record_`; stable across coroutine-frame destruction — the E-2 fix); `std::atomic<async_mutex_awaiter*> attached_awaiter_`; inline `exec_storage_` (E-2 Gap-B — bound-executor SBO, `static_assert` enforces fit, overflow ⇒ `sync_lock_alloc_failed`; no heap-allocating `any_io_executor`); `std::atomic<std::uint32_t> refcount_` (single-shot reclamation token, I-32).

**HALO eligibility:** the awaiter's ≤ 96 B / `alignas(8)` layout allows it to embed in the caller's coroutine frame without heap allocation when HALO fires; `alignof(waiter_record) >= 8` keeps the low-bit `not_locked` sentinel distinguishable. The 32-byte `slot_storage_` holds only the asio **completion** handler per E-1 (the cancellation closure lives in asio's per-thread recycler per E-4, D-5).

**`result_` validity and CAS-then-publish (v1.4; E-2-rehomed):** `record->result_` is owned storage on the stable node, valid for the waiter's suspension and read by `await_resume` via `record_`. **Only the CAS winner writes `record->result_`** — the winner first wins `record->phase_.compare_exchange(queued → terminal, acq_rel)` then writes `record->result_` and schedules resumption. CAS losers do not touch it. `await_resume` acquire-loads `record->phase_` before reading `record->result_`.

**Reclamation (I-32):** single-shot via `record->refcount_` (edges: +1 creator; one transferred in-lists membership ref across every `state_ ⇄ next_drain_head_` splice; +1 per scheduled prompt-resume; +1 while `attached_awaiter_ != nullptr`); the final `fetch_sub(1, acq_rel) == 1` reclaims to `waiter_pool_`/`mr`. Sound *because of* the single-drainer invariant (no competing walker) — no hazard pointers / epoch GC. This is a normative E-2 precondition, not an implementation detail.

**Anchor:** `[2f §1.1]` / `[2f §4.2]` Erratum E-2 (v1.6) / `[2f §4.2.1]` / `[2f §4.2.2]` / `[2f §4.2.3]` / `[2f §6.4]`.

---

### D-3 — Cancellation CAS-arbitration contract (§4.5, `cancellation_type::total` → `sync_lock_aborted`)

**Decision:** `cancellation_type::total` (and `terminal`, treated as `total`; `partial`, treated as `total` per `[2d §4.7]`) triggers a per-`waiter_record` CAS from `phase_ queued → cancelled` (**E-2:** `phase_` lives on the stable `waiter_record`, not the frame-local awaiter). The arbitration race with `unlock()`'s drain CAS (`queued → granted`) is resolved atomically: exactly one wins, the loser observes terminal phase and does not write `record->result_`. The winner writes `record->result_ = unexpected{sync_lock_aborted}` (on cancel) or an engaged guard (on grant) and schedules the resumption. E-2 makes prompt cancel-resume UAF-safe: the node, not the coroutine frame, is the stable storage.

`await_resume` returns `expected_t::unexpected{error::sync_lock_aborted}` when `record->phase_ == cancelled`. The 2f-boundary outcome joins `[2d §6.7]`'s `dispatch_aborted` and `clock_sleeps_cancelled` in the `FIXPP_ERR_CANCELLED` group at the C ABI per `[2d §4.7]` Appendix D §D.2 (applied when 2d ships — D-12).

The cancellation slot is registered exactly once at `await_suspend` and cleared once in `await_resume`. **E-4 (v1.6, `[2f §4.3.4]` Erratum E-4):** the slot is **not** `asio::bind_allocator`-wrapped — asio 1.36.0 has no allocator-binding hook on `cancellation_slot`; the closure storage is owned by asio's per-thread recycling cache (zero global `new`/`delete` in steady state by construction; one-time per-thread first-touch is §6.4 bench-soft). `detail::slot_allocator` is *not* bound to the slot (see D-5). Cancellation-after-resume is a safe no-op: the CAS observes `granted` or `cancelled` (terminal) and returns without dereferencing freed state.

**Anchor:** `[2f §4.5]` / `[2f §4.5.1]` / `[2f §4.3.4]` Erratum E-4 (v1.6) / `[2f §6.1.4]` / `[SYN §3.2 Q6a]` / `[2d §7.4]`.

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

**Implement-time STL-availability assumption (Codex P3 / Opus-confirmed, Gate A round 1):** the mutex member `std::atomic<std::shared_ptr<detail::drain_latch_state>>` (data-model.md E1; `[2f §1.2]`/`[2f §4.1]`) requires the `std::atomic<std::shared_ptr<T>>` partial specialization (C++20 / P0718). This is a documented **implementation assumption on the supported standard-library matrix** (libc++, libstdc++, MSVC-STL — `[const §IX.6]` Tier-1/Tier-2 toolchains). The design is honestly bounded at the authority level — `[2f §4.1]` (lines 637–640) pins the ordering and records "not lock-free in general … cold path … does not impact the hot-path cost" — so this is purely a *toolchain-availability* gate, not a design risk. A `static_assert`-style compile probe (a translation unit that instantiates `std::atomic<std::shared_ptr<int>>` and reads `is_always_lock_free` / `is_lock_free()`) MUST run as a `/speckit-verify` step-0 item on every supported STL **before `/implement`**, so a missing surface fails early rather than deep in the build (quickstart §0).

**T004 probe verdict (recorded 2026-05-18, pre-`/implement`):** the probe was run on all locally-available toolchains. **PASS** on `g++` (libstdc++) and `clang++ -std=c++23` (libstdc++) — the Tier-1 matrix every Linux Conan profile (`linux-clang-{debug,tsan,asan,ubsan,release,coverage}`, `linux-gcc-release`) actually pins (`compiler.libcxx=libstdc++11`). **FAIL** on `clang++ -stdlib=libc++` (LLVM 22 libc++): `std::atomic<std::shared_ptr<int>>` resolves to the *primary* `std::atomic<T>` template and trips its `is_trivially_copyable` static_assert — LLVM 22's libc++ does not provide a usable P0718 `atomic<shared_ptr>` specialization, compounded by `_LIBCPP_SHARED_PTR_TRIVIAL_ABI` on `shared_ptr`. **Resolution (user-approved):** libc++ is **not provisioned by any Conan profile in this repo** (no Linux profile uses libc++; Windows Tier-1 uses MSVC-STL, which *does* implement P0718 since VS 2019 16.x). The type therefore compiles and works on every toolchain this repo actually builds. libc++ is hereby recorded as an **out-of-matrix / Tier-2-waived** STL for 006-async-mutex: a libc++ probe failure is **not** a hard `/implement` blocker for this feature. `/speckit-verify` step-0 records libc++ as `SKIPPED-with-reason (unprovisioned; LLVM 22 libc++ lacks usable P0718 atomic<shared_ptr>)` and gates only on the provisioned libstdc++/MSVC-STL matrix. Revisit if a libc++ Conan profile is ever added.

**Anchor:** `[2f §4.7]` / `[2f §4.7.2]` / `[2f §4.7.3]` / `[2f §4.7.4]` / `[arch §5.5]`.

---

### D-5 — Explicit-`mr` PMR fallback + `slot_allocator` three-case storage (RC-C / RC#2)

**Decision:** RC#2 fix — `core::async_mutex` does NOT reach into `session/` or an engine handle for memory. The explicit `async_lock(std::pmr::memory_resource* mr = nullptr) noexcept` overload is the sole PMR fallback path.

> **E-4 (v1.6, source-verified non-implementable; `[2f §4.3.4]` Erratum E-4).** asio 1.36.0's `cancellation_slot::emplace`/`assign` obtains memory exclusively through `prepare_memory()` → `thread_info_base::allocate(cancellation_signal_tag)` — there is **no associated-allocator query and no `asio::bind_allocator` hook** on that path. Therefore `detail::slot_allocator` **cannot be bound to the cancellation slot** and is **not**. The cancellation-handler closure storage is owned by asio's **per-thread recycling cache**: the first cancellation-slot assignment on a thread does one global `aligned_new`; every subsequent assignment on that thread reuses the thread-local block → **zero global `new`/`delete` only in steady state, after a documented per-thread warm-up** (the one-time first-touch is amortized and treated as §6.4 / §4.3.4 case-2 *bench-soft*: observable, non-fatal — never routable to a project allocator). `detail::slot_allocator` is retained as the typed, `Allocator`-shaped storage-policy wrapper for the allocation 2f *does* control — the **`waiter_record` fallback** (E2a); its three-case body is unit-verified by seam #21 in isolation. The substantive contended-path zero-global-`new` guarantee is delivered by E-2's per-mutex `waiter_pool_` (embedded path) / caller `mr` (PMR path) — unaffected by E-4.

When `mr == nullptr` (default hot path): the awaiter is embedded in the caller's coroutine frame (HALO-friendly); the contended `waiter_record` is drawn from the per-mutex `waiter_pool_` (E-2; zero global `new`); the cancellation-slot closure lives in asio's per-thread recycler (E-4; not project-allocated).

When `mr != nullptr` (PMR fallback path): the awaiter and the `waiter_record` (E2a) are allocated from `mr` and reclaimed back to `mr`; the cancellation-slot closure still lives in asio's per-thread recycler (E-4 — no asio hook).

Three-case storage table (`[2f §4.3.4]`, **post-E-4 re-anchored to the `waiter_record` storage decision**):
- Case 1 — `mr == nullptr`: `waiter_record` from the per-mutex `waiter_pool_` (E-2); zero global-heap touch (steady state, post per-thread warm-up for the asio recycler).
- Case 2 — **N/A for the `waiter_record`**: it is never placed on the coroutine frame (pool- or `mr`-backed), so the HALO-not-firing branch does not exist for it; coroutine-frame HALO is verified separately for the awaiter by seam #9.
- Case 3 — PMR fallback (`mr != nullptr`): the `waiter_record` is drawn from / reclaimed to caller-supplied `mr`; zero global-heap.

The session-side helper `async_lock_via_session_executor` (declared by 2f, implemented by the session-module spec) recovers the per-session resource via `Session::session_arena()` (Appendix D §D.1 of 2f, applied when 2d ships) and forwards into `async_lock(mr)`. This helper lives in `session/` downstream of `core/` per `[arch §2.3]`'s leaf rule (RC#2 layering fix).

**Anchor:** `[2f §4.3]` / `[2f §4.3.1]` / `[2f §4.3.2]` / `[2f §4.3.4]` Erratum E-4 (v1.6) / `[2f §6.1.1]` / `[2f §8]`.

---

### D-6 — Memory-ordering specification + compile-time invariants + ARM64 correctness (RC#5)

**Decision:** Full memory-ordering sub-table per `[2f §6.2.2]` — every atomic operation on `state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `active_holders_count_`, `active_acquirers_count_`, `drain_latch_ptr_`, and per-`drain_latch_state` fields is pinned to a specific ordering with its rationale and pairing partner. ARM64 weak-memory is the load-bearing target (x86 TSO masks several of the required orderings).

Compile-time invariants (`static_assert`s per RC#5 / `[2f §4.1]`):
- `sizeof(uintptr_t) >= sizeof(void*)` — state encoding prerequisite.
- `std::atomic<uintptr_t>::is_always_lock_free` — wait-freedom claim requires lock-free atomic.
- `std::atomic<detail::waiter_record*>::is_always_lock_free` — **E-2:** `state_`/`next_drain_head_` hold `waiter_record*`; their exchange/CAS requires a lock-free atomic pointer.
- `alignof(waiter_record) >= 8` — **E-2:** the low-bit `not_locked` sentinel must be distinguishable from any real `waiter_record*` (placed after the `waiter_record` complete-type definition per Codex C-P3-7 order-validity close).

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
