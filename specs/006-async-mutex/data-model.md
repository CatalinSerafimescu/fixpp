# Phase 1 Data Model — 006-async-mutex

Source of truth: `.specify/2f-async-mutex.md` **v1.5** (Gate-A-converged). On conflict the design anchor wins.

Entities are the logical runtime objects that 2f introduces. They map to named C++ types in `include/fixpp/core/sync/` (plus one declaration-only type in `include/fixpp/session/`). Relationships and invariants are extracted verbatim from the referenced design-doc sections.

---

## Entities

### E1 — `fixpp::sync::async_mutex`

**Source:** `[2f §4.1]` + `[2f §6.2]`.

**Role:** The awaitable mutex value type. The only legal mutex shape in coroutine context per `[const §XI.3]` and `[const §XV.9]`. Non-copyable, non-movable. Consumer holds it by value at a stable address.

**Members (layout order):**

| Field | Type | Init | Role |
|---|---|---|---|
| `state_` | `std::atomic<uintptr_t>` | `not_locked` (= 1) | Lewis-Baker / cppcoro state encoding: `1` = not_locked; `0` = locked_no_waiters; `<ptr>` = LIFO head. **E-2:** `<ptr>` is a `detail::waiter_record*` (the stable node, E2a), not the frame-local awaiter. `alignof(waiter_record) >= 8` keeps the low-bit sentinel distinguishable from any real pointer. |
| `next_drain_head_` | `std::atomic<detail::waiter_record*>` | `nullptr` | RC-A v1.1 — mutex-owned residual FIFO chain. **E-2:** retyped from `async_mutex_awaiter*` to `waiter_record*` (E2a). `unlock()` walks this list first before the LIFO. Solves the v1.0 UAF where the granted waiter "owned" the residual list unreachable from `async_mutex*`-only state. |
| `waiter_pool_` | per-mutex bounded freelist/slab of `detail::waiter_record` | empty/pre-reserved | **E-2 (new):** stable-node storage for the contended `mr == nullptr` path. Zero global `operator new`/`delete`; an arena per `[const Art.VIII §5]` default. Exhaustion ⇒ `unexpected{sync_lock_alloc_failed}` (slot 44). |
| `draining_` | `std::atomic<bool>` | `false` | RC-B v1.1 — set by `cancel_and_drain()`; from that point onward every `async_lock(...)` fast-fails with `unexpected{sync_lock_drained}` (checked in `await_ready` BEFORE the fast-path CAS). |
| `drain_in_progress_` | `std::atomic_flag` | `ATOMIC_FLAG_INIT` | RC-B v1.1 — concurrent-call serialiser for `cancel_and_drain()`. Only the first caller (the reaper) wins `test_and_set`; all others subscribe to the drain epoch's latch. |
| `active_holders_count_` | `std::atomic<std::uint32_t>` | `0` | v1.2 / v1.3 RC-α — incremented WINNER-ONLY at the grant CAS-success (fast-path `await_ready` CAS or drain-walker grant CAS); decremented at `unlock()` entry. `cancel_and_drain()` waits for `== 0`. Closes Opus C-R3-P1-1 "phantom holder count on CAS loss" defect. |
| `active_acquirers_count_` | `std::atomic<std::uint32_t>` | `0` | NEW v1.3 RC-α — in-flight acquirer epoch counter. Incremented by the `async_lock(mr)` awaitable factory BEFORE `await_ready`'s `draining_` load; decremented at one of three exit points (fast-path success, drained-bypass, LIFO-enrol). `cancel_and_drain()` waits for `== 0` AND `active_holders_count_ == 0`. Closes Opus C-R3-P1-2 in-flight acquirer window. |
| `drain_latch_ptr_` | `std::atomic<std::shared_ptr<detail::drain_latch_state>>` | `{}` (empty) | NEW v1.3 RC-β — atomic owner-reference to the lazy-constructed `drain_latch_state`. Null until `cancel_and_drain()` is called. The reaper stores the `shared_ptr` here BEFORE setting `draining_ = true` (v1.4 ordering guarantee). Not lock-free in general; the update path is cold (only on `cancel_and_drain()` invocation). |
| `policy_` | `completion_policy const` | `completion_policy::dispatch` | Per-mutex completion policy. Set at construction via the explicit `async_mutex(completion_policy)` ctor; immutable thereafter. |

**Static constants:**

| Name | Value | Role |
|---|---|---|
| `not_locked` | `uintptr_t{1}` | State sentinel: mutex is free. Low-bit set; distinguishable from any 8-byte-aligned waiter pointer. |
| `locked_no_waiters` | `uintptr_t{0}` | State sentinel: mutex is held, LIFO list empty. |

**Compile-time invariants (order-valid placement per Codex C-P3-7):**

- `static_assert(sizeof(uintptr_t) >= sizeof(void*))` — placed after the namespace closes in §4.1.
- `static_assert(std::atomic<uintptr_t>::is_always_lock_free)` — algorithm's wait-freedom claim depends on this; reject targets where false.
- `static_assert(std::atomic<fixpp::sync::detail::async_mutex_awaiter*>::is_always_lock_free)` — `next_drain_head_` exchange requires lock-free atomic pointer.
- `static_assert(alignof(async_mutex_awaiter) >= 8)` — placed AFTER the awaiter struct definition (alignof on an incomplete class is ill-formed).

**Constructors:**

- `constexpr async_mutex() noexcept = default` — default-constructs an unlocked mutex with `dispatch` policy. Zero runtime cost; no executor dependency.
- `explicit constexpr async_mutex(completion_policy cp) noexcept` — constructs with an explicit policy.
- Copy and move constructors/assignments: `= delete` (non-copyable, non-movable).

**Destructor:** `~async_mutex()` — fires `std::terminate()` (or `fixpp::core::abort_invariant(...)`) if the mutex is held OR waiters are present in `state_` or `next_drain_head_`. Both debug AND release builds enforce this hard precondition. Callers MUST drain via `cancel_and_drain()` before destruction (RC#3 fix).

**Public methods:**

| Method | Return | Notes |
|---|---|---|
| `async_lock(mr = nullptr)` | `asio::awaitable<expected_t<async_lock_guard>>` | `[[nodiscard]]`, `noexcept`. PMR-aware (RC#2): `mr == nullptr` → embedded awaiter in caller's coroutine frame (HALO-friendly); `mr != nullptr` → allocate awaiter from `mr`. The awaitable factory increments `active_acquirers_count_` BEFORE `await_ready`'s `draining_` load. |
| `unlock()` | `void` | `noexcept`. Walks `next_drain_head_` first (RC-A), then LIFO from `state_`. The drain-walker CAS's `phase_: queued → granted` (first non-cancelled waiter only); winner writes `*result_` and increments `active_holders_count_`; splice remaining FIFO tail into `next_drain_head_`. Under `draining_ == true`, does NOT splice — notifies latch only (RC-α C-R3-P1-3 close). |
| `cancel_and_drain()` | `asio::awaitable<expected_t<void>>` | `[[nodiscard]]`, `noexcept`. The canonical drain primitive. Idempotent and concurrent-call-safe (RC-B). Reaper algorithm: allocate `drain_latch_state` via `make_shared`; store to `drain_latch_ptr_` (release) BEFORE `draining_.store(true, release)` (v1.4 ordering); exchange both `state_` and `next_drain_head_`; CAS each `queued` waiter to `cancelled` (winner-only `*result_` write); re-walk both lists in stable loop until null; wait for `active_holders_count_ == 0` AND `active_acquirers_count_ == 0` AND `in_flight_resumptions_ == 0` via `co_await latch_state->wait()`. Returns `expected_t<void>{}` on success or `unexpected{sync_lock_aborted}` if the awaitable is itself cancelled. |
| `policy()` | `completion_policy` | `[[nodiscard]]`, `const`, `noexcept`. Query the per-mutex completion policy. |

---

### E2 — `fixpp::sync::detail::async_mutex_awaiter`

**Source:** `[2f §4.2]` + `[2f §4.2.1]` + `[2f §4.2.2]` + `[2f §4.2.3]`.

**Role:** The waiter object — the suspension unit for `co_await m.async_lock()`. Lives inside the caller's coroutine frame on the hot path (HALO-eligible); allocated from `mr` on the PMR fallback path. Private; not part of the user surface.

> **E-2 split (v1.6, `[2f §4.2]` Erratum E-2):** the awaiter is **no longer the intrusive node**. The intrusive identity (`next_`, `phase_`, terminal `result_`, bound-executor handle) moves to the stable **`detail::waiter_record`** (E2a); the awaiter keeps only its frame-local machinery + a `record_` attachment pointer. The protocol/lifetime prose below reads against `waiter_record` for the contended path.

**Layout (`alignas(8)`, ≤ 96 B budget, v1.1; E-2-amended):**

| Field | Type | Role |
|---|---|---|
| `mutex_` | `async_mutex*` | Back-pointer to the originating mutex. |
| `record_` | `detail::waiter_record*` | **E-2 (new):** stable node (E2a) this awaiter is attached to on the contended path; `nullptr` on the uncontended fast path. Intrusive `next_`/`phase_`/terminal-result now live on `*record_`. |
| `slot_` | `asio::cancellation_slot` | Bound at `await_suspend` time via `asio::bind_allocator(slot_allocator{this, mr})`. |
| `coro_` | `std::coroutine_handle<>` | Continuation — stored at `await_suspend`, used to resume the coroutine. |
| `slot_storage_` | `std::array<std::byte, 32>` | RC-C v1.1 — inline 32-byte buffer for the cancellation handler closure on the embedded path with HALO firing. Fed to `detail::slot_allocator` when `mr == nullptr`. |

**E-2 relocation:** `next_`, `phase_`, and `result_` are **removed from the awaiter** and re-homed on `detail::waiter_record` (E2a). On the uncontended fast path (`await_ready` CAS wins) no `waiter_record` is created and `record_ == nullptr`.

**v1.1 layout changes vs v1.0:** removed `async_mutex_awaiter* residual_` (RC-A — mutex owns the residual list via `next_drain_head_`); added `slot_storage_` (RC-C — inline slot-handler storage).

**Awaiter protocol:**

- `bool await_ready() noexcept` — RC-α pre-step: `active_acquirers_count_.fetch_add(1, acq_rel)` in the awaitable factory BEFORE this call. Step 1: `draining_.load(acquire)` — if true, write `*result_ = unexpected{sync_lock_drained}`, set `phase_ = cancelled` (release), decrement `active_acquirers_count_` (RC-α decrement-point #1), return true (fast-fail). Step 2: `state_.compare_exchange_strong(not_locked → locked_no_waiters, acquire/relaxed)` — on success, increment `active_holders_count_` (winner-only), decrement `active_acquirers_count_` (RC-α decrement-point #2), return true. On failure, return false → `await_suspend` is invoked.
- `void await_suspend(coroutine_handle<> h) noexcept` — Step 1: `draining_.load(acquire)` check (defense-in-depth for the race window between §4.2.1 steps 1 and 2); if true: fast-fail with `sync_lock_drained`, decrement `active_acquirers_count_` (RC-α decrement-point #3a), resume inline. Steps 2–6: store `coro_ = h`; init `phase_ = queued` (relaxed); recover `cancellation_state`; bind slot allocator via `asio::bind_allocator(slot_allocator{this, mr})`; register `on_cancel`; LIFO push CAS retry loop (release/acquire). On CAS-success, decrement `active_acquirers_count_` (RC-α decrement-point #3b — tracking transfers to the LIFO walk). Step 7: if state transitions to `not_locked` mid-push, CAS to `locked_no_waiters` directly, decrement `active_acquirers_count_` (decrement-point #3c), increment `active_holders_count_`, resume inline.
- `expected_t<async_lock_guard> await_resume() noexcept` — `phase_.load(acquire)`: `granted` → return engaged `async_lock_guard{mutex_}`; `cancelled` → return `*result_` (either `unexpected{sync_lock_aborted}` or `unexpected{sync_lock_drained}`). Then clears `slot_`. The `async_lock_guard` engaged constructor is `private` + `friend`-only (Opus N-P3-1 close).
- `void on_cancel(cancellation_type type) noexcept` — CAS `phase_: queued → cancelled` (acq_rel/acquire). On CAS-success (winner): write `*result_ = unexpected{sync_lock_aborted}`, schedule resumption on bound executor. On CAS-failure (drain won): no-op — the waiter already holds the guard.

**Phase enum (`fixpp::sync::detail::waiter_phase`, collapsed from v1.0's four states — RC-A):**

| Value | `uint8_t` | Meaning |
|---|---|---|
| `queued` | 0 | Pushed onto LIFO or spliced into `next_drain_head_`; still cancellable. |
| `granted` | 1 | Drain CAS-granted ownership; `await_resume` returns the guard. Terminal. |
| `cancelled` | 2 | Cancellation handler or reaper CAS-acquired; `await_resume` returns `unexpected{sync_lock_aborted}`. Terminal. |

**Awaiter lifetime safety (RC-A close — Codex C-P1-1 UAF; E-2):** After `await_resume` returns, no external pointer threads through the *awaiter*. Intrusive pointers thread through the stable `waiter_record` (E2a), whose reclamation is governed by I-32, not by the coroutine frame. The drain physically detaches the entire LIFO chain (`state_.exchange(...)`) before walking it; cancelled `waiter_record`s are skipped (not re-spliced). On the PMR-fallback path the `waiter_record` de-allocates back to `mr` once its refcount hits zero.

---

### E2a — `fixpp::sync::detail::waiter_record`

**Source:** `[2f §4.2]` Erratum E-2 (v1.6) + `[2f §4.5]` + `[2f §4.5.1]` + `[2f §4.5.2]` + `[2f §4.7.2]`.

**Role:** The **stable intrusive waiter node** linked through `async_mutex::state_` (LIFO) and `async_mutex::next_drain_head_` (RC-A residual FIFO). Distinct from, and outlives, the frame-local `async_mutex_awaiter` (E2). Created only on the **contended** path; never on the uncontended fast path.

**Layout (`alignas(8)`):**

| Field | Type | Role |
|---|---|---|
| `mutex_` | `async_mutex*` | Back-pointer to the originating mutex. |
| `next_` | `waiter_record*` | Intrusive link — reused by both `state_`'s LIFO chain and `next_drain_head_`'s FIFO chain (RC-A). |
| `phase_` | `std::atomic<waiter_phase>` | Three-state atomic machine (`queued`/`granted`/`cancelled`). Arbitrates unlock-drain / reaper vs. cancellation handler via CAS. **v1.4 CAS-then-publish** applies here. |
| `result_` | `expected_t<async_lock_guard>` | **Terminal-result storage (owned, not a pointer).** Written only by the `phase_` CAS winner; read by `await_resume` via `record_`. Stable across coroutine-frame destruction (the E-2 fix). |
| `attached_awaiter_` | `std::atomic<async_mutex_awaiter*>` | The frame-local awaiter currently consuming this node; cleared at `await_resume`. Non-null contributes one refcount edge (I-32). |
| `exec_storage_` | `std::array<std::byte, N>` (inline, `alignas(max)`) | **E-2 Gap-B:** inline aligned storage for the bound executor (`[2d §4.8]` `session_executor` over a strand/`any_io_executor` SBO). `static_assert` enforces fit; overflow ⇒ `unexpected{sync_lock_alloc_failed}`. **No** heap-allocating type-erased `any_io_executor`. |
| `refcount_` | `std::atomic<std::uint32_t>` | Single-shot reclamation token (I-32). Edges: +1 creator; one **transferred** in-lists membership ref across every `state_ ⇄ next_drain_head_` splice; +1 per scheduled prompt-resume (resumer); +1 while `attached_awaiter_ != nullptr`. `fetch_sub(1, acq_rel) == 1` reclaims (to `waiter_pool_` or `mr`). |

**Reclamation (I-32) is sound _because of_ the single-drainer invariant.** Only one structural walker ever traverses the lists at a time: `unlock()` (runs solely under logical lock ownership) and `cancel_and_drain()`'s reaper (single via `drain_in_progress_.test_and_set(acq_rel)`) are mutually exclusive — `unlock()` short-circuits when `draining_ == true`. The cancellation handler performs no structural mutation (only the `phase_` CAS). Hence no competing-walker hazard; no hazard pointers / epoch GC are required. This is a **normative precondition** of E-2, not an implementation detail.

**Storage (extends RC#2 / RC-C):** (a) uncontended ⇒ no `waiter_record`; (b) contended `mr == nullptr` ⇒ from `async_mutex::waiter_pool_` (bounded arena; zero global `new`); (c) contended `mr != nullptr` ⇒ from `mr`. Exhaustion ⇒ `unexpected{sync_lock_alloc_failed}` (slot 44).

---

### E3 — `fixpp::sync::async_lock_guard`

**Source:** `[2f §4.4]`.

**Role:** RAII handle returned by `async_lock`'s awaitable completion. Movable, non-copyable. Releases the mutex on destruction via `async_mutex::unlock()`.

**Members:**

| Field | Type | Init | Role |
|---|---|---|---|
| `mutex_` | `async_mutex*` | `nullptr` | Back-pointer. `nullptr` = disengaged (default-constructed or moved-from). |

`sizeof(async_lock_guard) == sizeof(async_mutex*) == 8 B`.

**Constructor access (Opus N-P3-1 close):**
- `async_lock_guard() noexcept = default` — public; disengaged.
- `async_lock_guard(async_lock_guard&&) noexcept` — public move ctor; source becomes empty.
- `explicit async_lock_guard(async_mutex* mutex [[clang::lifetimebound]]) noexcept` — **private** + `friend class detail::async_mutex_awaiter`. The only legal guard-construction path is `co_await m.async_lock()`. The v1.0 public adopt-locked ctor + public `try_lock()` admitted a same-mutex aliasing bug; v1.1 closes the surface. Test seam #20 uses friend access for its fixture.
- Copy ctor/assignment: `= delete`.

**Destructive move-assignment (RC#1 / N-P1-3 close):** `operator=(async_lock_guard&&) noexcept` — if `*this` is engaged, calls `mutex_->unlock()` first; then takes ownership of `other`'s pointer. Self-assignment is a no-op (`this == &other` guard).

**Other public methods:**
- `~async_lock_guard() noexcept` — calls `mutex_->unlock()` if engaged.
- `[[nodiscard]] async_mutex* release() noexcept` — explicit early release; disengages and returns the back-pointer; subsequent destruction is a no-op.
- `[[nodiscard]] bool owns_lock() const noexcept` — returns `mutex_ != nullptr`.

---

### E4 — `fixpp::sync::detail::drain_latch_state`

**Source:** `[2f §3.1]` (RC-β row) + `[2f §4.7.2]` + `[2f §4.7.3]`.

**Role:** Lazy-constructed event-state object allocated as `std::shared_ptr<drain_latch_state>` inside `cancel_and_drain()`'s coroutine frame. Survives the reaper's coroutine frame destruction (shared_ptr lifetime). Not a by-value mutex member — the v1.2 by-value `detail::drain_latch` owning an `asio::steady_timer` was non-implementable (timer requires executor at construction; `async_mutex()` is `constexpr`) (RC-β / Opus C-R3-P1-4 / C-R3-P2-1 close).

**Members:**

| Field | Type | Role |
|---|---|---|
| `released_` | `std::atomic<bool>` | Set to `true` by `signal_release()` at the drain publication edge. Subscribers load this (acquire) after `wait()` returns to confirm a successful drain. |
| `aborted_` | `std::atomic<bool>` | NEW v1.4 — set to `true` by `signal_abort()` when the reaper is itself cancelled. Subscribers load this (acquire) after `wait()` to distinguish "aborted" from "released". |
| `in_flight_resumptions_` | `std::atomic<std::uint32_t>` | Count of scheduled-but-not-yet-completed reaper-resumption handlers. Incremented at every `schedule_resume_on_bound_executor(...)` call in step (f) of the reap_chain; decremented in the resumption-handler lambda. Lives on the state object (NOT on the reaper's stack) — closes Opus N-R3-P1-2 UAF. |
| channel | `asio::experimental::concurrent_channel<void()>` (or equivalent project-internal multi-waiter subscriber-list awaitable, per `[2f §4.7.3]` implementation pick) | Multi-waiter latch surface. `wait()` subscribes (parks the caller until release); `notify()` (called by `unlock()` and resumption handlers) sends to the channel; `signal_release()` + `signal_abort()` publish the terminal state. |

**Methods:**
- `asio::awaitable<void> wait()` — subscribes to the release/abort edge; parks the caller. Cancellation-slot integration per `[2f §4.7.3]`.
- `void notify()` — called by each holder's `unlock()` (observing `draining_ == true`) and each resumption handler; idempotent (channel `try_send`).
- `void signal_release()` — publishes the drain-complete edge (`released_.store(true, release)`); wakes all parked subscribers.
- `void signal_abort()` — NEW v1.4 — publishes the reaper-cancelled edge (`aborted_.store(true, release)`); wakes all parked subscribers with the abort outcome.

**Lifetime:** the reaper's `shared_ptr<drain_latch_state>` is stored atomically in `async_mutex::drain_latch_ptr_` for the duration of the drain epoch. In-flight resumption-handler lambdas capture the `shared_ptr` by value, keeping the state alive until each lambda fires. `drain_latch_ptr_` is cleared only after the terminal result is published via `signal_release()` or `signal_abort()`.

---

### E5 — `fixpp::sync::detail::slot_allocator`

**Source:** `[2f §4.3.4]` (RC-C close — Codex C-P2-6 / Opus N-P2-2), **superseded for the cancellation-slot closure by `[2f §4.3.4]` Erratum E-4 (v1.6)**.

> **E-4 (v1.6, source-verified non-implementable):** asio 1.36.0's `cancellation_slot::emplace`/`assign` has **no allocator-binding hook** (`prepare_memory()` → `thread_info_base::allocate(cancellation_signal_tag)`; per-thread recycling cache, not `bind_allocator`-aware). `slot_allocator` is therefore **not bound to the cancellation slot**. It is retained as the typed `Allocator`-shaped storage-policy wrapper for the allocation 2f *does* control — the **`waiter_record` fallback** (E2a). The cancellation-handler closure uses asio's per-thread recycler (zero global `new`/`delete` in steady state by construction; one-time per-thread first-touch is §6.4 bench-soft, like case 2). The three-case table below is re-anchored from "cancellation closure" to "`waiter_record` storage decision"; the awaiter's 32 B `slot_storage_` continues to hold only the asio **completion** handler per Erratum E-1.

**Role:** Project-internal `Allocator`-shaped storage-policy wrapper for the `waiter_record` fallback. Parameterised on `(async_mutex_awaiter* awaiter, std::pmr::memory_resource* mr)`. Declared in `fixpp::sync::detail`; unit-verified by §9 seam #21 in isolation.

**Three-case storage (exhaustive; post-E-4, re-anchored to `waiter_record`):**

| Case | Condition | Source | Notes |
|---|---|---|---|
| 1 — Embedded | `mr == nullptr` | per-mutex `waiter_pool_` slot (E-2; the `slot_allocator` inline-buffer arm models this) | Zero global-heap touch. Pool exhaustion → `trap_throw`/`bad_alloc` → `unexpected{sync_lock_alloc_failed}` (slot 44). `[const §VIII.5]`; §9 seam #10 verifies steady-state zero global `new` under `mallocnesia` (post per-thread warm-up). |
| 2 — N/A for the waiter record | (the `waiter_record` is never placed on the coroutine frame — it is pool- or `mr`-backed) | — | The HALO-not-firing branch does not exist for the `waiter_record`; coroutine-frame HALO is verified separately for the awaiter by §9 seam #9. |
| 3 — PMR fallback | `mr != nullptr` | `std::pmr::polymorphic_allocator<void>{mr}` forwarded directly | Zero global-heap. `mr->allocate()` failure → `trap_throw` → `unexpected{sync_lock_alloc_failed}`. |

---

### E6 — `fixpp::sync::completion_policy`

**Source:** `[2f §4.1]` (the `completion_policy` enum; NOT a class, NOT a plugin).

**Role:** Per-mutex completion policy enum. Governs the inline-vs-post behaviour of `unlock()`'s drain handoff.

**Members:**

| Enumerant | `uint8_t` | Behaviour |
|---|---|---|
| `dispatch` | 0 | ASIO `dispatch` semantics: if `executor.running_in_this_thread()` is true at unlock time, resume inline on the unlocking thread; otherwise post. Default; matches `[2d §7.4]` surface. |
| `post` | 1 | Always post the resumed coroutine through the bound executor; one executor hop per resumption regardless of caller thread. |

---

### E7 — `fixpp::session::async_lock_via_session_executor` (declaration only)

**Source:** `[2f §4.3.2]`.

**Role:** Thin session-layer helper. **Declared** by 2f; **implemented by the session-module spec, not this feature** (RC#2 layering fix — `core::async_mutex` carries zero dependency on `Session` / `EngineConfig`). Per design-doc §1.2 (line 94) / §4.3.2 (line 833) the session-module spec ships the implementation body; 2e is named only as 2f's *first consumer* and is NOT licensed to ship this body. Lives in `include/fixpp/session/async_lock_via_session_executor.hpp`, namespace `fixpp::session`, downstream of `core/` per `[arch §2.3]`.

**Declared signature:**

```cpp
[[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>>
    async_lock_via_session_executor(fixpp::sync::async_mutex& m) noexcept;
```

**Algorithm sketch (implementation responsibility of the session-module spec):**
1. `co_await asio::this_coro::executor` — recover the awaiter's bound executor.
2. Cast to `fixpp::core::session_executor` (per `[2d §4.8]`).
3. `session_executor::session_ptr()` → `Session*`.
4. `Session::session_arena()` (published by Appendix D §D.1 at sign-off) → `std::pmr::memory_resource*`.
5. `co_await m.async_lock(arena)` — forward the result.

If the bound executor is not a `session_executor` (bootstrap, listener, control-plane): return `unexpected{sync_lock_outside_session}`. No v1.0 hot path takes this branch.

**Layering note:** this file (`specs/006-async-mutex/contracts/async_lock_via_session_executor.hpp`) is a shape oracle (NOT the build header). The build header ships in `include/fixpp/session/` when the implementing spec lands.

---

## Memory-Ordering Invariant Table

Extracted from `[2f §6.2.2]`. Each row names the atomic operation, its ordering on success and failure, and the pairing rationale. ARM64 weak-memory model is the load-bearing target; x86 TSO would mask several of these.

| ID | Operation | Atomic | Success order | Failure order | Pairing / rationale |
|---|---|---|---|---|---|
| I-01 | Fast-path acquire CAS | `state_.compare_exchange_strong(not_locked → locked_no_waiters)` | `acquire` | `relaxed` | Acquire pairs with the prior holder's `unlock()` exchange release. Failure is a hint to enqueue. |
| I-02 | Initial head-load (contended push) | `state_.load()` | `acquire` | n/a | Pairs with prior `unlock()` exchange's release. |
| I-03 | LIFO push CAS | `state_.compare_exchange_weak(old → &record)` | `release` | `acquire` | **E-2:** pushes a `waiter_record*`. Release publishes `record->next_`/`phase_`/`attached_awaiter_`/`exec_storage_` writes to the unlocker. Failure-acquire sees the freshest head for retry. |
| I-04 | Unlock exchange | `state_.exchange(locked_no_waiters)` | `acq_rel` | n/a | Acquire pairs with each pushed waiter's release; release publishes the unlock-decision to the next acquirer. |
| I-05 | Empty-list close-out CAS | `state_.compare_exchange_strong(locked_no_waiters → not_locked)` | `acq_rel` | `acquire` | Failure-acquire: a new pusher arrived; read the new head. |
| I-06 | Per-waiter phase CAS (drain grants) | `record->phase_.compare_exchange_strong(queued → granted)` | `acq_rel` | `acquire` | **E-2: on `waiter_record::phase_`.** v1.4 CAS-then-publish: winner writes `record->result_` then schedules prompt bound-executor resumption. Loser performs no result write. Failure-acquire reads the cancel/reaper winner's update. |
| I-07 | Per-waiter phase CAS (cancel) | `record->phase_.compare_exchange_strong(queued → cancelled)` | `acq_rel` | `acquire` | **E-2: on `waiter_record::phase_`.** Same protocol as I-06 for the cancel winner; prompt resume is now UAF-safe because the node, not the frame, is stable. |
| I-08 | `await_resume` phase load | `record->phase_.load()` | `acquire` | n/a | **E-2: on `waiter_record::phase_`.** Pairs with the writer's release-CAS (I-06 or I-07). The resumed coroutine reads `record->result_` only after this acquire. |
| I-09 | `result_` slot publication | Non-atomic write by the CAS winner; sequenced AFTER the `phase_` release-CAS and BEFORE bound-executor resumption | n/a | n/a | **v1.4 CAS-then-publish.** No separate "ready publication" step — the per-waiter phase atom carries both the arbitration edge and the terminal-state signal. |
| I-10 | `next_drain_head_` push (residual splice from `unlock()`) | `next_drain_head_.compare_exchange_weak(nullptr → residual_head)` (or append-tail retry) | `release` | `acquire` | Release publishes residual chain's `next_` writes to the next unlocker. Failure-acquire reads freshest tail for append. |
| I-11 | `next_drain_head_` walk (drain start) | `next_drain_head_.exchange(nullptr)` | `acq_rel` | n/a | Acquire pairs with each pusher's release; release publishes the walker's atomic-take-ownership. |
| I-12 | `next_drain_head_` re-publish (after granted-waiter ownership transfer) | `next_drain_head_.compare_exchange_weak(nullptr → remaining_head)` (retry) | `release` | `acquire` | Same protocol as I-10; call site is `unlock()` step 1 (RC-A). |
| I-13 | `draining_` publish (set by `cancel_and_drain`) | `draining_.store(true)` after `drain_latch_ptr_.store(state)` | `release` | n/a | Release pairs with `await_ready`/`await_suspend` acquire-loads and second-call acquire-loads. v1.4: `drain_latch_ptr_` release-stored before this store. |
| I-14 | `draining_` load (in `await_suspend`) | `draining_.load()` | `acquire` | n/a | Pairs with `cancel_and_drain`'s I-13 release-store. |
| I-15 | `draining_` load (in `await_ready`, v1.2/v1.3 pre-CAS check) | `draining_.load()` | `acquire` | n/a | Same partner as I-14. Closes v1.1 fast-path bypass (Codex N-P1-1 round-2). |
| I-16 | `draining_` load (in `unlock`'s drain-aware short-circuit) | `draining_.load()` | `acquire` | n/a | Pairs with I-13. Under `draining_ == true`, unlocker does NOT splice into `next_drain_head_` — closes Opus C-R3-P1-3. |
| I-17 | `active_holders_count_` increment (winner-only post-CAS) | `active_holders_count_.fetch_add(1)` | `acq_rel` | n/a | Acquire pairs with prior holder's decrement; release publishes new holder's existence to `cancel_and_drain`'s wait loop. Performed at grant CAS-success ONLY (winner-only). |
| I-18 | `active_holders_count_` decrement (at `unlock()` entry) | `active_holders_count_.fetch_sub(1)` | `acq_rel` | n/a | Acquire pairs with increment at acquire time; release publishes holder exit to `cancel_and_drain`'s wait loop. |
| I-19 | `active_holders_count_` load (in `cancel_and_drain` wait loop) | `active_holders_count_.load()` | `acquire` | n/a | Pairs with I-18's release. Reaper waits for zero. |
| I-20 | `active_acquirers_count_` increment (in awaitable factory, before `await_ready`'s `draining_` load) | `active_acquirers_count_.fetch_add(1)` | `acq_rel` | n/a | Release publishes the in-flight acquirer's existence to `cancel_and_drain`'s wait loop. Sequenced-before the `draining_.load(acquire)` in `await_ready`. |
| I-21 | `active_acquirers_count_` decrement (fast-path success / drained-bypass / LIFO-enrol) | `active_acquirers_count_.fetch_sub(1)` | `acq_rel` | n/a | Three decrement-points (RC-α #1/#2/#3a/b/c per §4.2.1/§4.2.2). Release publishes the acquirer's transition to `cancel_and_drain`'s wait loop. |
| I-22 | `active_acquirers_count_` load (in `cancel_and_drain` wait loop) | `active_acquirers_count_.load()` | `acquire` | n/a | Pairs with I-21's release. Reaper waits for zero alongside I-19. Closes Opus C-R3-P1-2. |
| I-23 | `drain_latch_ptr_` store (by reaper before publishing `draining_`) | Atomic store of `shared_ptr<drain_latch_state>` | `release` | n/a | Release publishes the live state object's address to subscribers. Sequenced before I-13 (`draining_.store(true, release)`). |
| I-24 | `drain_latch_ptr_` load (by subscribers, by `unlock`'s notify path) | Atomic load of `shared_ptr<drain_latch_state>` | `acquire` | n/a | Pairs with I-23's release. Null after `draining_ == true` means the epoch already published and cleared the pointer. |
| I-25 | `drain_latch_state::released_` store (in `signal_release`) | `released_.store(true)` | `release` | n/a | Pairs with subscribers' `released_.load(acquire)` after `wait()` returns. |
| I-26 | `drain_latch_state::aborted_` store (in `signal_abort`) | `aborted_.store(true)` | `release` | n/a | NEW v1.4 — pairs with subscribers' `aborted_.load(acquire)` after `wait()` returns. |
| I-27 | `drain_latch_state::released_` / `aborted_` load (after `wait()` returns) | `released_.load()` / `aborted_.load()` | `acquire` | n/a | Disambiguates "released, drain succeeded" from "aborted, reaper cancelled". `released_ == false && aborted_ == true` → return `unexpected{sync_lock_aborted}`. |
| I-28 | `drain_latch_state::in_flight_resumptions_` increment (at `schedule_resume_on_bound_executor`) | `in_flight_resumptions_.fetch_add(1)` | `acq_rel` | n/a | Acquire pairs with prior resumption handler's decrement. Release publishes new in-flight resumption to the reaper's wait loop. |
| I-29 | `drain_latch_state::in_flight_resumptions_` decrement (in resumption-handler lambda) | `in_flight_resumptions_.fetch_sub(1)` | `acq_rel` | n/a | Acquire pairs with increment at scheduling time; release publishes completion to the reaper's wait loop. The last handler (observing `fetch_sub == 1`) calls `signal_release()` or `signal_abort()`. |
| I-30 | `drain_latch_state::in_flight_resumptions_` load (in `cancel_and_drain` wait loop) | `in_flight_resumptions_.load()` | `acquire` | n/a | Pairs with I-29's release. Reaper waits for zero alongside I-19 and I-22. |
| I-31 | `notify()` (channel `try_send`) | channel-side `try_send()` | relaxed (channel's internal sequencing is the synchronisation primitive) | n/a | Non-terminal wake. Receiver's `wait()` re-loads counter atomics with acquire ordering; `notify()` itself does not need to publish writes. |
| I-32 | `waiter_record` reclamation (E-2) | `record->refcount_.fetch_sub(1)` | `acq_rel` | n/a | Final drop (`== 1` observed) synchronises-with all prior ref-holding accesses and reclaims the node to `waiter_pool_`/`mr`. Single-shot; sound only under the single-drainer invariant (see E2a). Pairs with the +1 edges: creator, transferred in-lists membership, scheduled-resumer, `attached_awaiter_ != nullptr`. |

---

## State / Lifetime Model

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| Consumer's stack / value storage | consumer-controlled | `async_mutex` instance (by value at a stable address) | consumer's destructor (must follow successful `cancel_and_drain()`) |
| Caller's coroutine frame (HALO-friendly, embedded path) | caller coroutine's suspension | `async_mutex_awaiter` (≈ 96 B, ≤ 96 B budget) + `slot_storage_` (inline 32 B) | coroutine's own frame destruction on `await_resume` return |
| Caller-supplied `mr` (PMR fallback path) | caller-determined | `async_mutex_awaiter` (type-erased completion handlers; `any_completion_handler` callsites) | `await_resume` return → de-allocate back to `mr` |
| `SessionConfig::session_arena` (recovered via `Session::session_arena()` per Appendix D §D.1) | session lifetime | `async_mutex_awaiter` when the session-side helper (`E7`) supplies the resource | session destruction |
| Heap (via `std::make_shared`) | drain epoch (shared_ptr lifetime across reaper frame + captured lambdas) | `detail::drain_latch_state` (E4): `released_`, `aborted_`, `in_flight_resumptions_`, channel | last `shared_ptr` destructor after all resumption handlers fire |

**Lifetime classes for non-arena objects:**
- `async_mutex` (E1) — consumer-controlled; non-copyable, non-movable; `std::terminate()` precondition on destruction.
- `async_lock_guard` (E3) — flyweight; lifetime bounded by the originating mutex; movable with destructive move-assign; `[[clang::lifetimebound]]` on the engaged constructor.
- `async_mutex_awaiter` (E2) — embedded or PMR-allocated per the RC#2 / RC-C rules above.
- `drain_latch_state` (E4) — heap-allocated via `shared_ptr`; lazy; epoch-scoped.

---

## Error Slot Allocation

Occupancy verified against `include/fixpp/core/error.hpp` on branch `006-async-mutex` (2026-05-18). Last occupied slot: `wire_unexpected_tag = 42`. **First free slot: 43.**

Planned non-renumbering additions per `[const §X.4]`:

| Slot | Variant | Source section | Remediation class | C-ABI group (2i) |
|---|---|---|---|---|
| 43 | `sync_lock_aborted` | `[2f §4.5]` / `[2f §6.5]` — cancellation won the §4.5.1 CAS-arbitration race; waiter was not granted ownership. | Cancellation outcome — joins `[2d §6.7]` `dispatch_aborted` / `clock_sleeps_cancelled` / `store_cancelled` in `FIXPP_ERR_CANCELLED`. FSM treats this distinct from runtime errors: no state change. | `FIXPP_ERR_CANCELLED` |
| 44 | `sync_lock_alloc_failed` | `[2f §4.3]` / `[2f §6.5]` — PMR fallback's `allocate(...)` threw `std::bad_alloc` (caller-supplied `mr` exhausted) or embedded path's inline 32-byte buffer overflowed and `null_memory_resource()` rejected the allocation. Routed via `trap_throw` per `[2a §4.2]`. | Configuration / capacity error — operator raises the resource cap (PMR) or the algorithm fix-forward updates the inline buffer size (embedded). Mutex is unaffected. | `FIXPP_ERR_SYNC_RUNTIME` |
| 45 | `sync_lock_outside_session` | `[2f §4.3.2]` / `[2f §6.5]` — session-side helper `async_lock_via_session_executor` was called outside any session serialisation domain (bound executor is not a `session_executor` value). | Caller-error class — use the explicit `async_lock(mr)` overload directly. No v1.0 hot path takes this branch. | `FIXPP_ERR_SYNC_RUNTIME` |
| 46 | `sync_lock_drained` | `[2f §4.7.2]` / `[2f §6.5]` NEW v1.1 RC-B — `cancel_and_drain()` set `draining_ = true`; subsequent `async_lock(...)` callers observe the flag and fast-fail without enqueuing. | Lifecycle / shutdown class — the consumer's graceful close has progressed past the drain phase; the caller should bubble the failure through its own shutdown path. The mutex no longer accepts new acquisitions; `~async_mutex()` is safe to call. | `FIXPP_ERR_SYNC_RUNTIME` |

**Non-renumbering invariant:** once published, slots 43–46 are NEVER renumbered per `[const §X.4]`. Any future slot additions start at 47 or higher.

**2f does NOT introduce** an `async_mutex_destroyed_with_waiters` variant — the destructor fires `std::terminate()`, not an `expected_t` form (RC#3 fix).

**C-ABI mapping** (delegated to **2i**, not assigned here): `sync_lock_aborted` → `FIXPP_ERR_CANCELLED`; `sync_lock_alloc_failed` / `sync_lock_outside_session` / `sync_lock_drained` → `FIXPP_ERR_SYNC_RUNTIME`.
