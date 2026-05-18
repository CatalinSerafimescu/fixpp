# Feature Specification: Awaitable Mutex `fixpp::sync::async_mutex`

**Feature Branch**: `006-async-mutex`
**Created**: 2026-05-18
**Status**: Draft
**Input**: User description: "Implement the awaitable mutex `fixpp::sync::async_mutex` as a Phase-4 feature, realizing the signed-off Phase-2 design doc `.specify/2f-async-mutex.md` v1.5 as shipped code. First of three prerequisites (2f → 2d → 2e) the deferred `005-session-establishment-fsm` depends on."

> **Authority anchor:** This feature realizes the **signed-off Phase-2 design doc `.specify/2f-async-mutex.md` v1.5** (Gate-A-converged through v1.5) as shipped code. Where this spec and the design doc disagree, **the design doc wins; an inconsistency is a defect in this spec.** This is the first of three prerequisite features (`2f-async-mutex` → `2d-threading` → `2e-msgstore`) that the deferred `005-session-establishment-fsm` consumes; 2f is the lowest-level standalone concurrency primitive (it has no upstream code dependency beyond the merged 001–004 `core`/ASIO baseline). Catalogue row owned: **NFR-016** (NEW) — Awaitable mutex `fixpp::sync::async_mutex` (design doc Appendix A / §11; the row is added to `spec/feature-catalogue.md` + `spec/coverage-index.md` at sign-off).

## Normative References

Per `[const §VI.5]`: the design doc records (Appendix B, mirroring `architecture.md` Appendix B and `[2d Appendix B] §B.2`) that **2f's primary drivers are engineering judgment, not a FIX specification section** — **no `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies** to this feature. The governing sources are:

- `[const §XI.3]` Concurrency & Coroutines — *awaitable mutex required in coroutine context*; `fixpp::sync::async_mutex` is the only allowed mutex shape; **direct mandate** for this feature.
- `[const §XV.9]` Banned Patterns — plain `std::mutex` banned in any header that includes `asio::awaitable<...>`; the CI enforcement mechanism is 2f's deliverable.
- `[const §XI.5]` Hot-path lock policy — the store-write path always uses a mutex regardless of `SessionConfig::lock_policy`, binding 2f to the hot-path zero-allocation discipline on the contended path.
- `[const §XI.6]` HALO-first frame allocation — the awaiter is HALO-eligible by construction; PMR fallback via the explicit `mr` parameter.
- `[const §VIII.5]` Allocator policy — zero global `new`/`delete` on the v1.0 hot path.
- `[const §XIV.2]` Pluggable Interfaces — `async_mutex` is **NOT** a plugin (no virtual surface); the ≤5 pure-virtual rule does not bind.
- `[SYN §3.2 Q6a]` Cancellation propagation (DECIDED — ASIO native cancellation slots end-to-end; honour `cancellation_type::total`).
- `[SYN §3.2 Q6b]` Awaitable mutex (DECIDED — own implementation in `fixpp::sync`, BSL-1.0 algorithm attribution to `avast/asio-mutex` / cppcoro / Lewis-Baker); the **six-item design list** is the operating spec.
- `[arch §3]`/`[arch §4.1]`/`[arch §2.3]` — `fixpp::sync` lives physically under `core/`; class header `include/fixpp/core/sync/async_mutex.hpp`; the session-side helper lives downstream in `session/`.
- `[2d §7.4]` Locked contract surface — the executor-compat contract 2f must satisfy (completion on the awaiter's bound executor; `cancellation_type::total`; `dispatch` default / `post` override). `[2d §4.8]` `session_executor` wrapper, `[2d §6.7]` `FIXPP_ERR_*` coalescing precedent, `[2d §4.5]`/`[2d §4.7]` consumed contracts (recorded, not re-litigated — 2d ships as the *next* prerequisite).
- `[2e §3.1]`/`[2e §6.4]` — 2e's `MemoryStore` writer-mutex contract; **2f sign-off is the named hard hand-off gate for 2e**.
- `[2a §4.2]` `trap_throw`, `[2b §6.4]` flyweight lifetime contract, per-doc-prefix discipline `[2a/2b/2c/2d/2e §6.7]` → `FIXPP_ERR_SYNC_*`.

## Clarifications

*None required at `/speckit-specify` time.* The design doc is signed-off and Gate-A-converged through v1.5; all decisions (the six-item design list, the destructive-move guard, the `std::terminate()`-precondition + `cancel_and_drain()` shutdown shape, the explicit-`mr` PMR fallback, the cancellation CAS-arbitration contract, the memory-ordering specification) are fixed there. `/speckit-clarify` is still run per the pipeline; any residual ambiguity is recorded there.

### Session 2026-05-18 (Gate A round 1)

- Q: Is `try_lock()` part of `async_mutex`'s public surface? → A: **No.** Per design doc §4.1 (v1.1 / Opus N-P3-1 close) `try_lock()` was relocated to `detail::` and is `detail::`/friend-only (test-fixture friend access for §9 seam #20 only). The design-doc §1.2 OWNS-list still names "`try_lock()` semantics" as a 2f-owned concern, but that ownership is at the `detail::`-internal level, not a public method — §4.1 is authoritative on the surface. FR-001 and the FR-015 out-of-scope list are corrected to reflect this; the only legal guard-construction path is `co_await m.async_lock()` (a public `try_lock()` + adopt-locked-guard ctor reintroduces the signed-off-closed same-mutex aliasing bug).

## User Scenarios & Testing *(mandatory)*

The "users" of this feature are **downstream library code that needs mutual exclusion inside a coroutine**: the `2e` `MessageStore` per-instance writer mutex (`[2e §6.4]` — 2f sign-off is its hand-off gate), the Phase-4 session-module seqnum counter (`005`), pinset rotation (`2g`), and any future in-coroutine critical section. None of these consumers may use `std::mutex` (`[const §XV.9]`); `fixpp::sync::async_mutex` is the only legal shape (`[const §XI.3]`).

### User Story 1 - Acquire and release exclusive ownership inside a coroutine (Priority: P1)

A coroutine `co_await`s `async_lock()` on a shared `async_mutex`, receives a RAII `async_lock_guard` on success, runs its critical section, and releases ownership when the guard is destroyed (or via explicit `release()`). An uncontended acquire takes the single-CAS fast path; a contended acquire suspends the coroutine until the holder releases, then resumes it on the waiter's bound executor with FIFO fairness within the drain cycle.

**Why this priority**: This is the single critical-path capability — without correct acquire/release the primitive is useless and every downstream consumer (`2e`, `005`, `2g`) is blocked. `[2e §6.4]` writer-mutex and `[const §XI.3]` cannot be satisfied without it.

**Independent Test**: Drive two coroutines on one strand against a shared `async_mutex` through a test executor: assert the uncontended acquirer gets the guard on the fast path with no suspension; assert a second acquirer suspends while the first holds, then resumes (on its bound executor) exactly when the first guard is destroyed; assert mutual exclusion never overlaps; assert FIFO ordering across a multi-waiter drain cycle.

**Acceptance Scenarios**:

1. **Given** an unlocked `async_mutex`, **When** a coroutine `co_await`s `async_lock()`, **Then** it takes the uncontended fast path, no awaiter is suspended, and a valid `async_lock_guard` bound to the mutex is returned.
2. **Given** a held `async_mutex`, **When** a second coroutine `co_await`s `async_lock()`, **Then** it suspends without busy-waiting and is not granted ownership until the holder's guard is destroyed/released.
3. **Given** N coroutines queued on a held mutex, **When** the holder releases, **Then** the waiters are granted ownership one at a time with FIFO fairness within the drain cycle and each resumes on its own bound executor under the mutex's completion policy (`dispatch` default, `post` per-mutex opt-in).
4. **Given** an `async_lock_guard`, **When** it is move-assigned into, **Then** the prior contents are released first (destructive move-assignment) so ownership is never duplicated or leaked.

---

### User Story 2 - Cancel a pending acquire deterministically (Priority: P1)

A coroutine waiting on `async_lock()` has its acquire cancelled through an ASIO native cancellation slot (`cancellation_type::total`). The waiter is removed from the queue and the acquire completes with the cancellation outcome — never with stale ownership, never with a lost wakeup, never with undefined behaviour — even when the cancellation races the holder's release/drain.

**Why this priority**: `[2d §7.4]` and `[SYN §3.2 Q6a]` mandate working cancellation; the store-write path has unbounded wait (no spin fallback per `[const §XI.5]`/§2), so a non-cancellable acquire is unusable. The cancellation-vs-drain CAS-arbitration race is the design's hardest correctness obligation (design doc §4.5, RC#1/RC#4).

**Independent Test**: Suspend a waiter, signal `cancellation_type::total`, assert the acquire completes with `sync_lock_aborted` and the waiter is removed; then run the adversarial seam — fire the cancellation concurrently with the holder's release so cancel and drain contend for the same waiter, and assert exactly one of {granted, cancelled} wins per waiter with no double-resume and no lost waiter (TSan-clean).

**Acceptance Scenarios**:

1. **Given** a suspended waiter, **When** `cancellation_type::total` is signalled, **Then** the acquire completes with `sync_lock_aborted`, the waiter is removed from the queue, and no ownership is transferred to it.
2. **Given** a suspended waiter, **When** the holder's release-drain and the waiter's cancellation occur concurrently, **Then** exactly one of "granted ownership" or "cancelled (`sync_lock_aborted`)" is observed for that waiter, the coroutine is resumed exactly once, and no other waiter is lost or double-resumed.
3. **Given** a cancelled acquire, **When** the consumer's FSM inspects the outcome, **Then** `sync_lock_aborted` is distinguishable as a cancellation (no state change) rather than a runtime error.

---

### User Story 3 - Shut down safely: drain waiters, never silently corrupt (Priority: P2)

A consumer winding down (e.g. `2e` graceful close) calls `cancel_and_drain()` on its `async_mutex` before destroying its owner: every in-flight waiter is completed (granted as the lock frees, or aborted), new acquirers fast-fail with `sync_lock_drained`, and the call does not return until all holders, in-flight acquirers, and in-flight resumptions have quiesced. Destroying an `async_mutex` that still has waiters is a hard precondition violation that fires `std::terminate()` — never silent use-after-free.

**Why this priority**: Correctness of teardown is required before any consumer (`2e`/`005`) can compose 2f into a session lifecycle, but it is only exercised after the basic acquire/cancel paths (US1/US2) work. `[2d §10 Q3]` engine-shutdown ordering composes with this.

**Independent Test**: Queue N waiters, call `cancel_and_drain()`, assert every waiter completes (granted-or-aborted), assert post-drain `async_lock()` returns `sync_lock_drained` without enqueuing, and assert the call only returns once holder/acquirer/resumption counts reach zero. Separately, a death test: destroy a mutex with a live waiter and assert `std::terminate()` fires (no `expected_t` form, no UB).

**Acceptance Scenarios**:

1. **Given** N suspended waiters, **When** `cancel_and_drain()` is awaited, **Then** every waiter is completed exactly once and the call returns only after all holders, in-flight acquirers, and in-flight resumptions have quiesced.
2. **Given** a mutex on which `cancel_and_drain()` has set the drain flag, **When** a new coroutine `co_await`s `async_lock()`, **Then** it fast-fails with `sync_lock_drained` without enqueuing.
3. **Given** an `async_mutex` with at least one live waiter, **When** its destructor runs, **Then** `std::terminate()` is invoked (documented hard precondition) rather than returning an error or causing undefined behaviour.
4. **Given** `cancel_and_drain()` is invoked concurrently more than once, **When** the calls race, **Then** the drain epoch is serialised and each caller observes a consistent released-or-aborted outcome.

---

### User Story 4 - Zero global allocation on the hot path; PMR fallback when needed (Priority: P2)

On the v1.0 hot path the waiter is embedded in the awaiter object (HALO-eligible), so a contended acquire performs **zero global `new`/`delete`**; the cancellation-slot handler storage draws from the awaiter's inline buffer. When a caller cannot satisfy embedded storage, an explicit `async_lock(std::pmr::memory_resource* mr)` overload routes type-erased completion/handler storage to the caller-supplied resource — `core` never reaches into `session/` or an engine handle for memory.

**Why this priority**: `[const §VIII.5]`/`[const §XI.5]` make zero-hot-path-alloc a hard discipline because the store-write path sits on the outbound dispatch chain; required before merge but verified after the functional paths exist.

**Independent Test**: Run contended acquire/release/cancel/drain under an allocation-counting harness and assert zero global `new`/`delete` on the embedded path; then exercise the explicit-`mr` overload with an instrumented `memory_resource` and assert all fallback allocations hit the supplied resource and none hit the global heap; assert PMR exhaustion surfaces `sync_lock_alloc_failed` (trapped, not `std::terminate`).

**Acceptance Scenarios**:

1. **Given** the embedded-waiter path with HALO firing, **When** a contended acquire/cancel/drain cycle runs, **Then** the allocation-counting harness reports zero global `new`/`delete`.
2. **Given** the explicit `async_lock(mr)` overload, **When** the fallback path needs storage, **Then** every allocation is drawn from the caller-supplied `mr` and none from the global heap.
3. **Given** a caller-supplied `mr` that is exhausted, **When** the fallback allocation is attempted, **Then** the acquire completes with `sync_lock_alloc_failed` (trapped via `trap_throw`), the mutex state is unaffected, and the process does not terminate.

---

### User Story 5 - CI rejects `std::mutex` in coroutine context (Priority: P3)

The build/CI enforces `[const §XV.9]`: any header that includes `asio::awaitable<...>` and also names `std::mutex` (or `std::recursive_mutex`, etc.) fails the gate, pointing the contributor at `fixpp::sync::async_mutex`. This is the project-wide guarantee that 2f is actually the only mutex shape in coroutine context.

**Why this priority**: The enforcement is the constitutional point of the feature but is mechanically simple and independent of the runtime primitive; lowest priority because the primitive's correctness (US1–US4) is what unblocks consumers.

**Independent Test**: Run the gate against a labelled corpus — fixtures that legitimately use `async_mutex` in awaitable headers (must pass) and fixtures that use `std::mutex` in an `asio::awaitable`-including header (must fail with a message naming `async_mutex`); assert zero false negatives and zero false positives on the corpus.

**Acceptance Scenarios**:

1. **Given** a header that includes `asio::awaitable<...>` and uses `std::mutex`, **When** the CI gate runs, **Then** the build fails and the diagnostic names `fixpp::sync::async_mutex` as the required shape.
2. **Given** a header that includes `asio::awaitable<...>` and uses `fixpp::sync::async_mutex`, **When** the CI gate runs, **Then** it passes.

---

### Edge Cases

- **Destructor with a live holder or live waiters** → `std::terminate()` (documented hard precondition, RC#3) — never an `expected_t`, never UB.
- **Cancellation races the release-drain for the same waiter** → CAS-arbitration (§4.5) yields exactly one of {granted, cancelled}; no double-resume, no lost waiter.
- **In-flight acquirer during `cancel_and_drain()`** (between the drain-flag load and the fast-path CAS) → covered by the acquirer epoch counter; the drain does not return until that window closes.
- **`async_lock()` after the drain flag is set** → fast-fail `sync_lock_drained`, no enqueue.
- **Concurrent `cancel_and_drain()` calls** → serialised drain epoch; consistent released-or-aborted result to every caller.
- **Caller-supplied PMR exhausted** → `sync_lock_alloc_failed` (trapped), mutex unaffected.
- **Session-side helper used outside a session serialisation domain** (bound executor is not a `session_executor`) → `sync_lock_outside_session`; caller should use the explicit `async_lock(mr)` overload.
- **ARM64 / weak memory** → the published memory-ordering specification (acquire/release pairings + `static_assert`s) guarantees correctness; verified under a weak-memory seam.
- **Cross-strand resume** (waiter's bound executor ≠ unlocker's) → completes via `post`; long-tail latency is bench-harness-soft, not auto-fail.
- **Recursive acquire by the current holder** → unsupported by construction (not a recursive mutex, §2); the discipline forbids it (no carve-out per `[const §XV.9]`).
- **Cross-domain pathological contention** (O(N) acquirers) → correct but defence-in-depth; v1.0 callsites are bounded by single-session-serialisation discipline (O(1) steady state); no N=10⁶ seam required for v1.0 sign-off.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a value-typed, non-virtual, non-movable `fixpp::sync::async_mutex` at `include/fixpp/core/sync/async_mutex.hpp` (namespace `fixpp::sync`, physically under `core/` per `[arch §3]`), `constexpr`-default-constructible, exposing the public surface `async_lock()`, `unlock()`, `cancel_and_drain()`, and `policy()` per design doc §4.1. **`try_lock()` is NOT part of the public surface**: per design doc §4.1 (v1.1 / Opus N-P3-1 close) `try_lock()` was moved to `detail::` and is `detail::`/friend-only — the v1.0 public `try_lock()` + adopt-locked-guard ctor admitted a same-mutex aliasing bug, so the only legal guard-construction path is the awaitable form (`co_await m.async_lock()`). The §1.2 OWNS-list bullet "`try_lock()` semantics" is owned at the `detail::`-only level (it survives only for test-fixture friend access per §9 seam #20), NOT as a public method.
- **FR-002**: `async_lock()` MUST grant exclusive ownership via a single-CAS uncontended fast path and, when contended, suspend the caller (no spin — `[const §XI.5]`/§2) until ownership is available, then complete on the **awaiter's bound executor** (the `session_executor` wrapper for in-session callers per `[2d §4.8]`) per the `[2d §7.4]` executor-compat contract.
- **FR-003**: On success `async_lock()` MUST yield a `fixpp::sync::async_lock_guard` (same header) — a RAII flyweight back-pointing to the mutex, carrying `[[clang::lifetimebound]]` on its constructor, with **destructive move-assignment** (release-then-take-ownership, RC#1/N-P1-3) and explicit `release()`.
- **FR-004**: Releasing ownership (guard destruction/`release()` → `unlock()`) MUST grant the next waiter with **FIFO fairness within a drain cycle** (LIFO list reversed on `unlock`), resuming each waiter under the per-mutex completion policy: **`dispatch` by default, `post` per-mutex opt-in**; the inline-vs-post predicate is ASIO `dispatch` semantics on the bound executor (`running_in_this_thread()` true → inline, false → `post`).
- **FR-005**: `async_lock()` MUST honour ASIO native cancellation slots: a `cancellation_type::total` signal MUST remove the waiter and complete the acquire with `expected_t::unexpected{sync_lock_aborted}` at the 2f C++ boundary, with no ownership transfer and no lost wakeup, including when the cancellation races the release-drain for the same waiter (the §4.5 CAS-arbitration protocol: exactly one of {granted, cancelled} per waiter).
- **FR-006**: The system MUST provide `cancel_and_drain()` (awaitable): it sets the drain flag, completes every in-flight waiter exactly once (granted-as-freed or aborted), and does not return until active holders, in-flight acquirers, and in-flight resumptions have all quiesced; concurrent invocations MUST be serialised into one drain epoch with a consistent released-or-aborted result to every caller.
- **FR-007**: After the drain flag is set, new `async_lock()` calls MUST fast-fail with `sync_lock_drained` without enqueuing.
- **FR-008**: Destroying an `async_mutex` that still has **a live holder or any waiters** MUST invoke `std::terminate()` (documented hard precondition, RC#3; design doc §4.7 — both debug AND release; the destructor checks `state_`/`next_drain_head_` per `tasks.md` T050) — the system MUST NOT introduce an `async_mutex_destroyed_with_waiters` `expected_t` variant and MUST NOT exhibit use-after-free. (US3.3's acceptance scenario exercises the live-waiter case; it is a non-exhaustive instance of this requirement, not its full scope.)
- **FR-009**: The embedded-waiter (HALO-eligible) path MUST perform **zero global `new`/`delete`** on the v1.0 hot path (acquire/contend/cancel/drain), including the cancellation-slot handler storage (drawn from the awaiter's inline buffer via the project-internal `detail::slot_allocator`).
- **FR-010**: The system MUST provide an explicit `async_lock(std::pmr::memory_resource* mr)` overload that routes type-erased completion/handler storage to the **caller-supplied** resource; `core::async_mutex` MUST NOT reach into `session/` or an engine handle for memory. PMR exhaustion MUST surface `sync_lock_alloc_failed` trapped via `trap_throw` (`[2a §4.2]`), leaving the mutex state unaffected (no `std::terminate`).
- **FR-011**: The system MUST **declare** the session-side helper `fixpp::session::async_lock_via_session_executor(async_mutex&)` at `include/fixpp/session/async_lock_via_session_executor.hpp` (namespace `fixpp::session`, downstream of `core/` per `[arch §2.3]`). 2f owns the declaration/contract only; calling it outside a session serialisation domain MUST surface `sync_lock_outside_session`. **The implementation is delivered by the session-module spec, not this feature** (RC#2 layering boundary).
- **FR-012**: All failure outcomes MUST be reported through `expected_t<T>` and the `fixpp::core::error` enum with exactly these four `2f`-introduced variants: `sync_lock_aborted` (cancellation), `sync_lock_alloc_failed`, `sync_lock_outside_session`, `sync_lock_drained`. No new C-ABI surface is added by this feature (2f is C++-only); the `FIXPP_ERR_SYNC_*` C-ABI coalescing target is documented for the 2i owner (`sync_lock_aborted` → `FIXPP_ERR_CANCELLED` group; the rest → `FIXPP_ERR_SYNC_RUNTIME`).
- **FR-013**: The system MUST publish the **memory-ordering specification** (the design doc §6.2 acquire/release pairing sub-table) as enforced `static_assert`s on awaiter alignment, pointer round-trip, and atomic lock-freedom, sufficient for ARM64-correct (weak-memory) execution.
- **FR-014**: The system MUST provide the CI enforcement of `[const §XV.9]`: a gate (grep gate `tools/check_no_std_mutex_in_awaitable_headers.sh`, **created and owned by this feature** — `tasks.md` T015; the same-path `005` Phase-1 scaffold lives only on the unmerged `005` branch and is **not** inherited on this feature's `main`-based branch base, see Assumptions) that, when any header which (transitively, post-preprocessing `-E` scope) includes `asio::awaitable<...>` names any of `std::mutex`, `std::recursive_mutex`, `std::timed_mutex`, `std::recursive_timed_mutex`, `std::shared_mutex`, or `std::shared_timed_mutex`, fails the build with a diagnostic pointing at `fixpp::sync::async_mutex`. The banned set is exactly those six `std::`-qualified spellings; a user `using`/`typedef` alias that renames one of them is outside the grep gate's mechanical scope and remains a `[const §XV.9]` discipline matter (recorded limitation, not a false-negative against SC-006's labelled corpus).
- **FR-015**: The following are explicitly **out of scope** and MUST NOT be partially implemented in a way that implies support (design doc §2): `async_shared_mutex` / RW-mutex, `async_recursive_mutex`, `async_timed_mutex`, fairness modes beyond LIFO-pop+FIFO-drain, a pluggable allocator beyond the explicit-`mr` parameter, any spinning fallback, a `co_await std::mutex` adapter, a **public `try_lock()`** (relocated to `detail::`/friend-only in design doc §4.1 — a public form is the signed-off-closed same-mutex aliasing bug, FR-001), and any user-derived-type extension `concept`/CRTP (`async_mutex` is a closed non-plugin type per `[const §XIV.2]`/`[arch §6]`). The **consuming sites** (`2e` writer mutex `[2e §6.4]`, the `005` seqnum counter, `2g` pinset rotation) and the `[2d §6.5]` `cancellable_dispatch` / `[2d §4.8]` `session_executor` shapes are owned elsewhere — consumed/recorded, not implemented here.

### Key Entities

- **`async_mutex`**: the value-typed, non-movable, `constexpr`-default-constructible exclusive mutex; owns the atomic LIFO/FIFO state, the drain-epoch lifecycle state, and the completion policy. ~52 B raw / 56 B padded (one cache line). Not a pluggable interface.
- **`async_lock_guard`**: RAII flyweight bound to a mutex's lifetime (`[[clang::lifetimebound]]`), destructive move-assignment, explicit `release()`. Owning ⇒ exclusive ownership; destruction ⇒ `unlock()`.
- **Awaiter (`detail::async_mutex_awaiter`)**: the per-waiter object embedded in the caller's coroutine frame (HALO-eligible, ≤96 B), carrying the per-waiter atomic phase machine (`{queued, granted, cancelled}`), the intrusive LIFO/FIFO link, the cancellation slot, and the inline slot-handler buffer.
- **`completion_policy`**: per-mutex `dispatch` (default) | `post` selector for waiter resumption.
- **Drain-epoch state (`detail::drain_latch_state`)**: project-internal lazily-allocated state coordinating `cancel_and_drain()` (released/aborted signalling, in-flight-resumption accounting); null when no drain is in flight.
- **`detail::slot_allocator`**: project-internal allocator feeding the embedded inline buffer (or the explicit `mr`) so the cancellation handler never touches the global heap on the hot path.
- **Session-side helper (declared only here)**: `fixpp::session::async_lock_via_session_executor` — contract/declaration owned by 2f, implementation owned by the session-module spec.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In 100% of uncontended/contended acquire-release runs across a stress corpus, exclusive ownership is mutually exclusive (zero observed overlap) and every contended waiter is eventually granted with FIFO fairness within its drain cycle (zero starvation, zero lost waiter).
- **SC-002**: In 100% of cancellation runs — including the adversarial cancel-vs-drain concurrent seam — each waiter observes exactly one of {granted, cancelled(`sync_lock_aborted`)}, is resumed exactly once, and no other waiter is lost or double-resumed; clean under TSan and ASan.
- **SC-003**: `cancel_and_drain()` completes every in-flight waiter exactly once and returns only after holder/acquirer/resumption counts reach zero in 100% of drain runs; post-drain `async_lock()` returns `sync_lock_drained` with zero enqueues; the destroy-with-waiters death test fires `std::terminate()` in 100% of runs.
- **SC-004**: An allocation-counting harness reports **zero** global `new`/`delete` on the embedded-waiter hot path (acquire/contend/cancel/drain); with the explicit-`mr` overload, 100% of fallback allocations hit the supplied resource and zero hit the global heap; PMR exhaustion yields `sync_lock_alloc_failed` (no termination) in 100% of cases.
- **SC-005**: Latency Tier-1 ceilings hold (Linux/Clang/x86_64, warm cache; CI fails on >5% regression vs the previous tagged release): uncontended `async_lock` ≤ 25 ns, contended-suspend ≤ 80 ns, uncontended `unlock` ≤ 15 ns; the long-tail drain/cross-strand rows are tracked bench-harness-soft per the design doc §6.3.
- **SC-006**: The `[const §XV.9]` CI gate flags `std::mutex` in `asio::awaitable`-including headers with **zero false negatives and zero false positives** on the labelled corpus, and the diagnostic names `fixpp::sync::async_mutex`.
- **SC-007**: The memory-ordering `static_assert`s compile on every Tier-1 toolchain and the weak-memory (ARM64-modelled) seam passes, demonstrating the acquire/release pairing specification is correctly realized.
- **SC-008**: A consumer compile/link check confirms the shipped `<fixpp/core/sync/async_mutex.hpp>` satisfies the `[2e §6.4]` writer-mutex contract shape and the declared session-side-helper surface — i.e. `2e`/`005`/`2g` can build against 2f (2f sign-off is the `[2e §3.1]` hand-off gate).
- **SC-009**: The bundle is clean under the merged 001–004 sanitizer/static-analysis matrix (ASan+UBSan on all tests; **TSan** on the concurrency seams; clang-tidy/clang-format/cppcheck/IWYU; the `[const §XV.9]` grep gate) and meets the `[const §IX.1]` coverage floor (≥95% line / ≥85% branch on the touched `core/sync` (+ `session/` helper) modules — i.e. `include/fixpp/core/sync/async_mutex.hpp` (and `src/core/sync/async_mutex.cpp` if out-of-line) plus the declared `include/fixpp/session/async_lock_via_session_executor.hpp`, lcov DA/BRDA basis; the session helper is declaration-only here so its measured DA/BRDA contribution is nil and a zero-line header is not flagged uncovered — scope statement matches `plan.md` `[const §IX.1]` row and `quickstart.md` §8/verify-step-2 verbatim).
- **SC-010**: The **NFR-016** catalogue row is added to `spec/feature-catalogue.md` and `spec/coverage-index.md` at sign-off (design doc §11/Appendix A); no other A-/W-/D-/S- catalogue row is owned or silently touched.

## Assumptions

- This feature realizes the **signed-off, Gate-A-converged design doc `.specify/2f-async-mutex.md` v1.5** (Goals §1, Scope §1.2, API §4, NFR §6, Test seams §9, Appendix A/D). Design decisions locked there (the six-item list, atomic algorithm, memory-ordering table, CAS-arbitration cancellation contract, destructive-move guard, `std::terminate()`-precondition + `cancel_and_drain()` shutdown shape, explicit-`mr` PMR fallback) are **decided and not re-opened**; the design doc wins on any conflict.
- The merged **001–004** baseline provides the C++23 + ASIO (coroutines, `asio::awaitable`, native cancellation slots), `core::expected_t`, `core::error`, `trap_throw` (`[2a §4.2]`), and PMR plumbing 2f composes over; **no new external/Conan dependency** is introduced. BSL-1.0 algorithm attribution to `avast/asio-mutex` / cppcoro / Lewis-Baker is vendored-attribution only (no vendored code dependency).
- `2d-threading` and `2e-msgstore` are the **next two prerequisites** and are NOT yet shipped; 2f consumes only the *recorded contracts* it needs from `[2d §7.4]`/`[2d §4.8]` (executor-compat surface, `session_executor` shape) at the **declaration/contract** level. 2f does not depend on 2d/2e *code*; it is the lowest-level primitive and is built first. The session-side helper is **declared** here and **implemented** by the later session-module spec (RC#2 layering boundary).
- `async_mutex` is **NOT** a pluggable interface (no virtual surface user code subclasses); the `[const §XIV.2]` ≤5-pure-virtual rule and `[arch §6]` plugin pattern do not bind (recorded once per design doc §3).
- 2f is **C++-only**: no `extern "C"` symbol, no C-ABI shape; the `FIXPP_ERR_SYNC_*` coalescing is documented for the 2i owner and is not delivered here. `[const §IX.5]` abidiff is **N/A** for this feature (no C-ABI surface), recorded for explicit non-applicability.
- **No `[FIX-SL]`/`[FIXT]`/`[FIXS]` normative reference applies** — 2f is engineering-judgment-driven (design doc Appendix B, mirroring `architecture.md`/`[2d Appendix B]`).
- The `[const §XV.9]` grep gate `tools/check_no_std_mutex_in_awaitable_headers.sh` is **created from scratch and owned by this feature** (corpus, diagnostics, CI wiring — `tasks.md` T015 creates it, FR-014 finalizes it). A same-path Phase-1 scaffold exists only on the **unmerged `005` branch** (commit `d6a17b7`); `005` is a deferred feature whose code is not on `main`, so the script is **absent on this feature's `main`-based branch base** and is NOT inherited (verified 2026-05-18: absent on `main` and on `006-async-mutex`; present only on the `005` branch). The earlier "scaffolded by 005 Phase 1, committed on the 005 branch / main" framing was inaccurate and is corrected here to match `tasks.md` T015.
- Build/test/toolchain conventions follow the merged 001–004 pattern (project structure, sanitizer/bench/coverage gates, the 95/85 coverage floor per `[const §IX.1]`); specifics are deferred to `/speckit-plan`. **Gate A** is mandatory before `/speckit-tasks` (concurrency/threading-affecting per `[const §XVII.1]`); the Phase-2 design doc being signed-off does not waive the Phase-4 bundle Gate A. The full canonical pipeline order is stated once in `plan.md` Constitution-Check `[const §XVI.4]` row (single source of truth — not restated here to avoid drift).
