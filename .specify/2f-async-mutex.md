# Design Doc 2f — Awaitable Mutex `fixpp::sync::async_mutex`

> **Status:** Draft v1.5 (v1.6 errata E-1..E-4 applied post-006 sign-off; Gate A re-touch converged 2026-05-19); **shipped via 006-async-mutex PR #73 (merged 2026-05-19, squash `1e2f0e4`)**
> **Date:** 2026-05-08
> **Owner:** Opus (Phase 2 design author)
> **Headers:** `fixpp::sync::async_mutex` (`include/fixpp/core/sync/async_mutex.hpp`); RAII guard `fixpp::sync::async_lock_guard` (same header); session-side helper `fixpp::session::async_lock_via_session_executor(async_mutex&)` (`include/fixpp/session/async_lock_via_session_executor.hpp`); CI-side enforcement of `[const §XV.9]` (`tools/check_no_std_mutex_in_awaitable_headers.sh` — grep gate; clang-tidy custom check is the post-v1 path per §10 Q3).
> **Inherits:**
> - `[const §VIII.5]` Performance Budgets & Benchmarks — Allocator policy on the hot path (zero `new`/`delete` between parse and `fromApp`); 2f extends the discipline to the contended `async_lock` path because the v1.0 store-write mutex sits on the outbound dispatch chain.
> - `[const §XI.1]` Concurrency & Coroutines — `asio::awaitable<T>` is the session/transport composition primitive; 2f's `async_lock` returns `asio::awaitable<expected_t<async_lock_guard>>`.
> - `[const §XI.2]` Concurrency & Coroutines — ASIO native cancellation slots end-to-end; 2f honours `cancellation_type::total` per §4.5.
> - `[const §XI.3]` Concurrency & Coroutines — Awaitable mutex required in coroutine context; **direct mandate** for this doc.
> - `[const §XI.5]` Concurrency & Coroutines — Hot-path lock policy; the store-write path always uses mutex regardless of `SessionConfig::lock_policy`, so 2f is the lock for that path.
> - `[const §XI.6]` Concurrency & Coroutines — Coroutine frame allocation HALO-first + per-awaiter PMR fallback; 2f's awaiter is HALO-eligible by construction (item (1) of the six-item list) with PMR fallback per §4.3.
> - `[const §XIV.2]` Pluggable Interfaces — ≤5 pure-virtual on plugin interfaces. **`async_mutex` is NOT a plugin** (no virtual surface; value type owned by the consumer); §3 records this once.
> - `[const §XV.9]` Banned Patterns — `std::mutex` in coroutine context is banned; 2f is the only legal mutex shape, and 2f names the CI enforcement mechanism in §6.6 / §9.
> - `[arch §3]` Public Namespaces — `fixpp::sync` lives physically under `core/`; the public header path is `include/fixpp/core/sync/async_mutex.hpp`.
> - `[arch §4.1]` `core` — `fixpp::sync::async_mutex` is listed in the core public surface; 2f delivers the detail. **The session-side helper `async_lock_via_session_executor` lives in `session/`, downstream of `core/` per `[arch §2.3]`'s leaf rule** (RC#2 fix).
> - `[arch §5.1]` Executor model — `asio::any_io_executor` primitive, per-session strand default, coroutine composition, HALO-first; 2f's awaiter completes on the awaiter's bound executor (the `session_executor` wrapper class per `[2d §4.8]` for in-session callers).
> - `[arch §5.2]` Allocator policy — public API is PMR-aware, per-session `memory_resource` carried via the executor and `SessionConfig`; 2f's PMR fallback path is **caller-supplied via the explicit `mr` overload** (RC#2 fix).
> - `[arch §5.5]` Lifetime model — `async_mutex` is a value-typed, non-movable owned type held by its consumer; the `async_lock_guard` is a flyweight bound to the mutex's lifetime via `[[clang::lifetimebound]]`; the guard's move-assignment is destructive (unlock-then-take-ownership) per RC#1's N-P1-3 close.
> - `[arch §6]` Plugin Pattern — applies only when there is a virtual surface user code subclasses; **2f does not expose one** (§3 once).
> - `[arch §10]` row 2f — Awaitable mutex (six-item design list per `[SYN §3.2 Q6b]`); cross-cutting hooks: §5.1 executor model; §4.1 surface.
> - `[arch §11]` row 2 — Coroutine HALO firing on inbound dispatch path across the compiler matrix; **co-owned by 2d and 2f**; verification work tracked in §10 Q1.
> - `[SYN §3.2 Q6a]` — Cancellation propagation model (DECIDED — ASIO native cancellation slots end-to-end); 2f honours `cancellation_type::total`.
> - `[SYN §3.2 Q6b]` — Awaitable mutex (DECIDED — own implementation in `fixpp::sync`, BSL-1.0 algorithm attribution to `avast/asio-mutex` / cppcoro / Lewis-Baker); the **six-item design list** is the operating spec for §4 / §6.
> - `[2a §4.2]` `trap_throw` pattern — the no-terminate-on-PMR-throw mechanism; 2f's PMR fallback path routes throws through this helper.
> - `[2a §6.5]` Latency Tier 1 ceiling idiom — 2f mirrors this idiom in §6.3.
> - `[2b §6.4]` Lifetime contract on flyweights — 2f's `async_lock_guard` carries `[[clang::lifetimebound]]` per the same precedent.
> - `[2c §6.7]` Per-doc-prefix discipline (`FIXPP_ERR_DICT_*`) — 2f adopts the same shape with prefix `FIXPP_ERR_SYNC_*`.
> - `[2d §4.4]` `EngineConfig::default_session_resource` — recorded; not consumed by 2f's surface (the layering-clean fallback shape forbids `core::async_mutex` from reaching for an engine handle — RC#2 fix); the engine-bootstrap mutex caller passes `mr` explicitly.
> - `[2d §4.5]` `SessionConfig` — per-session PMR resource (`session_arena`) consumed by the **session-side helper** `async_lock_via_session_executor` (not by `core::async_mutex` directly); `lock_policy` enum recorded but not consumed by 2f directly (the store-write callsite cap in `[const §XI.5]` is what binds 2f to the store path). **Appendix D §D.1 publishes `Session::session_arena() noexcept` as an engine-internal accessor returning `std::pmr::memory_resource*`.**
> - `[2d §4.7]` Cancellation propagation API — two-phase close + per-mode effect table; the row at line 804 (`async_mutex::lock`) is 2f's inherited contract. **Appendix D §D.2 rewrites that row to surface `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI** (RC#4 fix; consistent with `[2d §6.5]`'s `cancellable_dispatch → dispatch_aborted` precedent).
> - `[2d §4.8]` `fixpp::core::session_executor` — project-owned wrapper class (NOT an alias to `any_io_executor`); 2f's awaitable completes on the awaiter's bound `session_executor` for in-session callers; the wrapper's `running_in_this_thread()` query disposition drives §4.6's inline-vs-post predicate.
> - `[2d §6.5]` `cancellable_dispatch` primitive — 2f's `async_lock` is **NOT** this primitive; it is the lower-level mutex the primitive uses internally for any internal serialisation. 2f's cancellation outcome (`error::sync_lock_aborted`) joins `[2d §6.7]`'s `dispatch_aborted` and `clock_sleeps_cancelled` in the `FIXPP_ERR_CANCELLED` group.
> - `[2d §6.7]` C-ABI coalescing precedent (`FIXPP_ERR_THREAD_*`); 2f introduces `FIXPP_ERR_SYNC_*` as a peer.
> - `[2d §7.4]` **Locked contract surface** for 2f — the executor-compat surface 2f must satisfy:
>   - completion on the awaiter's bound executor;
>   - honour `cancellation_type::total` (waiter removed, completes with the cancellation outcome named at the 2f boundary — `expected_t::unexpected{sync_lock_aborted}` per Appendix D §D.2);
>   - `dispatch` vs `post` policy with default `dispatch`; per-mutex override is 2f's call; the inline-vs-post predicate is ASIO `dispatch` semantics on the bound executor (§4.6).
> - `[2d §10] Q1` — `async_lock` signature DEFERRED to 2f; contract locked at `[2d §7.4]`. **2f closes this here in §4.1.1.**
> - `[2d §10] Q3` — engine-shutdown ordering; 2f's `cancel_and_drain()` member (§4.7) composes with the closure 2d v0.4 published.
> - `[2e §3.1]` Inherited primitives — store-write mutex is a `fixpp::sync::async_mutex`; 2e is 2f's first downstream consumer.
> - `[2e §4.2]` `MemoryStore` — single per-instance `fixpp::sync::async_mutex` per `[const §XI.3]`; the writer mutex per `[2e §6.4]`.
> - `[2e §6.4]` Writer mutex contract — every `MessageStore` mutating method serialises on the per-store-instance `fixpp::sync::async_mutex`; **2f sign-off is the named hard hand-off gate** for 2e implementation per `[2e §3.1]`.
> - `[2e §6.6]` Latency Tier 1 ceilings — `MemoryStore::store` 200-byte budget at ≤ 200 ns; 2f's uncontended-acquire ≤ 20–25 ns (per §6.3 row 1) leaves ≥ 175 ns of headroom inside the [2e §6.6] 200 ns envelope.
> - `[2e §6.7]` Per-doc-prefix discipline (`FIXPP_ERR_STORE_*`) — peer of `FIXPP_ERR_SYNC_*`.
> - `[2e §10] Q8` — 2f signature deferred; **2f closes this here in §4.1.1.**
> - `[const §VI.5]` Spec Coverage Discipline — exact-citation rule; this appendix's structure obeys it.
>
> **Cites:** `[SYN §3.2 Q6a]`, `[SYN §3.2 Q6b]`, `[const §XI.3]`, `[const §XV.9]`, `[const §VIII.5]`, `[const §XI.6]`, `[2d §7.4]`. Per `[const §VI.5]` the Normative References section is bound to spec sources (`[FIX-SL §...]`, `[FIXT §...]`, `[FIXS §...]`); **2f's primary drivers are engineering judgment, not a specific FIX spec section, and no `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies.** This is recorded in Appendix B per the precedent set by `architecture.md` Appendix B's closing note (line 678) and `[2d Appendix B] §B.2`.
>
> **Catalogue rows owned:** **NFR-016** (NEW row) — Awaitable mutex `fixpp::sync::async_mutex`. Drop-in language for `library/spec/feature-catalogue.md` and `library/spec/coverage-index.md` is in §11; the orchestrator applies the amendment at sign-off per `[2d §11]` precedent (the rewrite agent does not edit those files in this draft). Appendix A claims the row.
>
> **Convergence log pointer:** see Appendix C; populated after Gate A reviews; v0.1 archived as `2f-async-mutex.draft-r1.md` after full-rewrite reset 1/2 in Gate A round 1. **v1.1 addresses Codex review (3 P1 / 3 P2 / 2 P3) and Opus adversarial review (combined post-judging 5 P1 / 6 P2 / 4 P3; 3 root causes + 1 editorial RC; see Appendix C).** **v1.2 addresses Codex review (2 P1 / 2 P2 / 1 P3) and Opus adversarial review (combined post-judging 3 P1 / 4 P2 / 3 P3; 2 root causes + 1 editorial RC; see Appendix C).** **v1.3 addresses Codex review (4 P1 / 1 P2 / 1 P3) and Opus adversarial review (combined post-judging 4 P1 / 2 P2 / 2 P3; 2 structural root causes + 1 editorial; round-cap user-authorized post-cap pass; see Appendix C).** **v1.4 applies a focused line-edit pass for result-slot CAS-then-publish, deterministic drain-latch publication, reaper-abort subscriber wakeup, and latency/drop-in cleanup (see Appendix C).** **v1.5 applies focused cleanup for drain-latch member convergence, non-terminal notify wake semantics, and stale §3 latency wording (see Appendix C).**

---

## 1. Goals

1. **Deliver `fixpp::sync::async_mutex` per the six-item design list** in `[SYN §3.2 Q6b]`: (1) waiter embedded in the awaiter object; (2) PMR-aware fallback for type-erased completion handlers via the **explicit `mr` overload** (RC#2 fix); (3) ASIO cancellation slot support — `cancellation_type::total` removes the waiter and completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per Appendix D §D.2 (RC#4 fix); (4) per-mutex `dispatch` vs `post` policy with default `dispatch`; the inline-vs-post predicate is ASIO `dispatch` semantics on the bound executor — `running_in_this_thread()` true → inline, false → `post`; `direct_executor` mode + non-queryable executor falls to always-`post` (RC#4 fix); (5) **`std::terminate()` precondition + explicit `cancel_and_drain()` awaitable** (RC#3 fix — replaces v0.1's release-UB shape); (6) tests covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters under release (death test), contention stress, TSan + ASan, plus the four new RC#1 race seams + RC#3 cancel-and-drain seam + ARM64 weak-memory + move-assign seam. Each item lands at a named subsection in §4 or §6 and a named test seam in §9.

2. **Honour the executor-compat contract from `[2d §7.4]`** — the `async_lock` awaitable completes on the awaiter's bound executor (the `session_executor` wrapper for in-session callers per `[2d §4.8]`); cancellation flows through ASIO native slots and surfaces at the 2f C++ boundary as `expected_t::unexpected{sync_lock_aborted}` (Appendix D §D.2); `dispatch` is the default completion policy, `post` is the per-mutex opt-in. The contract is locked at 2d's level; 2f's job is the signature, the precise atomic algorithm, the memory-ordering specification, and the test seams that prove the contract is satisfied.

3. **Unblock `[2e §6.4]` writer-mutex contract and `[const §XI.3]` mandate.** The store-write path (per `[const §XI.5]`'s callsite cap) and any post-v1 store/audit/replication impl that wants in-coroutine mutual exclusion all consume `fixpp::sync::async_mutex`; v1.0 ships seqnum-counter, store-writer, and pinset-rotation use cases (per `[SYN §3.2 Q6b]`). 2f sign-off is the named hand-off gate for 2e implementation per `[2e §3.1]`.

### 1.1 Magnitude domain — what `async_mutex` is sized for

- **Worst-case waiter depth.** v1.0 hot paths (seqnum counter, per-store writer mutex, pinset rotation) all run inside a single session serialisation domain (per `[2d §4.8]`'s wrapper-class shape under both threading modes). Under that discipline contention is **structurally zero**: at most one coroutine ever calls `async_lock` on a given mutex instance at a time, so the `async_lock` always takes the uncontended fast path and the LIFO waiter list is empty in steady state. Worst-case waiter depth on any v1.0 hot path is therefore **O(1)** (one in-flight, none queued); the mutex is defence-in-depth against a session-serialisation-domain violation. **Cross-domain pathological case** (deliberate stress under §9 seam **"Contention stress"**): O(N) with N = number of concurrent acquirers; each waiter is one cache-line node embedded in its caller's coroutine frame, total memory ≈ N × 96 B. The mutex's atomic state is O(1) regardless of N (two atomic words: `state_` for the LIFO list and `next_drain_head_` for the residual FIFO list — RC-A close). Drain cost on `unlock()` is O(N); §6.3 row 4 budgets that explicitly. **Post-v1.0 design risk** (Opus N-P3-2 close): for post-v1.0 use cases that admit unbounded acquirer counts (tap-replication, audit-tee, gRPC fan-out), the unlock-drain O(N) cost is a defence-in-depth concern. v1.0 callsites are bounded by session-domain serialisation discipline; no §9 seam at N=10⁶ is required for v1.0 sign-off, and an unbounded-waiter-list shape is foreclosed for v1.0.

- **Embedded-waiter size budget — ≈ 96 B (≤ 1.5 cache lines worst case).** Per the §4.2 precise layout (revised in v1.1 per RC-C close — residual ownership moved to the mutex side per RC-A; inline slot-handler buffer added per RC-C): `async_mutex* mutex_` (8 B) + `async_mutex_awaiter* next_` intrusive LIFO/FIFO link (8 B; reused by both `state_` LIFO chain and `next_drain_head_` FIFO chain) + `std::atomic<waiter_phase> phase_` (1 B + alignment to 4 B) + `asio::cancellation_slot slot_` (≈ 16 B) + `std::coroutine_handle<> coro_` (8 B) + `expected_t<async_lock_guard>* result_` (8 B) + inline slot-handler-storage buffer `std::array<std::byte, 32> slot_storage_` (32 B per RC-C — feeds `detail::slot_allocator` on the embedded path so the cancellation handler closure does not touch the global heap when `mr == nullptr`). **Inline arithmetic (Opus N-P3-1 close — round-2 transparency):** 8 + 8 + 4 + 16 + 8 + 8 + 32 = **84 B raw; with `alignas(8)` trailing padding ≈ 88 B**. The "**≤ 96 B" total is the published ceiling**, carrying ≈ 8 B of headroom for `asio::cancellation_slot`'s implementation-defined size (ASIO's `cancellation_slot` is a typedef over an internal type whose size may grow under future ASIO versions). The 88 B → 96 B slack is intentional and named here. Total ≤ 96 B = at most 1.5 cache lines, fits in any reasonable coroutine frame's free space; HALO-eligible per §6.4. The size is named here so HALO-firing analysis (§6.4) can cite it. The previous 64-B claim (v1.0) carried a `residual_` field on the awaiter; v1.1 drops it (mutex owns the residual list — see §4.2) and adds the slot-handler-storage buffer. **v1.3 / RC-α / RC-β note (round-3 post-cap close):** the `cancel_and_drain` lifecycle members (`active_holders_count_`, `draining_`, `drain_latch_ptr_`) added to the mutex in v1.2/v1.3 live on the **mutex** itself, NOT the awaiter; the awaiter byte-budget is unchanged at ≤ 96 B.

- **Mutex object size budget — ≈ 52 B raw / 56 B padded (one cache line on x86_64 with 8 B headroom).** Per the §4.1 precise layout (revised in v1.3 per RC-β close — the v1.2 by-value `detail::drain_latch` member was non-implementable because `asio::steady_timer` is non-default-constructible without an executor and the mutex is `constexpr`-default-constructible per [arch §5.5]). v1.3 mutex members: `std::atomic<uintptr_t> state_` (8 B) + `std::atomic<async_mutex_awaiter*> next_drain_head_` (8 B) + `std::atomic<bool> draining_` (1 B + 3 B padding) + `std::atomic_flag drain_in_progress_` (typically 1 B + 3 B padding under libstdc++/libc++) + `std::atomic<std::uint32_t> active_holders_count_` (4 B) + `std::atomic<std::uint32_t> active_acquirers_count_` (4 B — NEW v1.3 / RC-α; covers in-flight acquirers between `await_ready`'s `draining_` load and the fast-path CAS) + `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` (≈ 16 B — two pointers; lazy-shared-ptr shape per RC-β; null when no `cancel_and_drain` is in flight) + `completion_policy const policy_` (1 B + 3 B padding). **Inline arithmetic (v1.3 / RC-β / RC-γ transparency):** 8 + 8 + 4 + 4 + 4 + 4 + 16 + 4 = **52 B raw; with `alignas(8)` trailing padding ≈ 56 B**. **One cache line on x86_64 (64 B); 8 B of headroom**. The v1.2 estimate ("≈ 56 B with by-value `detail::drain_latch`") was based on a non-implementable shape (`asio::steady_timer` requires an executor at construction; `async_mutex()` is `constexpr` and has no executor parameter). v1.3's lazy-`shared_ptr<drain_latch_state>` shape (constructed only inside `cancel_and_drain`'s call frame; the mutex stores a shared_ptr updated atomically during the epoch) keeps the mutex `constexpr`-default-constructible AND fits in one cache line. **No `alignas(64)` cache-line-alignment annotation is required**: the v1.0 hot path (`state_` + `next_drain_head_`) lives in the first 16 B of the object; the v1.3 RC-α additions (`active_holders_count_`, `active_acquirers_count_`) sit at offset 24–32 (still in line 1); `drain_latch_ptr_` and `policy_` are cold-path. False-sharing between `state_` and `active_holders_count_` is bounded — same cache line, but the holder count is incremented/decremented at acquire/release boundaries that already serialise against `state_`'s exchange, so the cache-line traffic is no worse than the existing `state_` traffic. See §4.1 for the full member list.

- **Cancellation slot integration cost.** Per the awaiter's `await_suspend` contract (§4.2), the cancellation handler is registered exactly once on suspend and de-registered exactly once on resume — no per-poll overhead. Registration cost: one atomic write to the cancellation state's slot list (≤ 5 ns on warm cache) + one allocator-bind (cancellation slot's storage uses the awaiter's PMR resource per §4.3 — never the global heap; Codex C-P2-7 / Opus N-P1 close). Both are charged into the §6.3 contended-enqueue Tier 1 ceiling.

### 1.2 Scope boundary — what 2f owns vs what it doesn't

2f **OWNS**:

- The `fixpp::sync::async_mutex` class definition (header location, atomic state encoding, construction, destruction, copy/move semantics, the `async_lock(std::pmr::memory_resource* mr = nullptr)` member, `unlock()` semantics, `try_lock()` semantics, `cancel_and_drain()` member). v1.1 adds a mutex-owned `std::atomic<async_mutex_awaiter*> next_drain_head_` field carrying the residual FIFO list (RC-A close — replaces v1.0's awaiter-owned `residual_` field). **v1.3 mutex member set** (RC-α / RC-β round-3 post-cap close — supersedes v1.2's by-value `detail::drain_latch` shape that was non-implementable): `std::atomic<std::uint32_t> active_holders_count_` (post-CAS holder accounting; incremented winner-only at the grant CAS-success on either fast-path or drain-grant path; decremented at `unlock()` entry); `std::atomic<std::uint32_t> active_acquirers_count_` (NEW v1.3 / RC-α — in-flight acquirer epoch covering the window between `await_ready`'s `draining_` load and the fast-path CAS); `std::atomic<bool> draining_` (drain-flag set by `cancel_and_drain`); `std::atomic_flag drain_in_progress_` (concurrent-call serialiser); `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` (NEW v1.3 / RC-β; UPDATED v1.5 — lazy event-state pointer; constructed only inside `cancel_and_drain`'s call frame; null when no drain in flight; replaces the v1.2 by-value `detail::drain_latch drain_latch_` member that owned an `asio::steady_timer` — non-implementable because the timer requires an executor at construction and the mutex is `constexpr`-default-constructible). The `detail::drain_latch_state` type is project-internal (declared in §3.1 row + §4.7.2 + new §4.7.3); the mutex no longer owns an `asio::steady_timer` or any other ASIO runtime state. The v1.2 `std::atomic<bool> drain_complete_` flag is folded into `drain_latch_state` (no longer a mutex member).
- The awaiter type (`detail::async_mutex_awaiter`) with the per-waiter atomic phase machine (RC#1). v1.1 collapses the phase enum to three states `{ queued, granted, cancelled }` (RC-A close — replaces v1.0's four-state `{ queued, draining, cancelling, completed }`); the awaiter no longer owns a residual list.
- The `fixpp::sync::detail::slot_allocator` project-internal allocator type (RC-C close — replaces v1.0's prose-only reference; the awaiter carries a 32-byte inline buffer, and `slot_allocator` allocates from it when `mr == nullptr` and from `mr` when non-null).
- The RAII guard (`async_lock_guard`) — back-pointer to mutex, `[[clang::lifetimebound]]` on the constructor, destructive move-assignment semantics (RC#1 / N-P1-3 close), explicit `release()`.
- The executor-compat surface satisfying `[2d §7.4]`: completion on the awaiter's bound executor, `cancellation_type::total` honoured under the precise CAS-arbitration protocol of §4.5, `dispatch` default with per-mutex `post` override.
- The PMR-aware fallback path via the explicit `mr` overload (§4.3) — caller-supplied resource; no `core` reach into `session/`.
- The destructor semantics (§4.7) — `std::terminate()` precondition + `cancel_and_drain()` drain primitive.
- The CI enforcement mechanism for `[const §XV.9]` (§6.6, §9 seam **"`std::mutex`-in-coroutine-context CI gate"**).
- The memory-ordering specification (§6.2 sub-table — RC#5).
- The compile-time invariants (`static_assert`s) on awaiter alignment, pointer round-trip, atomic lock-freedom (§4.1 — RC#5).

2f **does NOT own**:

- `async_shared_mutex` / `async_recursive_mutex` / `async_timed_mutex` — out-of-scope per `[SYN §3.2 Q6b]` and `[2e §1.1]`'s exclusive-only retire (§2 non-goals).
- The session-side helper `fixpp::session::async_lock_via_session_executor` — declared by 2f at §4.3.3 / §11 but lives in `session/`; the session-module spec ships the implementation (RC#2 layering fix).
- The consuming sites (seqnum counter — Phase-4 session-module spec; store writer mutex contract — `[2e §6.4]`; pinset rotation — `2g`).
- The `cancellable_dispatch` primitive (`[2d §6.5]`).
- The `session_executor` wrapper-class shape (`[2d §4.8]`).
- The C ABI shape — 2f is C++ only (§5).

---

## 2. Non-goals

The following are **out of scope** for v1.0 and are explicitly retired here:

- **No `async_shared_mutex` / RW-mutex.** Per `[SYN §3.2 Q6b]` the v1.0 use cases need only the basic exclusive form; `[2e §1.1]` retired the RW-mutex variant in favour of exclusive-only.
- **No `async_recursive_mutex`.** Per `[const §XV.9]` (no carve-out) and `[SYN §3.2 Q6b]`. `[2e §7.4]` removed the v0.1 transitional `std::recursive_mutex` adapter; 2f confirms there is no recursive variant.
- **No `async_timed_mutex`.** Timed acquire is not a v1.0 use case; the Phase-4 session-module spec's heartbeat / SendingTime windows use `Clock::sleep_until` per `[2d §4.1]`.
- **No fairness modes beyond LIFO-pop + FIFO-drain.** The cppcoro / Lewis-Baker algorithm gives FIFO fairness within a drain cycle (the LIFO list is reversed on `unlock`). The `dispatch` vs `post` policy (item 4) covers the HFT/fairness-sensitive sites' need; a strict-FIFO ticket-lock alternative would need a separate algorithm.
- **No pluggable allocator beyond the inherited PMR resource passed via the explicit `mr` overload.** `async_mutex` itself does not carry a `memory_resource*` field; the PMR fallback path (§4.3) takes the resource as a parameter.
- **No spinning fallback.** Per `[const §XI.5]` the store-write path always uses mutex; 2f cannot spin (the store-write path has unbounded disk-wait). Per `[2d §7.4]` cancellation must work; a spin loop without a cancellation check is incompatible.
- **No `co_await std::mutex` adapter.** Banned by `[const §XV.9]`; no carve-out, no transitional wrapper.
- **No extension-point `concept` / CRTP for user-derived async-mutex types in v1.0.** `async_mutex` is a closed, value-typed, non-virtual class. Users who want a custom mutex shape ship their own type. Per `[arch §6]` plugin pattern review: `async_mutex` is **NOT** a plugin (no virtual interface user code subclasses); the ≤ 5 pure-virtual rule does not bind.

---

## 3. Inherited surface

Per `[const §VI.5]`, every primitive 2f leans on must trace to its owning section. This section is the exhaustive enumeration; the precedent from `[2e §3.1]` is to enumerate everything.

From `[const §XI.3]` (Concurrency & Coroutines — Awaitable mutex required in coroutine context):

> Awaitable mutex required in coroutine context. `fixpp::sync::async_mutex` (own implementation, BSL-1.0 algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.** Enforced by clang-tidy custom check or grep gate.

**Implication for 2f:** the direct mandate; the entire doc operationalises this article. The grep gate / clang-tidy custom check is named in §6.6 / §9 seam **"`std::mutex`-in-coroutine-context CI gate"**.

From `[const §XI.5]` (Hot-path lock policy):

> Hot-path lock policy: per-session policy with hard-coded callsite caps. Default = mutex. Spin opt-in via session config. Store-write path always uses mutex regardless of policy.

**Implication for 2f:** the store-write callsite cap means `fixpp::sync::async_mutex` is the lock type on the v1.0 store-write hot path regardless of `SessionConfig::lock_policy`. 2f therefore inherits the hot-path zero-allocation discipline (`[const §VIII.5]`) on the contended path, not just the uncontended path. §6.1 / §6.4 carry this through.

From `[const §XI.6]` (HALO-first frame allocation):

**Implication for 2f:** the awaiter is HALO-eligible by construction; the PMR fallback path (item 2) consumes the explicit `mr` parameter the caller supplies (RC#2 fix). §4.3 spells the mechanism precisely.

From `[const §XV.9]` (Banned Patterns — `std::mutex` in coroutine context):

**Implication for 2f:** 2f is the only legal mutex shape for any header that includes `asio::awaitable<...>`. The CI enforcement mechanism (named §6.6 / §9) is 2f's deliverable.

From `[const §VIII.5]` (Allocator policy on the hot path):

**Implication for 2f:** `async_mutex` ships zero global `new`/`delete` on the v1.0 hot path. The waiter-embedded design (item 1) achieves this by construction; the PMR fallback (item 2) achieves it by drawing from the caller-supplied `mr`. The cancellation-slot's allocator is bound at `await_suspend` time via `asio::bind_allocator(detail::slot_allocator{this, mr})` (RC-C close — see §4.3.4 for the three-case storage table; Codex C-P2-7 / Opus N-P1 close). §6.1 records this.

From `[arch §3]` (Public Namespaces) — `fixpp::sync` lives under `core/` physically:

**Implication for 2f:** the class header lives at `include/fixpp/core/sync/async_mutex.hpp`. The session-side helper lives at `include/fixpp/session/async_lock_via_session_executor.hpp` per `[arch §2.3]`'s leaf rule (RC#2 fix).

From `[arch §4.1]` (`core` public surface):

**Implication for 2f:** the row exists; 2f delivers the detail.

From `[arch §5.1]` (Executor model):

**Implication for 2f:** the `async_lock` return type is `asio::awaitable<expected_t<async_lock_guard>>`; the awaitable completes on the awaiter's bound executor; cancellation flows through ASIO slots; the awaiter is HALO-eligible.

From `[arch §5.2]` (Allocator policy):

**Implication for 2f:** PMR fallback path is caller-supplied via the explicit `mr` overload (RC#2 fix); no global heap on any code path.

From `[arch §5.5]` (Lifetime model):

**Implication for 2f:** `async_mutex` is non-copyable and non-movable (a locked-mutex move would orphan its waiters; an unlocked-mutex move's `owner_` back-pointer patching is brittle under cancellation — see §4.1 for the justification). `async_lock_guard` is movable with **destructive** move-assignment (the move-into-engaged guard unlocks the previously-owned mutex first per RC#1 / N-P1-3 close); carries `[[clang::lifetimebound]]` on its constructor.

From `[arch §6]` (Plugin Pattern — ≤ 5 pure-virtual rule):

**Implication for 2f:** `async_mutex` exposes zero pure-virtual methods; it is **not** a plugin interface; the cap does not bind.

From `[arch §10]` row 2f (handoff):

**Implication for 2f:** the row exists; 2f delivers it.

From `[arch §11]` row 2 (HALO firing on inbound dispatch path):

**Implication for 2f:** 2f co-owns the HALO-firing verification spike with 2d. §10 Q1 tracks the spike; PMR fallback (§4.3) is the safety net.

From `[SYN §3.2 Q6a]` (Cancellation propagation model — DECIDED — ASIO native cancellation slots end-to-end):

**Implication for 2f:** `cancellation_type::total` removes the waiter from the LIFO list and completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`); no parallel `stop_token`. §4.5 spells the per-type behaviour and the cancellation-vs-drain CAS-arbitration race.

From `[SYN §3.2 Q6b]` (Awaitable mutex — DECIDED — own implementation in `fixpp::sync`):

**The six-item design list is the operating spec for §4 / §6. Verbatim, with section pointers:**

1. **Waiter embedded in the awaiter object** (cppcoro-style), not heap-allocated. → §4.2.
2. **PMR-aware fallback** for the rare cases where embedding isn't possible — via the explicit `mr` overload (RC#2 fix). → §4.3.
3. **ASIO cancellation slot support** — `cancellation_type::total` removes the waiter, completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary. → §4.5.
4. **`dispatch` vs `post` policy** on completion, configurable per-mutex (default `dispatch`); inline-vs-post predicate is ASIO `dispatch` semantics on the bound executor. → §4.6.
5. **Safe destructor semantics** — `std::terminate()` precondition + explicit `cancel_and_drain()` awaitable (RC#3 fix). → §4.7.
6. **Tests** covering FIFO fairness, cancellation mid-wait, destructor-with-waiters under release, contention stress, TSan + ASan, plus the four new RC#1 race seams + RC#3 cancel-and-drain seam + ARM64 + move-assign. → §9.

From `[2a §4.2]` (`trap_throw` pattern):

**Implication for 2f:** the PMR fallback path's `allocate(...)` throw routes through `fixpp::core::detail::trap_throw` and surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}`. §4.3 / §6.5 spell this.

From `[2a §6.5]` (Tier 1 latency-ceiling idiom):

**Implication for 2f:** §6.3 mirrors the per-component breakdown style.

From `[2b §6.4]` (lifetime-bound flyweight discipline):

**Implication for 2f:** `async_lock_guard` is a flyweight bound to `async_mutex`'s lifetime; `[[clang::lifetimebound]]` on the constructor surfaces caller-side misuse.

From `[2c §6.7]` (per-doc-prefix discipline):

**Implication for 2f:** the C-ABI coalescing prefix is `FIXPP_ERR_SYNC_*` (peer of `FIXPP_ERR_DECIMAL_*` / `FIXPP_ERR_WIRE_*` / `FIXPP_ERR_DICT_*` / `FIXPP_ERR_THREAD_*` / `FIXPP_ERR_STORE_*`).

From `[2d §4.4]` (`EngineConfig::default_session_resource`):

**Implication for 2f:** recorded for forward-compat; `core::async_mutex` does NOT reach for it (RC#2 layering fix). The engine-bootstrap caller passes `mr` explicitly to `async_lock(mr)`.

From `[2d §4.5]` (`SessionConfig` field list):

- `executor_override` → resolved executor reaches 2f via the awaiter's bound `session_executor`.
- `lock_policy` (recorded; not consumed by 2f directly).
- `clock_override` / `effective_clock` per `[2d §7.9]` — not consumed by 2f's v1.0 surface.
- `session_arena` (per `[2d §8]`) — the PMR resource the **session-side helper** `async_lock_via_session_executor` recovers via Appendix D §D.1's `Session::session_arena()` accessor and forwards into `async_lock(mr)`. Not reached by `core::async_mutex` directly.

From `[2d §4.7]` (Cancellation propagation API — two-phase close):

The row at line 804 (`async_mutex::lock`) is 2f's inherited contract; **Appendix D §D.2** rewrites the row to surface `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI), consistent with `[2d §6.5]`'s `cancellable_dispatch → dispatch_aborted` precedent (RC#4 fix).

**Implication for 2f:** during phase 1 of graceful close, in-flight `async_lock` waiters continue to run. Phase 2's root `cancellation_type::total` propagates to in-flight `async_lock` waiters; 2f CAS's their phase from `queued → cancelled` (v1.1 / RC-A — was `queued → cancelling` in v1.0's four-state machine), and `await_resume` returns `expected_t::unexpected{sync_lock_aborted}` per §4.5. `terminal` close skips phase 1 and goes directly to the same total-cancel behaviour. `partial` is dropped from v1.0; treated as `total` per §4.5.

From `[2d §4.8]` (`fixpp::core::session_executor` — project-owned wrapper class):

**Implication for 2f:** the awaiter's bound executor for in-session callers is a `session_executor` value (not an `asio::any_io_executor`). 2f's `async_lock` awaitable completes by binding to the executor returned from `co_await asio::this_coro::executor`. The wrapper's `running_in_this_thread()` query disposition drives §4.6's inline-vs-post predicate (RC#4 fix); under `direct_executor` mode + non-queryable user-attested executor, the predicate falls to always-`post`.

From `[2d §6.5]` (`cancellable_dispatch`):

**Implication for 2f:** 2f's `async_lock` is **NOT** `cancellable_dispatch`; it is the lower-level mutex `cancellable_dispatch` may use as a building block.

From `[2d §6.7]` (C-ABI coalescing precedent):

**Implication for 2f:** `error::sync_lock_aborted` joins `dispatch_aborted` and `clock_sleeps_cancelled` in the `FIXPP_ERR_CANCELLED` group at the C ABI; runtime errors take the new `FIXPP_ERR_SYNC_*` prefix.

From `[2d §7.4]` (executor-compat surface — **the locked contract for 2f**):

**Implication for 2f:** §4.1 / §4.5 / §4.6 each implement one bullet; §9 has named seams that prove satisfaction. The cancellation-result rewording is published as Appendix D §D.2 (RC#4 fix).

From `[2d §10] Q1` (signature deferred to 2f):

**Implication for 2f:** **2f closes this open question in §4.1.1** by picking the signature `[[nodiscard]] awaitable<expected_t<async_lock_guard>> async_lock(std::pmr::memory_resource* mr = nullptr) noexcept`.

From `[2d §10] Q3` (engine-shutdown ordering):

**Implication for 2f:** `cancel_and_drain()` (§4.7) composes with the closure 2d v0.4 published; the consumer-side discipline (graceful-close → `cancel_and_drain` → `~consumer`) is documented in §4.7.

From `[2e §3.1]` / `[2e §4.2]` / `[2e §6.4]` / `[2e §10] Q8`:

**Implication for 2f:** **2f sign-off is the named hard hand-off gate for 2e implementation.** **2f closes `[2e §10] Q8` in §4.1.1** with the same signature that closes `[2d §10] Q1`.

From `[2e §6.6]` (Latency Tier 1 ceilings):

**Implication for 2f:** `MemoryStore::store` 200-byte budget at ≤ 200 ns includes `async_mutex::async_lock` uncontended at "≤ 20–25 ns"; §6.3 row 1 budgets the uncontended path against that envelope (Codex C-P2-8 / Opus close).

From `[const §VI.5]`:

**Implication for 2f:** Appendix B obeys the exact-citation rule.

### 3.1 Inherited primitives — exhaustive list

| Primitive | Origin | Hand-off form | Used in 2f §… |
|---|---|---|---|
| `asio::awaitable<T>` | `[const §XI.1]` / `[arch §5.1]` | ASIO type | §4.1, §4.2. |
| `asio::cancellation_slot` | `[const §XI.2]` / `[SYN §3.2 Q6a]` | ASIO type | §4.2, §4.5. |
| `asio::cancellation_state` / `co_await asio::this_coro::cancellation_state` | `[const §XI.2]` / `[2d §4.7]` | ASIO type / awaitable | §4.2 (slot recovery on `await_suspend`). v1.1 / RC-B close: `cancel_and_drain` does NOT walk this state (the v1.0 "walks the awaiter's parent cancellation_state" prose was non-implementable; v1.1's `cancel_and_drain` is mutex-owned per §4.7.2). |
| `asio::cancellation_type` | `[const §XI.2]` / `[2d §4.7]` | ASIO type | §4.5. |
| `asio::bind_allocator` | `[const §XI.2]` (associated-allocator binding) | ASIO function | §4.3 (slot allocator binding under PMR fallback — Codex C-P2-7 close). |
| `expected_t<T>` | `[arch §4.1]` / `[arch §5.3]` | template alias = `std::expected<T, fixpp::core::error>` | §4.1, §6.5. |
| `fixpp::core::error` | `[arch §5.3]` | tagged enum | §6.5. |
| `fixpp::core::detail::trap_throw` | `[2a §4.2]` | helper that converts a thrown PMR `bad_alloc` into an `expected_t` failure without terminating | §4.3, §6.5. |
| `fixpp::core::session_executor` (project-owned wrapper class) | `[2d §4.8]` v0.4 | value-typed wrapper holding either `asio::strand` (per_session_strand) or attested `any_io_executor` (direct_executor); `session_ptr()` accessor; `running_in_this_thread()` query disposition | §4.1, §4.3 (session-side helper recovers PMR), §4.6 (inline-vs-post predicate), §6.1. |
| `Session::session_arena()` accessor | **published by Appendix D §D.1 at sign-off** | engine-internal, non-virtual `Session*` member returning `std::pmr::memory_resource*` | §4.3 (consumed by the session-side helper, NOT by `core::async_mutex` directly). |
| Per-session PMR resource (`SessionConfig::session_arena`) | `[2d §4.5]` / `[2d §8]` | `std::pmr::memory_resource*` | §4.3 (recovered by the session-side helper and forwarded into `async_lock(mr)`). |
| `[[clang::lifetimebound]]` annotation discipline | `[2b §6.4]` / `[2b §6.6]` | C++23 attribute | §4.4. |
| `cancellable_dispatch` | `[2d §6.5]` | higher-level primitive that uses `async_mutex`-shaped locks internally | §1.2 (scope boundary). |
| `error::dispatch_aborted` (peer cancellation variant) | `[2d §6.7]` | `expected_t::unexpected` outcome | §6.5 (`error::sync_lock_aborted` joins the same `FIXPP_ERR_CANCELLED` group). |
| `std::pmr::polymorphic_allocator<void>` | `[const §XI.2]` / `[arch §5.2]` | standard PMR allocator wrapper over `std::pmr::memory_resource*` | §4.2.2 / §4.3.1 — wraps `mr` for `bind_allocator` on the PMR-fallback branch (Opus N-P3-4 close). |
| `std::pmr::null_memory_resource()` | `[const §XI.2]` (PMR standard primitives) | standard "refuse to allocate" PMR resource (throws `std::bad_alloc` on `allocate`) | §4.3.4 — fed to `slot_allocator` on the embedded path with HALO firing, where the inline buffer covers the slot's storage and any allocation request is a contract violation that surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}` via `trap_throw` (RC-C close). |
| `fixpp::sync::detail::slot_allocator` | this doc (§4.2 / §4.3.4) — project-internal | typed `Allocator`-shaped wrapper that allocates from `async_mutex_awaiter::slot_storage_` (inline 32 B buffer) when `mr == nullptr` and from `std::pmr::polymorphic_allocator<void>{mr}` when non-null; falls back to `std::pmr::null_memory_resource()` on overflow under embedded-no-HALO build mode | §4.2.2 step 3 / §4.3.4 — supplies storage for the cancellation slot's handler closure (RC-C / Opus N-P2-2 close). |
| **NEW v1.3 / RC-β** — `fixpp::sync::detail::drain_latch_state` | this doc (§4.1 / §4.7.2 / §4.7.3) — project-internal | **Lazy-constructed event-state object** (NOT a `steady_timer`-owning by-value mutex member — the v1.2 shape was non-implementable because `asio::steady_timer` requires an executor at construction and `async_mutex()` is `constexpr` per `[arch §5.5]`; closes Opus C-R3-P1-4 / C-R3-P2-1 / N-R3-P1-1 / N-R3-P1-2). Allocated as `std::shared_ptr<detail::drain_latch_state>` inside `cancel_and_drain`'s call frame; the mutex stores a `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` (atomically updated on shared_ptr construction). The state object owns: (i) `std::atomic<bool> released_` (fresh = false; reaper publishes true at the publication edge); (ii) `std::atomic<std::uint32_t> in_flight_resumptions_` (reaper-resumption-handler count; lives on the state object instead of the reaper's stack — closes Opus N-R3-P1-2 UAF); (iii) a single `asio::experimental::concurrent_channel<void()>` (or equivalent project-internal awaitable subscriber list — see §4.7.3 for the implementation pick) for the multi-waiter latch surface. Exposes `wait()` (member-awaitable; subscribes to the released-edge), `notify()` (called by the holder's `unlock()` and the reaper's resumption handlers; idempotent), and a `signal_release()` helper that the reaper invokes at the final publication edge. Implementation precedent: `asio::experimental::concurrent_channel` is a real ASIO primitive (used in production `asio::experimental` builds and present in ASIO 1.30) that publishes `wait()` semantics with cancellation-slot integration — closes the Opus N-R3-P1-1 "cited precedent does not match" finding (v1.2 incorrectly cited `[2d §6.5]`'s `cancellable_dispatch`-over-`steady_timer`-poll-loop pattern; v1.3 cites `asio::experimental::concurrent_channel` directly). The mutex's constructor remains `noexcept` and `constexpr`-default-constructible — no executor dependency. | §4.1 (mutex `drain_latch_ptr_` member); §4.7.2 (constructed at call site); §4.7.3 (NEW v1.3 — drain-latch ownership and cancellation-propagation contract). |

**Hand-off gates:**

- 2f sign-off is the hard hand-off gate for **2e implementation** per `[2e §3.1]`.
- 2f sign-off unblocks **2g** (pinset rotation) and the **Phase-4 session-module spec** (seqnum counter).
- 2d v0.4 is sign-off-applied; 2f consumes the wrapper-class shape from `[2d §4.8]` directly.
- **Three sibling-doc Appendix D drop-ins are applied at 2f sign-off** (RC#2 + RC#4 + RC-D closes — v1.1):
  - **Appendix D §D.1** — `[2d §4.5]` publishes `Session::session_arena() noexcept -> std::pmr::memory_resource*` as engine-internal accessor (with `[2d §4.4]` resolution-chain reference; `fixpp::session/`-only scope).
  - **Appendix D §D.2** — `[2d §4.7]` per-mode effect table's `async_mutex::lock` row is rewritten to surface `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI).
  - **Appendix D §D.3** (NEW v1.1) — `[2d §7.4]`'s locked-contract-surface bullet for `async_mutex::lock` is rewritten to match §D.2's wording (closes Codex C-P2-5 / Opus N-P1-1 — sibling-doc inconsistency between `[2d §4.7]` table and `[2d §7.4]` surface).

This document refines the inherited surface; it does **not** diverge from any sibling-doc contract except where Appendix D drop-ins are queued.

---

## 4. Public C++ API

The header lives at `include/fixpp/core/sync/async_mutex.hpp`. Namespace is `fixpp::sync`. Every awaitable-returning method is `[[nodiscard]]`. The `async_lock_guard` constructor carries `[[clang::lifetimebound]]` per `[2b §6.4]` / `[2b §6.6]` precedent. No method on the public surface throws; PMR allocation throws are routed through `fixpp::core::detail::trap_throw` per `[2a §4.2]` and surface as `expected_t::unexpected{error::sync_lock_alloc_failed}` (§6.5).

### 4.1 `fixpp::sync::async_mutex` class — public surface

```cpp
// include/fixpp/core/sync/async_mutex.hpp
//
// fixpp::sync::async_mutex — the only legal mutex shape in coroutine context
// per [const §XI.3] / [const §XV.9]. Owned by the consumer (per-MessageStore
// instance, per-counter site, per-pinset). Not a plugin: zero pure-virtual,
// not a base class, not extensible.
//
// Algorithm: own implementation, BSL-1.0 algorithm attribution to
// avast/asio-mutex (the classic Lewis-Baker / cppcoro lock-free design).
//
//   std::atomic<uintptr_t> state_:
//     state_ == not_locked         (= 1)              — free.
//     state_ == locked_no_waiters  (= 0)              — held, LIFO list empty.
//     state_ == <pointer-to-waiter>                   — held, head of LIFO list.
//
//   std::atomic<async_mutex_awaiter*> next_drain_head_ (v1.1 / RC-A close):
//     mutex-owned residual FIFO chain. unlock() walks this list FIRST
//     (granting ownership to the first non-cancelled waiter) before walking
//     the LIFO list anchored at state_. Drains owned by the mutex, not by
//     any awaiter — solves the v1.0 lifetime defect where the granted
//     awaiter "owned" the residual list but unlock() could not recover the
//     owner from `async_mutex*`-only state.
//
//   std::atomic<bool> draining_ (v1.1 / RC-B close):
//     set by cancel_and_drain(); from that point onward every new
//     async_lock(...) returns expected_t::unexpected{sync_lock_drained}
//     without enqueuing — checked by await_ready BEFORE the fast-path CAS
//     (v1.2 / RC-B close). Idempotent — second cancel_and_drain() observes
//     true and returns immediately.
//
//   std::atomic_flag drain_in_progress_ (v1.1 / RC-B close — N-P2-1):
//     concurrent-call serialiser for cancel_and_drain(). Only the first
//     caller becomes the reaper; subsequent callers subscribe to the
//     drain_latch_state's released-edge and return success.
//
//   std::atomic<std::uint32_t> active_holders_count_ (v1.2 / v1.3 RC-α
//     winner-only close):
//     count of currently-acquired holders. v1.3 / RC-α (Opus C-R3-P1-1
//     close): incremented WINNER-ONLY at the successful CAS — fast-path
//     await_ready CAS-success on `state_: not_locked → locked_no_waiters`,
//     OR drain-walker grant CAS-success on `phase_: queued → granted`. NOT
//     incremented at LIFO push (the waiter is reachable via the LIFO; cov-
//     ered by the per-waiter phase atom). NOT incremented before a CAS that
//     might fail (no leak on CAS-loss). Decremented at unlock() entry. Used
//     by cancel_and_drain() to wait for any pre-drain holder to release
//     before declaring the drain complete. Drain is complete iff
//     (active_holders_count_ == 0) AND (active_acquirers_count_ == 0) AND
//     (state_ == not_locked) AND (next_drain_head_ == nullptr) AND every
//     reaper-cancelled waiter has resumed.
//
//   std::atomic<std::uint32_t> active_acquirers_count_ (NEW v1.3 / RC-α):
//     in-flight acquirer epoch counter. Incremented by async_lock(...)'s
//     awaitable factory BEFORE await_ready's draining_ load; decremented
//     after await_ready returns true (fast-path success or fast-fail) OR
//     after await_suspend either enrols the waiter on the LIFO (transfer-
//     ring tracking responsibility to the LIFO walk + per-waiter phase
//     atom) or fast-fails the drained-mutex bypass. cancel_and_drain()
//     waits for active_acquirers_count_ == 0 in addition to active_holders_
//     count_ == 0 before publishing the drain-complete edge — closes Opus
//     C-R3-P1-2 (in-flight acquirer between draining_.load() and the fast-
//     path CAS not covered by the v1.2 holder-count alone).
//
//   std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_
//     (NEW v1.3 / RC-β; UPDATED v1.5 — replaces v1.2's by-value
//     detail::drain_latch member):
//     shared owner-reference to the drain_latch_state object that the
//     in-flight cancel_and_drain caller (the reaper) constructed as a
//     std::shared_ptr in its own coroutine frame. Null when no
//     cancel_and_drain is in flight. The reaper atomic-stores a
//     shared_ptr<drain_latch_state> into this member, pinning the latch
//     state alive for the entire drain epoch; concurrent callers
//     atomic-load the shared_ptr and either subscribe (non-null = epoch in
//     flight) or observe the cleared epoch (null = drain result already
//     published). The state object owns released_ + in_flight_resumptions_
//     and survives the reaper's coroutine frame destruction (shared_ptr
//     lifetime per [arch §5.5]). Replaces v1.2's by-value
//     detail::drain_latch member that owned an asio::steady_timer —
//     non-implementable because the timer required an executor at
//     construction and async_mutex() is constexpr-default-constructible.
//     v1.3 / RC-β close: closes Opus C-R3-P1-4 / C-R3-P2-1 /
//     N-R3-P1-1 / N-R3-P1-2.
//
// Per-waiter atomic phase machine (RC#1 / RC-A fix; v1.1 collapses to three
// states):
//
//   enum class waiter_phase : std::uint8_t {
//       queued    = 0,  // pushed onto LIFO or spliced into next_drain_head_;
//                       //   still cancellable. The previous v1.0
//                       //   `residual_queued` distinction is dropped — both
//                       //   live as `queued`. The drain skips `cancelled`
//                       //   waiters and CASes the first `queued` waiter to
//                       //   `granted`; remaining `queued` waiters stay
//                       //   `queued` and are spliced into next_drain_head_.
//       granted   = 1,  // unlock()'s drain CAS-acquired this waiter;
//                       //   ownership transfer is complete. await_resume
//                       //   returns the guard.
//       cancelled = 2,  // cancellation handler CAS-acquired this waiter (or
//                       //   cancel_and_drain reaped it). await_resume
//                       //   returns unexpected{sync_lock_aborted}.
//   };
//
// Each async_mutex_awaiter carries std::atomic<waiter_phase> phase_;
// unlock()'s drain and the cancellation handler arbitrate via CAS from
// `queued` to either `granted` or `cancelled`; the loser observes the
// winner's phase and no-ops. The v1.0 intermediate `draining` /
// `cancelling` phases were redundant under the mutex-owned-residual shape
// (the drain CAS is now atomic with the ownership transfer; there is no
// "ownership in flight" interval). See §4.2 / §4.5 / §6.2.
namespace fixpp::sync {

class async_mutex;
class async_lock_guard;

namespace detail {
// Forward-declared here; defined in §4.2 below. The waiter_phase enum is
// also forward-declared so the awaiter struct can use it (Codex C-P3-7
// close — order-valid header sketch).
enum class waiter_phase : std::uint8_t;
class async_mutex_awaiter;
class slot_allocator;          // §4.3.4 — RC-C close.
class drain_latch_state;       // §3.1 row + §4.7.2 + §4.7.3 — v1.3 / RC-β
                                //   close. Lazy-constructed event-state
                                //   object owned by the reaper's
                                //   shared_ptr; mutex stores an atomic
                                //   shared_ptr for the drain epoch.
                                //   Replaces v1.2's by-value
                                //   detail::drain_latch (non-implementable;
                                //   asio::steady_timer in constexpr ctor).
}  // namespace detail

// Per-mutex completion policy — item 4 of [SYN §3.2 Q6b]'s six-item list.
// Default `dispatch`; HFT / fairness-sensitive sites pick `post`.
enum class completion_policy : std::uint8_t {
    // ASIO `dispatch` semantics on the bound executor: if
    // `executor.running_in_this_thread()` is true at unlock time, the resumed
    // coroutine runs inline on the unlocking thread; if false, the
    // resumption posts. Under `direct_executor` mode + a user-attested
    // executor that does not publish `running_in_this_thread()`, the
    // predicate falls to always-`post`. Default; matches the [2d §7.4]
    // surface lock.
    dispatch = 0,

    // Always post the resumed coroutine through the bound executor; one
    // executor hop per resumption regardless of caller thread.
    post = 1,
};

class async_mutex {
public:
    // Construct an unlocked mutex with the default completion policy.
    constexpr async_mutex() noexcept = default;

    // Construct an unlocked mutex with an explicit completion policy.
    explicit constexpr async_mutex(completion_policy cp) noexcept
        : policy_(cp) {}

    // Non-copyable, non-movable. A movable mutex would have to patch every
    // in-flight awaiter's `mutex_` back-pointer atomically with the move,
    // which is fragile under cancellation. Consumer holds the mutex by value
    // at a stable address.
    async_mutex(async_mutex const&)            = delete;
    async_mutex(async_mutex&&)                 = delete;
    async_mutex& operator=(async_mutex const&) = delete;
    async_mutex& operator=(async_mutex&&)      = delete;

    // Destructor — std::terminate() precondition (RC#3 fix). Both debug AND
    // release fire `std::terminate()` (or `fixpp::core::abort_invariant(...)`)
    // if waiters are present or the mutex is held. Hard precondition; no
    // release-mode UB. Callers MUST drain the mutex before destruction;
    // cancel_and_drain() is the canonical drain primitive.
    ~async_mutex();

    // Acquire the mutex. Closes [2d §10] Q1 and [2e §10] Q8. See §4.1.1 for
    // the rejected-alternatives analysis.
    //
    // Returns an awaitable that completes when the mutex is acquired (with
    // an `async_lock_guard` that releases on destruction); with
    // `expected_t::unexpected{error::sync_lock_aborted}` if cancellation
    // wins the §4.5 CAS-arbitration race; or with
    // `expected_t::unexpected{error::sync_lock_drained}` if the mutex has
    // already been drained via cancel_and_drain() (RC-B close — every
    // post-drain async_lock fast-fails without enqueuing).
    //
    // The awaitable's completion runs on the awaiter's bound executor per
    // [2d §7.4]. The completion policy (per-mutex `policy_`) decides the
    // inline-vs-post behaviour per §4.6.
    //
    // PMR-aware fallback (RC#2 fix): when `mr == nullptr` (default), the
    // awaiter is embedded in the caller's coroutine frame (HALO-friendly per
    // [const §XI.6] / §1.1's ≤ 96 B budget — v1.1 RC-C). When `mr != nullptr`, the
    // awaiter is allocated from `mr` via the per-promise allocator hook;
    // type-erased completion handlers (asio::any_completion_handler<...>) and
    // composed-operation callers pass `mr` explicitly. The cancellation
    // slot's allocator is bound at await_suspend time via
    // `bind_allocator(slot_allocator(mr))` so the handler closure storage
    // also uses `mr` (Codex C-P2-7 close).
    //
    // Cancellation: see §4.5. Honours `total` (waiter unlinked, completes
    // with `sync_lock_aborted`); `partial` is dropped from v1.0 (treated as
    // `total` per §4.5); `terminal` is treated as `total`.
    [[nodiscard]] asio::awaitable<expected_t<async_lock_guard>>
        async_lock(std::pmr::memory_resource* mr = nullptr) noexcept;

    // (v1.1 / Opus N-P3-1 close) `try_lock()` is moved to `detail::` and is
    // no longer part of the public surface. The v1.0 public `try_lock()` +
    // `async_lock_guard` adopt-locked ctor admitted a same-mutex aliasing
    // bug at the caller's logic level (constructing two guards for one
    // mutex via try_lock + adopt-locked ctor); v1.1 closes the surface by
    // making the only legal guard-construction path the awaitable form
    // (`co_await m.async_lock()`). The §9 seam #20 ("`async_lock_guard`
    // destructive move-assign") uses `friend` access for the test fixture
    // — see §4.2 / §9 #20. The §7 consumer sketches all use the awaitable
    // form; v1.0 callsite ergonomics are unchanged.

    // Release the mutex. Drains the LIFO waiter list per §6.2's algorithm.
    // Public so test seams and certain internal callsites can release.
    // Precondition: the mutex is held; UB in release if violated. (The
    // public surface generally uses async_lock_guard's destructor instead.)
    void unlock() noexcept;

    // Drain primitive (RC#3 + RC-B fix; v1.1 reshape; v1.2 round-2 fix;
    // v1.3 round-3 post-cap RC-α + RC-β fix). Mutex-owned reaping operation
    // — the v1.0 "walks the awaiter's parent cancellation_state" shape was
    // non-implementable; v1.1's invented asio::async_wait_for_drain_complete
    // API was replaced in v1.2 with a project-internal detail::drain_latch
    // owning an asio::steady_timer; v1.2's by-value drain_latch member was
    // ALSO non-implementable (steady_timer requires an executor at
    // construction; async_mutex() is constexpr — Opus C-R3-P1-4 /
    // C-R3-P2-1). v1.3 mechanism (post-cap user-authorized pass):
    //   (1) Allocate detail::drain_latch_state on the heap via
    //       std::make_shared<drain_latch_state>(...) inside this call's
    //       coroutine frame; atomic-store a shared_ptr<drain_latch_state>
    //       into drain_latch_ptr_, pinning the latch state alive for the
    //       entire drain epoch; concurrent callers atomic-load the
    //       shared_ptr and either subscribe (non-null = epoch in flight) or
    //       observe the cleared epoch (null = drain result already
    //       published).
    //   (2) Set draining_=true; subsequent async_lock(...) fast-fails with
    //       unexpected{sync_lock_drained} (await_ready checks draining_
    //       BEFORE the fast-path CAS — Codex N-P1-1 round-2 close).
    //   (3) RC-α / Opus C-R3-P1-3 close — atomic-exchange both state_ and
    //       next_drain_head_ in a single FIFO order; walk both lists with
    //       the publication primitive being the executor's dispatch/post
    //       happens-before edge (NOT the per-waiter phase CAS as in v1.2):
    //       CAS each `queued` waiter to `cancelled` FIRST; on CAS-success,
    //       schedule resumption via schedule_resume_on_bound_executor with
    //       *result_ written by the resumed handler (winner-only writes).
    //       Re-walk both state_ and next_drain_head_ in a stable loop until
    //       both observe nullptr (closes the unlock-vs-reaper splice race
    //       Opus C-R3-P1-3 named).
    //   (4) Wait for active_holders_count_ == 0 AND active_acquirers_count_
    //       == 0 (RC-α / Opus C-R3-P1-2 close — covers the in-flight acqu-
    //       irer between draining_.load and the fast-path CAS) AND in-
    //       flight reaped-resumption count == 0. The wait is implemented
    //       via co_await drain_latch_state::wait() — see §4.7.2 / §4.7.3
    //       for the cancellation-shield contract. Holder unlock() and
    //       reaper-resumption handlers atomic-load drain_latch_ptr_ and
    //       call drain_latch_state::notify() if the shared_ptr is non-null.
    //   (5) Return expected_t<void>{} on normal completion. RC-β / Opus
    //       C-R3-P1-4 close — if the cancel_and_drain awaitable is itself
    //       cancelled mid-drain (caller's parent cancellation_state fires
    //       cancellation_type::total), the reaper propagates and returns
    //       unexpected{sync_lock_aborted}: drain_latch_state stays alive
    //       via the reaper's shared_ptr until the in-flight resumption
    //       handlers finish; the mutex's draining_ remains true (subsequent
    //       async_lock returns sync_lock_drained); the reaper's in-flight
    //       CAS walks complete normally; partial-drain semantics are
    //       documented in §4.5 cancellation table + §4.7.3.
    // Idempotent (second call sees draining_==true and either subscribes
    // to the live drain_latch_state via drain_latch_ptr_.load() or — if
    // the drain has already published its release-edge AND the shared_ptr
    // is null — returns immediately with success); concurrent-call-safe
    // (drain_in_progress_ serialises walkers; non-reapers subscribe to the
    // same drain_latch_state via drain_latch_ptr_.load(acquire)).
    [[nodiscard]] asio::awaitable<expected_t<void>>
        cancel_and_drain() noexcept;

    // Query the current completion policy. Const, noexcept, lock-free.
    [[nodiscard]] completion_policy policy() const noexcept { return policy_; }

private:
    // Algorithm state encoding:
    //   not_locked         := uintptr_t{1}   (low-bit-set; distinguishable
    //                                          from any 8-byte-aligned waiter
    //                                          pointer).
    //   locked_no_waiters  := uintptr_t{0}.
    //   <pointer-to-waiter>:= the LIFO head (>= 8, 8-byte aligned).
    static constexpr uintptr_t not_locked        = 1;
    static constexpr uintptr_t locked_no_waiters = 0;

    std::atomic<uintptr_t>      state_             {not_locked};
    // RC-A close — mutex-owned residual FIFO chain (replaces v1.0's
    // awaiter-owned residual_ field). unlock() walks this list first; if
    // empty, walks the LIFO list anchored at state_.
    std::atomic<detail::async_mutex_awaiter*>
                                next_drain_head_   {nullptr};
    // RC-B close — drain primitive published by cancel_and_drain().
    std::atomic<bool>           draining_          {false};
    // RC-B close — concurrent-call serialiser for cancel_and_drain (Opus
    // N-P2-1). Only the first caller becomes the reaper.
    std::atomic_flag            drain_in_progress_ = ATOMIC_FLAG_INIT;
    // v1.2 / v1.3 RC-α winner-only close — post-CAS holder accounting.
    // v1.3 (Opus C-R3-P1-1 close) makes the increment WINNER-ONLY at the
    // grant CAS-success (fast-path await_ready CAS or drain-walker grant
    // CAS); decrement at unlock() entry. NOT incremented before any CAS
    // that may fail — closes the v1.2 "phantom holder count on CAS loss"
    // leak that blocked drain liveness.
    std::atomic<std::uint32_t>  active_holders_count_ {0};
    // NEW v1.3 / RC-α (Opus C-R3-P1-2 close) — in-flight acquirer epoch
    // counter. Incremented by async_lock(...)'s awaitable factory BEFORE
    // await_ready's draining_ load; decremented after await_ready returns
    // true (fast-path success or fast-fail) OR after await_suspend either
    // enrols on the LIFO (transferring tracking responsibility to the
    // LIFO walk) or fast-fails the drained-mutex bypass. cancel_and_drain
    // waits for this counter to observe zero in addition to active_holders_
    // count_ == 0. Closes the v1.2 race window between this thread's
    // draining_.load() and state_.compare_exchange(...) where the v1.2
    // active_holders_count_ alone did not cover the in-flight acquirer.
    std::atomic<std::uint32_t>  active_acquirers_count_ {0};
    // NEW v1.3 / RC-β (Opus C-R3-P1-4 / C-R3-P2-1 / N-R3-P1-1 / N-R3-P1-2
    // close) — atomic owner-reference to the lazy-constructed
    // detail::drain_latch_state owned by the in-flight reaper's shared_ptr.
    // Default-constructed (empty) at mutex construction — no executor
    // dependency, no asio::steady_timer member, no by-value runtime state;
    // the mutex stays constexpr-default-constructible per [arch §5.5]. The
    // reaper inside cancel_and_drain() constructs the state object via
    // std::make_shared and atomic-stores a shared_ptr<drain_latch_state>
    // into this member, pinning the latch state alive for the entire drain
    // epoch; concurrent callers atomic-load the shared_ptr and either
    // subscribe (non-null = epoch in flight) or observe the cleared epoch
    // (null = drain result already published).
    //
    // Note: std::atomic<std::shared_ptr<T>> is not lock-free in general.
    // The §6.2.2 row pins the ordering. The atomic-update path is cold
    // (only exercised when cancel_and_drain is called) and does not impact
    // the hot-path acquire/release cost.
    std::atomic<std::shared_ptr<detail::drain_latch_state>>
                                drain_latch_ptr_   {};
    completion_policy const     policy_            {completion_policy::dispatch};

    friend class detail::async_mutex_awaiter;
};

}  // namespace fixpp::sync
```

**Order-valid invariants (Codex C-P3-7 close):** the `static_assert` on `alignof(detail::async_mutex_awaiter)` is order-dependent on the `async_mutex_awaiter` definition; per the Codex P3 finding, place it **after** the awaiter struct's definition in §4.2, not at the §4.1 location. The order-valid invariants in §4.1 (independent of the awaiter's complete-type requirement) are:

```cpp
// In include/fixpp/core/sync/async_mutex.hpp, after the namespace closes
// in §4.1 but before the §4.2 awaiter definition is included:

static_assert(sizeof(uintptr_t) >= sizeof(void*),
              "fixpp::sync: state encoding requires uintptr_t to fit a "
              "pointer; reject targets that violate.");
static_assert(std::atomic<uintptr_t>::is_always_lock_free,
              "fixpp::sync: async_mutex requires lock-free "
              "std::atomic<uintptr_t>; the algorithm's wait-freedom claim "
              "depends on it. Targets where this is false are out of "
              "[const §II] Tier 1/2 scope and the algorithm rejects them at "
              "compile time (Opus N-P3-3 close).");
static_assert(std::atomic<fixpp::sync::detail::async_mutex_awaiter*>::is_always_lock_free,
              "fixpp::sync: next_drain_head_ atomic exchange requires "
              "lock-free std::atomic<async_mutex_awaiter*>.");
```

The `alignof(...)` invariant moves to §4.2 (after the awaiter struct's definition). The `enum class waiter_phase` is forward-declared in the namespace block above so the awaiter struct may name `std::atomic<waiter_phase>` in its body without requiring a prior complete-type definition.

#### 4.1.1 Rejected `async_lock` signatures

Closes `[2d §10] Q1` and `[2e §10] Q8`. Three viable alternatives were considered and rejected:

- **(a) Token-shaped: `awaitable<async_lock_guard> async_lock(asio::completion_token_for<...> auto token = asio::use_awaitable)`.** Forces the awaiter type to be a public template parameterised on the token; inflates the surface for zero gain on the v1.0 callsites, all of which do `co_await mutex.async_lock();` and never pass a non-default token. Per `[const §XI.1]` the engine standardises on `asio::awaitable<T>` as the composition primitive. **Rejected.**

- **(b) Direct-`co_await`-mutex: `co_await mutex` (cppcoro idiom).** Requires `async_mutex` itself to be an awaitable; mixes the lock-acquisition surface with the awaiter surface and makes `[[nodiscard]]` non-obvious to the caller. The named member-function gives one stable place where the `[[nodiscard]]` and cancellation contracts live. **Rejected.**

- **(c) Void-return-with-separate-guard: `awaitable<expected_t<void>> async_lock(); ... unlock() / lock_guard separately`.** Loses the RAII-via-return-value discipline; callers must remember to construct the guard from a successful return; mismatches the `[2e §6.4]` consuming pattern (`auto guard_or_err = co_await mutex.async_lock();` reads naturally). **Rejected.**

**Chosen:**

```cpp
[[nodiscard]] asio::awaitable<expected_t<async_lock_guard>>
    async_lock(std::pmr::memory_resource* mr = nullptr) noexcept;
```

Completion on awaiter's bound executor (per `[2d §7.4]`); honours `total` (per `[SYN §3.2 Q6b]` item 3) under the §4.5 CAS-arbitration race; default `dispatch` (per `[2d §7.4]`); per-mutex override per item 4 via the constructor's `completion_policy` argument; PMR fallback via the explicit `mr` overload (RC#2 fix). The `expected_t<async_lock_guard>` shape is consistent with `[2d §6.5]`'s `cancellable_dispatch → expected_t<void>` precedent and surfaces the cancellation outcome cleanly without crossing the `[arch §5.3]` "no exceptions on the hot path" rule.

### 4.2 The waiter awaiter type — atomic phase machine (RC#1 / RC-A)

The waiter is declared in `fixpp::sync::detail::async_mutex_awaiter` (private; not part of the user surface beyond `async_lock`'s return-shape contract). Layout precisely (≤ 96 B per §1.1; v1.1 / RC-A drops the awaiter-owned `residual_` field; v1.1 / RC-C adds the inline slot-storage buffer; v1.1 / Codex C-P3-7 reflows declaration order so the `enum class waiter_phase` definition precedes its use).

```cpp
// include/fixpp/core/sync/async_mutex.hpp (continued, in detail::)
namespace fixpp::sync::detail {

// Phase enum DEFINITION — the namespace block above forward-declared this
// at §4.1; the complete definition lives here (Codex C-P3-7 close —
// order-valid header sketch). v1.1 collapses the v1.0 four-state machine
// `{ queued, draining, cancelling, completed }` to three states (RC-A
// close — the intermediate `draining`/`cancelling` distinction is
// redundant under the mutex-owned-residual shape).
enum class waiter_phase : std::uint8_t {
    queued    = 0,  // pushed onto LIFO (state_) or spliced into
                    //   next_drain_head_ (after a residual handoff); still
                    //   cancellable. Drain CAS's `queued → granted` for
                    //   the first non-cancelled waiter; remaining `queued`
                    //   waiters stay `queued`.
    granted   = 1,  // unlock()'s drain (or fast-path acquire) has granted
                    //   ownership to this waiter; await_resume returns the
                    //   guard. Terminal.
    cancelled = 2,  // cancellation handler (or cancel_and_drain reaper)
                    //   CAS-acquired this waiter; await_resume returns
                    //   unexpected{sync_lock_aborted}. Terminal.
};

// async_mutex_awaiter — the waiter object. On the embedded path (RC#2
// default + RC-C inline-buffer path), lives INSIDE the caller's coroutine
// frame. On the PMR fallback path (RC#2 explicit `mr` overload), allocated
// from `mr`.
//
// v1.1 layout differences from v1.0:
//   - removes async_mutex_awaiter* residual_ (RC-A — mutex owns the residual
//     list via async_mutex::next_drain_head_).
//   - adds std::array<std::byte, 32> slot_storage_ (RC-C — feeds
//     detail::slot_allocator on the embedded-default path so the
//     cancellation handler closure does not touch the global heap when
//     mr == nullptr).
//
// Per [arch §5.5] alignment requirement: alignas(8) so the LIFO state_
// encoding's low-bit `not_locked` sentinel is distinguishable from any
// real waiter pointer.
struct alignas(8) async_mutex_awaiter {
    async_mutex*                     mutex_;     // back-pointer to the mutex.
    async_mutex_awaiter*             next_;      // intrusive link, reused by
                                                 //   both state_'s LIFO chain
                                                 //   and next_drain_head_'s
                                                 //   FIFO chain (RC-A).
    std::atomic<waiter_phase>        phase_;     // RC-A 3-state machine.
    asio::cancellation_slot          slot_;      // bound at await_suspend.
    std::coroutine_handle<>          coro_;      // continuation.
    expected_t<async_lock_guard>*    result_;    // sink for await_resume.
    std::array<std::byte, 32>        slot_storage_;  // RC-C inline buffer
                                                     //   used by
                                                     //   detail::slot_allocator
                                                     //   when mr == nullptr.

    bool await_ready() noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    expected_t<async_lock_guard> await_resume() noexcept;

    // Internal: invoked from the cancellation_slot handler. CAS's phase_
    // from `queued` to `cancelled`; if successful, writes result_ and then
    // schedules the resumed coroutine on its bound executor with
    // `unexpected{sync_lock_aborted}`. If the CAS loses to `granted`
    // (unlock() got there first), the cancellation is no-op — the waiter
    // has already been granted ownership and will receive the guard.
    void on_cancel(asio::cancellation_type type) noexcept;
};

// Compile-time alignment invariant — placed AFTER the awaiter definition
// (Codex C-P3-7 close: alignof on an incomplete class is ill-formed).
static_assert(alignof(async_mutex_awaiter) >= 8,
              "fixpp::sync: async_mutex_awaiter must be 8-byte-aligned so "
              "the low-bit `not_locked` sentinel is distinguishable from a "
              "real waiter pointer.");

}  // namespace fixpp::sync::detail
```

> **── v1.5 Erratum E-1 (2026-05-18, recorded at `/implement`; user-approved) ──**
>
> **Defect:** §4.2/§4.2.2 describe `async_mutex_awaiter` as a *raw C++20 awaiter*
> driven directly by `co_await m.async_lock()` (storing `coro_ = h` in
> `await_suspend(std::coroutine_handle<>)`), while §3/§6.1 (line ~155) require
> `async_lock()` to return `asio::awaitable<expected_t<async_lock_guard>>`. These
> are **mutually inconsistent**: `asio::awaitable<T>`'s promise `await_transform`
> does not pass arbitrary raw C++ awaiters through, and this mutex is *only* ever
> consumed inside asio coroutines (`[const §XI.1]`). The Gate-A rounds (1–2)
> verified document/transcription fidelity only, never implementability, so this
> slipped through. This erratum resolves it **without re-opening any design
> decision** — every guarantee (FIFO, cancellation CAS-arbitration, RC-A residual,
> drain, zero-global-heap, PMR three-case, ≤96 B awaiter) is preserved.
>
> **Resolution (binding for `/implement`; the design doc remains authoritative as
> amended by this erratum):** `async_lock(mr)` is realized as an
> `asio::async_compose`/`asio::async_initiate` operation over completion
> signature `void(expected_t<async_lock_guard>)`. The `async_mutex_awaiter` is the
> **operation/waiter state object** (not separately `new`'d). Field mapping:
> the design's `coro_` continuation is **replaced by the composed-operation
> completion handler** (semantically identical: "resume the suspended caller with
> the result"); all other fields (`mutex_`, `next_`, `phase_`, `slot_`, `result_`,
> `slot_storage_`) and every I-01..I-31 ordering are unchanged. `await_ready` /
> `await_suspend` / `await_resume` prose maps onto the initiation/intermediate/
> completion stages of the composed op respectively; the §4.2.2 step-4
> `asio::bind_allocator(detail::slot_allocator{this, mr})` is bound as the
> **completion handler's associated allocator**, so `async_compose`'s own
> operation-state storage **and** the cancellation-slot handler closure both draw
> from the §4.3.4 three-case storage (caller-frame inline `slot_storage_` when
> `mr==nullptr`+HALO / promise-allocator when no-HALO / `mr` when non-null) —
> **never a raw global `new`**. The zero-global-heap (`[const §VIII.5]`), HALO
> (§6.4, seam #9), PMR-fallback (§4.3, seam #10), `slot_allocator` three-case
> (seam #21) and awaiter byte-budget (≤96 B, T011/T078) contracts are thereby
> preserved exactly. Any implementation that separately heap-allocates the waiter
> node via global `operator new` is **non-conforming** to this erratum.

> **── v1.6 Erratum E-2 (2026-05-18, recorded at `/implement`; Codex-diagnosed, Opus-cross-reviewed + source-confirmed, user-authorized) ──**
>
> **Defect (cancellation/lifetime contradiction introduced by E-1).** E-1 made
> `async_mutex_awaiter` *frame-local* while §4.2 keeps the awaiter object itself
> as the intrusive node linked through `state_` and `next_drain_head_` (it carries
> `next_`, `phase_`, `result_`). §4.5 (`total` row), §4.5.1 window 4, and the
> memory-ordering invariant I-07 mandate that the cancel-CAS winner
> (`queued → cancelled`) write the aborted result and **then schedule resumption
> promptly on the bound executor**. For a cancelled *interior* waiter still linked
> in the singly-linked intrusive LIFO, prompt resumption runs the bound-executor
> completion, which resumes and then destroys the `async_lock(mr)` coroutine frame
> — destroying the frame-local awaiter (hence its `next_`/`phase_`/`result_`)
> **while `state_` / `next_drain_head_` still hold raw pointers to it**. A
> Treiber-style intrusive LIFO has no lock-free interior-node deletion, so the
> cancel winner cannot first unlink itself. Result: a guaranteed use-after-free.
> The as-implemented mitigation (defer resumption to a later `unlock()` walk)
> avoids the UAF but loses liveness when no later walker exists (seams #16
> `sync_race_multi_cancel`, #17 `sync_race_cancel_during_resume` hang) and
> swallows residual cancellation (#22 `sync_residual_cancel_graceful`), and
> `state_`'s grant path mis-arbitrates against a cancelled node (#4
> `sync_cancellation_mid_wait` aborts — cancelled waiter wrongly granted). Codex
> (2026-05-18) proved the three constraints — *E-1 frame-local awaiter*, *§4.2
> awaiter-is-the-node*, *§4.5 prompt bound-executor cancel-resume* — are jointly
> unsatisfiable by any surgical implementation; independently cross-reviewed and
> source-confirmed by Opus (`async_mutex.hpp:214/411` — node is `async_mutex_awaiter*`;
> `:511-524` — `on_cancel` defers; `:819-865` — walker derefs the frame).
>
> **Resolution (binding for `/implement`; the design doc remains authoritative as
> amended by E-1 *and* this erratum).** Split the waiter into two objects, changing
> exactly one assumption — *"the frame-local awaiter is the intrusive node"* →
> *"the frame-local awaiter attaches to a stable, pooled intrusive node"*:
>
> 1. `detail::async_mutex_awaiter` — **remains frame-local and HALO-eligible**;
>    owns the E-1 composed-op completion-handler machinery + `slot_storage_`,
>    plus a `detail::waiter_record* record_` attachment pointer. It is **no longer**
>    the intrusive node.
> 2. `detail::waiter_record` — **the stable intrusive node** linked through
>    `state_` and `next_drain_head_`. Carries `next_`, the `phase_` CAS atom, the
>    terminal `expected_t<async_lock_guard>` result storage, the back-pointer
>    `mutex_`, an `std::atomic<async_mutex_awaiter*> attached_awaiter_`, inline
>    bound-executor storage (Gap-B below), and a `std::atomic<std::uint32_t>
>    refcount_` lifetime token. Its lifetime is independent of the coroutine frame.
>
> **Normative remapping (no semantic change to any other clause).** Wherever
> §4.2 / §4.2.1 / §4.2.2 / §4.2.3 / §4.5 / §4.5.1 / §4.5.2 / §4.7.2 and the
> `result_`-ownership paragraph below say the *awaiter* is the intrusive node or
> carries `next_`/`phase_`/`result_`, **read `waiter_record`**. `state_` and
> `next_drain_head_` store `detail::waiter_record*`. The §4.5 prompt-resume
> semantics, the v1.4 CAS-then-publish writer discipline, the one-winner walk, FIFO,
> RC-A residual observability, the three-state phase machine, and every I-01..I-31
> ordering are **unchanged** — they now apply to `waiter_record::phase_` /
> `waiter_record::result_`. This erratum makes the §4.5 contract *implementable*;
> it does not relax it. `await_resume` reads the terminal result through
> `record_` (the frame may legitimately outlive nothing; the *node* is the
> stable storage), superseding the "result_ points into caller-frame storage"
> clause of the v1.2/v1.4 paragraph below for the contended path.
>
> **Gap-close A — reclamation is pinned to the existing single-drainer invariant
> (no hazard pointers / epoch GC).** The mutex already serializes all list
> *consumption*: the only two structural walkers are `unlock()` (runs solely
> under logical lock ownership ⇒ at most one) and `cancel_and_drain()`'s reaper
> (single, gated by `drain_in_progress_.test_and_set(acq_rel)`), and they are
> **mutually exclusive** because `unlock()` takes the drain-aware short-circuit
> (§4.5.2 — does not walk/grant) when `draining_ == true`. Hence at most one
> walker ever traverses the lists; the only other actor on a node is its
> per-waiter cancellation handler, which performs **no structural mutation** —
> it arbitrates solely via the `phase_` CAS. `waiter_record` reclamation is
> therefore single-shot via `refcount_` with edges: **+1 creator** (the
> `async_lock` initiation); **a single in-lists membership ref _transferred_**
> (never re-counted) across every `state_ ⇄ next_drain_head_` splice; **+1 per
> scheduled prompt-resume** (resumer ref, dropped when the bound-executor
> completion runs); **+1 while `attached_awaiter_ != nullptr`** (dropped at
> `await_resume`). The final `refcount_.fetch_sub(1, acq_rel) == 1` reclaims the
> node (returns it to the pool / `mr`) — new invariant **I-32**. Because there is
> never a *competing* walker, the classic "load pointer, it's freed before I take
> a ref" hazard cannot arise; the only race is the sole walker vs. the
> cancel-CAS winner on one node, fully arbitrated by `phase_` + the resumer ref
> keeping the node alive until the scheduled completion runs. Refcount is
> sufficient and TSan-clean *because of* the single-drainer property — this is a
> normative precondition of the protocol, not an implementation detail.
>
> **Gap-close B — inline bound-executor storage (no type-erasure heap).**
> `waiter_record` stores the bound executor in an inline aligned buffer
> `exec_storage_`, **not** a heap-allocating type-erased `asio::any_io_executor`,
> mirroring E-1's `slot_storage_` discipline. A `static_assert` requires the
> project's bound executor (the `[2d §4.8]` `session_executor` wrapper over a
> strand / `any_io_executor` SBO) to fit `exec_storage_`; overflow surfaces
> `unexpected{sync_lock_alloc_failed}` (error slot 44) — the identical contract
> to the §4.3.4 case-1 inline-buffer overflow. This keeps the contended
> `mr == nullptr` path free of global `operator new` from executor type-erasure.
>
> **`waiter_record` storage policy (extends §4.3 / §4.3.4).** (a) **Uncontended**
> (`await_ready` fast-path CAS wins): **no `waiter_record`** is created — zero
> allocation by construction (unchanged). (b) **Contended, `mr == nullptr`:**
> `waiter_record` is drawn from a **per-mutex bounded, pre-reserved freelist/slab**
> owned by `async_mutex` — zero global `operator new`/`delete` on this path.
> (c) **Contended, `mr != nullptr`:** `waiter_record` is allocated from `mr` and
> reclaimed back to `mr` (extends the §4.3 PMR three-case to the node). Pool or
> `mr` exhaustion surfaces `unexpected{sync_lock_alloc_failed}` (slot 44).
> **Constitutional posture (corrects the "weakens `[const Art.VIII §5]`"
> framing).** `[const Art.VIII §5]` makes *arena/PMR the **default** sanctioned
> allocator on the hot path*; a bounded per-mutex arena is exactly that
> mechanism, **not** a deviation. E-2 therefore **preserves** the zero-global-`new`
> guarantee (uncontended: by construction; contended: by the pre-reserved arena),
> and does not require a constitution amendment or a §VIII deviation
> justification. The HALO (§6.4 #9), PMR-fallback (§4.3 #10), `slot_allocator`
> three-case (#21), and CAS-then-publish (#28) seams are preserved; seams #4 / #16
> / #17 / #22 become satisfiable. Any implementation that uses global
> `operator new` for a `waiter_record` on the contended `mr == nullptr` path, or
> that uses a heap-allocating type-erased executor, is **non-conforming** to E-2.
>
> **Scope / Gate note.** E-2 amends Gate-A-converged 2f. Recorded at `/implement`;
> Codex-diagnosed, Opus-authored after independent source confirmation,
> user-authorized 2026-05-18 ("I finalize E-2, then Codex implements"). Companion
> edits land in `specs/006-async-mutex/data-model.md` (new entity **E2a
> `waiter_record`**; `state_`/`next_drain_head_` retyped to `waiter_record*`;
> E2 `async_mutex_awaiter` gains `record_`, loses `next_`/`phase_`/`result_`
> ownership; new invariant **I-32**; I-06/I-07/I-08 redefined on
> `waiter_record::phase_`). This re-touches 006 Gate A scope — flag for the
> eventual `/gate-a`.

> **E-5 / 047 amendment — drain convergence (I-33) + the seq_cst feeder/sink
> handshakes (shipped via `047-async-mutex-drain-reap`).** A latent lost-wake in
> `cancel_and_drain()` was surfaced by the in-flight 046-atomic-shared-ptr witness
> and is fixed here. The v1.5 reaper read its lists, then quiesced on the holder
> count, then finalized in a single linear pass; a waiter that pushed onto `state_`
> (or a residual onto `next_drain_head_`) **after** the reaper's list read but
> **before** finalize was orphaned — the reaper never re-scanned. Root cause: the
> walk happened **before** the counter quiesce, and the publication of a late push
> was not ordered against the reaper's termination decision.
>
> - **I-33 (drain convergence — NEW).** `cancel_and_drain()` is a **converging
>   reap+quiesce loop**, not a linear walk: it drains both lists, reads the three
>   **feeder** counters (`active_acquirers_count_`, `active_unlockers_count_`
>   **seq_cst**; `in_flight_resumptions_` acquire) **before** the **sink**
>   (`active_holders_count_` acquire), waits on any nonzero feeder, then performs a
>   **confirming list-exchange**; it finalizes **only** when, in one pass, every
>   feeder is 0 ∧ the sink is 0 ∧ the confirming exchange privatizes an empty
>   `state_`/`next_drain_head_`. Edge #1 (push-visibility): a contended push is
>   sequenced-before the acquirer's terminal decrement, so the feeder-wait→re-loop
>   →re-drain path cannot miss it. This is the terminating invariant that makes the
>   late-waiter reap sound.
> - **I-32 single-walker soundness — AMENDED.** I-32's "no competing walker"
>   precondition is now **enforced during finalize** by the `active_unlockers_count_`
>   bracket (below): the reaper waits `unlockers==0` before the confirming exchange
>   and finalize, so no concurrent `unlock()` is mid-walk during the finalize CAS —
>   single-walker, hence no `next_drain_head_` residual can be pushed in the
>   ~tens-of-instructions gap between the confirming exchange and the finalize CAS
>   that the two-atomic state cannot re-verify atomically.
> - **`active_unlockers_count_` Dekker handshake (edge #2′ — NEW mutex member).**
>   `unlock()`'s whole body is bracketed by a single RAII guard that
>   `fetch_add(1, seq_cst)` at the very top (**before** `holders--` and the seq_cst
>   `draining_` read) and decrements+notifies (seq_cst, via the audited
>   `feeder_dec_and_notify` helper) on scope exit **after** any `push_residual`/
>   grant. The two recursive re-drives (`unlock()` tail calls) each take their own
>   guard → transient count 1→2→1→0, never a premature 0 mid-walk. This is the
>   feeder the reaper waits on to guarantee single-walker (see amended I-32).
> - **seq_cst `draining_` ↔ `active_acquirers_count_` handshake (edge #2).** The
>   acquirer-side entry `active_acquirers_count_.fetch_add` and **both** `async_lock`
>   `draining_` loads are **seq_cst** (the Dekker store/loads), and
>   `cancel_and_drain`'s `draining_.store(true)` is seq_cst. Either the acquirer
>   sees `draining_` and fast-fails (decrement+notify), or the reaper sees the
>   acquirer's increment and waits — never both miss. All acquirer + unlocker
>   terminal decrements route through `feeder_dec_and_notify` (the single audited
>   `fetch_sub(seq_cst); if (draining_) notify()` publication site).
> - **Single-atomic `drain_terminal` arbitration (replaces `released_`/`aborted_`).**
>   `drain_latch_state` now carries one `std::atomic<drain_terminal>{pending}`
>   (`enum class drain_terminal { pending, released, aborted }`) instead of two
>   independent bools. `signal_release()`/`signal_abort()` **CAS from `pending`**,
>   close the channel **only on a win**, and return whether they won — two bools
>   could not be mutually arbitrated (a late cancel could publish `aborted` while
>   the reaper published `released`). The idempotent fast paths and `subscribe()`
>   read the single terminal state; F-2 (abort epoch keeps the latch published) is
>   preserved. The reaper finalizes using the `signal_release()` CAS-win result
>   (clear `drain_latch_ptr_` + report success only on a win; else keep the latch
>   and return aborted).
> - **Two cancellation defects fixed alongside the drain restructure.** (1) An
>   **entry-pending acquirer leak**: asio `await_transform` throws `operation_aborted`
>   **before** running an async op's initiation when a `total` cancellation is
>   already pending; the entry increment lived before the `co_await`, leaking its
>   in-lambda decrement → `cancel_and_drain` hung on `acquirers!=0`. Fixed by moving
>   the seq_cst increment to the initiation's first statement (a skipped initiation
>   never increments) + a try/catch converting the throw to the
>   `unexpected{sync_lock_aborted}` contract return. (2) A **grant/second-gate
>   double-schedule**: the drained-gate and the push-loop grant did an unconditional
>   `phase_.store + schedule` while `on_cancel` was wired → double-`invoke_handler`
>   UAF; fixed by `queued→{cancelled,granted}` CAS (schedule only on the win; on
>   grant CAS-loss release the lock the cancelled waiter will never use via
>   `unlock()`).
> - **Witnessing / SC-006.** The drain-convergence and feeder/sink ordering are
>   witnessed by W-orig (`drain_latch_publish_acquire`, 4×32×100) + W-B1..W-B3 and
>   the mutation-revert matrix. The three **cross-thread-only** windows (entry-pending
>   leak, second-gate B4 CAS, grant CAS-loss) are NOT same-thread witnessable —
>   `async_lock`'s own `reset_cancellation_state(enable_total)` at entry wipes any
>   pre-entry cancellation and there is no suspension between that reset and the
>   throwing `async_initiate`, so the throw window is reachable only by a cross-thread
>   emit (where asio `cancellation_signal::emit` is itself thread-unsafe — a harness
>   UAF). They carry SC-006 waivers-with-proof + the `DISABLED_` W-B4 stress
>   reproducer (**L-047**). The parked-waiter cancellation path stays same-thread
>   witnessed by `test_race_cancel_pre_drain`.
>
> **Scope / Gate note.** E-5 amends Gate-A-converged 2f (047 Gate A converged 3
> rounds). New mutex member `active_unlockers_count_` (4 B) + `drain_latch_state`
> `released_`/`aborted_`→`terminal_` are private; no public/ABI surface change
> (FR-007). Companion edits in `specs/047-async-mutex-drain-reap/` (research.md
> multi-edge proof, data-model I-33 + the `active_unlockers_count_` model).

**`result_` validity window and ownership (v1.2 / Opus N-P3-3 round-2 close; v1.4 CAS-then-publish close).** `result_` is a non-owning raw pointer that points into the *caller-frame storage* for the `expected_t<async_lock_guard>` value the coroutine yields via `await_resume`. Concretely: the awaiter is constructed in-place at the `co_await m.async_lock()` site; `result_` is initialised by the `async_lock(...)` awaitable factory to point at a stack-local `expected_t<async_lock_guard>` slot in the suspended coroutine's frame (or, on the PMR fallback path, at the equivalent slot allocated alongside the awaiter from `mr`). **Lifetime contract:** `result_` is valid from `await_suspend(h)` entry through `await_resume()` return, inclusive — i.e., for the duration of the awaiter's suspension. **Validity ends at `await_resume` return** (the coroutine resumes, reads `*result_`, and the awaiter is destroyed in the embedded path or de-allocated back to `mr` in the PMR-fallback path). **Writer-side discipline (v1.4): CAS-then-publish.** Potential writers (the unlock-walker and the cancellation-handler, OR the reaper-walker in §4.7.2 step (f) and the cancellation-handler) first arbitrate ownership by CAS'ing `phase_` from `queued` to their terminal state (`granted` or `cancelled`) with `memory_order_acq_rel`. **Only the CAS winner writes `*result_`; the loser observes terminal phase and does not touch `*result_`.** The winner then writes `*result_` and schedules the coroutine on its bound executor. The resumed coroutine's `await_resume` performs `phase_.load(acquire)` first, then reads `*result_`; that acquire-load synchronises with the winner's release-CAS, and the result-slot write is sequenced before the bound-executor resumption. **Critical section closure:** the winner-side `*result_ = ...` write is sequenced *before* `schedule_resume_on_bound_executor(awaiter)`, so the awaiter remains alive for the duration of the writer's critical section and `result_` continues to point at valid storage until the resumed coroutine returns from `await_resume()`. The §6.2.2 row "`result_` slot publication" governs the cross-thread publication ordering. The §9 seam #28 verifies the CAS-then-publish arbitration under TSan.

**Residual ownership lives on the mutex (RC-A close).** The v1.0 design carried `residual_` on the awaiter — the granted waiter "owned" the FIFO chain of remaining queued waiters from the unlock-drain. Codex C-P1-2 / Opus C-P1-2 / Opus N-P1-1 / Opus N-P1-2 collectively showed this is unimplementable: the `async_lock_guard` carries only `async_mutex*` (§4.4), `unlock()` is invoked on `async_mutex*`-only state, and the awaiter is destroyed/deallocated when `await_resume` returns. The granted waiter's residual list was unreachable from `async_mutex::unlock()` and was destroyed on holder-cancellation mid-critical-section. v1.1's fix moves the residual chain into `async_mutex::next_drain_head_` (a `std::atomic<async_mutex_awaiter*>` field on the mutex itself); every `unlock()` walks `next_drain_head_` first; `cancel_and_drain()` (§4.7.2) atomically exchanges and reaps both `state_` and `next_drain_head_`.

#### 4.2.1 `await_ready` — uncontended fast path (v1.2 RC-B + v1.3 RC-α post-cap close)

`await_ready` is preceded by the awaitable factory's **acquirer-epoch increment** (NEW v1.3 / RC-α — Opus C-R3-P1-2 close), then performs two atomic operations on the fast path:

**Awaitable-factory pre-step (NEW v1.3 / RC-α).** When `async_lock(mr)` is called, the awaitable factory (the function that constructs the awaiter object before `co_await` starts driving it) executes `mutex_->active_acquirers_count_.fetch_add(1, memory_order_acq_rel)` BEFORE the awaiter's `await_ready` is invoked. This covers the in-flight acquirer window between `await_ready`'s `draining_.load()` and the fast-path CAS — closes Opus C-R3-P1-2 (v1.2's `active_holders_count_` covered POST-CAS holders but not in-flight acquirers, so a `cancel_and_drain` could observe count == 0 and complete while a fresh acquirer was mid-fast-path-CAS). The decrement is paired in three places (specified below); every code path through `await_ready` / `await_suspend` decrements exactly once.

`await_ready` then performs **two** atomic operations:

1. **Drain-state pre-check.** First, `mutex_->draining_.load(memory_order_acquire)`. If `true`, the mutex has been drained via `cancel_and_drain()` (§4.7.2) and is no longer accepting acquisitions; `await_ready` writes `*result_ = unexpected{sync_lock_drained}`, sets `phase_ = cancelled` (release; for symmetry with the `await_suspend` drained-fast-fail path), **decrements `active_acquirers_count_.fetch_sub(1, memory_order_acq_rel)`** (RC-α decrement-point #1), and **returns true**. The coroutine resumes via `await_resume()`, reads `*result_`, and yields the unexpected outcome to the caller without ever touching `state_`.

2. **Acquire CAS.** Only if step (1) observed `draining_ == false` does `await_ready` run the existing acquire CAS — `state_: not_locked → locked_no_waiters` (`memory_order_acquire` on success, `memory_order_relaxed` on failure). On CAS-success, `await_ready` increments `mutex_->active_holders_count_.fetch_add(1, memory_order_acq_rel)` (winner-only, post-CAS — v1.3 / RC-α / Opus C-R3-P1-1 close: the v1.2 pre-CAS increment leaked on CAS-loss; v1.3 increments the holder count ONLY on the successful CAS), **decrements `active_acquirers_count_.fetch_sub(1, memory_order_acq_rel)`** (RC-α decrement-point #2 — the acquirer is now a counted holder), then returns true. On CAS-failure, `await_ready` returns false and `await_suspend` is invoked (the acquirer-epoch decrement is NOT performed here; `await_suspend` either decrements after enrolling on the LIFO or after fast-failing the drained-mutex bypass, decrement-point #3).

**Race window between step (1) and step (2) — closed under v1.3 / RC-α.** A `cancel_and_drain()` call may execute its `draining_.store(true)` between this thread's step (1) `draining_.load()` and step (2) `state_.compare_exchange(...)` (v1.4: after first publishing `drain_latch_ptr_`). The acquirer-epoch counter `active_acquirers_count_` covers this window: `cancel_and_drain` waits for `active_acquirers_count_ == 0` AND `active_holders_count_ == 0` before completing the drain — closes Opus C-R3-P1-2 (v1.2's `active_holders_count_` alone did not cover this in-flight window because the count was incremented only AFTER the CAS, leaving a window where the acquirer was neither in the holder count nor enrolled on the LIFO; v1.3 introduces `active_acquirers_count_` for exactly this in-flight tracking). If the CAS succeeds in this window, the acquirer becomes a holder; cancel_and_drain observes `active_holders_count_ > 0` and continues to wait until the holder unlocks. If the CAS fails (state_ was acquired by someone else), the acquirer enters await_suspend; the drained-bypass path in §4.2.2 fast-fails the acquirer with `sync_lock_drained` and decrements `active_acquirers_count_`. Either way, no acquirer slips past the drain. The §9 seam **"`cancel_and_drain` covers in-flight acquirer"** (#25, NEW v1.3) verifies this window.

On step-(2)-success, `await_suspend` is **not** called and the coroutine never suspends; `await_resume` runs immediately and returns the `async_lock_guard`. **Zero allocation**, **zero cancellation-slot wiring**, **three atomic operations** on the success path (`active_acquirers_count_` increment from the factory + `draining_` load + `state_` CAS) plus the holder-counter increment + acquirer-counter decrement, no executor hop. Tier 1 ceiling ≤ 20–25 ns warm-cache (§6.3 row 1) — the added `active_acquirers_count_` increment + decrement are two warm-cache atomic RMWs (≈ 5 ns each — §6.3 row 1's per-component breakdown updates accordingly).

The cancellation slot is not registered on the fast path — there is no waiter to cancel.

#### 4.2.2 `await_suspend` — enqueue + slot-bind + phase init

`await_suspend(h)`:

1. Checks `mutex_->draining_.load(memory_order_acquire)`. If true (RC-B close — defense-in-depth for the case where v1.2's §4.2.1 `await_ready` `draining_` check observed false but `cancel_and_drain` landed concurrently between steps (1) and (2) of `await_ready`), writes `*result_ = unexpected{sync_lock_drained}`, sets `phase_ = cancelled` (release), **decrements `mutex_->active_acquirers_count_.fetch_sub(1, memory_order_acq_rel)`** (RC-α decrement-point #3a — Opus C-R3-P1-2 close: drained-bypass acquirer is no longer in flight), and resumes the continuation inline on the calling thread — no enqueue, no slot wiring. The two layered checks (§4.2.1 `await_ready` BEFORE the fast-path CAS + here in `await_suspend`) jointly close the post-drain fast-path bypass per Codex N-P1-1 / Opus C-N-P1-1 round-2.
2. Otherwise stores `coro_ = h`; initialises `phase_ = queued` (relaxed init — visible only after the LIFO push's release CAS).
3. Recovers the awaiter's `cancellation_state` via `co_await asio::this_coro::cancellation_state` (or `h.promise().get_cancellation_state()` exposition-equivalent).
4. **Binds the cancellation slot's handler-closure allocator** via `asio::bind_allocator(detail::slot_allocator{this, mr})` where `this` provides access to the awaiter's inline `slot_storage_` buffer and `mr` is the explicit parameter passed to `async_lock(mr)` (RC-C close — replaces the v1.0 prose-only `slot_allocator(mr)` reference; see §4.3.4 for the three-case storage table). Codex C-P2-6 / Opus N-P2-2 / RC-C close: the slot's handler closure storage uses (i) the awaiter's inline 32-byte buffer when `mr == nullptr` and HALO fires; (ii) the per-promise allocator (typically global heap) when `mr == nullptr` and HALO does not fire — observable in benchmark mode and noted by §6.4 as non-fatal iff the §9 PMR-fallback seam #10 passes; (iii) `std::pmr::polymorphic_allocator<void>{mr}` when `mr != nullptr`. Never the global heap on the v1.0 hot path.
5. `slot_.assign(...)` registers `on_cancel` as the cancellation handler; the lambda captures `this` and dispatches into `on_cancel(type)`.
6. CAS-pushes onto the LIFO list per the §6.2 push protocol (`compare_exchange_weak(state_, old → &this, release / acquire)` with retry on stale `old`). On CAS-success, **decrement `mutex_->active_acquirers_count_.fetch_sub(1, memory_order_acq_rel)`** (RC-α decrement-point #3b — the acquirer is now enrolled on the LIFO and is reachable via the LIFO walk + per-waiter `phase_` atom; tracking responsibility transfers from the acquirer-epoch counter to the LIFO).
7. If during the push the state transitions to `not_locked` (concurrent unlock raced), the awaiter CAS's to `locked_no_waiters` directly, **decrements `active_acquirers_count_`** (decrement-point #3c — the acquirer transitioned to a holder via the unlock-race fast path; `active_holders_count_` is incremented at the same CAS-success, mirroring §4.2.1 step 2's winner-only protocol), and resumes inline — no enqueue, no slot fired.

#### 4.2.3 `await_resume` — read result, clear slot

`await_resume()` reads the `phase_` atomic (`memory_order_acquire`) and the `result_` slot, returns the `expected_t<async_lock_guard>` value:

- `phase_ == granted` → returns `expected_t<async_lock_guard>{std::in_place, async_lock_guard{mutex_}}` (the friend-only ctor — §4.4).
- `phase_ == cancelled` && `result_` carries `unexpected{sync_lock_aborted}` (cancelled, normal path) or `unexpected{sync_lock_drained}` (RC-B fast-fail under `draining_ == true`) → returns the unexpected.

Then clears the slot via `slot_.clear()`. Cancellation-after-resume is a no-op on this awaiter (Opus N-P1 close, retained from v1.0): the slot is cleared, and even if a stale signal arrives between the read and the clear, the lambda's first action is to CAS-acquire `phase_`; the CAS fails because phase is already `granted` or `cancelled` (terminal), so the lambda returns without dereferencing potentially-freed state.

**Awaiter lifetime is now safe under the mutex-owned-residual shape (RC-A close — closes Codex C-P1-1 UAF defect).** v1.0's design left the awaiter's `next_` pointer reachable from `state_` even after the cancelled awaiter resumed and was destroyed (cancellation was "logical only"; `state_` was unaffected). v1.1's drain (§4.5.2) physically detaches the entire LIFO chain (`state_.exchange(...)`) before walking it; cancelled waiters are observed via `phase_ == cancelled` and skipped from the residual splice into `next_drain_head_`. After `await_resume` returns and the awaiter is destroyed (embedded path) or deallocated (PMR path), no external pointer threads through it: cancelled waiters are not pushed into `next_drain_head_`, and the LIFO list anchored at `state_` was emptied at unlock time. The PMR-fallback awaiter (`mr != nullptr`) de-allocates itself back to `mr` after `await_resume` returns — driven by the awaitable's promise/awaiter lifetime.

### 4.3 The PMR-aware fallback path (RC#2)

#### 4.3.1 Mechanism — caller supplies the resource

The waiter-embedded design (item 1) requires the awaiter to live in the caller's coroutine frame. There is one class of callers where that does not hold: type-erased completion handlers (`asio::any_completion_handler<...>`) and any composed operation whose intermediate state is type-erased.

**The rule (RC#2 fix; replaces v0.1's invented "compile-time detection at the `co_await` site"):** the caller passes `mr` explicitly via `async_lock(mr)`. When `mr == nullptr` (the default, the v1.0 hot path), the awaiter is constructed in-place in the caller's coroutine frame (HALO-friendly). When `mr != nullptr`, the awaiter is allocated from `mr` via the per-promise allocator hook, and the cancellation slot's storage is bound to the same resource via `asio::bind_allocator(detail::slot_allocator{this, mr})` (RC-C close — see §4.3.4 for the three-case storage table that resolves Codex C-P2-6 / Opus N-P2-2). No global-heap touch on the v1.0 hot path.

Allocation failure under `mr` (or under the embedded inline-buffer path on overflow) routes through `fixpp::core::detail::trap_throw` (per `[2a §4.2]`) and surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}` (§6.5).

`core::async_mutex` therefore **does not reach into `session/`** (RC#2 layering fix); it operates on the resource the caller supplies. The §9 seam **"PMR fallback exercise"** (#10) gates against an actual `asio::any_completion_handler` shape with `mallocnesia` interceptor and verifies zero global-heap allocations on **both** the `mr == nullptr` (embedded) and `mr != nullptr` (PMR) branches (Codex C-P2-6 close — the v1.0 single-branch coverage is split).

#### 4.3.2 Session-side helper — `fixpp::session::async_lock_via_session_executor`

The session-module spec ships a thin helper that recovers the per-session resource from the awaiter's bound `session_executor` and forwards into `async_lock(mr)`:

```cpp
// include/fixpp/session/async_lock_via_session_executor.hpp
//
// Layering note: this helper lives in session/, downstream of core/. It is
// the [2d §4.5] / [2d §8] / [2d §4.8] glue that core::async_mutex does NOT
// own. core::async_mutex ships zero dependency on Session/EngineConfig.
namespace fixpp::session {

[[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>>
    async_lock_via_session_executor(fixpp::sync::async_mutex& m) noexcept;

}  // namespace fixpp::session
```

Implementation (sketch):

1. `co_await asio::this_coro::executor` returns the awaiter's bound executor; recover the `fixpp::core::session_executor` value (per `[2d §4.8]`).
2. `session_executor::session_ptr()` (the public member-function accessor — `[2d §4.8]` v0.4) yields the typed `Session*`.
3. **`Session::session_arena() noexcept -> std::pmr::memory_resource*`** (engine-internal accessor, **published by Appendix D §D.1 at sign-off**) yields the per-session resource carried as `SessionConfig::session_arena` per `[2d §4.5]`.
4. The helper calls `co_await m.async_lock(arena)` and forwards the result.

If the awaiter's bound executor is **not** a `session_executor` value (engine bootstrap, listener accept, control-plane handlers — i.e., outside any session), the helper returns `expected_t::unexpected{error::sync_lock_outside_session}`; the caller is expected to use the explicit `async_lock(mr)` overload directly with whatever resource is contextually appropriate. **No v1.0 hot path takes this branch** (every v1.0 use case acquires from inside a session).

#### 4.3.3 What 2f does NOT specify

- The Phase-4 session-module spec governs how non-store-write callers acquire the resource for type-erased shapes.
- 2f does NOT inspect `EngineConfig::default_session_resource` (RC#2 layering fix). If a non-session caller wants engine-default behaviour, the caller passes `EngineConfig::default_session_resource` explicitly to `async_lock(mr)`.

#### 4.3.4 Slot-allocator storage cases (RC-C close — Codex C-P2-6 / Opus N-P2-2)

The cancellation slot's handler-closure storage is supplied by the project-internal allocator type `fixpp::sync::detail::slot_allocator` (declared in §3.1 row, defined in `include/fixpp/core/sync/async_mutex.hpp` `detail::` block). The allocator is `Allocator`-shaped (modelling the standard allocator concept so it composes with `asio::bind_allocator`) and parameterised on `(async_mutex_awaiter* awaiter, std::pmr::memory_resource* mr)`. Three cases are exhaustive:

| Case | Condition | `slot_allocator` source | Storage | Allocation-failure path |
|---|---|---|---|---|
| 1 — Embedded path with HALO firing | `mr == nullptr` AND the awaiter is HALO-elided (i.e., the caller's coroutine frame contains the awaiter, including its inline `slot_storage_` buffer) | The 32-byte `awaiter->slot_storage_` inline buffer; `slot_allocator` over `std::pmr::null_memory_resource()` is fed as the fallback (refuses any allocation request that exceeds the inline buffer's capacity). | Caller's coroutine frame; zero global-heap touch. | If a hypothetical handler closure exceeded 32 B (current ASIO `cancellation_slot` handler closures are typically ≤ 24 B; the 32-byte budget carries 25% headroom), `null_memory_resource()` throws `std::bad_alloc`, caught by `trap_throw`, surfaces as `unexpected{sync_lock_alloc_failed}`. **Hard contract violation under `[const §VIII.5]` zero-`new`/`delete` discipline** — the §9 PMR-fallback seam #10 verifies the inline-buffer path holds for the v1.0 ASIO version. |
| 2 — Embedded path with HALO not firing | `mr == nullptr` AND HALO did not elide the awaiter (the awaiter is on the heap per the coroutine machinery's promise allocator — typically global heap unless the user customised `operator new`/`operator delete` via a promise-allocator hook) | The same global heap the coroutine frame uses (the awaiter's inline `slot_storage_` buffer is still preferred but lives on the heap; `null_memory_resource()` fallback applies on overflow). | Coroutine frame on the global heap (HALO failed to elide). | Same as case 1. **Observable in benchmark mode** — §6.4's HALO-firing seam #9 detects this; non-fatal **iff** §9 PMR-fallback seam #10 passes for the same toolchain (Opus N-P3 close). |
| 3 — PMR fallback path | `mr != nullptr` (caller-supplied PMR resource — type-erased completion handlers, `asio::any_completion_handler` callsites, the session-side helper `async_lock_via_session_executor`) | `std::pmr::polymorphic_allocator<void>{mr}`; the `slot_allocator` wrapper forwards `allocate`/`deallocate` directly to `mr`. | `mr` (caller-supplied — typically `SessionConfig::session_arena` per `[2d §4.5]` / `[2d §8]` for in-session callers, `EngineConfig::default_session_resource` for engine bootstrap). | `mr->allocate(...)` throws `std::bad_alloc`, caught by `trap_throw`, surfaces as `unexpected{sync_lock_alloc_failed}`. |

The §9 seam **"slot-allocator storage cases"** (#21) verifies the per-case storage selection for all three cases — placing the awaiter under each of (case 1) HALO-firing path, (case 2) HALO-not-firing path with promise-allocator instrumentation, and (case 3) PMR fallback path with a `monotonic_buffer_resource`; verifies zero global-heap allocations on cases 1 and 3, and detects (does not auto-fail) the case-2 global-heap touch.

> **── v1.6 Erratum E-4 (2026-05-19, recorded at `/implement`; source-verified non-implementable, Opus-authored, user-authorized) ──**
>
> **Defect (no asio allocator-binding hook on the cancellation slot).** §4.3.4 ¶1, §4.5 step 4 (line ~960 "*Binds the cancellation slot's handler-closure allocator via `asio::bind_allocator(detail::slot_allocator{this, mr})`*"), §6.1, the §6.3 enqueue cost line, and NFR-016 ("*32-byte inline slot-handler-storage buffer per RC-C … zero global-heap on the v1.0 contended path*") all assume the cancellation-handler closure's storage can be redirected to `detail::slot_allocator` via `asio::bind_allocator`. **Source-verified against the pinned asio 1.36.0**, this is not implementable: `asio::cancellation_slot::emplace`/`assign` (`asio/cancellation_signal.hpp:143`) obtains memory exclusively through `prepare_memory()` → `asio::detail::thread_info_base::allocate(thread_info_base::cancellation_signal_tag(), …)` (`asio/impl/cancellation_signal.ipp:52`; `asio/detail/thread_info_base.hpp:138`). There is **no associated-allocator query and no `bind_allocator` hook** on that path; the coroutine promise allocator does not reach it either. The cancellation-handler closure storage is owned by asio's **per-thread recycling cache** (`reusable_memory_[cancellation_signal_tag]`): the first cancellation-slot assignment on a given thread performs one global `aligned_new`; every subsequent assignment on that thread reuses the thread-local block → zero global `new`/`delete` in steady state, but **never** routable to a project allocator. `detail::slot_allocator` cannot be wired the way §4.3.4 / line ~960 prescribe.
>
> **Resolution (binding for `/implement`).**
> 1. **asio's per-thread cancellation recycling cache is the sanctioned cancellation-handler-closure allocator.** The NFR-016 / `[const §VIII.5]` "zero global `new`/`delete` on the v1.0 contended path" guarantee is satisfied **by construction in steady state** (post per-thread warm-up). The one-time per-thread first-touch `aligned_new` is amortized and is **not** a hot-path event — it is treated exactly like §6.4 / §4.3.4 case-2 *bench-soft* (observable, non-fatal). The dominant contended-path allocation — the `waiter_record` — remains genuinely zero-global-`new` via the per-mutex `waiter_pool_` (Erratum E-2) on the embedded path, and from the caller `mr` on the PMR path; that property is unaffected and is the substantive content of US4 / SC-004.
> 2. **`detail::slot_allocator` is NOT bound to the cancellation slot** (no asio hook exists). It is retained as the typed, `Allocator`-shaped storage-policy wrapper for the allocation 2f *does* control — the `waiter_record` fallback. Its three-case body (T058) is implemented and **unit-verified by seam #21 in isolation** as the storage-decision type. The production `waiter_record` allocation in `async_lock` already realizes the same exhaustive three cases: case 1 = embedded per-mutex `waiter_pool_` slot (E-2; zero global heap); case 2 = N/A for the waiter record (it is *never* placed on the coroutine frame — it is pool- or `mr`-backed, so the HALO-not-firing branch does not exist for it); case 3 = `pmr_waiter_block` drawn from caller `mr`. Overflow / `mr` exhaustion → `null`-equivalent → surfaces as `unexpected{error::sync_lock_alloc_failed}` (no `std::terminate`), matching the §4.3.4 failure column.
> 3. **§4.3.4's three-case table is superseded for the cancellation-slot closure** and **re-anchored to the `waiter_record` storage decision** (the mapping in (2)). The "*so it composes with `asio::bind_allocator`*" clause in §4.3.4 ¶1, the `asio::bind_allocator(detail::slot_allocator{this, mr})` wording in §4.5 step 4 / line ~960 / §6.1 / §6.3, and the NFR-016 "32-byte inline slot-handler-storage buffer per RC-C" phrasing are **factually superseded** by this erratum. The awaiter's 32-byte `slot_storage_` buffer continues to hold the asio **completion** handler via placement-new per Erratum E-1 (unchanged); it does **not** back the cancellation closure (asio's recycler does).
> 4. **Seam #21 / seam #10 assert, post-E-4:** (a) `slot_allocator`'s three-case `allocate`/`deallocate` logic directly (inline-buffer hit / null-resource overflow-trap → `bad_alloc` → `trap_throw` → `sync_lock_alloc_failed` / PMR forward-to-`mr`); (b) the embedded contended path is zero global `new`/`delete` under the `mallocnesia` interceptor **in steady state**, measured after a documented per-thread warm-up iteration that primes asio's cancellation recycler (the warm-up is an explicit, commented harness step — not a measurement-window allocation); (c) the PMR path draws all fallback allocations from the supplied resource, none global; (d) `mr` exhaustion ⇒ `sync_lock_alloc_failed`, trapped (no `terminate`).
>
> **Contract preservation.** NFR-016 / SC-004 (zero global heap on the v1.0 contended path) is preserved in steady state — the substantive `waiter_record` allocation is genuinely zero-global (E-2 pool); only the per-thread-amortized asio cancellation first-touch is conceded, consistent with the pre-existing §6.4 / §4.3.4 case-2 bench-soft treatment. RC#2 layering (`core` never reaches into `session/`) is preserved — `slot_allocator` and the PMR `waiter_record` path take `mr` purely as a parameter. The cancellation *semantics* (§4.5 CAS arbitration, `on_cancel`, `sync_lock_aborted`) are entirely unaffected — E-4 changes only *where the closure bytes live*, which the 22 green US1–US3 seams already exercise through asio's recycler.
>
> **Scope / Gate note.** E-4 amends Gate-A-converged 2f §4.3.4 / §4.5 step 4 / §6.1 / §6.3 / NFR-016. Source-verified non-implementability (asio 1.36.0), Opus-authored, user-authorized 2026-05-19. Re-touches 006 Gate A scope (flag for `/gate-a`, alongside E-2 and E-3).

### 4.4 The RAII lock guard

```cpp
namespace fixpp::sync {

// async_lock_guard — the RAII handle returned by async_lock's awaitable
// completion. Movable, non-copyable, releases on destruction.
//
// Lifetime contract: bound to the originating async_mutex's lifetime. The
// guard MUST NOT outlive its mutex; the [[clang::lifetimebound]] on the
// constructor surfaces caller-side misuse. Per [2b §6.4] precedent.
//
// Move-assignment is DESTRUCTIVE (RC#1 / N-P1-3 close): moving into an
// engaged guard unlocks the previously-owned mutex first.
//
// v1.1 / Opus N-P3-1 close: the engaged-guard constructor is `private`
// + `friend`-only. The v1.0 public adopt-locked ctor + public try_lock()
// admitted a same-mutex aliasing bug at the caller's logic level
// (constructing two guards for one mutex). v1.1 closes the surface — the
// only legal guard-construction path is the awaitable form
// (`co_await m.async_lock()`). The §9 seam #20 ("`async_lock_guard`
// destructive move-assign") uses friend access for the test fixture.
class async_lock_guard {
public:
    // Default-constructed guard — disengaged; safe to move-into.
    async_lock_guard() noexcept = default;

    // Move ctor — source becomes empty.
    async_lock_guard(async_lock_guard&& other) noexcept
        : mutex_(std::exchange(other.mutex_, nullptr)) {}

    // Destructive move-assignment (RC#1 / N-P1-3 close). If `*this` is
    // engaged, unlock its mutex first; then take ownership of `other`'s
    // mutex. Self-assignment is a no-op (other == this).
    async_lock_guard& operator=(async_lock_guard&& other) noexcept {
        if (this == &other) return *this;
        if (mutex_) { mutex_->unlock(); }
        mutex_ = std::exchange(other.mutex_, nullptr);
        return *this;
    }

    async_lock_guard(async_lock_guard const&)            = delete;
    async_lock_guard& operator=(async_lock_guard const&) = delete;

    ~async_lock_guard() noexcept { if (mutex_) { mutex_->unlock(); } }

    // Explicit early release — disengages the guard and returns the
    // back-pointer; subsequent destruction is a no-op. Useful when the
    // caller wants to shorten the critical section.
    [[nodiscard]] async_mutex* release() noexcept {
        return std::exchange(mutex_, nullptr);
    }

    [[nodiscard]] bool owns_lock() const noexcept { return mutex_ != nullptr; }

private:
    // Engaged constructor (private + friend-only — Opus N-P3-1 close).
    // Called only from detail::async_mutex_awaiter::await_resume() and
    // from the §9 #20 test fixture (friend access).
    explicit async_lock_guard(async_mutex* mutex
                              [[clang::lifetimebound]]) noexcept
        : mutex_(mutex) {}

    friend class detail::async_mutex_awaiter;

    async_mutex* mutex_ {nullptr};
};

}  // namespace fixpp::sync
```

`sizeof(async_lock_guard) == sizeof(async_mutex*)` = 8 B. Per `[arch §5.5]` lifetime-classes discipline the guard does not own the mutex (the consumer that constructed the mutex is the owner). The §9 seam **"`async_lock_guard` destructive move-assign"** (#20) verifies the destructive-move semantics.

### 4.5 Cancellation contract (item 3) — CAS-arbitration with the drain (RC#1 + RC#4 + RC-A)

Per `[2d §4.7]`'s per-mode effect table — **rewritten by Appendix D §D.2 (effect-table row) and §D.3 (executor-compat surface bullet, new in v1.1) to surface `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary** (mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`).

v1.1 collapses the v1.0 four-state phase enum to three states `{ queued, granted, cancelled }` (RC-A close). The drain CAS is now `queued → granted`; the cancellation CAS is `queued → cancelled`. The intermediate `draining` phase is dropped — the drain CAS that grants ownership is atomic with the ownership transfer (no "ownership in flight" interval). Residual waiters stay `queued` (still cancellable) and are spliced into `next_drain_head_` (mutex-owned).

| `asio::cancellation_type` | 2f awaiter behaviour |
|---|---|
| `total` | `on_cancel` CAS's `phase_` from `queued → cancelled` (release-acquire). On CAS-success: write `result_ = unexpected{sync_lock_aborted}` and then schedule resumption on the bound executor. On CAS-failure (loser): the current value is `granted` (drain got there first); cancellation is no-op and does **not** touch `result_` — the waiter has been granted ownership and will receive the guard via `await_resume`. The mutex's `state_` is unaffected by the cancellation (the unlinked-from-LIFO bookkeeping is logical only on the cancel side; `state_` is mutated solely by `unlock()`'s exchange, `cancel_and_drain`'s exchange, and `async_lock`'s push — the §6.2 / §6.2.2 ordering protocol covers all three). The drain's walk skips `cancelled` waiters (they are not pushed into `next_drain_head_`); the cancelled awaiter resumes on its bound executor and is destroyed/deallocated independently — the mutex no longer threads any pointer through it (RC-A / Codex C-P1-1 UAF close). |
| `partial` | **Dropped from v1.0 surface.** Per `[2d §4.7]` the project does not issue `partial`; if a third party emits it on a 2f waiter, the awaiter's `on_cancel` treats it as **`total`** — CAS the phase, return the unexpected outcome. Rationale: there is no "partial acquisition" of a mutex; falling back to `total` is the only well-defined behaviour. |
| `terminal` | **Treated as `total`.** Per `[2d §4.7]` (`terminal` skips phase 1) the closest analogue for an awaitable mutex is "treated as `total`" — the waiter has not yet entered a critical section, so there is nothing to skip-phase-1 over. Behaviour matches `total` exactly. |
| **NEW v1.3 / RC-β; UPDATED v1.4** — `cancel_and_drain` awaitable cancelled mid-drain (`total` propagated to the reaper's parent state) | The reaper coroutine inside `cancel_and_drain()` propagates `cancellation_type::total` from its own bound `cancellation_state`, calls `drain_latch_state::signal_abort()`, and returns `expected_t::unexpected{sync_lock_aborted}`. The `drain_latch_state` shared_ptr held by the reaper survives the reaper's coroutine frame destruction (lifetime extended by the `shared_ptr`); pending in-flight reaper-resumption handlers retain their reference to the state object and complete normally. The mutex's `draining_` flag stays `true` (subsequent `async_lock(...)` returns `unexpected{sync_lock_drained}`); subscribers parked in `state->wait()` are woken by `signal_abort()`, observe `released_ == false` AND `aborted_ == true`, and return `expected_t::unexpected{sync_lock_aborted}` rather than suspending forever. The residual `active_holders_count_` / `active_acquirers_count_` is left to be observed by the next `cancel_and_drain` call. The §9 seam **"`cancel_and_drain` awaitable cancellation propagation"** (#26) verifies this. **Closes Opus C-R3-P1-4** (cancellation-slot interaction with the latch was unspecified in v1.2). |

#### 4.5.1 The cancellation-vs-drain race — precise specification (RC-A — four windows)

v1.0 enumerated three race windows for the head waiter of the LIFO. v1.1 adds a fourth window covering residual waiters parked on `next_drain_head_` (Opus N-P1-2 — holder-cancellation mid-critical-section was missed in v1.0). Each is resolved by the per-waiter atomic `phase_` CAS:

1. **Cancellation lands BEFORE drain CAS (head waiter).** `on_cancel` CAS's `queued → cancelled` first. When `unlock()`'s drain walks the FIFO chain and reaches this waiter, its CAS `queued → granted` sees the current value `cancelled` and fails. The drain skips the cancelled waiter (the cancelled waiter is *not* spliced into `next_drain_head_`) and grants ownership to the next non-cancelled FIFO waiter in the chain.

2. **Drain CAS lands BEFORE cancellation (head waiter).** `unlock()`'s drain CAS's `queued → granted` first. When the cancellation handler's `on_cancel` runs, its CAS `queued → cancelled` sees `granted` and fails — the waiter has already been granted ownership; cancellation is no-op (the waiter sees the guard, not `unexpected`). **Hostile-but-correct path (Opus N-P1-2 ride-along):** if the granted waiter's parent coroutine is itself cancelled mid-critical-section (its OWN cancellation_state, distinct from the mutex's per-waiter slot), the cancellation propagates through normal coroutine unwinding; the guard's destructor runs and calls `unlock()`, which walks `next_drain_head_` and grants the next residual waiter. The mutex itself is unaffected by holder-cancellation; the residual chain lives on the mutex side (RC-A close), so the holder's coroutine-frame destruction does not destroy any residual ownership.

3. **Cancellation lands AFTER `await_resume` clears the slot (post-resumption).** Slot is cleared on `await_resume` (§4.2.3); a stale cancellation signal that arrives after the clear is no-op on this awaiter (Opus N-P1 close). Even if the lambda fires before the clear completes, its first CAS `queued → cancelled` fails because `phase_` is already `granted` or `cancelled` (terminal). The lambda returns without dereferencing potentially-freed state.

4. **NEW v1.1 — Residual-cancellation race (parked on `next_drain_head_`).** A waiter spliced into `next_drain_head_` by an unlock's drain stays in phase `queued` (not `granted` — it has not been selected as the next owner yet) and remains cancellable. If `cancellation_type::total` is signalled while the waiter is parked on `next_drain_head_` (the canonical case: graceful close phase 2 fires on a long-tail waiter behind a slow critical section), `on_cancel` CAS's `queued → cancelled` and resumes the waiter on its bound executor with `unexpected{sync_lock_aborted}`. The next `unlock()` walking `next_drain_head_` observes `phase_ == cancelled` and skips the waiter (does not splice it back, does not grant ownership). This closes Codex C-P1-3 (v1.0 incorrectly CAS'd residual waiters to `draining`, swallowing cancellation) and Opus N-P1-1 (v1.0's `cancel_and_drain` could not reach residual waiters because they were unreachable from the mutex). **Concurrent with `cancel_and_drain`'s reaper detaching `next_drain_head_` (Opus N-P3-2 round-2 close):** the per-waiter `phase_` CAS arbitrates correctly even when the cancellation handler and the reaper race on the same waiter. Whoever wins `queued → cancelled` schedules the resumption (only once); the loser observes terminal `cancelled` and no-ops. The reaper's failure-path comment in §4.7.2 step (e) handles the case where its CAS observes `cancelled` (the cancellation handler beat it). Sub-case (a) — every residual waiter is `cancelled` and the walk falls through — is covered by §4.5.2 step (1f). Sub-case (b) — `cancel_and_drain` already detached `next_drain_head_` before the cancellation lands — is covered by the per-waiter `phase_` atom: the awaiter object is reachable from the reaper's local chain pointer, the cancellation handler's lambda captures the awaiter directly via `this`; both paths arbitrate via `phase_` independently of which list the awaiter is reachable from. The §9 seam **"residual-chain cancellation under graceful close"** (#22) verifies this window.

#### 4.5.2 The `unlock()` algorithm — one-winner walk with mutex-owned residual (RC-A; v1.2 + v1.3 RC-α post-cap rewrite)

`unlock()`'s drain (v1.4 CAS-then-publish close — supersedes v1.3's speculative pre-CAS `*result_` writes that admitted a write-race between unlock-walker and cancellation handler on the same awaiter). The walk is **mechanically one-winner under the three-state phase atom**: at most one waiter is CAS'd from `queued → granted` per `unlock()` call; every other `queued` waiter is left untouched (`phase_` literally unchanged) and spliced into `next_drain_head_` for the next unlock to service.

**Pre-walk holder accounting.** Before stepping into the walk, the unlocking thread decrements `mutex_->active_holders_count_.fetch_sub(1, memory_order_acq_rel)` — it is no longer a holder once `unlock()` is entered. (The grant CAS later increments the counter for the new holder; see step 1c below.)

**Drain-aware short-circuit (v1.2 / RC-B + v1.3 / RC-α close).** If `mutex_->draining_.load(memory_order_acquire) == true`, `unlock()` MUST NOT grant ownership to any waiter — `cancel_and_drain()` is reaping (or has reaped) every parked waiter, and granting would race against the reaper's CAS. Instead, the unlocker:

- Does NOT walk `next_drain_head_` or `state_` for granting purposes — the reaper's late-walker loop in §4.7.2 (which now re-walks BOTH `state_` and `next_drain_head_` per the v1.3 / RC-α / Opus C-R3-P1-3 close) is responsible for finding every `queued` waiter and CAS'ing it to `cancelled`.
- CAS's `state_` from `locked_no_waiters → not_locked` (acq_rel). If the LIFO is non-empty, the late-walker loop in §4.7.2 will exchange it on its next iteration; the unlocker does NOT detach the LIFO under draining_ (closes the v1.2 unlock-vs-reaper splice race where the unlocker detached `state_` and spliced into `next_drain_head_` after the reaper had exchanged and finished its first walk — Opus C-R3-P1-3).
- Atomically loads `drain_latch_ptr_` (under the project-internal `std::atomic<std::shared_ptr<T>>`-equivalent helper); if the load returns a non-null `shared_ptr<drain_latch_state>` (the reaper is in flight, latch state is alive), call `drain_latch_state::notify()` so the reaper's `wait()` re-checks the holder count and acquirer count. The notify is idempotent.
- Returns. The waiter's eventual cancellation comes from the reaper's CAS in §4.7.2, not from this unlocker. The §9 seam **"unlock-vs-reaper splice race closure"** (#27, NEW v1.3) verifies that the unlocker does NOT splice into `next_drain_head_` under draining_ and the reaper observes both lists stable.

Otherwise (the normal, non-draining path):

1. **Walk `next_drain_head_` FIRST (residual FIFO from prior unlocks).**
   - **(1a)** `next_drain_head_.exchange(nullptr, memory_order_acq_rel)` — atomically take ownership of the residual FIFO chain. Local pointer `head_residual` now owns the chain; the mutex's `next_drain_head_` is empty until step (1d).
   - **(1b)** Walk forward from `head_residual`, **finding the first `queued` waiter** while skipping `cancelled` heads. For each head node, `phase_.load(memory_order_acquire)`: if the value is `cancelled`, advance to `next_` (cancelled waiters were resumed by their own cancellation handler; the unlocker does NOT re-resume them and does NOT splice them forward); if the value is `queued`, this node is the **winner candidate**, stop the search.
   - **(1c)** **Grant ownership to the winner candidate (only this one waiter).** **v1.4 CAS-then-publish:** CAS `phase_` first, and only the CAS winner writes `*winner->result_`. Sequence:
     - **(1c-i)** CAS `winner->phase_` from `queued → granted` (`memory_order_acq_rel` on success, `memory_order_acquire` on failure).
     - **(1c-ii)** On CAS-SUCCESS, write `*winner->result_ = expected_t<async_lock_guard>{std::in_place, async_lock_guard{mutex_}}` (using the friend-only engaged ctor — §4.4). The cancellation handler has lost the CAS and MUST NOT touch `*winner->result_`.
     - **(1c-iii)** On CAS-SUCCESS, increment `active_holders_count_.fetch_add(1, memory_order_acq_rel)` — winner-only post-CAS holder accounting (v1.3 / RC-α / Opus C-R3-P1-1 close — closes the v1.2 pre-CAS phantom-count leak on CAS-loss).
     - **(1c-iv)** On CAS-FAILURE, the unlocker lost to a concurrent cancellation handler or reaper; it performs no `*result_` write and does not schedule a resumption. The CAS winner owns result-slot publication.
   - **(1d)** **Splice the untouched tail back into `next_drain_head_`.** The "untouched tail" is `winner->next_` onward — every waiter in the tail is *still* in phase `queued` (the unlocker never CAS'd them). Re-publish via `next_drain_head_.compare_exchange_weak(nullptr → tail_head, memory_order_release, memory_order_acquire)`; on failure, append the tail at the existing residual head's tail under a single `compare_exchange_weak` retry loop. **No `phase_` change is performed on tail waiters** — they remain cancellable, exactly as Codex C-P1-3 / RC-A requires.
   - **(1e)** **Schedule the winner's resumption** per §4.6's inline-vs-post predicate. The cancellation arbitration (cancel handler racing the grant CAS) is published already by step (1c-ii)'s release-CAS; the loser observes `granted` and no-ops.
   - **(1f) Edge case — every residual waiter is `cancelled` (no winner found in step 1b):** there is no winner, no grant, no splice. The chain `head_residual` consists entirely of `cancelled` waiters that the unlocker drops (their resumptions were already scheduled by their own cancellation handlers). The unlocker's `active_holders_count_` decrement at the start of `unlock()` is the only mutex-state change so far; proceed to step 2 to walk the LIFO list.

2. **If `next_drain_head_` produced no winner (or was empty), walk `state_`.**
   - **(2a)** `state_.exchange(locked_no_waiters, memory_order_acq_rel)` — snapshot the head and clear `state_` to `locked_no_waiters`. Call the result `state_snapshot`.
   - **(2b)** **Sentinel discrimination (v1.2 / Opus N-P1-1 close — also applied here for consistency with §4.7.2 step (d)).** If `state_snapshot == locked_no_waiters` (i.e., `0`, an empty LIFO list — the unlocker is the only thread that has touched `state_` since the last push), there is no LIFO chain to walk. CAS `state_` from `locked_no_waiters → not_locked` (`acq_rel`); on success, `unlock()` returns. **Race window:** a concurrent `async_lock` may push a new waiter between exchange and close-out CAS — handled by the CAS-failure-acquire path; on CAS failure, the waiter that just pushed is reachable via `state_` and `unlock()` re-runs from step 2 (walking the new head). The `state_snapshot == not_locked` (= `1`) case is impossible under the unlock precondition (the mutex is held); but the discriminator is documented here so the same protocol applies in §4.7.2's looser precondition (where `not_locked` IS reachable).
   - **(2c)** Otherwise `state_snapshot` is a real waiter pointer (8-byte-aligned, ≥ 8). Walk the LIFO chain forward from `state_snapshot`, reversing `next_` pointers to produce a FIFO-ordered list. The reversal is local to the unlocking thread (the chain is detached from `state_`).
   - **(2d)** **Walk the reversed FIFO chain — same one-winner protocol as step 1.** Find the first `queued` waiter (skipping `cancelled` heads); apply the CAS-then-publish (1c-i) → (1c-iv) sequence: CAS `winner->phase_` `queued → granted` (`acq_rel`); on CAS-SUCCESS, write `*winner->result_ = engaged-guard` and increment `active_holders_count_`; on CAS-FAILURE, do not write `*result_`. Splice the *untouched tail* (every waiter past `winner->next_` whose phase is still `queued` — the unlocker never CAS'd them) plus any `queued` waiters that preceded the winner but were skipped past `cancelled` heads INTO `next_drain_head_` via the same `compare_exchange_weak` retry loop as step (1d). The cancellation-skipped waiters are dropped (their resumptions were already scheduled). Schedule the winner's resumption per §4.6 only after the winner has written `*result_`.
   - **(2e) Edge case — every LIFO waiter is `cancelled`:** the walk completes without finding a winner; there is no grant, no splice. CAS `state_` from `locked_no_waiters → not_locked` (`acq_rel`) and return.

**Cancellation arbitration + result-slot publication (precondition for step 1c / 2d's grant CAS — v1.4 CAS-then-publish rewrite).** A concurrent cancellation handler running on the candidate waiter races the grant CAS via `phase_.compare_exchange(queued → cancelled, acq_rel)`. **Both paths CAS first; only the CAS winner writes `*result_`; the loser observes terminal phase and performs no `*result_` write.** Concretely: if the cancel handler wins, it CASes to `cancelled`, writes `unexpected{sync_lock_aborted}`, and schedules the resumption. The unlocker observes `cancelled`, does not touch `*result_`, and does not schedule. If the unlocker wins, it CASes to `granted`, writes the engaged guard, and schedules the resumption; the cancel handler observes `granted`, does not touch `*result_`, and returns. `await_resume`'s acquire-load on `phase_` synchronises with the WINNER's release-CAS; the winner's `*result_` write is sequenced before the bound-executor resumption. **No two writers race the non-atomic `*result_` slot.** The §9 seam **"`*result_` CAS-then-publish arbitration"** (#28) verifies the contract under TSan with N=64 simultaneous unlock-grant + cancellation-handler interleavings on the same awaiter.

**Crucial v1.3 invariant:** after the grant CAS-succeeds, the only waiters in `next_drain_head_` (whether spliced this `unlock()` or pre-existing) are in phase `queued` — none have been physically CAS'd to any terminal phase by the unlocker. This is the mechanical foundation for "the next `unlock()` finds the next `queued` waiter and grants ownership" without observing stale terminal phases.

When the granted waiter's eventual `unlock()` runs, it re-runs this protocol — walking `next_drain_head_` first; only when both lists produce no `queued` winner does it CAS `state_` to `not_locked`. Concurrent `async_lock` pushes that land while `next_drain_head_` is non-empty enter the LIFO at `state_` normally; the next `unlock()` (after the residual is drained) services them.

**Mutual exclusion preserved:** at every transition the mutex is in exactly one of three regimes: (i) `state_ == not_locked` and `next_drain_head_ == nullptr` (free); (ii) `state_ == locked_no_waiters` (or pointing at LIFO head) and `next_drain_head_` chain non-empty (held; residual waiting); (iii) `state_ == locked_no_waiters` (or pointing at LIFO head) and `next_drain_head_ == nullptr` (held; no residual). The CAS protocol ensures at most one waiter is `granted` at a time across the union of `state_` LIFO and `next_drain_head_` FIFO — and the v1.3 one-winner walk ensures the `granted` count strictly tracks the active-holder count, with no waiter terminally pinned in `granted` while another holder is acquiring.

This is the cppcoro / Lewis-Baker "one-owner-per-unlock" algorithm with the residual chain hoisted from the awaiter to the mutex (RC-A close). **The §6.2.2 ordering sub-table is updated with rows for `active_holders_count_`, `active_acquirers_count_`, `draining_`'s additional pairing partners, `drain_latch_ptr_`, and the v1.4 CAS-then-publish `*result_` slot publication rule.**

### 4.6 `dispatch` vs `post` policy (item 4) — bound-executor predicate (RC#4)

Per `[2d §7.4]`, the default is `dispatch`; per-mutex override is 2f's call.

#### 4.6.1 How the policy is set

**Constructor argument.** The `completion_policy` enum is passed to the `async_mutex` constructor; defaults to `completion_policy::dispatch`. **Not** a template parameter. **Not** a runtime-mutable field — `policy_` is declared `const`-initialised by the constructor; no setter. The §9 seam **"`dispatch` vs `post` policy effect on completion"** (#12) verifies the per-policy effect.

```cpp
async_mutex m1;                                 // default — dispatch.
async_mutex m2{completion_policy::dispatch};    // explicit dispatch.
async_mutex m3{completion_policy::post};        // HFT/fairness-sensitive site.
```

#### 4.6.2 The inline-vs-post predicate (RC#4 fix)

Replaces v0.1's invalid "`dispatch` falls through to `post`" prose. The predicate is precisely **ASIO `dispatch` semantics on the bound executor**:

- **`dispatch` policy.** When the granted waiter is resumed (per §4.5.2 step 5), the unlocker calls `asio::dispatch(bound_executor, resumption_handler)`. ASIO's `dispatch` invokes the handler **inline / synchronously** if `bound_executor.running_in_this_thread()` returns `true`; otherwise it **posts** the handler. The `running_in_this_thread()` query is well-defined under `per_session_strand` mode (the `asio::strand` wrapper publishes it). Under `direct_executor` mode + a user-attested executor that does NOT publish `running_in_this_thread()`, the predicate falls to **always-`post`** (RC#4 fix; v0.1's "always-`dispatch`" claim was unsafe because ASIO `dispatch` on a non-queryable executor has unspecified inline-vs-post behaviour). Cost: ≈ 0 ns inline; ≈ 25 ns post hop on warm cache.

- **`post` policy.** Every resumption goes through one `asio::post(bound_executor, resumption_handler)` hop, regardless of caller thread. Cost: ≈ 25 ns extra per resumption. HFT / fairness-sensitive sites pick `post` so every waiter pays the same cost.

The §9 seam **"Cross-strand acquire"** (#13) exercises `direct_executor` + cross-thread unlock and verifies the resumption lands on the bound executor with at most one post hop.

The v1.0 default is `dispatch`; 2e's writer mutex on `MemoryStore`/`FileStore` keeps the default. A future high-frequency seqnum counter or pinset-rotation site may pick `post`; that is a per-callsite decision documented at the consumer's design level.

> **── v1.6 Erratum E-3 (2026-05-18, recorded at `/implement`; TSan-diagnosed, Opus-authored, user-authorized) ──**
>
> **Defect (re-entrant inline resume = heap-UAF).** §4.6.2's `dispatch`-policy
> rule — *"`asio::dispatch(bound_executor, resumption_handler)`; invoked inline
> if `bound_executor.running_in_this_thread()`"* — is **unsafe for 2f waiter
> resumption**. The resumption handler resumes a *suspended `asio::awaitable`
> coroutine* (the parked `async_lock()` waiter). In 2f that resumption is
> **always** driven from inside `unlock()` (the granting holder's guard
> destructor) or `on_cancel()` (the cancellation handler) — both of which
> execute **nested within another `asio::awaitable` coroutine's
> `awaitable_thread`** on the bound executor's thread. There,
> `running_in_this_thread()` is `true`, so `asio::dispatch` runs the resumption
> **synchronously and re-entrantly**: the waiter coroutine runs to `co_return`
> and its frame is destroyed *while still nested in the unlocker's
> `awaitable_thread` frame stack*. asio's `awaitable_thread`/`awaitable_frame`
> chaining is **not re-entrant across a nested coroutine resume**; the freed
> frame is then read by asio's `pop_frame`/`entry_point` → `heap-use-after-free`
> (observed under TSan on US1 `sync_fifo_fairness` and US2
> `sync_cancellation_mid_wait`; non-waivable per `[const Art.VII]` TSan gate).
>
> **Resolution (binding for `/implement`; no other §4.6 clause changes).** 2f
> waiter resumption is **always `post`ed** to the bound executor — never
> inline-`dispatch`ed — *regardless of `completion_policy` or
> `running_in_this_thread()`*. Rationale: 2f's resumption site is
> *intrinsically re-entrant* (always inside `unlock()`/`on_cancel()`, always
> inside a coroutine on the bound-executor thread), so the §4.6.2 inline
> fast-path is **never** safe here. The `post` hop (≈25 ns, §4.6.2) decouples
> the resumed waiter onto a fresh top-level `awaitable_thread`, eliminating the
> re-entrancy. **`completion_policy` is preserved as a semantic/intent knob**
> (and remains constructor-set, `const`, queryable via `policy()`), but for
> waiter resumption both `dispatch` and `post` post; the §4.6.2
> "inline if `running_in_this_thread()`" sentence is **superseded for waiter
> resumption** by this erratum (it would still apply to any *non-reentrant*
> completion context, of which 2f has none).
>
> **Contract preservation.** `[2d §7.4]` (completion on the awaiter's bound
> executor) is preserved — `post` lands on the bound executor. Seam #12
> (`dispatch vs post`) asserts only mutual exclusion + completion under both
> policies (not inline-ness) — unaffected. Seam #13 (cross-strand) requires
> "at most one post hop" — satisfied (exactly one). The only forfeited property
> is the ≈0 ns inline latency optimisation, which was never sound at 2f's
> re-entrant resume site. Any implementation that inline-`dispatch`es a 2f
> waiter resumption is **non-conforming** to E-3.
>
> **Scope / Gate note.** E-3 amends Gate-A-converged 2f §4.6.2. TSan-diagnosed,
> Opus-authored, user-authorized 2026-05-18. Re-touches 006 Gate A scope
> (flag for `/gate-a`, alongside E-2).

### 4.7 Destructor — `std::terminate()` precondition + `cancel_and_drain()` (RC#3)

The choice from the brief's three options:

- **(a) Drain (debug + release).** Destructor blocks until the LIFO list is empty. **Rejected** — the destructor runs synchronously; blocking on a coroutine drain inside a destructor introduces a re-entrancy hole.
- **(b) Pre-conditioned destructor + release UB (v0.1's choice).** Debug-only assert; release silently UB on violation. **Rejected** — `[arch §5.3]` says invariant violations abort in release; they do not silently become UB. `[SYN §3.2 Q6b]` item 5 explicitly retired this shape.
- **(c) `std::terminate()` precondition + explicit `cancel_and_drain()` drain primitive.** **Chosen** (RC#3 fix).

#### 4.7.1 The destructor mechanism

```cpp
async_mutex::~async_mutex() {
    // Hard precondition: state_ == not_locked (no holder, no waiters).
    // Both DEBUG and RELEASE fire std::terminate() on violation; no
    // release-mode UB. The §9 seam #5 is a release-mode death test.
    if (state_.load(std::memory_order_acquire) != not_locked) {
        // fixpp::core::abort_invariant(...) is the project-internal helper
        // that emits a structured abort message + std::terminate().
        fixpp::core::abort_invariant(
            "fixpp::sync::async_mutex destroyed with waiters or while held; "
            "drain the mutex via co_await cancel_and_drain() before "
            "destruction.");
    }
}
```

| Build | Behaviour on `~async_mutex()` with waiters or while held |
|---|---|
| Debug | `abort_invariant(...)` fires `std::terminate()` — visible bug, immediate stack trace. |
| Release | `abort_invariant(...)` fires `std::terminate()` — same as debug. Hard precondition; no UB. |

#### 4.7.2 `cancel_and_drain()` — the drain primitive (v1.1 reshape; v1.2 round-2 RC-B fix; v1.3 round-3 post-cap RC-α + RC-β fix)

`[[nodiscard]] asio::awaitable<expected_t<void>> cancel_and_drain() noexcept`:

The v1.0 prose ("walks the awaiter's parent `cancellation_state` and emits `cancellation_type::total` to every in-flight waiter") was non-implementable. v1.1 reshaped the primitive as a mutex-owned reaping operation but referenced an invented `asio::async_wait_for_drain_complete[_count](...)` API that does not exist. v1.2 introduced a project-internal `detail::drain_latch` owning an `asio::steady_timer`; that shape was ALSO non-implementable (`asio::steady_timer` requires an executor at construction, but `async_mutex()` is `constexpr`-default-constructible — Opus C-R3-P2-1). v1.2 also admitted three structural defects at the boundaries of its three mechanically-correct fixes (write-race on `*result_`, in-flight acquirer not covered by holder count, unlock-vs-reaper splice race; Opus C-R3-P1-1 / C-R3-P1-2 / C-R3-P1-3). v1.3 / RC-α + RC-β post-cap user-authorized pass closes all of these:

1. **Pre-drain holder lifecycle (RC-α / Opus C-R3-P1-1 retained).** Every successful holder-acquire (fast-path CAS-success in §4.2.1 OR drain-walker grant CAS-success in §4.5.2) increments `active_holders_count_` WINNER-ONLY; every `unlock()` decrements. `cancel_and_drain` waits for `active_holders_count_ == 0`.

2. **In-flight acquirer coverage (NEW v1.3 / RC-α / Opus C-R3-P1-2 close).** Every `async_lock(...)` factory call increments `active_acquirers_count_` BEFORE `await_ready`'s `draining_` load; the counter is decremented at one of three exit points (fast-path success, drained-bypass, await_suspend LIFO-enrol — see §4.2.1 / §4.2.2). `cancel_and_drain` waits for both `active_acquirers_count_ == 0` AND `active_holders_count_ == 0` before publishing the release-edge.

3. **Unlock-vs-reaper splice race closure (NEW v1.3 / RC-α / Opus C-R3-P1-3 close).** Under `draining_ == true`, `unlock()` does NOT walk `next_drain_head_` or splice anything; the reaper's late-walker loop re-walks BOTH `state_` AND `next_drain_head_` in a stable loop until both observe nullptr (the v1.2 protocol re-walked only `state_`, missing residual splices the reaper never observed). The unlocker simply notifies the latch.

4. **Sentinel-cast UAF on a free mutex (RC-B / Opus N-P1-1 round-2 close, retained from v1.2).** Sentinel-discriminate the `state_.exchange(...)` snapshot against `not_locked` / `locked_no_waiters` BEFORE casting to `async_mutex_awaiter*`.

5. **Lazy-constructed `drain_latch_state` (NEW v1.3 / RC-β / Opus C-R3-P1-4 / C-R3-P2-1 / N-R3-P1-1 close; UPDATED v1.4 publication ordering).** No `asio::steady_timer` member on the mutex; the mutex stays `constexpr`-default-constructible. The reaper allocates a `std::shared_ptr<detail::drain_latch_state>` inside its own coroutine frame; the mutex stores an atomic `std::shared_ptr<detail::drain_latch_state> drain_latch_ptr_`. The first reaper stores `drain_latch_ptr_.store(state, release)` **before** setting `draining_.store(true, release)`, so any second caller that acquire-loads `draining_ == true` can acquire-load a non-null latch pointer for the epoch. The state object owns `released_` (bool), `aborted_` (bool), `in_flight_resumptions_` (atomic count moved off the reaper's stack — closes Opus N-R3-P1-2 UAF), and a `asio::experimental::concurrent_channel<void()>` (or equivalent project-internal multi-waiter subscriber-list awaitable — see §4.7.3 for the implementation pick) for the multi-waiter latch surface. The state object survives the reaper's coroutine frame destruction (shared_ptr lifetime); pending in-flight resumption handlers retain their reference and decrement the count safely. `drain_latch_ptr_` is cleared only after the drain result is published via `signal_release()` or `signal_abort()`.

6. **Reaper cancellation propagation contract (NEW v1.3 / RC-β / Opus C-R3-P1-4 close).** If the `cancel_and_drain` awaitable is itself cancelled (caller's parent `cancellation_state` fires `cancellation_type::total`), the reaper propagates and returns `expected_t::unexpected{sync_lock_aborted}`. The `drain_latch_state`'s shared_ptr (held by the reaper) survives the reaper's frame destruction; in-flight reaper-resumption handlers complete normally; the mutex's `draining_` flag stays `true` (subsequent `async_lock` fast-fails with `sync_lock_drained`). See §4.7.3 for the full lifetime invariants and the cancellation-shield-vs-propagation pick.

**Mechanism (v1.3 code-shape sketch — replaces v1.2's by-value-`drain_latch` sketch):**

```cpp
asio::awaitable<expected_t<void>> async_mutex::cancel_and_drain() noexcept {
    // (a) Idempotent fast path. If draining_ is already true, this is
    //     either a second call on a drained mutex or a concurrent call
    //     while a reaper is in-flight. v1.4 ordering guarantee:
    //     drain_latch_ptr_ is stored before draining_ is set, so a true
    //     draining_ load during an epoch always has a non-null latch.
    if (draining_.load(std::memory_order_acquire)) {
        if (auto state = drain_latch_ptr_.load(std::memory_order_acquire)) {
            co_await state->wait();
            if (state->released_.load(std::memory_order_acquire)) {
                co_return expected_t<void>{};
            }
            if (state->aborted_.load(std::memory_order_acquire)) {
                co_return std::unexpected(error{
                    error_code::sync_lock_aborted});
            }
        }
        // Null after draining_ == true means the prior epoch has already
        // published its result and cleared the pointer.
        co_return expected_t<void>{};
    }

    // (b) Concurrent-call serialiser — only the first caller observing
    //     drain_in_progress_ false becomes the reaper.
    if (drain_in_progress_.test_and_set(std::memory_order_acq_rel)) {
        // Lost the race before the reaper has necessarily published
        // draining_. Wait until draining_ is visible, then the latch pointer
        // is guaranteed non-null for the epoch.
        while (!draining_.load(std::memory_order_acquire)) {
            co_await asio::post(co_await asio::this_coro::executor,
                                asio::use_awaitable);
        }
        if (auto state = drain_latch_ptr_.load(std::memory_order_acquire)) {
            co_await state->wait();
            if (state->released_.load(std::memory_order_acquire)) {
                co_return expected_t<void>{};
            }
            if (state->aborted_.load(std::memory_order_acquire)) {
                co_return std::unexpected(error{
                    error_code::sync_lock_aborted});
            }
        }
        co_return expected_t<void>{};
    }

    // (c) Allocate the lazy drain_latch_state (NEW v1.3 / RC-β —
    //     std::make_shared inside this coroutine frame; lives on the heap
    //     with the reaper's shared_ptr). v1.4: publish the shared_ptr
    //     before setting draining_, so concurrent callers can never observe
    //     draining_ == true with an expired/missing latch during the epoch.
    auto latch_state = std::make_shared<detail::drain_latch_state>(
        co_await asio::this_coro::executor);
    drain_latch_ptr_.store(latch_state, std::memory_order_release);
    draining_.store(true, std::memory_order_release);

    // (d) Bind the reaper's own cancellation_state. RC-β / Opus C-R3-P1-4
    //     close — if the caller's parent state fires total during the
    //     reaper's run, propagate (do NOT cancellation-shield); the in-
    //     flight CAS-walks complete; drain_latch_state's shared_ptr keeps
    //     in-flight resumption handlers' captures alive.
    auto reaper_cs = co_await asio::this_coro::cancellation_state;

    // (e) Atomic exchange both lists out — sentinel-discriminated cast.
    auto raw_state = state_.exchange(locked_no_waiters,
                                     std::memory_order_acq_rel);
    auto* lifo_head = (raw_state == not_locked
                       || raw_state == locked_no_waiters)
                      ? nullptr
                      : reinterpret_cast<async_mutex_awaiter*>(raw_state);
    auto* fifo_head = next_drain_head_.exchange(
        nullptr, std::memory_order_acq_rel);

    // (f) Walk both lists; CAS each `queued` waiter to `cancelled`. CAS
    //     happens before result-slot publication; only the CAS winner
    //     writes *result_. The
    //     in_flight count lives on latch_state (NOT on the reaper's stack
    //     — closes Opus N-R3-P1-2 UAF).
    auto reverse_lifo = [](async_mutex_awaiter* head) -> async_mutex_awaiter* {
        async_mutex_awaiter* prev = nullptr;
        while (head) { auto* next = head->next_; head->next_ = prev;
                       prev = head; head = next; }
        return prev;
    };
    auto reap_chain = [latch_state, this](async_mutex_awaiter* head) {
        while (head) {
            auto* next = head->next_;
            waiter_phase expected = waiter_phase::queued;
            if (head->phase_.compare_exchange_strong(
                    expected, waiter_phase::cancelled,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                *head->result_ = std::unexpected(error{
                    error_code::sync_lock_aborted});
                // Reaper won the CAS — schedule resumption. Capture
                // latch_state by shared_ptr (keeps it alive until the
                // resumption handler decrements in_flight_resumptions_).
                latch_state->in_flight_resumptions_.fetch_add(
                    1, std::memory_order_acq_rel);
                schedule_resume_on_bound_executor(head,
                    [this, latch_state] {
                        if (latch_state->in_flight_resumptions_.fetch_sub(
                                1, std::memory_order_acq_rel) == 1
                            && active_holders_count_.load(
                                   std::memory_order_acquire) == 0
                            && active_acquirers_count_.load(
                                   std::memory_order_acquire) == 0
                            && state_.load(std::memory_order_acquire)
                                   == locked_no_waiters
                            && next_drain_head_.load(
                                   std::memory_order_acquire) == nullptr) {
                            latch_state->signal_release();
                        } else {
                            latch_state->notify();   // re-check
                        }
                    });
            }
            // CAS lost — phase was `granted` (holder will unlock and
            // quiesce) or `cancelled` (waiter's own cancel beat reaper).
            head = next;
        }
    };
    reap_chain(reverse_lifo(lifo_head));
    reap_chain(fifo_head);

    // (g) Re-walk BOTH state_ AND next_drain_head_ in a stable loop
    //     (NEW v1.3 / RC-α / Opus C-R3-P1-3 close — v1.2 re-walked only
    //     state_ and missed unlock-vs-reaper splice into next_drain_head_).
    //     Loop until both observe nullptr in a single iteration (no further
    //     splices observable after this point because draining_ == true
    //     short-circuits unlock()'s splice path per §4.5.2).
    while (true) {
        auto raw_late = state_.exchange(locked_no_waiters,
                                        std::memory_order_acq_rel);
        auto* late_lifo = (raw_late == not_locked
                           || raw_late == locked_no_waiters)
                          ? nullptr
                          : reinterpret_cast<async_mutex_awaiter*>(raw_late);
        auto* late_fifo = next_drain_head_.exchange(
            nullptr, std::memory_order_acq_rel);
        if (!late_lifo && !late_fifo) break;
        if (late_lifo) reap_chain(reverse_lifo(late_lifo));
        if (late_fifo) reap_chain(late_fifo);
    }

    // (h) Wait for in-flight acquirers, holders, and reaper-resumption
    //     handlers all to quiesce. cancellation propagation: if the
    //     caller's parent state fires total during this co_await, the
    //     reaper's wait() awaitable surfaces operation_aborted; we observe
    //     it and propagate (do NOT shield — see §4.7.3 for the rationale).
    while (active_holders_count_.load(std::memory_order_acquire) > 0
           || active_acquirers_count_.load(std::memory_order_acquire) > 0
           || latch_state->in_flight_resumptions_.load(
                  std::memory_order_acquire) > 0) {
        // Park on the latch state's wait awaitable (asio::experimental::
        // concurrent_channel-backed; see §4.7.3 for shape).
        auto wait_result = co_await latch_state->wait();
        if (!wait_result) {
            // Cancellation propagated — wake subscribers as aborted, then
            // return unexpected. The latch_state shared_ptr (held by this
            // frame and by in-flight resumption handlers) keeps
            // in_flight_resumptions_ alive until they decrement; mutex's
            // draining_ stays true; subsequent async_lock returns
            // sync_lock_drained.
            latch_state->signal_abort();
            drain_latch_ptr_.store(nullptr, std::memory_order_release);
            co_return std::unexpected(error{error_code::sync_lock_aborted});
        }
    }

    // (i) Mutex is now drained. Sentinel-discriminated CAS to set state_
    //     to not_locked.
    auto expected_state = locked_no_waiters;
    state_.compare_exchange_strong(expected_state, not_locked,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire);

    // (j) Publish release-edge for any subscribers (idempotent fast-path
    //     callers in step (a)/(b)).
    latch_state->signal_release();
    drain_latch_ptr_.store(nullptr, std::memory_order_release);

    co_return expected_t<void>{};
}
```

**Note on `detail::drain_latch_state` shape (NEW v1.3 / RC-β close; UPDATED v1.5).** `drain_latch_state` is a project-internal type allocated lazily via `std::make_shared<>` inside `cancel_and_drain`'s coroutine frame; the mutex stores an atomic `std::shared_ptr<detail::drain_latch_state>` in `drain_latch_ptr_`. The shared_ptr pins the state alive during the epoch and is cleared only after `signal_release()` or `signal_abort()` publishes the result. The state object owns:
- `std::atomic<bool> released_` — fresh = false; reaper publishes true at the successful publication edge via `signal_release()`.
- `std::atomic<bool> aborted_` — fresh = false; cancelled reaper publishes true via `signal_abort()` before returning `unexpected{sync_lock_aborted}`.
- `std::atomic<std::uint32_t> in_flight_resumptions_` — reaper-resumption-handler count; incremented at every `schedule_resume_on_bound_executor` call; decremented in the resumption handler. Lives on the state object instead of the reaper's stack — closes Opus N-R3-P1-2 (in_flight UAF on reaper cancellation).
- A multi-waiter latch surface implemented as `asio::experimental::concurrent_channel<void()>` (or, equivalently, a project-internal `asio::async_initiate`-composed awaitable that subscribes a completion handler to the released-edge and is woken by `signal_release()`). The implementation pick is documented in §4.7.3.

The mutex's constructor stays `noexcept(true)` and `constexpr`-default-constructible; no executor dependency. **Closes Opus C-R3-P2-1's "non-implementable `steady_timer` in `constexpr` mutex constructor" finding** (the v1.2 by-value `detail::drain_latch` member is gone). The §9 seam **"drain-latch lazy state lifetime"** (#24-extended in v1.3) verifies the wait/signal protocol AND the shared_ptr-survives-reaper-frame-destruction lifetime invariant.

**Behavioural contract (v1.3 — updated):**

1. Allocates `std::shared_ptr<detail::drain_latch_state>` inside the reaper's coroutine frame (NEW v1.3 / RC-β); atomically publishes the shared_ptr into `drain_latch_ptr_` with release semantics (v1.4) so concurrent waiters subscribe to the same non-expiring state object during the epoch.
2. Sets `draining_ = true` only **after** `drain_latch_ptr_` is published (v1.4 deterministic latch-publication order). From that point onward, every new `async_lock(...)` fast-fails with `expected_t::unexpected{sync_lock_drained}` — **enforced in `await_ready` BEFORE the fast-path CAS** (v1.2, retained). A second caller that acquire-loads `draining_ == true` then acquire-loads `drain_latch_ptr_`; the pointer is non-null until the drain result is published.
3. Atomically exchanges `state_` (sentinel-discriminated) and `next_drain_head_`; walks both lists, CAS'ing every `queued` waiter to `cancelled`; only the CAS winner writes `result_ = unexpected{sync_lock_aborted}` and resumes the cancelled waiter on its bound executor (v1.4 CAS-then-publish). The `in_flight_resumptions_` counter lives on `drain_latch_state` (not the reaper's stack) per RC-β.
4. **Re-walks BOTH `state_` AND `next_drain_head_` in a stable loop** until both observe nullptr in a single iteration (NEW v1.3 / RC-α — closes the v1.2 unlock-vs-reaper splice race; under `draining_ == true`, unlock's splice path is short-circuited per §4.5.2's drain-aware short-circuit, so this loop is guaranteed to terminate).
5. Waits for `active_holders_count_ == 0` AND `active_acquirers_count_ == 0` (NEW v1.3 / RC-α — covers in-flight acquirer per Opus C-R3-P1-2) AND `in_flight_resumptions_ == 0` via `co_await latch_state->wait()`.
6. **Cancellation propagation (NEW v1.3 / RC-β; UPDATED v1.4).** If the caller's parent `cancellation_state` fires `cancellation_type::total` during the wait, the reaper observes `wait()`'s `operation_aborted` outcome, calls `latch_state->signal_abort()`, and returns `expected_t::unexpected{sync_lock_aborted}`. Subscribers parked in `state->wait()` wake, observe `released_ == false` AND `aborted_ == true`, and return `expected_t::unexpected{sync_lock_aborted}`. The `drain_latch_state` shared_ptr (held by the reaper's frame and by every in-flight resumption handler's lambda capture) survives the reaper's frame destruction; pending resumption handlers complete normally and decrement `in_flight_resumptions_`. The mutex's `draining_` flag stays `true`; subsequent `async_lock` fast-fails with `sync_lock_drained`.
7. On normal completion, calls `latch_state->signal_release()`, then clears `drain_latch_ptr_`, and returns `expected_t<void>{}`. After return, the mutex is in a state where `~async_mutex()` is safe — concretely: `state_ == not_locked`, `next_drain_head_ == nullptr`, `active_holders_count_ == 0`, `active_acquirers_count_ == 0`, `draining_ == true`, `latch_state->released_ == true`. The post-drain fast-fail rule (per §4.2.1's `await_ready` `draining_` check) prevents any new acquirer from entering the mutex after this point.
8. **Idempotent.** A second `cancel_and_drain()` call sees `draining_ == true` (step (a) fast path); if `drain_latch_ptr_` is non-null, subscribes to the current reaper's `wait()`; if `drain_latch_ptr_` is null (drain result has fully published and the pointer was cleared), returns `expected_t<void>{}` immediately. Cancellation-propagation case (the original reaper was cancelled): the second call sees `draining_ == true`, may find a live or cleared shared_ptr depending on whether the abort has been published; if live, subscribes and observes abort; if cleared, treats the epoch as already reported.
9. **Concurrent-call safe.** Two coroutines invoking `cancel_and_drain()` simultaneously race on `drain_in_progress_.test_and_set(...)`; only one becomes the reaper. The reaper publishes `drain_latch_ptr_` before publishing `draining_ == true`; non-reapers that lose the serialiser wait until `draining_` is visible, then subscribe to the non-null `drain_latch_state` and propagate the reaper's outcome (success or `sync_lock_aborted` if the reaper was cancelled).

**Cancellation-vs-reaper race (Codex C-P1-1 round-1 close, retained):** if a waiter mid-cancellation races with `cancel_and_drain`'s walker, the per-waiter `phase_` CAS arbitrates. If the waiter's own cancellation handler wins the CAS to `cancelled` first, the reaper's CAS observes `cancelled` and skips (does not double-resume). If the reaper wins, the waiter's `await_resume` observes `phase_ == cancelled` (set by the reaper) and reads `result_ = unexpected{sync_lock_aborted}` correctly. No waiter is double-resumed; no waiter is lost. The §9 seams — #19 single-walker; #23 multi-caller; #24 pre-drain holder + drain-latch protocol; #25 in-flight acquirer; #26 awaitable cancellation propagation; #27 unlock-vs-reaper splice closure; #28 `*result_` write-race arbitration — together verify the v1.3 contract surface.

#### 4.7.3 Drain-latch ownership and cancellation-propagation contract (NEW v1.3 / RC-β)

The `detail::drain_latch_state` lifetime model is the binding contract for the v1.3 `cancel_and_drain` mechanism. The model balances five forces: (a) the mutex stays `constexpr`-default-constructible and executor-free per `[arch §5.5]`; (b) the latch state must outlive the reaper's coroutine frame on the cancellation path (in-flight resumption handlers reference it); (c) idempotent and concurrent callers must subscribe to a SINGLE consistent view of the drain's progress; (d) the cancellation-propagation contract must distinguish "drain-in-progress, reaper alive" from "drain committed-complete, reaper released" from "reaper-cancelled-mid-drain"; (e) no executor pre-binding (the mutex does not know the reaper's executor at construction).

**Invariants:**

- **I-1 (lazy lifetime and deterministic publication).** `drain_latch_state` is constructed via `std::make_shared<>` ONLY inside the first `cancel_and_drain` caller's coroutine frame (the reaper). The mutex holds an atomic `std::shared_ptr<drain_latch_state> drain_latch_ptr_` for the current epoch. The reaper publishes `drain_latch_ptr_.store(state, release)` before `draining_.store(true, release)`. Every other caller first acquire-loads `draining_`; if true, it acquire-loads `drain_latch_ptr_`, which is non-null until the epoch's result is published. If `drain_latch_ptr_` is null after `draining_ == true`, the prior epoch has already published release/abort and cleared the pointer. The mutex constructor default-initialises `drain_latch_ptr_` to null.
- **I-2 (shared_ptr survives reaper frame).** The reaper's `shared_ptr<drain_latch_state>` is captured by every `schedule_resume_on_bound_executor` lambda. If the reaper's awaitable is cancelled mid-wait, the reaper's frame is destroyed, but the lambda captures retain shared_ptr references; `in_flight_resumptions_` continues to be observed correctly until each handler decrements it. Closes Opus N-R3-P1-2 (in_flight UAF on reaper cancellation).
- **I-3 (atomic shared_ptr update).** `drain_latch_ptr_` is an atomic `std::shared_ptr<drain_latch_state>` (or equivalent project-internal helper) specified in §6.2.2 with release/acquire semantics. The reaper publishes via `store(state, release)` before setting `draining_`; subscribers load via `load(acquire)`. The release-acquire pairing guarantees that subscribers observing `draining_ == true` during the epoch observe the live shared_ptr, and subscribers observing null do so only after `signal_release()` or `signal_abort()` has published the epoch result and the pointer has been cleared.
- **I-4 (multi-waiter latch).** The latch's `wait()` member is implemented as `asio::experimental::concurrent_channel<void()>`'s `async_receive(...)` — a real ASIO primitive (present in `asio::experimental` since ASIO 1.27; used in production builds). `signal_release()` calls `try_send(...)` for every parked subscriber AND sets `released_` to true; subsequent `wait()` calls fast-path on `released_ == true`. The channel's executor is captured at the reaper's `co_await this_coro::executor` and bound to the state object — closes Opus N-R3-P1-1 (the v1.2 cited precedent — `[2d §6.5]` `cancellable_dispatch`-over-`steady_timer`-poll-loop — was wrong; v1.3 cites `asio::experimental::concurrent_channel` directly, which is a real ASIO primitive that publishes `wait()` semantics with cancellation-slot integration). The cited precedent now matches the implementation. **Implementation alternative:** if `asio::experimental::concurrent_channel` is unavailable on a target toolchain, the project ships a minimal `detail::drain_latch_state` implementation as `asio::async_initiate<...>` over a project-owned subscriber-list (a `std::vector<completion_handler>` guarded by a small spinlock or an MPSC list); `signal_release()` invokes every subscriber on its bound executor. The fallback shape is byte-equivalent in semantics; the §9 seam #24-extended verifies both shapes.
- **I-5 (cancellation propagation, not shielding; subscribers wake).** The reaper does NOT cancellation-shield. If the caller's parent `cancellation_state` fires `cancellation_type::total` while the reaper is parked on `latch_state->wait()`, the wait awaitable surfaces `operation_aborted`; the reaper observes the failed `wait_result`, calls `signal_abort()`, and returns `expected_t::unexpected{sync_lock_aborted}`. `signal_abort()` sets `aborted_ = true` (release) without setting `released_`, wakes every parked subscriber, and makes subsequent `wait()` calls fast-path on `aborted_ == true`. Subscribers observe `released_ == false` AND `aborted_ == true` and return `expected_t::unexpected{sync_lock_aborted}`. The mutex's `draining_` flag stays `true` (subsequent `async_lock` returns `sync_lock_drained` — the partial drain is observably "draining"); the in-flight resumption handlers (reaped waiters whose cancellation was already CAS'd) continue to run on their bound executors; `in_flight_resumptions_` decrements as they complete. **Rationale for propagation over shielding:** shielding the reaper would make `cancel_and_drain` un-cancellable from its caller's parent state, violating the `[2d §7.4]` cancellation-honoring contract. Propagation surfaces the cancellation outcome to the caller cleanly via `unexpected{sync_lock_aborted}` (the same shape as a normal in-flight `async_lock` cancellation per §4.5); the caller's graceful-close path can decide whether to retry, treat as failure, or proceed with mutex destruction (subject to the destructor's `state_ == not_locked` precondition, which the partial drain satisfies if all in-flight handlers complete before destruction is attempted).
- **I-6 (destructor precondition under partial drain).** A partial-drain mutex (cancel_and_drain returned `unexpected{sync_lock_aborted}` but in-flight resumption handlers are still pending) is NOT yet safe to destroy. The consumer's graceful-close discipline must either (a) re-call `cancel_and_drain` and wait for normal completion, OR (b) externally synchronise on the in-flight resumption handlers' completion before invoking `~async_mutex()`. The destructor's `state_ == not_locked` check fires `std::terminate` if violated. The §9 seam #26 ("`cancel_and_drain` awaitable cancellation propagation") includes a sub-test for this lifetime invariant.
- **I-7 (two terminal latch signals).** `signal_release()` and `signal_abort()` are mutually terminal and idempotent. `signal_release()` sets `released_ = true`, leaves `aborted_ == false`, wakes subscribers, and authorises clearing `drain_latch_ptr_` after the drain result is published. `signal_abort()` sets `aborted_ = true`, leaves `released_ == false`, wakes subscribers, and authorises clearing `drain_latch_ptr_` after the abort result is published. A subscriber never waits on a latch whose terminal result has already been published.
- **I-8 (non-terminal wake).** notify() is a non-terminal wake signal distinct from signal_release() / signal_abort(). Calling notify() does NOT publish a terminal result on drain_latch_state; subscribers parked in wait() are woken (the awaitable returns success) and re-evaluate the latch's counter conditions (active_holders_count_ == 0, active_acquirers_count_ == 0, in_flight_resumptions_ == 0). On a notify() wake, the reaper re-checks counters and either parks again on a fresh wait() (counters non-zero) or proceeds to terminal publication (counters zero). notify() is idempotent and may be called multiple times; the channel-side cost is bound to a single try_send() per call.

**Cancellation-slot result table for `cancel_and_drain` (cross-reference §4.5 table):**

| Caller's `cancellation_type` signalled mid-drain | Reaper behaviour | `cancel_and_drain`'s return value |
|---|---|---|
| (no cancellation) | Normal: walks lists, waits for quiescence, signals release. | `expected_t<void>{}` |
| `total` (or `terminal` — treated as `total`) | Propagate: observe `wait_result` failed; call `signal_abort()`; return unexpected. drain_latch_state lives on via in-flight resumption handlers' shared_ptr captures. | `expected_t::unexpected{sync_lock_aborted}` |
| `partial` | Treated as `total` per `[2d §4.7]` (same as §4.5 cancellation table). | `expected_t::unexpected{sync_lock_aborted}` |

#### 4.7.4 Consumer-side discipline

The canonical pre-destruction shape:

```cpp
// Inside the consumer's graceful close (e.g., MessageStore impl):
co_await my_mutex.cancel_and_drain();   // every in-flight waiter completes
                                         // with unexpected{sync_lock_aborted}.
// Consumer is now safe to destruct; ~async_mutex's precondition is met.
```

Composes with `[2d §10] Q3` engine-shutdown ordering: `~Engine` blocks on session drains; each `~Session`'s graceful close awaits `cancel_and_drain()` on every mutex it owns before destroying the mutex's owner. The §9 seam **"`cancel_and_drain()` reaps every in-flight waiter"** (#19) verifies under contention; the §9 seam **"Destructor-with-waiters fires `std::terminate`"** (#5, release-mode death test) verifies the precondition holds.

---

## 5. Public C ABI

**Delegated to 2i.** `fixpp::sync::async_mutex` is C++ only and does not cross the C ABI boundary. Per `[const §X]` and `[arch §4.10]`, the C ABI cannot expose templates or coroutine types; an `awaitable<expected_t<async_lock_guard>>` is both. 2f records that no symbol or type is owed to 2i.

The error variants 2f introduces (§6.5) — `error::sync_lock_aborted`, `error::sync_lock_alloc_failed`, `error::sync_lock_outside_session` — *do* surface at the C ABI under the `FIXPP_ERR_*` enum that 2i locks; per the per-doc-prefix discipline established by `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]`, 2f's prefix is `FIXPP_ERR_SYNC_*`. The cancellation variant joins the existing `FIXPP_ERR_CANCELLED` group per `[2d §6.7]` (Appendix D §D.2 codifies the mapping).

---

## 6. Behavioural contract

### 6.1 Allocation, exceptions, threading, cancellation

#### 6.1.1 Allocation

- **Uncontended fast path** — zero allocation. `await_ready` runs one CAS and returns true; `await_resume` constructs the `async_lock_guard` (8 B, single pointer copy). No PMR call, no global heap.
- **Contended path under HALO (embedded)** — zero allocation on the v1.0 hot path. The waiter is embedded in the caller's coroutine frame; the cancellation slot's handler closure storage uses the awaiter's inline 32-byte `slot_storage_` buffer via `detail::slot_allocator{this, nullptr}` (RC-C close — §4.3.4 case 1). Per the §4.3.4 three-case sub-table the embedded-with-HALO branch routes to `null_memory_resource()` on overflow (a bench-harness alarm, not a hot-path event). The §9 seam #21 verifies the inline-buffer storage selection.
- **Contended path under HALO failure (embedded-no-HALO)** — observable global-heap touch (§4.3.4 case 2); non-fatal **iff** the §9 PMR-fallback seam #10 passes for the same toolchain (Opus N-P3 close). The §6.4 HALO-firing seam #9 detects this; the budget is benchmark-mode-soft.
- **PMR fallback path** (§4.3.4 case 3) — one allocation from caller-supplied `mr` for the awaiter and one for the slot handler closure (allocated via `detail::slot_allocator{this, mr}` → `std::pmr::polymorphic_allocator<void>{mr}`). No global heap. Allocation failure routes through `fixpp::core::detail::trap_throw` and surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}`.
- **Per `[const §VIII.5]`** — zero `new`/`delete` between parse and `fromApp` extends to the contended `async_lock` path on the store-write mutex per `[const §XI.5]`. 2f satisfies the discipline on the v1.0 hot path (case 1); the §9 seam **"PMR fallback exercise"** (#10) verifies under `mallocnesia` for both the embedded (case 1) and PMR (case 3) branches (Codex C-P2-6 split coverage close).

#### 6.1.2 Exceptions

- All public methods are `noexcept`. The PMR fallback path's `allocate(...)` may throw `std::bad_alloc`; `trap_throw` (per `[2a §4.2]`) converts the throw into an `expected_t::unexpected{error::sync_lock_alloc_failed}` without terminating. No exception crosses the coroutine boundary.
- Per `[arch §5.3]` "no exceptions on the hot path" — 2f satisfies the rule by construction.

#### 6.1.3 Threading

- **The mutex itself is thread-safe.** `state_` is `std::atomic<uintptr_t>`; the §6.2 sub-table pins the memory ordering on every atomic operation.
- **The v1.0 invariant** — every consuming site of `async_mutex` runs inside a single session serialisation domain (per `[2d §4.8]`). Under that discipline, contention is structurally zero (§1.1); the mutex is defence-in-depth.
- **Cross-domain pathological case** (§9 seam **"Cross-strand acquire"**, #13): two consumers on different domains acquire the same mutex; the second arrival suspends on the LIFO list; the §4.5.2 drain delivers FIFO fairness within the drain cycle.
- **`unlock` on a different executor than the holder.** The waiter completes on its own bound executor (per `[2d §7.4]`) under the §4.6 inline-vs-post predicate.

#### 6.1.4 Cancellation

Per §4.5: `total` triggers the CAS-arbitration race; the winner determines whether the awaiter resumes with the guard or with `unexpected{sync_lock_aborted}`. `partial` is treated as `total`; `terminal` is treated as `total`. The 2f-boundary outcome (`expected_t::unexpected{sync_lock_aborted}`) is observable in the same shape as `[2d §6.5]`'s `cancellable_dispatch → expected_t::unexpected{dispatch_aborted}`. The C-ABI mapping joins `FIXPP_ERR_CANCELLED` per `[2d §6.7]` (Appendix D §D.2).

### 6.2 The atomic state machine + memory-ordering sub-table (RC#1 + RC#5)

#### 6.2.1 Three-state encoding

```
                   ┌──────────────┐
                   │  not_locked  │   = uintptr_t{1}
                   └──────┬───────┘
                          │ CAS(1 → 0) on async_lock fast path (acquire/relaxed)
                          ▼
                   ┌──────────────┐
                   │   locked_    │   = uintptr_t{0}
                   │ no_waiters   │
                   └──┬───┬───────┘
        unlock()─────┐│   │
        exchange     ▼│   ▼ async_lock contended
        (0 → 1)──► not_locked     CAS(0 → &waiter, release/acquire)
                      │
                      ▼
                   ┌──────────────┐
                   │  &head       │   pointer to LIFO head
                   └──┬───┬───────┘
                      │   │
        async_lock ───┘   │  unlock() walks next_drain_head_ FIRST
        (more waiters)    │  (RC-A v1.1 — mutex-owned residual);
        CAS(&old_head     ▼  exchange(head → 0, acq_rel) on state_ if
            → &new,            residual empty; reverse LIFO to FIFO;
            release/acquire)   v1.2 one-winner walk: skip `cancelled`
                               heads, find the FIRST `queued` waiter,
                               CAS only that waiter's phase_ from
                               queued → granted (acq_rel), then the CAS
                               winner writes *winner->result_ and increments
                               active_holders_count_; splice the UNTOUCHED tail
                               (every waiter past winner->next_, all
                               still in phase queued) into
                               next_drain_head_ via
                               compare_exchange_weak(nullptr →
                               tail_head); cancelled waiters are dropped
                               (their resumption was scheduled by their
                               own cancel handler).
```

**Ordering specification for the grant CAS (v1.4 CAS-then-publish close).** The drain-side `phase_.compare_exchange_strong(queued → granted, memory_order_acq_rel)` is the single arbitration primitive that determines the sole writer allowed to publish the awaiter's `result_` slot. The `acq_rel` order is mandatory:

- **Release** (writer side, on the unlocking thread): publishes the terminal phase transition that `await_resume` acquire-loads before reading `*result_`.
- **Acquire** (writer side): pairs with any prior cancel-CAS-loser's writes to the same awaiter's state (the `phase_` load on a failed cancel-CAS publishes-and-becomes-visible to subsequent grant-CAS on the same atom).
- **Resumed-coroutine side**: `await_resume`'s `phase_.load(memory_order_acquire)` pairs with the grant-CAS-release; the winner writes `*result_` after the CAS and before scheduling the bound-executor resumption.

The same protocol applies to the cancel CAS (`queued → cancelled`) — the CAS winner writes `result_` after the CAS and before scheduling the bound-executor resumption; the loser never touches `result_`. **No separate "ready publication" step is needed**; the per-waiter phase atom carries both the arbitration edge and the terminal-state signal in one CAS.

#### 6.2.2 Memory-ordering sub-table (RC#5 fix)

Pins the ordering on every atomic operation. ARM64 weak-memory model is the load-bearing target; x86's TSO would mask several of these otherwise. The §9 seam **"ARM64 weak-memory contention stress"** (#18) exercises this on a Linux-ARM64 TSan run.

| Operation | Atomic | Success order | Failure order | Rationale |
|---|---|---|---|---|
| Fast-path acquire | `state_.compare_exchange_strong(not_locked → locked_no_waiters)` | `memory_order_acquire` | `memory_order_relaxed` | Acquire pairs with the prior holder's release-exchange in `unlock()`. Failure is a hint to enqueue. |
| Initial head-load on contended push | `state_.load()` | `memory_order_acquire` | n/a | Pairs with prior `unlock()` exchange's `release`. |
| LIFO push CAS | `state_.compare_exchange_weak(old → &waiter)` | `memory_order_release` | `memory_order_acquire` | Release publishes the awaiter's `next_`/`phase_` writes to the unlocker. Failure-acquire so the retry sees the freshest head. |
| Unlock exchange | `state_.exchange(locked_no_waiters)` | `memory_order_acq_rel` | n/a | Acquire pairs with each pushed waiter's release; release publishes the unlock-decision to the next acquirer. |
| Empty-list close-out CAS | `state_.compare_exchange_strong(locked_no_waiters → not_locked)` | `memory_order_acq_rel` | `memory_order_acquire` | CAS-failure means a new pusher arrived; the failure-acquire reads the new head. |
| Per-waiter phase CAS (drain) | `phase_.compare_exchange_strong(queued → granted)` | `memory_order_acq_rel` | `memory_order_acquire` | Drain grants ownership and elects the sole result-slot publisher. **v1.4 CAS-then-publish:** on CAS-success the unlocker writes `*winner->result_ = engaged-guard` and schedules the resumption; on CAS-failure it performs no `*result_` write. Failure-acquire reads the cancellation/reaper winner's update. v1.1: target phase is `granted` (was `draining` in v1.0; RC-A close). |
| Per-waiter phase CAS (cancel) | `phase_.compare_exchange_strong(queued → cancelled)` | `memory_order_acq_rel` | `memory_order_acquire` | Cancellation elects the sole result-slot publisher. **v1.4 CAS-then-publish:** on CAS-success the cancellation handler or reaper writes `*head->result_ = unexpected{sync_lock_aborted}` and schedules the resumption; on CAS-failure it performs no `*result_` write. Failure-acquire reads the drain/cancel winner's update. v1.1: target phase is `cancelled` (was `cancelling` in v1.0). |
| Per-waiter phase CAS (drain — `await_resume` finalise) | `phase_` is set by the drain CAS itself; no separate finalise store | n/a | n/a | v1.1 / RC-A close: the drain CAS to `granted` is terminal (no separate `completed` store needed because the v1.1 enum collapses `draining → completed` into one CAS). |
| `await_resume` phase load | `phase_.load()` | `memory_order_acquire` | n/a | Pairs with the writer's release-CAS (drain winner published `granted`, or cancel winner published `cancelled`). The resumed coroutine reads `*result_` only after this acquire-load; the winner has written `*result_` before scheduling the bound-executor resumption. |
| `result_` slot publication | non-atomic write by the CAS winner only; **ordered AFTER** the `phase_` release-CAS and BEFORE bound-executor resumption | n/a | n/a | **v1.4 CAS-then-publish.** The writer first wins `phase_.compare_exchange(queued → granted)` or `(queued → cancelled)` with `memory_order_acq_rel`; only that winner writes `*result_`; CAS losers do not touch the non-atomic slot. The winner then resumes the coroutine on the awaiter's bound executor. `await_resume` acquire-loads `phase_` before reading `*result_`, and the winner's result write is sequenced before resumption. This removes the two-pre-CAS-writers data race. |
| **NEW v1.1 / RC-A** — `next_drain_head_` push (residual splice from `unlock()`) | `next_drain_head_.compare_exchange_weak(nullptr → residual_chain_head)` (or append-tail retry loop) | `memory_order_release` | `memory_order_acquire` | Release publishes the residual chain's `next_` writes to the next unlocker that walks `next_drain_head_`. Failure-acquire reads the freshest existing residual head for tail append. |
| **NEW v1.1 / RC-A** — `next_drain_head_` walk (drain start, in `unlock()` and `cancel_and_drain()`) | `next_drain_head_.exchange(nullptr)` | `memory_order_acq_rel` | n/a | Acquire pairs with each pusher's release; release publishes the walker's atomic-take-ownership decision (no other walker observes the same chain). |
| **NEW v1.1 / RC-A** — `next_drain_head_` re-publish after granted-waiter ownership transfer | same as residual-splice push (`compare_exchange_weak(nullptr → remaining_head)` with retry) | `memory_order_release` | `memory_order_acquire` | Same protocol as the residual splice; documented as a separate row because the call site is in `unlock()` step 1 (RC-A close — not `unlock()` step 5). |
| **NEW v1.1 / RC-B; UPDATED v1.4** — `draining_` publish (set by `cancel_and_drain`) | `draining_.store(true)` after `drain_latch_ptr_.store(state)` | `memory_order_release` | n/a | Release pairs with `await_ready`/`await_suspend` and second-call acquire-loads. v1.4 requires `drain_latch_ptr_` to be release-stored before this store so concurrent callers observing `draining_ == true` can acquire-load a non-null latch during the epoch. |
| **NEW v1.1 / RC-B** — `draining_` load (in `await_suspend`) | `draining_.load()` | `memory_order_acquire` | n/a | Pairs with `cancel_and_drain`'s release-store. |
| **NEW v1.3 / RC-α** — `draining_` load (in `await_ready`, NEW v1.2 fast-path pre-check) | `draining_.load()` | `memory_order_acquire` | n/a | Pairs with `cancel_and_drain`'s release-store (same partner as `await_suspend` row above; the `await_ready` site is the v1.2 pre-CAS check that closes the v1.1 fast-path bypass). |
| **NEW v1.3 / RC-α** — `draining_` load (in `unlock`'s drain-aware short-circuit) | `draining_.load()` | `memory_order_acquire` | n/a | Pairs with `cancel_and_drain`'s release-store. The `unlock()` short-circuit per §4.5.2 is now a load-only check; under `draining_ == true`, the unlocker does NOT splice into `next_drain_head_` — closes Opus C-R3-P1-3 (unlock-vs-reaper splice race). |
| **NEW v1.3 / RC-α** — `active_holders_count_` increment (winner-only post-CAS) | `active_holders_count_.fetch_add(1)` | `memory_order_acq_rel` | n/a | Acquire pairs with the prior holder's `unlock()`-time decrement (sequencing the holder count's monotonic transitions); release publishes the new holder's existence to `cancel_and_drain`'s observation point in step (h). Performed AT the grant CAS-success site (fast-path `await_ready` step 2 OR drain-walker grant-CAS step 1c-iii in §4.5.2) — winner-only, NOT before any CAS. Closes Opus C-R3-P1-1's "phantom holder count on CAS loss" leak. |
| **NEW v1.3 / RC-α** — `active_holders_count_` decrement (at `unlock()` entry) | `active_holders_count_.fetch_sub(1)` | `memory_order_acq_rel` | n/a | Acquire pairs with the increment-CAS-success at acquire time; release publishes the holder's exit to `cancel_and_drain`'s wait loop. |
| **NEW v1.3 / RC-α** — `active_holders_count_` load (in `cancel_and_drain` step (h) wait-loop) | `active_holders_count_.load()` | `memory_order_acquire` | n/a | Pairs with the latest holder's `fetch_sub` release. The reaper waits for this counter to observe zero before publishing the release-edge. |
| **NEW v1.3 / RC-α** — `active_acquirers_count_` increment (in awaitable factory, before `await_ready`'s `draining_` load) | `active_acquirers_count_.fetch_add(1)` | `memory_order_acq_rel` | n/a | Acquire pairs with the previous acquirer's decrement at one of the three exit points (fast-path success, drained-bypass, await_suspend LIFO-enrol); release publishes the in-flight acquirer's existence to `cancel_and_drain`'s observation point. The increment is sequenced-before the `draining_.load(acquire)` in `await_ready` step 1, closing Opus C-R3-P1-2's in-flight acquirer window. |
| **NEW v1.3 / RC-α** — `active_acquirers_count_` decrement (at fast-path success / drained-bypass / await_suspend LIFO-enrol) | `active_acquirers_count_.fetch_sub(1)` | `memory_order_acq_rel` | n/a | Three decrement-points per §4.2.1 / §4.2.2; each is sequenced after the corresponding exit decision (CAS-success, drained-fast-fail, LIFO-push). Release publishes the acquirer's transition (to holder, to fast-fail return, or to LIFO-enrolled waiter) to `cancel_and_drain`'s wait loop. |
| **NEW v1.3 / RC-α** — `active_acquirers_count_` load (in `cancel_and_drain` step (h) wait-loop) | `active_acquirers_count_.load()` | `memory_order_acquire` | n/a | Pairs with the latest decrement's release. The reaper waits for this counter to observe zero before publishing the release-edge — closes Opus C-R3-P1-2 (the v1.2 holder-count alone did not cover in-flight acquirers). |
| **NEW v1.3 / RC-β; UPDATED v1.4** — `drain_latch_ptr_` store (by reaper before publishing `draining_`) | atomic-store of `std::shared_ptr<drain_latch_state>` (or equivalent helper) | `memory_order_release` | n/a | Release publishes the live state object's address to subscribers in step (a)/(b). This store is sequenced before `draining_.store(true, release)`, so a caller that acquire-loads `draining_ == true` can acquire-load a non-null latch pointer during the epoch. The atomic-update path is cold (only on `cancel_and_drain` invocation); not on the hot acquire/release path. |
| **NEW v1.3 / RC-β; UPDATED v1.4** — `drain_latch_ptr_` load (by subscribers in step (a)/(b), by `unlock`'s notify path) | atomic-load of `std::shared_ptr<drain_latch_state>` | `memory_order_acquire` | n/a | Pairs with the reaper's release-store. During an active epoch, callers that observed `draining_ == true` receive a non-null shared_ptr. A null load after `draining_ == true` means the epoch already published release/abort and cleared the pointer. |
| **NEW v1.3 / RC-β** — `drain_latch_state::released_` store (in `signal_release`) | `released_.store(true)` | `memory_order_release` | n/a | Pairs with subscribers' `released_.load(acquire)` after their `wait()` completes. Publishes the drain-complete decision to all parked subscribers. |
| **NEW v1.4** — `drain_latch_state::aborted_` store (in `signal_abort`) | `aborted_.store(true)` | `memory_order_release` | n/a | Pairs with subscribers' `aborted_.load(acquire)` after their `wait()` completes. Publishes the reaper-cancelled decision and wakes subscribers that would otherwise remain parked. |
| **NEW v1.3 / RC-β; UPDATED v1.4** — `drain_latch_state::released_` / `aborted_` load (after `wait()` returns) | `released_.load()` / `aborted_.load()` | `memory_order_acquire` | n/a | Pairs with `signal_release`'s or `signal_abort`'s release-store. Subscribers use this to disambiguate "released, drain succeeded" from "aborted, reaper cancelled"; `released_ == false && aborted_ == true` returns `unexpected{sync_lock_aborted}`. |
| **NEW v1.5** — `notify()` (channel try_send) | channel-side `try_send()` | relaxed for the channel send (the channel's internal sequencing is the synchronisation primitive) | n/a | non-terminal wake; the receiver's `wait()` re-loads counter atomics with acquire ordering, so `notify()` itself does not need to publish writes. |
| **NEW v1.3 / RC-β** — `drain_latch_state::in_flight_resumptions_` increment (at every `schedule_resume_on_bound_executor` call in step (f) reap_chain) | `in_flight_resumptions_.fetch_add(1)` | `memory_order_acq_rel` | n/a | Acquire pairs with the previous resumption handler's `fetch_sub` (sequencing the count's monotonic transitions); release publishes the new in-flight resumption to the reaper's step (h) wait loop. |
| **NEW v1.3 / RC-β** — `drain_latch_state::in_flight_resumptions_` decrement (in resumption-handler lambda) | `in_flight_resumptions_.fetch_sub(1)` | `memory_order_acq_rel` | n/a | Acquire pairs with the increment at scheduling time; release publishes the resumption's completion to the reaper's wait loop. The handler that observes `fetch_sub == 1` (i.e., it was the last) AND the count conditions are met calls `signal_release`. |
| **NEW v1.3 / RC-β** — `drain_latch_state::in_flight_resumptions_` load (in `cancel_and_drain` step (h) wait-loop) | `in_flight_resumptions_.load()` | `memory_order_acquire` | n/a | Pairs with the latest decrement's release. The reaper waits for this counter to observe zero (along with the holder and acquirer counts) before exiting the wait loop. |

The cppcoro / Lewis-Baker reference algorithm uses exactly this pattern for the LIFO state; v1.3's `active_holders_count_` / `active_acquirers_count_` / `drain_latch_state` rows are project-novel additions documented above for ARM64-correct implementation. The table transcribes it for ARM64-correct implementation.

### 6.3 Latency Tier 1 ceilings

Per the 2a v0.3 §6.5 / 2b v0.2 §6.6 / 2d v0.4 §6.3 / 2e v0.4 §6.6 idiom: Linux/Clang/x86_64 warm-cache, named workload. CI fails on >5% regression vs the previous tagged release.

| Operation | Workload | Ceiling | Per-component breakdown |
|---|---|---|---|
| `async_lock` uncontended (await_ready wins) | Single CAS fast path; awaiter not constructed; guard returned via `await_resume`. | **≤ 20–25 ns** (v1.3 / RC-α / Opus N-R3-P2-1 close — raised from v1.2's lower ceiling to accommodate the new acquirer-counter and holder-counter RMWs; Codex C-P2-8 close retained for the qualitative "single CAS fast path" claim) | `active_acquirers_count_.fetch_add(acq_rel)` (v1.3 / RC-α) ≈ 5 ns + `draining_.load(acquire)` ≈ 1–2 ns + `state_` CAS atomic on warm L1 ≈ 5 ns + `active_holders_count_.fetch_add(acq_rel)` (v1.3 / RC-α winner-only) ≈ 5 ns + `active_acquirers_count_.fetch_sub(acq_rel)` ≈ 5 ns + `await_ready` true ≈ 0 ns + `async_lock_guard` ctor (single pointer copy) ≈ 2 ns + `co_await` resume cost (HALO-elided) ≈ 3 ns ≈ ≤ 25 ns ceiling. **Note (v1.4):** this row states only 2f's local ceiling; it does not declare or renegotiate a cross-doc MemoryStore envelope. The acquirer-counter and holder-counter atomic RMWs are on the same cache line as `state_` (§1.1 mutex layout), so cache-line traffic is bounded; under contention with concurrent `cancel_and_drain` traffic the actual cost may approach the ceiling. |
| `async_lock` contended (waiter suspends) | Waiter pushed onto LIFO; cancellation slot bound; coroutine handle captured. | ≤ 80 ns | Initial CAS-fail ≈ 10 ns + awaiter construction (HALO-elided) ≈ 10 ns + slot bind via `asio::bind_allocator(detail::slot_allocator{this, mr})` (RC-C) ≈ 5 ns + LIFO push CAS retry (1–2 attempts low contention) ≈ 15 ns + suspend boilerplate ≈ 30 ns + headroom. |
| `unlock` uncontended | Empty LIFO; atomic exchange + close-out CAS. | **≤ 15 ns** (v1.3 / RC-α / Opus N-R3-P2-1 close — raised from v1.2's ≤ 10 ns to accommodate the holder-counter decrement) | `active_holders_count_.fetch_sub(acq_rel)` (v1.3 / RC-α) ≈ 5 ns + Exchange ≈ 5 ns + close-out CAS ≈ 3 ns + return boilerplate ≈ 2 ns ≈ ≤ 15 ns. |
| `unlock` contended drain — drain-side handoff cost (N waiters) | LIFO reversed to FIFO; per-waiter phase CAS; granted waiter receives ownership; remaining `queued` waiters spliced into `next_drain_head_` (RC-A); resumed under §4.6 policy. | ≤ 30 ns + (≤ 50 ns per waiter handoff, **same-strand `dispatch` only — drain-side cost only**) | Atomic exchange (`state_` and/or `next_drain_head_`) ≈ 5 ns + LIFO-to-FIFO reversal (walk) ≈ 5 ns/waiter — bookkeeping ≈ 30 ns total for the head; per-waiter resumption handoff (same-strand dispatch): phase CAS ≈ 5 ns + slot clear (lazy via `await_resume`) ≈ 0 ns + dispatch inline ≈ 0 ns + headroom ≈ 30 ns. **Excludes the resumed coroutine's own work** — see next row (Opus N-P2-4 close — v1.0 conflated handoff cost with resumed-coroutine first-checkpoint cost). |
| Resumed-coroutine first-checkpoint latency (per granted waiter) | The granted waiter's coroutine resumes inline on the unlocking thread under `dispatch` policy; runs its critical section + its own `co_await` chain until the first suspend point or completion. | **bench-harness-soft, unbounded by 2f's surface** | This is the resumed coroutine's own work (critical section + its first `co_await` chain); 2f's drain budget covers the handoff only. Profiled by the §6.3 footer's bench-harness-soft policy (per `[2e §6.6]` precedent) — 2e's downstream rows (`MemoryStore::store` ≤ 200 ns) build on the handoff cost, not on this row. |
| `unlock` contended drain (cross-strand resume) | Waiter's bound executor differs from unlocker's. | ≤ 30 ns + (≤ 250 ns per waiter, **bench-harness-soft**) | Per-waiter cost dominated by `asio::post` cross-thread wakeup per `[2d §6.3]` row 2 (250 ns). Investigated, not auto-fail. |
| `cancel_and_drain()` per N in-flight waiters | Allocates lazy `std::shared_ptr<drain_latch_state>` (v1.3 / RC-β) and publishes it into `drain_latch_ptr_` before setting `draining_`; atomically exchanges `state_` (sentinel-discriminated) and `next_drain_head_`; CAS'es each `queued` waiter to `cancelled` and only the CAS winner writes `*result_`; resumes each on its bound executor; re-walks BOTH lists in a stable loop (v1.3 / RC-α / Opus C-R3-P1-3 close); waits for `active_holders_count_ == 0` AND `active_acquirers_count_ == 0` (v1.3 / RC-α) AND `in_flight_resumptions_ == 0` via `co_await latch_state->wait()` over `asio::experimental::concurrent_channel` (v1.3 / RC-β — replaces v1.2's by-value `detail::drain_latch` member that owned an `asio::steady_timer` — the v1.2 shape was non-implementable per Opus C-R3-P2-1). | ≤ 120 ns + (≤ 80 ns per waiter, **bench-harness-soft**) | `make_shared<drain_latch_state>` heap allocation ≈ 25 ns (cold path, only on `cancel_and_drain` invocation) + `drain_latch_ptr_` store + `draining_` store + sentinel-discriminated `state_` exchange + `next_drain_head_` exchange ≈ 15 ns + per-waiter phase CAS + winner-only `result_` write ≈ 7 ns + scheduling resumption on bound executor (per `[2d §6.3]`) + `concurrent_channel`-backed `wait()` wakeup. The §9 seams #19, #23, #24, #25 (in-flight acquirer), #26 (awaitable cancellation propagation), #27 (unlock-vs-reaper splice closure), #28 (`*result_` CAS-then-publish arbitration), and #29 (reaper-abort subscriber wakeup) cover the v1.4 budgets. |

The cumulative drain cost is bench-harness-soft (per `[2e §6.6]` precedent on long-tail rows); the per-row hard budget is the named ceiling.

### 6.4 HALO discipline

Per `[const §XI.6]` HALO-first discipline:

- The `async_lock` awaiter is HALO-eligible by construction: ≤ 96 B layout per §1.1 v1.1 (RC-A drops `residual_`; RC-C adds the 32-B `slot_storage_` inline buffer), no escape to the heap on the embedded path with HALO firing (§4.3.4 case 1), intrusive `next_` link is awaiter-internal and is reused by both the `state_` LIFO chain and `next_drain_head_` FIFO chain (RC-A).
- HALO firing on the inbound dispatch path (per `[arch §11]` row 2) is the verification spike co-owned with 2d. Failure is non-fatal for v1.0 **iff the PMR fallback conformance seam (§9 #10) passes for the same toolchain** (Opus N-P3 close); otherwise the toolchain is blocked for that path.
- §10 Q1 records the open spike work; §9 seam **"HALO firing across compiler matrix"** (#9) is the verification harness across Linux/Clang, Linux/GCC, Windows/MSVC.

### 6.5 Errors introduced by this design

Per the per-doc-prefix discipline established by `[2a §6.7]` / `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]`: 2f adopts the prefix **`FIXPP_ERR_SYNC_*`** for its C-ABI mapping target, owned by 2i.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `sync_lock_aborted` | §4.5 — cancellation won the §4.5.1 CAS-arbitration race against the drain. The waiter was not granted ownership. | Cancellation outcome — joins `[2d §6.7] dispatch_aborted` and `[2d §6.7] clock_sleeps_cancelled` and `[2e §6.7] store_cancelled` in the `FIXPP_ERR_CANCELLED` group at the C ABI per Appendix D §D.2. The FSM treats this distinct from runtime errors: cancellation = no state change. |
| `sync_lock_alloc_failed` | §4.3 / §4.3.4 — PMR fallback path's `allocate(...)` threw `std::bad_alloc` (caller-supplied `mr` exhausted), or the embedded path's inline buffer overflowed and `null_memory_resource()` rejected the allocation. Trapped via `trap_throw` per `[2a §4.2]`. | Configuration / capacity error — operator raises the resource cap (PMR case) or the algorithm fix-forward updates the inline buffer size (embedded case). The mutex itself is unaffected. Group `FIXPP_ERR_SYNC_RUNTIME`. |
| `sync_lock_outside_session` | §4.3.2 — the session-side helper `async_lock_via_session_executor` was called outside any session serialisation domain (the awaiter's bound executor is not a `session_executor` value). | Caller-error class — the caller should use the explicit `async_lock(mr)` overload directly. Group `FIXPP_ERR_SYNC_RUNTIME`. |
| **NEW v1.1 / RC-B** — `sync_lock_drained` | §4.7.2 — `cancel_and_drain()` set `draining_ = true`; subsequent `async_lock(...)` callers observe the flag and fast-fail without enqueuing. | Lifecycle / shutdown class — the consumer's graceful close has progressed past the drain phase; the caller should bubble the failure up through its own shutdown path. The mutex is no longer accepting new acquisitions; `~async_mutex()` is safe to call. Group `FIXPP_ERR_SYNC_RUNTIME`. |

4 variants (v1.1 adds `sync_lock_drained` per RC-B). **2f does NOT introduce an `async_mutex_destroyed_with_waiters` variant** — the destructor (§4.7) fires `std::terminate()`, not an `expected_t` form (RC#3 fix).

C-ABI mapping (delegated to **2i**):

- Cancellation → **`FIXPP_ERR_CANCELLED`** per `[2d §6.7]` (joining `dispatch_aborted`, `clock_sleeps_cancelled`, `store_cancelled`): `sync_lock_aborted` (Appendix D §D.2 codifies the rewording).
- Runtime / capacity → **`FIXPP_ERR_SYNC_RUNTIME`**: `sync_lock_alloc_failed`, `sync_lock_outside_session`, **`sync_lock_drained`** (v1.1 / RC-B close).

Final coalescing is 2i's call.

### 6.6 Enforcement of `[const §XV.9]` — `std::mutex`-in-coroutine-context CI gate

Per `[const §XV.9]`, plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`. Per `[const §XI.3]` ("Enforced by clang-tidy custom check or grep gate"), 2f names the enforcement mechanism:

**v1.0: grep gate** — `tools/check_no_std_mutex_in_awaitable_headers.sh`, run in Tier 1 CI per `[const §IX.4]`. The gate scans every header under `include/fixpp/...` and `src/`, **after preprocessing** (post-include-expansion), for the conjunction `<mutex>` (or `std::mutex` declaration) AND `asio::awaitable` / `<asio/awaitable.hpp>`. Hits fail the build with documentation pointers to `[const §XV.9]` and `[2f §4]`. The post-preprocessing scan addresses Codex C-P2-10's transitive-include concern.

**Post-v1: clang-tidy custom check** — `fixpp-no-std-mutex-in-coroutine-context`. More precise (catches alias / macro forms the grep gate cannot reach). v1.0 ships the grep gate with post-preprocessing scope; the clang-tidy check is post-v1 follow-up. §10 Q3 records the choice.

The §9 seam **"`std::mutex`-in-coroutine-context CI gate"** (#14) verifies the gate fires on a deliberately-violating fixture.

---

## 7. Integration with adjacent modules

### 7.1 MessageStore (2e) — direct client of the writer mutex

Per `[2e §6.4]`: every `MessageStore` mutating method serialises against the per-store-instance writer mutex; the mutex is `fixpp::sync::async_mutex` per `[const §XI.3]`.

The lock-acquisition shape used by `MemoryStore::store` / `retrieve` / `next_seqnum` / `reset`:

```cpp
asio::awaitable<expected_t<void>> MemoryStore::store(seqnum_t seq,
                                                    std::span<const std::byte> frame,
                                                    direction_t dir) noexcept {
    // The store is in-session; type-erasure is not in play; mr=nullptr.
    auto guard_or_err = co_await writer_mutex_.async_lock();
    if (!guard_or_err) {
        // sync_lock_aborted (cancellation) — bubble through to expected_t
        // per [2e §6.1.4]; mapped to store_cancelled on this layer.
        co_return std::unexpected(error{error_code::store_cancelled});
    }
    auto guard = std::move(*guard_or_err);  // RAII; releases on scope exit.
    // … verify seqnum order, copy bytes, advance entry index per [2e §4.2].
    co_return {};
}
```

**2f sign-off is the named hard hand-off gate for 2e implementation** per `[2e §3.1]`. The `MessageStore` impl sequences `cancel_and_drain()` on the writer mutex during its graceful close before returning from `~MemoryStore` / `~FileStore` (§7.6 cross-module ordering).

### 7.2 Pinset rotation (2g)

Per `[SYN §3.2 Q6b]` v1.0 use cases. The shape mirrors 2e's:

```cpp
asio::awaitable<expected_t<void>> Pinset::add(cert_t cert) noexcept {
    auto guard_or_err = co_await rotation_mutex_.async_lock();
    if (!guard_or_err) co_return std::unexpected(error{error_code::pinset_cancelled});
    auto guard = std::move(*guard_or_err);
    co_return {};
}
```

### 7.3 Seqnum counter (Phase-4 session-module spec)

Per `[SYN §3.2 Q6b]` v1.0 use cases. The session FSM's outbound-seqnum increment runs on the session strand under v1.0's single-domain discipline; the mutex is defence-in-depth (contention is structurally zero per §1.1). The Phase-4 session-module spec (not yet drafted) consumes `fixpp::sync::async_mutex` for the seqnum bookkeeping; 2f records the forward dependency.

### 7.4 Threading + Clock (2d) — supplier (executor-compat surface)

2f consumes the executor-compat surface locked at `[2d §7.4]`:

- The `async_lock` awaitable completes on the awaiter's bound executor (the `session_executor` wrapper class per `[2d §4.8]` for in-session callers); the inline-vs-post predicate is ASIO `dispatch` semantics on the bound executor (§4.6).
- `cancellation_type::total` is honoured per the §4.5 CAS-arbitration race; the 2f-boundary outcome is `expected_t::unexpected{sync_lock_aborted}` (Appendix D §D.2 rewords `[2d §4.7]`).
- Default completion policy is `dispatch`; per-mutex override is 2f's call.

2f does **not** consume the `Clock` surface (no timed acquire). 2f does **not** consume `cancellable_dispatch`.

`cancel_and_drain()` (§4.7) composes with `[2d §10] Q3` engine-shutdown ordering: every consumer's graceful close awaits drain on every mutex it owns before destroying the mutex's owner.

### 7.5 C ABI (2i)

`async_mutex` is C++ only. The error variants 2f introduces (§6.5) surface at the C ABI under the `FIXPP_ERR_SYNC_*` prefix and the existing `FIXPP_ERR_CANCELLED` group; 2i locks the symbol shape.

### 7.6 Phase-4 session-module spec

Consumes 2f's surface for:

- **Seqnum counter** — per `[SYN §3.2 Q6b]` item 1; defence-in-depth.
- **Lock-policy consumer** — per `[const §XI.5]` the store-write callsite cap binds 2e to mutex regardless of `SessionConfig::lock_policy`; the Phase-4 spec governs how `lock_policy` is consumed for any non-store-write site.
- **Graceful-close drain ordering** — every `Session` close path issues `co_await mutex.cancel_and_drain()` on every mutex the session owns before destroying the consumer that holds the mutex (§4.7.3).

The Phase-4 spec is not yet drafted; 2f records the forward dependency.

---

## 8. PMR — recap

`fixpp::sync::async_mutex` itself does **NOT** carry a `std::pmr::memory_resource*` field. The mutex's atomic state and policy field are stack/instance-allocated by the consumer. The PMR fallback path (§4.3) uses **caller-supplied** `mr` via the explicit `async_lock(mr)` overload (RC#2 fix). The session-side helper `fixpp::session::async_lock_via_session_executor` recovers the per-session resource via Appendix D §D.1's `Session::session_arena()` accessor and forwards into `async_lock(mr)`; this lives in `session/`, downstream of `core/` per `[arch §2.3]`.

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| Caller's coroutine frame (HALO-friendly) | caller's coroutine lifetime | the `async_mutex_awaiter` (**≈ 96 B / ≤ 96 B**, per §1.1 v1.1 budget — RC-C inline slot-handler-storage buffer; v1.2 / v1.3 add no awaiter-side fields, the new `active_holders_count_` / `active_acquirers_count_` / `drain_latch_ptr_` lifecycle members live on the **mutex** per §4.1; v1.3 / RC-β note: the v1.2 by-value `detail::drain_latch` and `drain_complete_` mutex members are retired in v1.3, replaced by the lazy atomic shared_ptr to `drain_latch_state`) on the contended embedded path; the cancellation slot's handler closure (allocator-bound to the awaiter's resource) | coroutine's own destruction |
| Caller-supplied `mr` (PMR fallback) | caller-determined | the awaiter when the embedded shape is unavailable (type-erased completion handlers); the slot's handler closure | caller-determined; awaiter de-allocates back on `await_resume` |
| `SessionConfig::session_arena` (per `[2d §4.5]` / `[2d §8]`; recovered via `Session::session_arena()` per Appendix D §D.1) | session lifetime | the awaiter when the session-side helper supplies the resource | session destruction |

**Lifetime classes for non-arena objects:**

- **`async_mutex` instance** — consumer-controlled lifetime (per-`MessageStore` / per-`Pinset` / per-counter-site); non-copyable, non-movable; held by-value at a stable address; `std::terminate()` precondition on destruction (§4.7).
- **`async_lock_guard`** — flyweight; lifetime bounded by the originating mutex; movable with destructive move-assign; releases on destruction; `[[clang::lifetimebound]]` on the constructor.
- **`async_mutex_awaiter`** — embedded in the caller's coroutine frame on the hot path; allocated from `mr` on the type-erased fallback path.

Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp`, **extended to the contended-acquire path**. The waiter-embedded design (item 1) and the PMR fallback (item 2) jointly satisfy the discipline; the slot's allocator is bound to the awaiter's resource (Codex C-P2-7 close); the §9 seam **"PMR fallback exercise"** (#10) verifies under `mallocnesia`.

---

## 9. Test seams

Per `[arch §10]` requirement (4) and `[const §VII]`. v1.4 ships **29 seams** (≥ 14 required; ≥ v1.3's 28); v1.4 adds #29 for reaper-cancellation subscriber wakeup and updates latency seams #1/#2 plus result-slot seam #28 to the CAS-then-publish shape. v1.3 shipped 28 seams from v1.2's 24 seams + four new seams from the v1.3 post-cap convergence pass (RC-α / RC-β closes: #25 in-flight acquirer coverage, #26 awaitable cancellation propagation, #27 unlock-vs-reaper splice closure, #28 `*result_` write-race arbitration). v1.1 added three over v1.0: #21 slot-allocator-storage cases, #22 residual-chain cancellation under graceful close, #23 concurrent `cancel_and_drain` is serialised. v1.2 added one over v1.1: #24 drain-latch event signalling + pre-drain holder block (extended in v1.3 to verify the lazy `drain_latch_state` shape per RC-β). Seams referenced by name; ordinals may shift across review rounds.

1. **Uncontended-acquire latency Tier 1.** Google Benchmark on `async_mutex::async_lock` uncontended (single CAS fast path). Verify §6.3 row 1 ceiling **≤ 20–25 ns** warm-cache; CI fails on >5% regression. Lives in `bench/sync/bench_async_mutex_uncontended.cpp`.
2. **Contended-enqueue latency Tier 1.** Google Benchmark on `async_mutex::async_lock` contended. Verify §6.3 row 2 ceiling ≤ 80 ns. Variants for `unlock` uncontended (**≤ 15 ns** per §6.3 row 3) and `unlock` contended drain (≤ 30 ns + ≤ 50 ns/waiter same-strand). Lives in `bench/sync/bench_async_mutex_contended.cpp`.
3. **FIFO fairness across drain cycles** (item 6; RC#1). Spawn N=64 coroutines that all `co_await mutex.async_lock()`; each waits, releases, records acquisition order. Verify within each drain cycle the order is FIFO (LIFO push reversed). Lives in `tests/sync/test_fifo_fairness.cpp`.
4. **Cancellation mid-wait** (item 6; RC#4). For each `cancellation_type` (`total`, `terminal`), park a coroutine in `async_lock`; fire the cancellation slot from a different thread; verify the parked coroutine completes with `expected_t::unexpected{error::sync_lock_aborted}` within ≤ 100 µs and the LIFO list is empty. Variant for `partial` — verify it is treated as `total`. Lives in `tests/sync/test_cancellation_mid_wait.cpp`.
5. **Destructor-with-waiters fires `std::terminate()` (RELEASE-MODE death test)** (item 6; RC#3). Construct an `async_mutex`, hold it via `co_await m.async_lock()` (or via friend access to `detail::try_lock()` in the test fixture per Opus N-P3-1 close), attempt destruction — verify `std::terminate()` fires under release with the documented `abort_invariant` message. Variant: park a waiter without cancelling, attempt destruction — verify the same. Variant: properly drain via `cancel_and_drain()` — verify destruction succeeds. Lives in `tests/sync/test_destructor_release_death.cpp`.
6. **Contention stress (≥ 10⁴ coroutines)** (item 6). Spawn 10⁴ coroutines that each `co_await async_lock()`, perform a 1-ns critical section, release. Verify all complete; counter reads exactly 10⁴; LIFO list empty at end. Lives in `tests/sync/test_contention_stress.cpp`.
7. **TSan clean under stress** (item 6). Build the contention-stress test under `linux-clang-tsan`; verify zero TSan reports on a 10⁴-iteration run. Catches data races on LIFO push / unlock exchange / FIFO drain. Lives in `tests/sync/test_tsan_clean.cpp`.
8. **ASan clean under stress** (item 6). Build the contention-stress test under `linux-clang-asan`; verify zero ASan reports. Variant under UBSan. Lives in `tests/sync/test_asan_clean.cpp`.
9. **HALO firing across compiler matrix** (co-owned 2d per `[arch §11]` row 2). Compile under Linux/Clang, Linux/GCC, Windows/MSVC; exercise an in-session `async_lock`; dump assembly via `llvm-objdump`/equivalent; verify the awaiter's coroutine frame is HALO-elided. Failure is non-fatal **iff** seam #10 passes for the same toolchain (Opus N-P3 close). Lives in `tests/sync/test_halo_firing.cpp`.
10. **PMR fallback exercise** (RC#2). Construct a real `asio::any_completion_handler<expected_t<async_lock_guard>(...)>` wrapping `co_await mutex.async_lock(mr)`; run under `tools/check_alloc.py` + `mallocnesia` interceptor. Verify zero global-heap allocations; verify the awaiter is allocated from the supplied `mr`; verify the cancellation slot's handler closure storage is also from `mr` (Codex C-P2-7 close). Lives in `tests/sync/test_pmr_fallback.cpp`.
11. **Executor-compat: completion runs on awaiter's bound executor** (`[2d §7.4]`). Construct an `async_mutex` and a `session_executor` over a strand; spawn a coroutine bound to the wrapper that does `co_await async_lock()`; the unlocking thread is on a different executor. Verify the awaiter resumes on the wrapper's thread. Variant under both `per_session_strand` and `direct_executor` modes. Lives in `tests/sync/test_executor_compat.cpp`.
12. **`dispatch` vs `post` policy effect on completion** (item 4). Two mutexes — one `dispatch`, one `post`. Park a waiter on each from a session strand; trigger `unlock()` from the same strand. Verify `dispatch` resumes inline (≈ 0 ns above unlock); `post` resumes via one hop (≈ 25 ns extra). CI fails on >5% regression. Lives in `tests/sync/test_dispatch_vs_post.cpp`.
13. **Cross-strand acquire (FIFO-fair drain across strands)**. Two coroutines on different strands; A acquires, B parks, A releases. Verify B resumes on its own strand. Variant: 100 coroutines split across 2 strands; verify FIFO fairness within each drain cycle. Includes `direct_executor` + cross-thread unlock case (RC#4 fix verification). Lives in `tests/sync/test_cross_strand_acquire.cpp`.
14. **`std::mutex`-in-coroutine-context CI gate** (`[const §XV.9]`). Place a deliberately-violating header in `tests/fixtures/header_with_std_mutex_and_awaitable.hpp`; run `tools/check_no_std_mutex_in_awaitable_headers.sh` (post-preprocessing scope per Codex C-P2-10 close); verify the gate fires. Variant: a non-violating header — verify the gate does NOT fire. Variant: a header that pulls in `<asio/awaitable.hpp>` only via a transitive include — verify the post-preprocessing scope catches it. Lives in `tests/sync/test_no_std_mutex_ci_gate.cpp`.
15. **NEW (RC#1): cancel-after-detach-pre-drain race.** Park three waiters on a mutex; release the holder so `unlock()` enters its drain; from a different thread, fire `cancellation_type::total` on the second waiter's slot **between** the unlock's exchange and the per-waiter phase CAS. Verify (a) the second waiter completes with `unexpected{sync_lock_aborted}`; (b) the third waiter receives the residual handoff; (c) no double resume; (d) LIFO list empty at end; (e) ASan/TSan clean. Lives in `tests/sync/test_race_cancel_pre_drain.cpp`.
16. **NEW (RC#1): multi-cancel-same-list race.** Park N=8 waiters; release the holder; fire `total` on every waiter's slot from N different threads simultaneously while the unlocker drains. Verify each waiter completes exactly once (either with the guard or with `unexpected{sync_lock_aborted}`); the union of guarded + cancelled waiters covers all N exactly; ASan/TSan clean. Lives in `tests/sync/test_race_multi_cancel.cpp`.
17. **NEW (RC#1): cancel-during-await_resume race.** Park a waiter; release the holder; the waiter's drain CAS wins; **before** `await_resume` clears the slot, fire a second cancellation signal on the same slot. Verify the second signal is no-op (Opus N-P1 close); the waiter receives the guard; the slot is cleared cleanly on `await_resume`. Lives in `tests/sync/test_race_cancel_during_resume.cpp`.
18. **NEW (RC#1 / RC#5): ARM64 weak-memory contention stress.** Re-run seam #6's contention stress on a Linux-ARM64 host (Graviton or equivalent) under TSan; verify zero TSan reports on a 10⁴-iteration run; verify the counter reads exactly 10⁴ (no lost updates from a memory-ordering bug). Catches a v0.1-style ARM64-only defect that x86 TSO masks. Lives in `tests/sync/test_arm64_weak_memory.cpp`.
19. **NEW (RC#3): `cancel_and_drain()` reaps every in-flight waiter.** Park N=16 waiters across 2 strands; invoke `co_await m.cancel_and_drain()` from the holder's coroutine; verify every waiter completes with `unexpected{sync_lock_aborted}`; `cancel_and_drain()` returns `expected_t<void>{}`; `state_ == not_locked` at end; subsequent `~async_mutex()` succeeds. Variant: idempotent — call `cancel_and_drain()` twice on a drained mutex; verify the second returns immediately. Lives in `tests/sync/test_cancel_and_drain.cpp`.
20. **NEW (N-P1-3): `async_lock_guard` destructive move-assign.** Construct two engaged guards (mutex_a, mutex_b) via friend-access to the engaged-guard ctor (§4.4 — Opus N-P3-1 close moves the public adopt-locked ctor to friend-only; the test fixture uses friend access); move-assign mutex_b's guard into mutex_a's guard; verify mutex_a is unlocked exactly once (by the move-assign); the destination guard now owns mutex_b; the source guard is empty. Variant under TSan + ASan. Lives in `tests/sync/test_guard_destructive_move.cpp`.
21. **NEW v1.1 (RC-C): slot-allocator storage cases.** For each case in §4.3.4: (case 1) embedded path with HALO firing — verify the awaiter's inline 32-byte `slot_storage_` buffer holds the cancellation handler closure; assert zero PMR-resource and zero global-heap allocations under `mallocnesia` interceptor. (case 2) embedded path with HALO not firing — instrument the promise allocator to detect coroutine-frame-on-heap; verify the slot's storage tracks the awaiter; the `mallocnesia` interceptor records the global-heap touch but the test does NOT auto-fail (bench-harness-soft per §6.4). (case 3) PMR fallback — pass an explicit `monotonic_buffer_resource` as `mr` and verify the slot's allocator is bound to `std::pmr::polymorphic_allocator<void>{mr}`; assert zero global-heap allocations. Lives in `tests/sync/test_slot_allocator_storage.cpp`.
22. **NEW v1.1 (RC-A): residual-chain cancellation under graceful close.** Park N=8 waiters on a held mutex; release the holder so `unlock()` enters its drain — the first waiter is granted, the remaining 7 are spliced into `next_drain_head_` (RC-A). Without releasing the granted waiter, fire `cancellation_type::total` on every parked waiter's slot from N different threads. Verify (a) every cancelled waiter completes with `unexpected{sync_lock_aborted}` within ≤ 100 µs (cancellation lands while parked on `next_drain_head_`, NOT lost as in v1.0's incorrect `draining` swallow per Codex C-P1-3); (b) the granted waiter's eventual `unlock()` walks `next_drain_head_`, observes every waiter's `phase_ == cancelled`, skips them all, and CAS's `state_` to `not_locked`; (c) ASan/TSan clean. Closes the v1.0 RC-A defect class. Lives in `tests/sync/test_residual_cancel_graceful.cpp`.
23. **NEW v1.1 (RC-B); UPDATED v1.3 (RC-β); UPDATED v1.4: concurrent `cancel_and_drain` is serialised.** Two coroutines (graceful-close + watchdog) invoke `co_await m.cancel_and_drain()` simultaneously while N=16 waiters are parked. Verify (a) only one caller becomes the reaper (`drain_in_progress_.test_and_set` succeeds for one); (b) the reaper publishes `drain_latch_ptr_` before `draining_ == true`; (c) the non-reaper observes `draining_ == true`, loads a non-null `drain_latch_ptr_`, subscribes to the reaper's `drain_latch_state`, and returns `expected_t<void>{}` after the reaper signals release; (d) every parked waiter is reaped exactly once (no double resume); (e) every subsequent `async_lock(...)` returns `unexpected{sync_lock_drained}`; (f) ASan/TSan clean. Variant: third caller invokes `cancel_and_drain` after both reapers return — verify it observes `draining_ == true` AND `drain_latch_ptr_ == nullptr` and returns immediately (idempotency). Lives in `tests/sync/test_cancel_and_drain_concurrent.cpp`.
24. **NEW v1.2 + EXTENDED v1.3 + UPDATED v1.4 (RC-B round-2 + RC-β post-cap): `cancel_and_drain` blocks pre-drain holder + lazy `drain_latch_state` lifetime/event signalling.** Sub-tests covering both the v1.2 round-2 RC-B close and the v1.3/v1.4 lazy-state shape. **Sub-test (a) — pre-drain holder lifecycle:** acquire the mutex via `co_await m.async_lock()` (holder coroutine A); from a separate coroutine B (with the holder still in its critical section), invoke `co_await m.cancel_and_drain()`. Verify B does NOT return until A's guard destructor calls `unlock()` (B observes `active_holders_count_ > 0` and parks on `latch_state->wait()`); A's `unlock()` observes `draining_ == true`, decrements `active_holders_count_` to zero, atomically loads `drain_latch_ptr_`, calls `latch_state->notify()`; B's parked `wait()` resumes, observes the conditions met, and returns `expected_t<void>{}`. Subsequent `co_await m.async_lock()` returns `unexpected{sync_lock_drained}`. **Sub-test (b) — post-drain fast-path bypass closure:** issue `co_await m.cancel_and_drain()` on a free mutex; verify it completes immediately. Then issue a fresh `co_await m.async_lock()`: verify `await_ready` observes `draining_ == true` BEFORE its CAS, writes `*result_ = unexpected{sync_lock_drained}`, sets `phase_ = cancelled`, and returns true; the coroutine's `await_resume` reads the unexpected and returns it. Verify the mutex's `state_` was NOT mutated by the failed acquire. **Sub-test (c) — sentinel-cast UAF:** invoke `co_await m.cancel_and_drain()` on a fresh, free, never-acquired mutex. Verify the sentinel-discriminated cast observes `raw_state == not_locked` and treats `lifo_head` as nullptr; the walker terminates without dereferencing; ASan/TSan clean. **Sub-test (d) — drain-latch wait/notify protocol:** spawn N=4 non-reaper callers parked on `latch_state->wait()` (subscribed via `drain_latch_ptr_.load(acquire)`) while the reaper holds the latch; verify the reaper's `signal_release()` via `asio::experimental::concurrent_channel::try_send(...)` (or the project-internal subscriber-list fallback per §4.7.3 I-4) wakes all four; all four return `expected_t<void>{}`. **Sub-test (e) — NEW v1.3 / RC-β: lazy state lifetime under reaper-frame destruction.** Spawn N=8 in-flight reaper-resumption handlers (waiters whose CAS to `cancelled` succeeded but whose resumption has not yet fired); use TestFixture::force_destroy_reaper_frame() to destroy the reaper's coroutine frame mid-wait; verify the `drain_latch_state` shared_ptr captured by each pending lambda keeps `in_flight_resumptions_` alive; verify each lambda decrements correctly as it fires; verify ASan reports zero use-after-free. Closes Opus N-R3-P1-2. **Sub-test (f) — NEW v1.3 / RC-β: mutex constructor `constexpr`-ability.** Use `static_assert(noexcept(async_mutex{}))` AND `constexpr async_mutex test_mutex{};` in a constant-expression context; verify compilation succeeds (closes Opus C-R3-P2-1). Lives in `tests/sync/test_drain_latch_holder_lifecycle.cpp`.

25. **NEW v1.3 (RC-α): `cancel_and_drain` covers in-flight acquirer (between `draining_.load()` and fast-path CAS).** Reproduces Opus C-R3-P1-2's specific race window. Two coroutines A (acquirer) and B (drainer) run on different threads; A is paused at a TestFixture::pause_acquirer_after_draining_load() instrumentation hook IMMEDIATELY AFTER `await_ready`'s `draining_.load()` returns false but BEFORE the fast-path CAS. While A is paused, B invokes `co_await m.cancel_and_drain()`; verify (a) B parks on `latch_state->wait()` because `active_acquirers_count_.load(acquire) > 0` (A's awaitable factory incremented this BEFORE its `draining_.load()`); (b) A is released from the pause; A's CAS-success increments `active_holders_count_` to 1 AND decrements `active_acquirers_count_` to 0; (c) A runs its critical section, calls `unlock()`, decrements `active_holders_count_` to 0, notifies the latch; (d) B's parked `wait()` resumes, observes both counters at zero, signals release, returns success; (e) subsequent `async_lock` returns `unexpected{sync_lock_drained}`. Verify the post-drain destructor precondition holds (`state_ == not_locked`) before the test's `~async_mutex()` runs. Closes Opus C-R3-P1-2. Lives in `tests/sync/test_in_flight_acquirer_coverage.cpp`.

26. **NEW v1.3 (RC-β); UPDATED v1.4: `cancel_and_drain` awaitable cancellation propagation.** Verifies the §4.5 cancellation table row + §4.7.3 cancellation-propagation contract. **Sub-test (a) — non-reaper cancellation:** spawn a non-reaper caller subscribed to `latch_state->wait()`; from its parent state, fire `cancellation_type::total`; verify the wait awaitable surfaces `operation_aborted`; the caller returns `expected_t::unexpected{sync_lock_aborted}` (the partial-drain result) — NOT success. The reaper continues unaffected. **Sub-test (b) — reaper cancellation:** spawn a reaper caller; while the reaper is parked on `latch_state->wait()`, fire root cancellation on the reaper; verify the reaper observes the failed `wait_result`, calls `signal_abort()` (not `signal_release()`), and returns `expected_t::unexpected{sync_lock_aborted}`. Verify the in-flight CAS-walks the reaper had started before the cancellation completed normally (their resumption handlers fire on their bound executors and decrement `in_flight_resumptions_` correctly via the shared_ptr capture). Verify the mutex's `draining_` flag stays `true`; subsequent `async_lock` returns `unexpected{sync_lock_drained}`. Verify ASan/TSan clean. **Sub-test (c) — partial-drain destructor precondition:** in the same scenario as (b), attempt to invoke `~async_mutex()` while in-flight resumption handlers are still pending; verify the destructor's `state_ == not_locked` precondition fires `std::terminate` (via `abort_invariant`). Then in a separate scenario, wait for in-flight handlers to complete via TestFixture::wait_for_resumptions_drained(); verify the destructor succeeds. Closes Opus C-R3-P1-4 + Opus N-R3-P3-1. Lives in `tests/sync/test_drain_awaitable_cancellation.cpp`.

27. **NEW v1.3 (RC-α): unlock-vs-reaper splice race closure.** Reproduces Opus C-R3-P1-3's specific race. Park N=8 waiters; release the holder so `unlock()` enters its drain — first waiter is granted, remaining 7 spliced into `next_drain_head_`. Without releasing the granted waiter, from coroutine B fire `co_await m.cancel_and_drain()`. Verify (a) `cancel_and_drain` sets `draining_ = true`; (b) the granted waiter's eventual `unlock()` observes `draining_ == true` and short-circuits — does NOT walk `next_drain_head_`, does NOT splice; (c) the reaper's stable-loop in step (g) re-walks BOTH `state_` AND `next_drain_head_` until both observe nullptr; (d) every waiter is reaped (every `phase_ == cancelled` after step (g)); (e) ASan/TSan clean. Variant: introduce TestFixture::pause_unlock_walker_after_state_exchange() between the unlock's `state_` exchange and any subsequent action; while paused, fire `cancel_and_drain`; release the unlock; verify the reaper's stable loop catches every waiter in either list (the v1.2 protocol that re-walked only `state_` would have missed the splice into `next_drain_head_` here). Closes Opus C-R3-P1-3. Lives in `tests/sync/test_unlock_reaper_splice.cpp`.

28. **NEW v1.3 (RC-α); UPDATED v1.4: `*result_` CAS-then-publish arbitration under unlock-grant + cancellation-handler interleaving.** Reproduces Opus C-R3-P1-1's specific data-race. For N=64 simultaneous unlock-grant + cancellation-handler races on the same awaiter: both paths first CAS `phase_` from `queued` to their terminal state; exactly one wins; only the CAS winner writes `*result_`; the loser observes terminal phase and does not touch `*result_`. Verify under TSan that (a) the per-waiter `phase_` atom serialises the two CAS attempts — exactly one wins; (b) only the WINNER writes `*result_`; (c) the resumed coroutine reads the winner's result after `await_resume` acquire-loads `phase_`; (d) the resumed coroutine never observes a torn value (no half-engaged-guard, no half-unexpected); (e) TSan reports zero data races on `*result_`. Variant: same protocol applied to the reaper-vs-cancellation-handler race in §4.7.2 step (f). Lives in `tests/sync/test_result_write_race.cpp`.

29. **NEW v1.4: reaper cancellation wakes subscribers with `sync_lock_aborted`.** Spawn one reaper and M=4 non-reaper callers subscribed to the same `drain_latch_state::wait()`. Fire `cancellation_type::total` on the reaper while all subscribers are parked. Verify the reaper calls `signal_abort()`, `released_ == false`, `aborted_ == true`, every subscriber wakes and returns `expected_t::unexpected{sync_lock_aborted}`, and no subscriber remains suspended. Verify `drain_latch_ptr_` is cleared only after the abort result is published; ASan/TSan clean. Lives in `tests/sync/test_drain_reaper_abort_subscribers.cpp`.

---

## 10. Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **HALO firing across compiler matrix** (co-owned 2d per `[arch §11]` row 2). Does HALO elide the awaiter's coroutine frame on Linux/Clang, Linux/GCC, Windows/MSVC default toolchains? PMR fallback (§4.3) is the safety net at impl time. | OPEN — verification spike at 2f implementation start. The §9 seam **"HALO firing across compiler matrix"** (#9) is the harness; failure non-fatal **iff** seam #10 passes for the same toolchain. | 2f + 2d co-owned per `[arch §11]` row 2 |
| 2 | **`Session::session_arena()` accessor disposition.** The session-side helper `async_lock_via_session_executor` consumes a non-virtual engine-internal `Session::session_arena() noexcept -> std::pmr::memory_resource*`. | CLOSED in v1.0 at sign-off — Appendix D §D.1 publishes the accessor as engine-internal. | 2f closes via Appendix D §D.1 |
| 3 | **CI enforcement: clang-tidy custom check vs grep gate.** | CLOSED for v1.0 — grep gate with post-preprocessing scope (Codex C-P2-10 close). clang-tidy custom check is post-v1 follow-up. | 2f |
| 4 | **Closes `[2d §10] Q1`** — `async_lock` signature locked at §4.1.1. | CLOSED in v1.0. | 2f closes |
| 5 | **Closes `[2e §10] Q8`** — 2f signature delivered. | CLOSED in v1.0. | 2f closes |

---

## 11. Hand-off

**Docs unblocked by 2f sign-off (downstream):**

- **2e implementation** — the writer-mutex contract on `MessageStore` per `[2e §6.4]` is satisfied; `MemoryStore::store` / `retrieve` / `next_seqnum` / `reset` consume `async_mutex` per §7.1. **2f sign-off is the named hard hand-off gate from `[2e §3.1]` last bullet.**
- **2g** (TLS `cert_source` + pinset rotation) — pinset rotation can use `async_mutex` per `[SYN §3.2 Q6b]` v1.0 use cases (§7.2). 2g's Codex Gate A may proceed once 2f signs off.
- **Phase-4 session-module spec** (not yet drafted) — seqnum counter (§7.3) consumes `async_mutex`; the spec's coroutine FSM design references 2f's signature.
- **Session-side helper `fixpp::session::async_lock_via_session_executor`** — declared by 2f at §4.3.2; lives in `session/` per `[arch §2.3]`'s leaf rule (RC#2 fix); the Phase-4 session-module spec / 2e implementation ships the body.

**Catalogue + coverage-index amendments owed at sign-off** (drop-in language pattern from `[2d §11]` / `[2c App D]` / `[2e App D]`; the orchestrator applies these during the sign-off commit, not the 2f rewrite agent):

- Add **NFR-016** to `library/spec/feature-catalogue.md` (one row, mirroring the NFR-015 row format from `feature-catalogue.md` line 225):

  > **NFR-016** | OFFICIAL | nfr | Awaitable mutex `fixpp::sync::async_mutex` — own implementation (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with not_locked/locked_no_waiters/pointer-to-LIFO encoding + mutex-owned `std::atomic<async_mutex_awaiter*> next_drain_head_` residual FIFO chain per RC-A v1.1, per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` for unlock/cancel CAS arbitration with WINNER-ONLY post-CAS holder accounting per v1.3 RC-α); waiter embedded in the awaiter object inside the caller's coroutine frame with 32-byte inline slot-handler-storage buffer per RC-C v1.1 (zero global-heap on the v1.0 contended path); PMR-aware fallback via the explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`; ASIO `cancellation_type::total` removes the waiter and completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI); per-mutex `dispatch`/`post` completion policy with default `dispatch` and ASIO `running_in_this_thread()` predicate; `std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive (RC-B v1.1, RC-α + RC-β v1.3 post-cap rewrite) with `std::atomic<bool> draining_` + `std::atomic_flag drain_in_progress_` concurrent-call serialiser + `std::atomic<std::uint32_t> active_holders_count_` post-CAS winner-only holder accounting + `std::atomic<std::uint32_t> active_acquirers_count_` in-flight acquirer epoch (covers the window between `await_ready`'s `draining_` load and the fast-path CAS) + lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` with state object allocated via `std::make_shared` inside the reaper's coroutine frame and a `asio::experimental::concurrent_channel`-backed multi-waiter latch surface (NOT a by-value `asio::steady_timer` member — the v1.2 shape was non-implementable per Opus C-R3-P2-1; v1.3 keeps the mutex `constexpr`-default-constructible and executor-free); reaper cancellation propagates per the §4.7.3 contract returning `unexpected{sync_lock_aborted}` on `cancellation_type::total` from the caller's parent state; new error variant `sync_lock_drained` per RC-B; the only legal mutex shape in coroutine context per `[const §XI.3]` (CI-enforced via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate with post-preprocessing scope per `[const §XV.9]`). | all | `[2f §4.1] / [arch §1.1]` | backlog | `.specify/2f-async-mutex.md` v1.3 | — | — | — |

- Add a corresponding entry to `library/spec/coverage-index.md` linking `[2f §4.1]` and `[arch §1.1]` (concurrency primitives promise) to **NFR-016**.

- Update `[arch §11]` row 2 disposition note to reference 2f as one of the two HALO-spike co-owners (no text change to the row itself; "Owner: **2d**, **2f**" is intact).

- **Cross-doc Appendix D drop-ins for 2d** (RC#2 + RC#4 + RC-D closes; orchestrator applies at 2f sign-off):
  - **Appendix D §D.1** — `[2d §4.5]` publishes `Session::session_arena() noexcept -> std::pmr::memory_resource*` as engine-internal accessor (with the `[2d §4.4]` resolution chain reference; engine-internal scope is `fixpp::session/` only).
  - **Appendix D §D.2** — `[2d §4.7]` per-mode effect table's `async_mutex::lock` row is rewritten to surface `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary (mapped to `FIXPP_ERR_CANCELLED` at the C ABI).
  - **Appendix D §D.3** (NEW v1.1 — RC-D close) — `[2d §7.4]`'s locked-contract-surface bullet for `async_mutex::lock` is rewritten to match §D.2's `expected_t::unexpected{sync_lock_aborted}` wording. Closes the v1.0 sibling-doc inconsistency between `[2d §4.7]` (table) and `[2d §7.4]` (surface).

The amendments are **not** applied by 2f itself; per `[2c App D]` / `[2d §11]` / `[2e App D]` precedent the orchestrator (parent session) applies the amendment text during the sign-off commit.

---

## Appendix A — Catalogue row coverage

This doc owns one new catalogue row.

### A.1 Owned

| Row | Family | What 2f covers | Status |
|---|---|---|---|
| **NFR-016** (NEW) | NFR — Awaitable mutex `fixpp::sync::async_mutex` | The class definition, the awaiter type with the per-waiter atomic phase machine (RC#1), the destructive-move RAII guard (RC#1 / N-P1-3), the executor-compat surface satisfying `[2d §7.4]`, the explicit-`mr` PMR fallback path + session-side helper (RC#2), the cancellation contract per `cancellation_type` with §4.5 CAS arbitration (RC#1 + RC#4), the per-mutex `dispatch`/`post` policy with the `running_in_this_thread()` predicate (RC#4), the `std::terminate()` precondition + `cancel_and_drain()` primitive (RC#3), the memory-ordering sub-table + `static_assert`s (RC#5), the test seams covering items (3) (5) (6) of `[SYN §3.2 Q6b]`, and the CI grep gate enforcing `[const §XV.9]`. | Claimed by 2f; row added to `feature-catalogue.md` at sign-off (§11). |

### A.2 Cross-references

(No A-XXX, W-XXX, or D-XXX rows are owned or touched by 2f. Per the per-doc-prefix discipline established by `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]`: 2f's claim is bounded to the awaitable-mutex primitive; the row owners that *consume* it — 2e for S-011..S-014, the Phase-4 session-module spec for the seqnum counter, 2g for pinset rotation — discharge their own rows.)

---

## Appendix B — Normative References

Per `[const §VI.5]`. Sparse; engineering-judgment refs cited inline at point of use.

### B.1 Coverage-index normative references

**None.** 2f's design is engineering judgment, not spec-driven — there is no `[FIX-SL §...]`, `[FIXT §...]`, or `[FIXS §...]` section that bears on the awaitable-mutex algorithm or its surface. This Appendix records that fact per `[const §VI.5]`'s exact-citation rule, mirroring the precedent set by `architecture.md` Appendix B's closing note (line 678) and `[2d Appendix B] §B.2`.

### B.2 Design constraints and sibling contracts (cited inline at point of use)

Design decisions whose primary driver is engineering judgment rather than a specific spec section — **the `async_mutex` algorithm itself, the per-waiter phase machine, the awaiter shape, the destructor `std::terminate()` precondition, the per-mutex completion policy, the LIFO-push + FIFO-drain semantics with one-owner-per-unlock + residual chain, the cancellation behaviour per `asio::cancellation_type` with the §4.5 CAS-arbitration race, the explicit-`mr` PMR fallback, the `[const §XV.9]` CI grep gate** — cite `[const §X.y]` / `[arch §X.y]` / `[SYN §3.x Q#]` / `[2X §X.y]` inline at point of use; they are not spec normatives and are intentionally omitted here.

**No `[FIX-SL §...]`, `[FIXT §...]`, or `[FIXS §...]` reference applies to 2f's design.** The awaitable-mutex primitive is not described in any FIX session-layer, FIXT, or FIXS spec section; it is a project-owned engineering primitive driven by `[const §XI.3]`'s mandate, `[const §XV.9]`'s ban, `[SYN §3.2 Q6b]`'s six-item design list, and `[2d §7.4]`'s locked executor-compat contract surface.

---

## Appendix C — Convergence log

> v0.1 → reset 1/2 (2026-05-08). Closing recommendation from `opus_2f_async-mutex_adversarial_review.md` was **"needs full rewrite / structural rethink"** — five intersecting structural defects (atomic algorithm + cancellation arbitration jointly under-specified; PMR fallback layering violation + invented `Session::session_arena()` accessor + non-existent type-erasure detection; release-UB destructor; cancellation-result/dispatch-policy contract divergence from `[2d §7.4]`; toolchain compliance gap on memory ordering / MSVC ABI / ARM64). v0.1 archived as `2f-async-mutex.draft-r1.md`. v1.0 re-lays the body against the round-1 review's recommended single-fix shapes (RC#1–RC#5):
>
> - RC#1 — per-waiter `std::atomic<waiter_phase>` machine + one-owner-per-unlock with residual chain handoff (§4.2 / §4.5 / §6.2.2 + four new race seams #15–#18).
> - RC#2 — explicit `async_lock(std::pmr::memory_resource* mr = nullptr)` overload + session-side helper `async_lock_via_session_executor` in `session/` (§4.1 / §4.3 + Appendix D §D.1 publishes `Session::session_arena()` as engine-internal accessor; `bind_allocator(slot_allocator(mr))` closes Codex C-P2-7 / Opus N-P1).
> - RC#3 — `std::terminate()` precondition (both debug AND release) + `awaitable<expected_t<void>> cancel_and_drain()` member (§4.7 + new seam #19; release-mode death test #5).
> - RC#4 — `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI (Appendix D §D.2 rewords `[2d §4.7]`'s row); inline-vs-post predicate is ASIO `dispatch` semantics on the bound executor with `direct_executor` + non-queryable executor falling to always-`post` (§4.6).
> - RC#5 — §6.2.2 memory-ordering sub-table; §4.1 `static_assert`s (alignment, pointer round-trip, lock-freedom); §9 seam #18 (ARM64 weak-memory contention stress).
>
> Round-by-round Codex / Opus tallies for v1.0+ will land below as Gate A round 1 (post-reset) executes.

### v1.4 → v1.5

1. **Fix 1 (P1):** reconciled `drain_latch_weak_` → `drain_latch_ptr_` across 7 declaration sites (§4.1 member block, §4.1 docstring, §1.1 size budget, §1.2 scope-boundary bullet, §3.1 inherited-primitives row, §4.7.2 Note paragraph, §11 NFR-016 row).
2. **Fix 2 (P2):** added I-8 invariant (non-terminal wake contract) to §4.7.3; added `notify()` row to §6.2.2 memory-ordering table.
3. **Fix 3 (P3):** updated stale `≤ 10–20 ns` claims in §3 (two sites) to `≤ 20–25 ns` matching §6.3 row 1.

### v1.3 → v1.4 (focused line-edit pass; 2026-05-08)

**Requirements addressed:**

1. **Result-slot publication is atomic with the phase transition.** Applied the required **CAS-then-publish** shape: unlocker, reaper, and cancellation handler first win `phase_` (`queued → granted` or `queued → cancelled`) with `memory_order_acq_rel`; only the CAS winner writes `*result_`; CAS losers do not touch `*result_`. Sections touched: §4.2, §4.5.2, §4.7.2, §6.2.1, §6.2.2, §9 seam #28.
2. **Lazy drain-latch publication ordering is deterministic for concurrent callers.** Replaced the weak-latch epoch wording in the drain flow with a non-expiring `drain_latch_ptr_` (`std::atomic<std::shared_ptr<drain_latch_state>>` or equivalent): first reaper stores the shared_ptr with release semantics, then publishes `draining_ == true`; second callers acquire-load `draining_` and then acquire-load a non-null pointer during the epoch. Sections touched: §4.7.2, §4.7.3, §6.2.2.
3. **Subscribers wake on reaper cancellation.** Added the two-terminal-signal latch contract: `signal_release()` for normal drain completion, `signal_abort()` for reaper cancellation. Cancelled reaper calls `signal_abort()` before returning `unexpected{sync_lock_aborted}`; subscribers observe `released_ == false && aborted_ == true` and return the same error. Sections touched: §4.5 cancellation table, §4.7.2, §4.7.3, §9 seams #26 and #29.
4. **Latency seams and dangling drop-in claim cleaned up.** Swept §9 seams #1/#2 to cite §6.3 ceilings exactly (`async_lock` uncontended ≤ 20–25 ns; `unlock` uncontended ≤ 15 ns). Removed the dangling `[2e §6.6]` cross-doc drop-in claim from §6.3; no new Appendix D §D.4 is declared. Sections touched: §6.3, §9.

**Net-effect summary:**

- **Test-seam delta:** v1.3 = 28 seams → v1.4 = **29 seams** (+#29 reaper cancellation wakes subscribers with `sync_lock_aborted`; #26 and #28 updated).
- **Error-variant delta:** v1.3 = 4 → v1.4 = **4** (no new variant; reaper/subscriber abort uses existing `sync_lock_aborted`).
- **Appendix D drop-in delta:** v1.3 = 3 → v1.4 = **3**; path (b) chosen for Requirement 4, so no §D.4 is declared.

### v1.2 → v1.3 (Gate A round 3 post-reset, user-authorized post-cap pass; 2026-05-08)

**Inputs:**
- Codex review `research/reviews/codex_2f_3_async-mutex_review.md` — 4 P1 / 1 P2 / 1 P3.
- Opus adversarial review `research/reviews/opus_2f_3_async-mutex_adversarial_review.md` — combined post-judging **4 P1 / 2 P2 / 2 P3**, 2 structural root causes (RC-α — drain-walker ↔ holder-counter ↔ result-publication coordination, encompassing C-R3-P1-1 + C-R3-P1-2 + C-R3-P1-3 + N-R3-P2-1 + N-R3-P2-2; RC-β — `drain_latch_` cancellation/executor/constructor contracts, encompassing C-R3-P1-4 + C-R3-P2-1 + N-R3-P1-1 + N-R3-P1-2 + N-R3-P3-1) + 1 editorial cluster (RC-γ — handoff/Appendix-D drift, encompassing C-R3-P3-1 + N-R3-P3-2). Closing recommendation: **"round cap hit; needs user-authorized post-cap pass."** v1.2's correctly-shaped fixes for round-2 RC-A/RC-B each opened new defects at adjacent boundaries (write-race on `*result_`; in-flight acquirer not covered by holder count; unlock-vs-reaper splice race; `steady_timer` in `constexpr` mutex non-implementable; `in_flight` UAF on reaper cancellation). The fix shape is bigger than 2c v1.3 / 2d v0.4 / 2e v0.4 round-3 line-edits (publication-primitive re-pick; acquirer-quiescence counter; re-routed unlock-walker-vs-reaper splice protocol; lazy-shared_ptr drain-latch shape; cancellation-shield-vs-propagation pick) but smaller than a full structural rewrite — the v1.0-spine (three-state phase enum, mutex-owned `next_drain_head_`, slot-allocator three-case table, two-phase-close cancellation contract, Appendix D drop-ins) is intact and signs off cleanly. **This is the user-authorized post-cap pass per the round-cap convention** — the `[2c v1.3]` / `[2d v0.4]` / `[2e v0.4]` precedent established the convention that the user explicitly authorizes a single post-cap convergence pass when the round-3 trajectory shows residuals that are mechanical rather than line-edit class. Reset budget remains 1/2.

**Root causes called out (per Opus, source of truth):**

- **RC-α — Drain-walker ↔ holder-counter ↔ result-publication coordination is internally inconsistent under the v1.2 algorithm.** Each of the v1.2 fixes was individually correct in shape but the three were not co-designed; each closed its round-2 finding while opening a new one at an adjacent boundary. **(α-1) Write-race on `*result_`.** v1.2 reordered the unlock walker and the reaper to write `*winner->result_` BEFORE the CAS to terminal phase (closes Codex N-P2-1's release-ordering inversion); but a concurrent cancellation handler running on the same awaiter ALSO writes `*head->result_` BEFORE its CAS to `cancelled`. Both writes target the same non-atomic slot before either CAS arbitrates the winner — textbook C++ data race. **(α-2) In-flight acquirer not covered by holder count.** v1.2's `active_holders_count_` is incremented at the fast-path CAS-success, AFTER the CAS. The window between `await_ready`'s `draining_.load()` and `state_.compare_exchange(...)` admits an acquirer that is neither in the holder count nor in the LIFO; `cancel_and_drain` waiting on `active_holders_count_ == 0` could observe zero and complete while a fresh acquirer was mid-fast-path-CAS, then the destructor could fire on a still-acquirable mutex. **(α-3) Unlock-vs-reaper splice race.** v1.2's `unlock()` drain-aware short-circuit walked `next_drain_head_` and `state_` "for publication purposes" but skipped the grant-CAS; meanwhile, the reaper's late-walker loop in step (f) re-walked ONLY `state_`, not `next_drain_head_`. If a concurrent `unlock()` observed `draining_ == false` BEFORE step (a)'s exchange, walked `next_drain_head_`, found a winner, granted ownership, and was mid-splice of the untouched tail back into `next_drain_head_` while the reaper was running steps (d)/(e)/(f) — the splice landed AFTER the reaper's `next_drain_head_` exchange and was NOT picked up by step (f). **Single fix:** v1.3 / RC-α does NOT re-pick the publication primitive (Codex's "executor-publication route" counter-proposal would have required a §4.5.2 / §4.7.2 mechanical re-design under user supervision; the user-prescribed shape preserves the v1.2 release-CAS publication primitive and instead ARBITRATES the writers via the per-waiter `phase_` atom): both writers (unlock-walker and cancel-handler) write `*result_` BEFORE their respective CAS attempts; the WINNER's release-CAS publishes its prior `*result_` write; the LOSER's CAS observes terminal phase and aborts BEFORE its release-CAS, so the LOSER's write is overwritten without ever publishing. (α-1 close.) `active_holders_count_` is incremented WINNER-ONLY at the CAS-success (post-CAS); a NEW `active_acquirers_count_` epoch counter is incremented BEFORE `await_ready`'s `draining_.load()` and decremented at one of three exit points (fast-path success, drained-bypass, await_suspend LIFO-enrol); `cancel_and_drain` waits for both counters at zero. (α-2 close.) The unlock-walker's drain-aware short-circuit is rewritten to NOT walk `next_drain_head_` or splice anything under `draining_ == true`; the reaper's late-walker loop is rewritten to re-walk BOTH `state_` AND `next_drain_head_` in a stable loop until both observe nullptr in a single iteration. (α-3 close.) §6.2.2 grows 11 new rows (4 for `draining_` additional pairing partners; 3 for `active_holders_count_`; 3 for `active_acquirers_count_`; 1 for `drain_latch_weak_` shared_ptr update — closes Opus N-R3-P2-2). §6.3 row 1 ceiling raised from ≤ 10–20 ns to ≤ 20–25 ns (closes Opus N-R3-P2-1 — the v1.2 atomic RMW count was undercounted); §6.3 row 3 (unlock uncontended) raised from ≤ 10 ns to ≤ 15 ns. Total fix scope ≈ 80 lines across §4.2.1 / §4.2.2 / §4.5.2 / §4.7.2 / §6.2.2 / §6.3.

- **RC-β — `drain_latch_` cancellation, executor, and constructor contracts are jointly under-specified and partially non-implementable under v1.2.** v1.2's by-value `detail::drain_latch drain_latch_` member owned an `asio::steady_timer`, but `asio::steady_timer` requires an executor at construction and `async_mutex()` is `constexpr`-default-constructible per `[arch §5.5]` — non-implementable. The reaper's `in_flight` was a local stack atomic captured by reference in resumption-handler lambdas; reaper-cancellation-mid-loop could destroy the reaper's frame, leaving the captured reference dangling — UAF. v1.2 cited `[2d §6.5]`'s `cancellable_dispatch` precedent for the latch shape, but `cancellable_dispatch` is `asio::async_initiate` over a posted dispatch node, not a `steady_timer`-poll-loop wrapping a bool atomic — cited precedent does not match. v1.2 was also silent on what happens if the `cancel_and_drain` awaitable is itself cancelled mid-drain. **Single fix (per the user-prescribed shape — keep the mutex executorless and constexpr-default-constructible; pick a real ASIO multi-waiter primitive for the latch surface; choose cancellation-PROPAGATION over cancellation-SHIELDING):** v1.3 / RC-β replaces the v1.2 by-value `detail::drain_latch` member with a `std::weak_ptr<detail::drain_latch_state>` member (default-initialised empty; the mutex stays `constexpr`-default-constructible and executor-free). The `drain_latch_state` is allocated lazily via `std::make_shared<>` inside the reaper's coroutine frame; the reaper atomic-stores the `weak_ptr` into `drain_latch_weak_`; concurrent waiters subscribe via `drain_latch_weak_.load(acquire).lock()`. The state object owns `released_`, `in_flight_resumptions_` (moved off the reaper's stack — closes Opus N-R3-P1-2 UAF), and a `asio::experimental::concurrent_channel<void()>`-backed multi-waiter latch surface (a real ASIO primitive — closes Opus N-R3-P1-1's "cited precedent does not match" finding). The state object outlives the reaper's coroutine frame via the `shared_ptr` captures in resumption-handler lambdas. The reaper does NOT cancellation-shield: if its parent state fires `total`, the reaper observes the failed `wait_result`, does NOT call `signal_release()`, and returns `expected_t::unexpected{sync_lock_aborted}`; in-flight resumption handlers complete normally; subsequent `cancel_and_drain` callers either subscribe to the still-live state (if any handlers are pending) or observe expired weak_ptr and return success. New §4.7.3 spells the lifetime invariants (I-1 through I-6); §1.1 mutex-size budget recomputed (≈ 56 B with `weak_ptr`, vs v1.2's incorrect ≈ 56 B with `steady_timer`); §3.1 row updated to cite `asio::experimental::concurrent_channel` directly (or the project-internal subscriber-list fallback per I-4). Total fix scope ≈ 60 lines across §1.1 / §3.1 / §4.1 / §4.7.2 / new §4.7.3 + §4.5 cancellation-table row.

- **RC-γ — Handoff/Appendix-D drift behind v1.2 (editorial).** §11 NFR-016 row labelled the source as `.specify/2f-async-mutex.md v1.1` and described the lifecycle as `draining_` + `drain_in_progress_`, omitting v1.2's `active_holders_count_` / `drain_complete_` / `detail::drain_latch`. Appendix D §D.2 / §D.3 used `async_mutex::lock` in the rewritten body while 2f's published surface is `async_mutex::async_lock(...)`. **Single fix:** §11 NFR-016 row rewritten to v1.3 wording with the full v1.3 lifecycle members named (`active_holders_count_`, `active_acquirers_count_`, `drain_latch_weak_`, lazy `drain_latch_state`, `concurrent_channel`-backed latch, cancellation-propagation contract) and source labelled `v1.3`. Appendix D §D.2 / §D.3 "with this:" bodies annotate the historical→2f mapping inline (`async_mutex::lock` (historical row name; corresponds to `async_mutex::async_lock(...)` per `[2f §4.1.1]` v1.3)) preserving citation lineage while making the surface mapping clear. Total fix scope ≈ 10 lines, three sites.

**Per-finding resolution table (mirrors `[2c App C]` / `[2d App C]` / `[2e App C]` post-cap-pass style):**

| Source / ID | Severity | Verdict (Opus judge) | Resolution |
|---|---|---|---|
| Codex C-R3-P1-1 — Pre-CAS `result_` writes race; pre-CAS holder-count leaks on CAS loss | P1 | **Confirm @ P1; cluster into RC-α** | §4.5.2 v1.3 — both unlock-walker AND cancel-handler write `*result_` BEFORE their CAS; the per-waiter `phase_` atom arbitrates which write becomes "the published value" via release-acquire on `phase_` (the CAS WINNER's write is published; the LOSER's CAS aborts BEFORE its release-CAS so its write is overwritten without publishing). `active_holders_count_` increment is moved to WINNER-ONLY post-CAS (closes the v1.2 phantom-count leak on CAS-loss). New seam #28 verifies the write-race arbitration under TSan. §6.2.2 grows the `active_holders_count_` increment/decrement/load rows. Closes via release-acquire arbitration without introducing a "claimed" intermediate phase or executor-publication re-pick. |
| Codex C-R3-P1-2 — `cancel_and_drain` does not close `await_ready` fast-path race (acquirer-quiescence) | P1 | **Confirm @ P1; cluster into RC-α** | §4.1 v1.3 declares NEW `std::atomic<std::uint32_t> active_acquirers_count_` member; `async_lock(...)` factory increments BEFORE `await_ready`'s `draining_.load()`; decrement at one of three exit points (fast-path success / drained-bypass / await_suspend LIFO-enrol) per §4.2.1 / §4.2.2. `cancel_and_drain` waits for `active_acquirers_count_ == 0` AND `active_holders_count_ == 0` before publishing the release-edge. New seam #25 verifies the in-flight acquirer window. §6.2.2 grows three rows for the new counter. The acquirer-counter is the cheaper "epoch counter" Codex's counter-proposal named (the alternative — documented quiescence precondition — was rejected because it shifts liveness onto the consumer; v1.3 picks the strict counter shape). |
| Codex C-R3-P1-3 — `cancel_and_drain` can miss residual splice from concurrent `unlock()` | P1 | **Confirm @ P1; cluster into RC-α** | §4.5.2 v1.3 — `unlock()` drain-aware short-circuit no longer walks `next_drain_head_` or splices anything under `draining_ == true`; instead, it loads `drain_latch_weak_`, calls `notify()` if alive, and returns. §4.7.2 v1.3 reaper's late-walker loop re-walks BOTH `state_` AND `next_drain_head_` in a single stable loop until both observe nullptr in one iteration. Under `draining_ == true`, no further splices into `next_drain_head_` are observable (the unlock-side splice path is short-circuited), so the reaper's stable loop is guaranteed to terminate. New seam #27 verifies the splice-race closure. |
| Codex C-R3-P1-4 — `drain_latch_` unsafe cancellation semantics; unowned executor/timer shape | P1 | **Confirm @ P1; cluster into RC-β** | §4.1 v1.3 replaces v1.2's by-value `detail::drain_latch drain_latch_` member with `std::weak_ptr<detail::drain_latch_state> drain_latch_weak_` (default-initialised empty; the mutex stays `constexpr`-default-constructible). §4.7.2 v1.3 allocates `drain_latch_state` lazily via `std::make_shared<>` inside the reaper's coroutine frame. NEW §4.7.3 names the cancellation-PROPAGATION contract (NOT shielding): if the reaper's parent state fires `total`, the reaper returns `unexpected{sync_lock_aborted}`; in-flight resumption handlers complete via shared_ptr captures; mutex's `draining_` stays true. §4.5 cancellation table grows a row for "cancel_and_drain awaitable cancelled mid-drain". New seam #26 verifies awaitable cancellation propagation; new seam #24 sub-tests (e)/(f) verify lazy-state lifetime and constexpr ctor. |
| Codex C-R3-P2-1 — §4.1 class layout is not order-valid; mutex size budget is not recomputed | P2 | **Confirm @ P2; cluster into RC-β** | §4.1 v1.3 — by-value `detail::drain_latch drain_latch_` removed; replaced with `std::weak_ptr<detail::drain_latch_state> drain_latch_weak_` (default-initialised empty). The forward-declaration `class drain_latch_state` (NOT `class drain_latch` — v1.2's name is retired) is a `weak_ptr` template parameter, which only requires the type to be declared, not complete. §1.1 v1.3 mutex-size budget recomputed against the `weak_ptr` shape (≈ 52 B raw / 56 B padded with `alignas(8)` — fits one cache line on x86_64 with 8 B headroom). The `constexpr async_mutex() noexcept = default` constructor is preserved (closes the v1.2 `steady_timer`-in-`constexpr`-ctor non-implementable issue). |
| Codex C-R3-P3-1 — Handoff text and Appendix D drift behind v1.2 | P3 | **Confirm @ P3; cluster into RC-γ** | §11 NFR-016 row v1.3 — source label updated to `.specify/2f-async-mutex.md v1.3`; row body updated to name the v1.3 lifecycle members (`active_holders_count_`, `active_acquirers_count_`, `drain_latch_weak_`, lazy `drain_latch_state`, `concurrent_channel`-backed latch, cancellation-propagation contract). Appendix D §D.2 / §D.3 "with this:" bodies annotate the historical `async_mutex::lock` → 2f's `async_mutex::async_lock(...)` mapping inline. |
| Opus C-R3-P1-1 — judgment of Codex C-R3-P1-1 | P1 | NEW judgment row (clusters with Codex C-R3-P1-1) | Same RC-α fix as Codex C-R3-P1-1: §4.5.2 v1.3 release-acquire write-race arbitration on the per-waiter `phase_` atom; winner-only `active_holders_count_` increment. |
| Opus C-R3-P1-2 — judgment of Codex C-R3-P1-2 | P1 | NEW judgment row (clusters with Codex C-R3-P1-2) | Same RC-α fix as Codex C-R3-P1-2: §4.1 v1.3 `active_acquirers_count_` epoch counter; §4.2.1 / §4.2.2 increment/decrement points; §4.7.2 wait-loop covers both counters. |
| Opus C-R3-P1-3 — judgment of Codex C-R3-P1-3 | P1 | NEW judgment row (clusters with Codex C-R3-P1-3) | Same RC-α fix as Codex C-R3-P1-3: §4.5.2 v1.3 unlock-side short-circuit no-splice + §4.7.2 v1.3 reaper stable loop over both lists. |
| Opus C-R3-P1-4 — judgment of Codex C-R3-P1-4 | P1 | NEW judgment row (clusters with Codex C-R3-P1-4) | Same RC-β fix as Codex C-R3-P1-4: lazy `drain_latch_state`; cancellation-propagation contract; new §4.7.3. |
| Opus C-R3-P2-1 — judgment of Codex C-R3-P2-1 | P2 | NEW judgment row (clusters with Codex C-R3-P2-1) | Same RC-β fix as Codex C-R3-P2-1: §4.1 + §1.1 v1.3 size recomputation; constexpr ctor preserved. |
| Opus C-R3-P3-1 — judgment of Codex C-R3-P3-1 | P3 | NEW judgment row (clusters with Codex C-R3-P3-1) | Same RC-γ fix as Codex C-R3-P3-1: §11 + Appendix D updates. |
| Opus N-R3-P1-1 — `[const §VI.5]` exact-citation rule on `detail::drain_latch` cited precedent | P1 | NEW (Opus) | §3.1 v1.3 row updated — cites `asio::experimental::concurrent_channel<void()>` directly (a real ASIO primitive that publishes `wait()` semantics with cancellation-slot integration). The v1.2 incorrect citation of `[2d §6.5]`'s `cancellable_dispatch`-over-`steady_timer`-poll-loop pattern is removed. New §4.7.3 I-4 documents the project-internal subscriber-list fallback if `concurrent_channel` is unavailable on a target toolchain. |
| Opus N-R3-P1-2 — `cancel_and_drain` reaper holds `in_flight` by reference across `co_await` | P1 | NEW (Opus) | §4.7.2 v1.3 — `in_flight_resumptions_` moved off the reaper's stack onto `drain_latch_state` (heap-allocated via shared_ptr); resumption-handler lambdas capture `latch_state` by shared_ptr (not by reference to a stack atomic); the state object survives the reaper's coroutine frame destruction. New seam #24 sub-test (e) verifies under ASan that no resumption-handler lambda dereferences a freed reaper-frame stack atomic. |
| Opus N-R3-P2-1 — Tier 1 ceiling §6.3 row 1 / row 3 do not include v1.2 acquirer-counter cost | P2 | NEW (Opus) | §6.3 v1.3 — row 1 (`async_lock` uncontended) ceiling raised from ≤ 10–20 ns to ≤ 20–25 ns to accommodate the new `active_acquirers_count_` increment + `active_holders_count_` increment + `active_acquirers_count_` decrement (3 acq_rel RMWs ≈ 15 ns added). Row 3 (`unlock` uncontended) ceiling raised from ≤ 10 ns to ≤ 15 ns to accommodate the `active_holders_count_` decrement. The `[2e §6.6]` `MemoryStore::store` 200 ns envelope still fits with ≥ 175 ns headroom. |
| Opus N-R3-P2-2 — §6.2.2 memory-ordering table missing rows for `active_holders_count_` / `drain_latch_state` | P2 | NEW (Opus) | §6.2.2 v1.3 grows 11 new rows: 1 for `draining_.load(in await_ready)`; 1 for `draining_.load(in unlock)`; 3 for `active_holders_count_` (increment/decrement/load); 3 for `active_acquirers_count_` (increment/decrement/load); 1 for `drain_latch_weak_` store; 1 for `drain_latch_weak_` load; 1 for `drain_latch_state::released_` store/load pair; 1 for `drain_latch_state::in_flight_resumptions_` increment/decrement/load. Each row names the pairing partner and the rationale per the existing table style. |
| Opus N-R3-P3-1 — §9 seam list missing `cancel_and_drain` cancellation-mid-await test | P3 | NEW (Opus) | §9 v1.3 grows new seam #26 ("`cancel_and_drain` awaitable cancellation propagation") with three sub-tests covering non-reaper cancellation, reaper cancellation, and partial-drain destructor precondition. New seam #24 sub-test (e) covers reaper-cancellation-mid-loop UAF risk per Opus N-R3-P1-2. |
| Opus N-R3-P3-2 — Appendix D §D.2 / §D.3 use `async_mutex::lock` instead of `async_lock(...)` | P3 | NEW (Opus); clusters with Codex C-R3-P3-1 into RC-γ | Same RC-γ fix as Codex C-R3-P3-1: Appendix D §D.2 / §D.3 "with this:" bodies annotate the historical `async_mutex::lock` → 2f's `async_mutex::async_lock(...)` mapping inline. The "Replace this:" body is preserved byte-faithful for diff-application correctness; the rewritten row text annotates the mapping in-line. |

**Codex findings disagreed with — none.** All 6 Codex findings (4 P1 / 1 P2 / 1 P3) were judged "Confirm" by Opus and applied verbatim or via the RC clustering. No disagreement to record.

**Net-effect summary:**

- **Test-seam delta:** v1.2 = 24 seams → v1.3 = **28 seams** (+4 new: #25 in-flight acquirer coverage; #26 awaitable cancellation propagation; #27 unlock-vs-reaper splice closure; #28 `*result_` write-race arbitration). Existing seam #24 extended in v1.3 to verify the lazy `drain_latch_state` shape (sub-tests (e) and (f)). Net seam count ≥ 26 as required by the post-cap pass brief.
- **Error-variant delta:** v1.2 = 4 (`sync_lock_aborted`, `sync_lock_alloc_failed`, `sync_lock_outside_session`, `sync_lock_drained`) → v1.3 = **4** (no change). Round-3 fixes do NOT introduce new variants. (The cancellation-propagation contract surfaces `sync_lock_aborted` — an existing variant — for the partial-drain case.)
- **Appendix D drop-in delta:** v1.2 = 3 (§D.1, §D.2, §D.3) → v1.3 = **3** (no change). RC-γ updates the "with this:" body of §D.2 / §D.3 to annotate the historical row name → 2f API mapping; no new drop-in.
- **Awaiter layout delta:** v1.2 = ≤ 96 B → v1.3 = **≤ 96 B** (no change). v1.3's RC-α / RC-β additions (`active_acquirers_count_`, `drain_latch_weak_`) live on the **mutex**, not the awaiter; the awaiter byte-budget is unchanged.
- **Mutex layout delta:** v1.2 fields (`state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `active_holders_count_`, `drain_complete_`, `drain_latch_` by-value, `policy_`) → v1.3 fields (`state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `active_holders_count_`, `active_acquirers_count_` NEW, `drain_latch_weak_` REPLACES `drain_complete_` + `drain_latch_`, `policy_`). Mutex object size: v1.2 ≈ 56 B (incorrectly estimated against non-implementable `steady_timer` member); v1.3 ≈ **52 B raw / 56 B padded with `alignas(8)`** (fits one cache line on x86_64 with 8 B headroom). The `drain_complete_` flag is folded into `drain_latch_state::released_` (no longer a mutex member).
- **Phase enum delta:** v1.2 three-state `{ queued, granted, cancelled }` → v1.3 **three-state, unchanged**. RC-α fix is a §4.5.2 / §4.7.2 algorithm rewrite (publication-arbitration via release-acquire on `phase_`; winner-only counter increments; stable-loop reaper over both lists), NOT a phase-enum change.
- **Constructor delta:** v1.2 `constexpr async_mutex() noexcept = default` was non-implementable due to by-value `steady_timer` in `detail::drain_latch`; v1.3 `constexpr async_mutex() noexcept = default` is **implementable** (the `weak_ptr<drain_latch_state>` member is default-constructible empty; no executor dependency). Verified via §9 seam #24 sub-test (f) (`constexpr async_mutex test_mutex{}` in constant-expression context).
- **Section rewrites enumerated:** §1.1 (mutex-size budget recomputed against `weak_ptr` shape); §1.2 (mutex member set updated for v1.3 — `active_acquirers_count_` added; `drain_complete_` folded into `drain_latch_state`; `drain_latch_` replaced with `drain_latch_weak_`); §3.1 (drain_latch row rewritten — cites `asio::experimental::concurrent_channel` and lazy `shared_ptr` shape); §4.1 (mutex docstring updated; member declarations updated for v1.3; cancel_and_drain member docstring rewritten); §4.2.1 (acquirer-epoch counter increment + winner-only post-CAS holder counter); §4.2.2 (decrement points for `active_acquirers_count_`); §4.5 (cancellation table grows row for awaitable cancelled mid-drain); §4.5.2 (full rewrite — release-acquire write-race arbitration; winner-only counter; drain-aware short-circuit no-splice); §4.7.2 (full rewrite — lazy `drain_latch_state`; cancellation-propagation contract; stable-loop reaper over both lists; in_flight_resumptions_ on state object); §4.7.3 NEW (drain-latch ownership and cancellation-propagation contract; six invariants); §6.2.2 (11 new memory-ordering rows); §6.3 (row 1 ceiling raised to ≤ 20–25 ns; row 3 to ≤ 15 ns; cancel_and_drain row updated with v1.3 mechanism); §9 (4 new seams + #24 extended); §11 (NFR-016 row updated to v1.3); Appendix D §D.2 / §D.3 (`async_mutex::lock` → `async_mutex::async_lock(...)` mapping annotated inline); Appendix C (this entry, above the v1.1 → v1.2 entry).

---

### v1.1 → v1.2 (Gate A round 2 post-reset, convergence pass; 2026-05-08)

**Inputs:**
- Codex review `research/reviews/codex_2f_2_async-mutex_review.md` — 2 P1 / 2 P2 / 1 P3.
- Opus adversarial review `research/reviews/opus_2f_2_async-mutex_adversarial_review.md` — combined post-judging **3 P1 / 4 P2 / 3 P3**, 2 root causes (RC-A residual: §4.5.2 drain-walk contradiction under the three-state phase enum; RC-B residual: `cancel_and_drain` lifecycle gaps — post-drain fast-path bypass, pre-drain holder, undeclared `drain_complete_` member, invented ASIO awaitable API + sentinel-cast UAF) + 1 editorial root cause (RC-C residual: stale 64-B note in §8 + `result_` validity-window prose missing). Closing recommendation: **"v1.2 can ship after a single convergence pass."**

**Root causes called out (per Opus, source of truth):**

- **RC-A (round-2 residual) — §4.5.2 drain-walk contradiction under three-state phase enum.** v1.1's §4.5.2 prose said "for each waiter, CAS phase_ from queued → granted" while *also* saying "Waiters whose CAS succeeds AND are not the first-winner: stay queued." Mechanically impossible: a successful `compare_exchange_strong(queued → granted)` physically writes `granted`. Either the prose is contradictory (Opus' reading 1 — no waiters can both CAS-succeed and stay queued) or the algorithm CASes every queued waiter to terminal `granted` and recreates the v1.0 multi-grant defect (Opus' reading 2 — every residual waiter is in terminal `granted` phase, none ever receive `result_` writes from the next unlock). This cross-cut Codex C-N-P2-1's `result_` ordering inversion (release publishes writes that *follow* — no, release publishes writes that *precede*); the §6.2.2 row's "release for `result_` writes that follow" was unsound and the v1.1 algorithm wrote `result_` *after* the CAS. **Single fix:** §4.5.2 v1.2 rewritten as a true one-winner walk — skip `cancelled` heads, find the FIRST `queued` waiter, write `*winner->result_` BEFORE the CAS, CAS only that waiter `queued → granted`, splice the *untouched tail* (every waiter past `winner->next_` whose phase is still `queued`) into `next_drain_head_`. The "Waiters whose CAS succeeds AND are not the first-winner" bullet is **deleted** — no such waiters exist. §6.2.2 ordering rows for the drain CAS, cancel CAS, and `result_` slot publication are corrected to claim "release publishes writes that PRECEDE the CAS." §6.2.1 grows an explicit ordering specification for the `acq_rel` grant-CAS. §4.5.1 window 4 grows one sentence on the cancel-and-drain-vs-residual-cancellation interaction (Opus N-P3-2 ride-along).

- **RC-B (round-2 residual) — `cancel_and_drain` lifecycle / API gaps (escalated, four sub-defects).** Three structural gaps + one editorial: (1) **Post-drain fast-path bypass** — v1.1's `await_ready` ran the acquire CAS without checking `draining_`; a fresh `co_await m.async_lock()` after `cancel_and_drain` finished could acquire and the destructor was no longer safe. (2) **Pre-drain holder lifecycle** — `cancel_and_drain` did not block on an existing pre-drain holder; the holder's eventual `unlock()` could race against `cancel_and_drain`'s state mutation. (3) **Sentinel-cast UAF** — v1.1's `state_.exchange(...)` cast to `async_mutex_awaiter*` without sentinel discrimination; on a free mutex (`state_ == not_locked = 1`) the cast produced an invalid pointer and the walker dereferenced `0x1->next_`. (4) **Invented ASIO API** — v1.1 used `asio::async_wait_for_drain_complete[_count](...)`, which is not a published ASIO primitive; no project-internal alternative was declared, and the `drain_complete_` member that backed the API was missing from §4.1's class layout. **Single fix:** §4.2.1 `await_ready` checks `draining_.load(acquire)` BEFORE the fast-path CAS; on `draining_ == true`, fast-fails with `unexpected{sync_lock_drained}`. §4.1 declares two new mutex members — `std::atomic<std::uint32_t> active_holders_count_` (pre-drain holder accounting) and `std::atomic<bool> drain_complete_` (drain-completion flag) — plus a `detail::drain_latch drain_latch_` awaitable wrapper (§3.1 row added). §4.7.2 v1.2 sentinel-discriminates the `state_.exchange(...)` snapshot before casting. §4.7.2 v1.2 holds an in-flight reaped-resumption counter and waits for `active_holders_count_ == 0` AND in-flight = 0 before publishing `drain_complete_`; the holder's `unlock()` (per §4.5.2 v1.2 drain-aware short-circuit) observes `draining_ == true`, decrements the holder counter, and signals `drain_complete_` if the conditions are met. §4.7.2 v1.2 replaces `asio::async_wait_for_drain_complete[_count]` with `co_await drain_latch_.wait()` / `wait_one_step()` — a project-internal awaitable composed via `asio::async_initiate<...>` over a per-mutex `asio::steady_timer` (matches `[2d §6.5]`'s `cancellable_dispatch` precedent for project-owned ASIO-composed awaitables). One new §9 seam (#24 — `cancel_and_drain` blocks pre-drain holder + drain-latch event signalling, with sub-tests for the four sub-defects).

- **RC-C (round-2 editorial) — Stale 64-B layout claim + `result_` validity window prose.** §8 PMR recap still said "the `async_mutex_awaiter` (≤ 64 B)" — a v1.0 artefact contradicting §1.1's v1.1 ≤ 96 B claim. §1.1's inline arithmetic did not foot to 96 B (84 B raw, 88 B padded; 96 B is a ceiling carrying ≈ 8 B of cancellation_slot-size headroom). §4.2's `result_` field had no documented validity-window or ownership prose. **Single fix:** §8 says "≈ 96 B / ≤ 96 B" with the v1.1 RC-C inline buffer note; §1.1 grows an inline-arithmetic footnote (84 B raw + alignas(8) padding ≈ 88 B; 96 B ceiling for `cancellation_slot` slack); §4.2 grows a one-paragraph `result_` validity-window block (points into caller-frame storage; lifetime bounded by `await_resume` return; writer-side discipline; the writer's `*result_ = ...` write is sequenced before `schedule_resume_on_bound_executor`). Three line-edits + one new paragraph; no algorithm change.

**Per-finding resolution table (mirrors `[2e App C]` / `[2c App C]` / `[2d App C]` style):**

| Source / ID | Severity | Verdict (Opus judge) | Resolution |
|---|---|---|---|
| Codex N-P1-1 — `cancel_and_drain` does not block existing holder; uncontended `await_ready` bypasses `draining_` | P1 | **Confirm @ P1; cluster into RC-B** | §4.2.1 v1.2 — `await_ready` checks `draining_.load(memory_order_acquire)` BEFORE the fast-path CAS; on `draining_ == true`, writes `*result_ = unexpected{sync_lock_drained}`, sets `phase_ = cancelled`, returns true. §4.1 v1.2 declares `active_holders_count_` for pre-drain holder accounting; §4.7.2 v1.2 waits for `active_holders_count_ == 0` before publishing `drain_complete_`; §4.5.2 v1.2 drain-aware short-circuit makes the holder's `unlock()` observe `draining_ == true` and signal `drain_complete_` instead of granting. New seam #24 sub-test (a) + sub-test (b). |
| Codex N-P1-2 — `unlock()` walk terminal-marks more than one waiter; three-state phase model and §4.5.2 prose contradict | P1 | **Confirm @ P1; cluster into RC-A** | §4.5.2 v1.2 fully rewritten as a one-winner walk — skip `cancelled` heads, find the FIRST `queued` waiter, write `*winner->result_` BEFORE the CAS, CAS only that node `queued → granted`, splice the untouched tail (every waiter past `winner->next_` whose phase is still `queued`) into `next_drain_head_`. The "Waiters whose CAS succeeds AND are not the first-winner" bullet is deleted — no such waiters exist mechanically. §6.2.1 grows an explicit ordering specification for the grant CAS. §6.2.2 row "Per-waiter phase CAS (drain)" updated to apply only to the selected winner. |
| Codex N-P2-1 — Memory-ordering table claims `result_` is ordered by phase CAS, but algorithm writes `result_` AFTER the CAS | P2 | **Confirm @ P2; cluster into RC-A (cross-cuts RC-B)** | §6.2.2 v1.2 rows for "Per-waiter phase CAS (drain)", "Per-waiter phase CAS (cancel)", "`await_resume` phase load", and "`result_` slot publication" are corrected: release publishes writes that **PRECEDE** the CAS (consistent with C++23 release semantics). §4.5.2 v1.2 reorders the drain-grant path to write `*winner->result_` BEFORE the CAS. §4.7.2 v1.2 reorders the reaper path: `*head->result_ = unexpected{sync_lock_aborted}` before the cancel CAS. The cancellation-handler-cancel path is similarly reordered. The cross-thread happens-before edge is now valid: writer's release-CAS publishes the prior `result_` write; reader's acquire-load on `phase_` pairs with the release. |
| Codex N-P2-2 — `cancel_and_drain` depends on undeclared `drain_complete_` member and undefined awaitable-latch state machine | P1 (escalated by Opus from Codex P2) | **Escalate P2 → P1; cluster into RC-B** | §4.1 v1.2 declares `std::atomic<bool> drain_complete_` and `detail::drain_latch drain_latch_` as private members; the docstring describes the state machine ("fresh = false; reaper publishes true when `active_holders_count_ == 0` AND in-flight reaped count = 0 AND lists are empty"). §3.1 v1.2 grows a row for `fixpp::sync::detail::drain_latch` (project-internal awaitable; `asio::async_initiate` over per-mutex `asio::steady_timer` — `[2d §6.5]` precedent). §4.7.2 v1.2 consumes `drain_latch_.wait()` / `wait_one_step()` as the published awaitable surface. New seam #24 sub-test (d) verifies the wait/notify protocol. |
| Codex N-P3-1 — §8 PMR recap still carries the v1.0 64-B awaiter-size claim | P3 | **Confirm @ P3** | §8 v1.2 — the table row's awaiter-size column reads "≈ 96 B / ≤ 96 B, per §1.1 v1.1 budget — RC-C inline slot-handler-storage buffer; v1.2 adds no awaiter-side fields, the new `drain_complete_` / `active_holders_count_` lifecycle members live on the mutex per §4.1." One-line edit. |
| Opus C-N-P1-1 — `cancel_and_drain` does not block existing holder; uncontended `await_ready` bypasses `draining_` | P1 | NEW judgment row (clusters with Codex N-P1-1 above) | Same RC-B fix as Codex N-P1-1: §4.2.1 fast-path `draining_` check, §4.1 + §4.7.2 holder accounting and waiting. |
| Opus C-N-P1-2 — `unlock()` walk terminal-marks more than one waiter (judgment of Codex N-P1-2) | P1 | NEW judgment row (clusters with Codex N-P1-2 above) | Same RC-A fix as Codex N-P1-2: §4.5.2 v1.2 one-winner walk. |
| Opus C-N-P2-1 — Memory-ordering inversion (judgment of Codex N-P2-1) | P2 | NEW judgment row (clusters with Codex N-P2-1 above) | Same RC-A/§6.2.2 fix. |
| Opus C-N-P2-2 — `cancel_and_drain` depends on undeclared `drain_complete_` (judgment of Codex N-P2-2 — escalated to P1) | P1 (escalation) | NEW judgment row (clusters with Codex N-P2-2 above) | Same RC-B/§4.1/§4.7.2 fix. |
| Opus N-P1-1 — `state_.exchange(...)` cast to `async_mutex_awaiter*` walks sentinel pointers as nodes (UAF on free mutex) | P1 | NEW (Opus) | §4.7.2 v1.2 step (d) — sentinel-discriminated cast: `auto raw_state = state_.exchange(locked_no_waiters); auto* lifo_head = (raw_state == not_locked || raw_state == locked_no_waiters) ? nullptr : reinterpret_cast<async_mutex_awaiter*>(raw_state);`. Step (f)'s late-walker loop applies the same discrimination. §4.5.2 v1.2 step (2b) documents the same protocol for `unlock()` (where `not_locked` is impossible by precondition but the discriminator is documented for symmetry). New seam #24 sub-test (c). |
| Opus N-P2-1 — `asio::async_wait_for_drain_complete[_count]` is invented ASIO API | P2 | NEW (Opus) | §3.1 v1.2 grows a row for `fixpp::sync::detail::drain_latch` (project-internal awaitable composed via `asio::async_initiate<...>` over a per-mutex `asio::steady_timer`; matches `[2d §6.5]` `cancellable_dispatch` precedent). §4.7.2 v1.2 replaces every reference to `asio::async_wait_for_drain_complete[_count]` with `co_await drain_latch_.wait()` / `wait_one_step()`. The latch publishes `wait()`, `wait_one_step()`, and `notify()` as member-awaitables / member functions (no novel ASIO API). |
| Opus N-P3-1 — §1.1 inline 96-B layout sums do not foot to the field list | P3 | NEW (Opus) | §1.1 v1.2 — closes the inline arithmetic with "8 + 8 + 4 + 16 + 8 + 8 + 32 = **84 B raw; with `alignas(8)` trailing padding ≈ 88 B**. The '≤ 96 B' total is the published ceiling, carrying ≈ 8 B of headroom for `asio::cancellation_slot`'s implementation-defined size." Pure editorial transparency. |
| Opus N-P3-2 — §4.5.1 window 4 prose incomplete on residual-cancellation observability | P3 | NEW (Opus) | §4.5.1 window 4 v1.2 grows one paragraph on the `cancel_and_drain`-vs-residual-cancellation interaction: per-waiter `phase_` CAS arbitrates; whoever wins `queued → cancelled` schedules the resumption; loser observes terminal `cancelled` and no-ops. Sub-cases (a) every-residual-cancelled-walk-falls-through and (b) reaper-already-detached are explicitly named. |
| Opus N-P3-3 — `result_` is non-owning raw-pointer storage with no documented validity window | P3 | NEW (Opus) | §4.2 v1.2 grows a one-paragraph contract block after the awaiter struct: `result_` points into caller-frame storage for the `expected_t<async_lock_guard>` value the coroutine yields; lifetime bounded by `await_resume()` return; writes are exclusive to the per-waiter `phase_` CAS-winner; writer-side `*result_ = ...` is sequenced before `schedule_resume_on_bound_executor`, so the awaiter remains alive for the duration of the writer's critical section. |

**Codex findings disagreed with — none.** All 5 Codex findings (2 P1 / 2 P2 / 1 P3) were judged "Confirm" or "Escalate" by Opus and applied verbatim or via the RC clustering. No disagreement to record. (One Codex finding — N-P2-2 — was escalated from P2 to P1 by Opus on the rationale that the missing `drain_complete_` member is part of the algorithm spec, not a documentation gap; the resolution is the same regardless of severity.)

**Net-effect summary:**

- **Test-seam delta:** v1.1 = 23 seams → v1.2 = **24 seams** (+1 new: #24 `cancel_and_drain` blocks pre-drain holder + drain-latch event signalling, with four sub-tests covering pre-drain-holder lifecycle, post-drain fast-path bypass closure, sentinel-cast UAF on free mutex, and drain-latch wait/notify protocol).
- **Error-variant delta:** v1.1 = 4 (`sync_lock_aborted`, `sync_lock_alloc_failed`, `sync_lock_outside_session`, `sync_lock_drained`) → v1.2 = **4** (no change). RC-B round-2 fixes do not introduce new variants.
- **Appendix D drop-in delta:** v1.1 = 3 (§D.1, §D.2, §D.3) → v1.2 = **3** (no change). RC fixes do not touch sibling-doc text.
- **Awaiter layout delta:** v1.1 = ≤ 96 B → v1.2 = **≤ 96 B** (no change). v1.2's RC-B additions (`active_holders_count_`, `drain_complete_`, `drain_latch_`) live on the **mutex**, not the awaiter; the awaiter byte-budget is unchanged.
- **Mutex layout delta:** v1.1 fields (`state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `policy_`) → v1.2 adds **`active_holders_count_`** (4 B `std::atomic<std::uint32_t>`), **`drain_complete_`** (1 B + 3 B padding `std::atomic<bool>`), and **`drain_latch_`** (a `detail::drain_latch` member — back-pointer to mutex + `asio::steady_timer` ≈ 24 B). Mutex object grows from ≈ 24 B (v1.1) to ≈ 56 B (v1.2); still fits in one cache line.
- **Phase enum delta:** v1.1 three-state `{ queued, granted, cancelled }` → v1.2 **three-state, unchanged**. RC-A round-2 fix is a §4.5.2 algorithm rewrite, NOT a phase-enum change.
- **Section rewrites enumerated:** §1.1 (inline-arithmetic transparency for 96-B budget; mutex-layout note for v1.2 RC-B members); §1.2 (mutex grew with `active_holders_count_` + `drain_complete_` + `drain_latch_`); §3.1 (one new primitive row — `fixpp::sync::detail::drain_latch`); §4.1 (mutex private-block + member docstring updated; `cancel_and_drain` member docstring rewritten; namespace forward-declares `drain_latch`); §4.2 (one new paragraph on `result_` validity window after the awaiter struct + alignment static_assert); §4.2.1 (full rewrite — `draining_.load(acquire)` BEFORE fast-path CAS; race-window between (1) and (2) noted; new `active_holders_count_` increment); §4.5.1 (window 4 grows residual-cancellation arbitration paragraph); §4.5.2 (full rewrite as one-winner walk with mutex-owned residual; pre-walk holder-counter decrement; drain-aware short-circuit when `draining_ == true`; `result_` write before grant CAS); §4.7.2 (full rewrite of `cancel_and_drain` mechanism — sentinel-discriminated cast, `result_` write before reap CAS, holder-counter wait, `drain_latch_.wait()` replaces invented `asio::async_wait_for_drain_complete[_count]`); §6.2.1 (ordering specification for grant CAS — release publishes preceding writes); §6.2.2 (rows for drain CAS, cancel CAS, `await_resume` phase load, `result_` slot publication corrected); §8 (PMR recap awaiter-size column updated to ≈ 96 B / ≤ 96 B); §9 (one new seam #24); Appendix C (this entry, above the v1.0 → v1.1 entry).

---

### v1.0 → v1.1 (Gate A round 1 post-reset, convergence pass; 2026-05-08)

**Inputs:**
- Codex review `research/reviews/codex_2f_v1_async-mutex_review.md` — 3 P1 / 3 P2 / 2 P3.
- Opus adversarial review `research/reviews/opus_2f_v1_async-mutex_adversarial_review.md` — combined post-judging **5 P1 / 6 P2 / 4 P3**, 3 root causes (RC-A residual-ownership/phase-naming; RC-B `cancel_and_drain` API mechanism; RC-C contended-default slot allocator) + 1 editorial root cause (RC-D Appendix D drop-in completeness). Closing recommendation: **"v1.1 can ship after a single convergence pass."**

**Root causes called out (per Opus, source of truth):**

- **RC-A — Residual-chain ownership + phase encoding (atomic algorithm).** v1.0 stored the residual list on the granted awaiter (unreachable from `unlock()`'s `async_mutex*`-only state; destroyed when `await_resume` returns or when the holder is cancelled mid-critical-section); CAS'd residual waiters to `draining`, swallowing later cancellations. Single fix (per the user-prescribed shape that takes precedence over Opus's split-enum proposal): **(a)** move residual ownership to `async_mutex::next_drain_head_` (`std::atomic<async_mutex_awaiter*>`); **(b)** collapse the phase enum to three states `{ queued, granted, cancelled }` (the v1.0 four-state `{ queued, draining, cancelling, completed }` had an unnecessary intermediate `draining` state — the drain CAS to `granted` is atomic with the ownership transfer, no intermediate "ownership in flight" interval needed); **(c)** drain CAS's `queued → granted` for the first non-cancelled waiter; remaining `queued` waiters stay `queued` and are spliced into `next_drain_head_` (still cancellable, closing C-P1-3); **(d)** `unlock()` walks `next_drain_head_` first, then the `state_` LIFO; **(e)** holder cancellation mid-critical-section is the "hostile-but-correct" path — the holder's coroutine unwinds and the guard's destructor calls `unlock()`, which walks `next_drain_head_` (mutex-side, unaffected by holder coroutine-frame destruction). Section rewrites: §4.2 (awaiter struct drops `residual_`; phase enum rewritten); §4.5 (cancellation table; §4.5.1 four windows including new residual-cancellation window; §4.5.2 mutex-owned drain algorithm); §6.2 (memory-ordering sub-table grows three rows for `next_drain_head_`); §1.1 (worst-case waiter depth + 96-B layout). New seam #22 (residual-chain cancellation under graceful close).

- **RC-B — `cancel_and_drain()` API mechanism.** v1.0 prose ("walks the awaiter's parent `cancellation_state`") was non-implementable — the mutex does not own waiters' parent cancellation_states. Single fix: rewrite §4.7.2 as a mutex-owned reaping operation. **(a)** `async_mutex::draining_` (`std::atomic<bool>`) — set by `cancel_and_drain`; subsequent `async_lock(...)` returns `unexpected{sync_lock_drained}` (NEW error variant — added to §6.5). **(b)** `cancel_and_drain` atomically exchanges both `state_` and `next_drain_head_`, walks both lists, CAS's every `queued` waiter to `cancelled`, schedules each cancelled waiter's resumption on its bound executor with `result_ = unexpected{sync_lock_aborted}`, awaits an internal `drain_complete_` notification primitive, returns `expected_t<void>{}`. **(c)** Concurrent-call safe via `drain_in_progress_` `std::atomic_flag` (Opus N-P2-1 close). **(d)** Idempotent (second call observes `draining_ == true`, returns immediately). Section rewrites: §4.7.2 (full mechanism rewrite with code-shape sketch); §6.5 (`sync_lock_drained` variant); §4.1 class members (`draining_`, `drain_in_progress_`, `next_drain_head_` — RC-A + RC-B share the latter); §4.2.2 step 1 (`await_suspend` checks `draining_`). New seams #23 (concurrent `cancel_and_drain` is serialised); existing #19 (cancel-and-drain reaper) augmented.

- **RC-C — Contended-default slot allocator storage.** v1.0 referenced `slot_allocator(mr)` in prose without defining storage when `mr == nullptr` (Codex C-P2-6) or what `slot_allocator` itself is (Opus N-P2-2). Single fix: declare `fixpp::sync::detail::slot_allocator` as a project-internal allocator type; add a 32-byte inline buffer `slot_storage_` to `async_mutex_awaiter`; document the three-case storage table in §4.3.4. **(case 1)** `mr == nullptr && HALO fires` → inline buffer + `null_memory_resource()` fallback (refuses overflow). **(case 2)** `mr == nullptr && HALO does not fire` → global heap (observable in benchmark mode; non-fatal iff §9 #10 PMR-fallback seam passes). **(case 3)** `mr != nullptr` → `std::pmr::polymorphic_allocator<void>{mr}`. Section rewrites: §4.2 awaiter layout (32-B inline buffer); §4.3 (§4.3.1 references §4.3.4; new §4.3.4 three-case table); §6.1.1 (per-case allocation prose); §3.1 (two new primitive rows — `std::pmr::polymorphic_allocator<void>`, `detail::slot_allocator`, `null_memory_resource()`); §1.1 (96-B awaiter budget). New seam #21 (slot-allocator storage cases).

- **RC-D — Sibling-doc Appendix D drop-ins are incomplete (editorial).** v1.0's Appendix D §D.1 / §D.2 used prose-summary format without verbatim "Replace this:" / "with this:" exact-text quoting, missed the `[2d §7.4]` surface bullet (Codex C-P2-5 / Opus N-P1-1), said `Session::session_arena()` was callable from `core/` plumbing (Codex C-P3-8 — `[arch §2.3]` leaf-rule violation), and did not name the `[2d §4.4]` resolution chain backing the never-null claim (Opus N-P2-3). Single fix: rewrite §D.1 and §D.2 in exact-text format mirroring `[2e App D]` style; add §D.3 (NEW v1.1) for the `[2d §7.4]` surface bullet; strike "and `fixpp::core/` plumbing" from §D.1 closing prose; add the `[2d §4.4]` chain reference and `[arch §5.6]` frozen-config citation to the §D.1 accessor docstring. Section rewrites: Appendix D §D.1, §D.2, §D.3 (new); §3.1 hand-off gates list; §11 cross-doc drop-ins list.

**Per-finding resolution table (mirrors `[2e App C]` / `[2c App C]` / `[2d App C]` style):**

| Source / ID | Severity | Verdict (Opus judge) | Resolution |
|---|---|---|---|
| Codex C-P1-1 — Cancellation leaves a waiter physically linked after the awaiter may resume and deallocate (UAF on cancellation path) | P1 | **Confirm @ P1; cluster into RC-A** | §4.5.2 v1.1 atomically detaches both `state_` LIFO and `next_drain_head_` FIFO at drain time; cancelled waiters are observed via `phase_ == cancelled` and skipped from the residual splice (no pointer threads through them after `await_resume` returns). The §9 seam #15 (cancel-after-detach-pre-drain race) covers; new seam #22 covers residual cancellation. |
| Codex C-P1-2 — Residual chain stored in granted awaiter, but guard does not carry that awaiter or its residual ownership | P1 | **Confirm @ P1; cluster into RC-A** | §4.5.2 v1.1 — residual chain hoisted to `async_mutex::next_drain_head_` (mutex-owned); every `unlock()` walks it first. Awaiter no longer carries `residual_` field (§4.2 v1.1 layout). Lifetime defect closed structurally. |
| Codex C-P1-3 — Residual waiters marked `draining` before they acquire, so later cancellation incorrectly becomes no-op | P1 | **Confirm @ P1; cluster into RC-A** | §4.5.2 v1.1 — residual waiters stay `phase_ == queued` (not CAS'd to `granted`); cancellation against them succeeds (CAS `queued → cancelled`). The drain on the next `unlock()` walks `next_drain_head_` and skips `cancelled` waiters. Closes the silent-lost-cancellation defect. New seam #22. |
| Codex C-P2-4 — `cancel_and_drain()` specified through non-existent ability to walk parent cancellation states | P2 | **Confirm @ P2; cluster into RC-B** | §4.7.2 v1.1 fully rewritten as a mutex-owned reaping operation with `draining_` flag + atomic exchange of both lists + concurrent-call serialisation. Code-shape sketch added. |
| Codex C-P2-5 — 2d cross-doc cancellation amendment misses the locked §7.4 executor-compat wording | P2 | **Confirm @ P2; cluster into RC-D** | New Appendix D §D.3 added — exact-text "Replace this bullet:" / "with this:" for `[2d §7.4]`'s surface bullet. §11 hand-off list updated. |
| Codex C-P2-6 — Hot-path slot allocator story still lacks concrete storage for `mr == nullptr` | P2 | **Confirm @ P2; cluster into RC-C** | §4.3.4 (NEW) — three-case storage table; §4.2 awaiter grows inline 32-B `slot_storage_` buffer; `detail::slot_allocator` declared in §3.1 row + `[const §XI.2]` reference. §9 seam #10 split into both branches; new seam #21 verifies all three cases. |
| Codex C-P3-7 — Compile-time invariant sketch is not C++-order-valid as written | P3 | **Confirm @ P3** | §4.1 v1.1 — forward-declare `enum class waiter_phase : std::uint8_t;` and `class slot_allocator` in the namespace block; the `static_assert(alignof(...))` moves to the end of §4.2 after the awaiter struct's complete definition. Order-valid. |
| Codex C-P3-8 — Appendix D §D.1 says `Session::session_arena()` accessor is callable from `fixpp::core` plumbing | P3 | **Confirm @ P3; cluster into RC-D** | Appendix D §D.1 v1.1 — strikes "and `fixpp::core/` plumbing"; engine-internal scope is `fixpp::session/` only. Closes `[arch §2.3]` leaf-rule risk. |
| Opus N-P1-1 — `cancel_and_drain()` cannot reach waiters parked behind a granted waiter's residual chain | P1 | NEW (Opus) | RC-A + RC-B joint close — residual chain on mutex side; `cancel_and_drain` atomically exchanges `next_drain_head_` and reaps. New seam #22 (residual-chain cancellation under graceful close); new seam #23 (concurrent `cancel_and_drain`). |
| Opus N-P1-2 — Phase encoding admits a four-way race (holder cancellation mid-critical-section destroys residual chain) | P1 | NEW (Opus) | RC-A close — residual chain is mutex-owned (§4.5.2 v1.1), unaffected by holder coroutine-frame destruction. §4.5.1 grows fourth race window covering this case explicitly; "hostile-but-correct path" prose added — holder cancellation propagates through normal coroutine unwinding; guard destructor's `unlock()` services residual list. |
| Opus N-P2-1 — `cancel_and_drain()` non-idempotent under two concurrent calls | P2 | NEW (Opus) | RC-B close — `drain_in_progress_` `std::atomic_flag` serialises walkers; non-reaper callers `co_await drain_complete_` notification. New seam #23. |
| Opus N-P2-2 — `bind_allocator(slot_allocator(mr))` cites a primitive that is not standard ASIO | P2 | NEW (Opus) | RC-C close — `fixpp::sync::detail::slot_allocator` declared in §3.1 row + project-internal type in `detail::` block; §4.2.2 step 3 spells out `bind_allocator(detail::slot_allocator{this, mr})`. |
| Opus N-P2-3 — `Session::session_arena()` accessor publication consistency with `[arch §5.6]` mid-session-swap ban | P2 | NEW (Opus) | RC-D close — Appendix D §D.1 v1.1 grows the `[2d §4.4]` resolution-chain reference (`SessionConfig::session_arena ?: EngineConfig::default_session_resource ?: std::pmr::get_default_resource()`) and the `[arch §5.6]` frozen-at-open invariant citation backing the never-null claim. |
| Opus N-P2-4 — Latency Tier 1 row 4 (contended drain) per-waiter cost still ignores resumed-coroutine work | P2 | NEW (Opus) | §6.3 row 4 split into two rows: "drain-side handoff cost (hard budget, ≤ 50 ns)" and "resumed-coroutine first-checkpoint latency (bench-harness-soft, unbounded by 2f's surface)" — making the inline-execution conflation visible. |
| Opus N-P3-1 — `try_lock()` + adopt-locked guard surface still admits same-mutex aliasing bug | P3 | NEW (Opus) | §4.1 — `try_lock()` removed from the public surface (moved to `detail::`; see §4.1 comment block). §4.4 — `async_lock_guard` engaged constructor is private + `friend`-only; default-constructed guard is the only public ctor. Closes the aliasing-hole class. §9 seam #20 uses friend access. |
| Opus N-P3-2 — DoS surface (unbounded waiter list) is still unaddressed | P3 | NEW (Opus) | §1.1 grows a "Post-v1.0 design risk" sentence naming the unlock-drain-cost ceiling at large N; v1.0 callsites are bounded by session-domain serialisation discipline, no §9 seam at N=10⁶ required. Foreclosed for v1.0. |
| Opus N-P3-3 — `static_assert` on `is_always_lock_free` rejects non-LP64 ABIs at compile time but the doc only commits to platform-rejection | P3 | NEW (Opus) | §4.1 v1.1 — the `static_assert` block grows the comment "Targets where `std::atomic<uintptr_t>::is_always_lock_free` is false are out of `[const §II]` Tier 1/2 scope and the algorithm rejects them at compile time." |
| Opus N-P3-4 — Inherited-primitives table missing `std::pmr::polymorphic_allocator<void>` row | P3 | NEW (Opus) | §3.1 v1.1 — three new primitive rows added (`std::pmr::polymorphic_allocator<void>`, `std::pmr::null_memory_resource()`, `fixpp::sync::detail::slot_allocator`) with the §4.2.2 / §4.3.1 / §4.3.4 use sites cited. |

**Codex findings disagreed with — none.** All 8 Codex findings (3 P1 / 3 P2 / 2 P3) were judged "Confirm" by Opus and applied verbatim or via the RC clustering. No disagreement to record.

**Net-effect summary:**

- **Test-seam delta:** v1.0 = 20 seams → v1.1 = **23 seams** (+3 new: #21 slot-allocator storage cases, #22 residual-chain cancellation under graceful close, #23 concurrent `cancel_and_drain` is serialised). Existing seam #10 (PMR fallback) explicitly split coverage into `mr == nullptr` and `mr != nullptr` branches (Codex C-P2-6 close).
- **Error-variant delta:** v1.0 = 3 (`sync_lock_aborted`, `sync_lock_alloc_failed`, `sync_lock_outside_session`) → v1.1 = **4** (+`sync_lock_drained` per RC-B); group `FIXPP_ERR_SYNC_RUNTIME` grows by one variant.
- **Appendix D drop-in delta:** v1.0 = 2 (§D.1, §D.2) → v1.1 = **3** (+§D.3 for `[2d §7.4]` surface bullet). All three are now in exact-text "Replace this:" / "with this:" format mirroring `[2e App D]` style.
- **Awaiter layout delta:** v1.0 = ≤ 64 B (with `residual_`); v1.1 = ≤ 96 B (drops `residual_`, adds 32-B `slot_storage_` inline buffer per RC-C). Still HALO-eligible (§6.4); §1.1 budget rephrased.
- **Mutex layout delta:** v1.0 had `state_` + `policy_`; v1.1 adds `next_drain_head_` (RC-A), `draining_` (RC-B), `drain_in_progress_` (RC-B / N-P2-1). Three new memory-ordering sub-table rows.
- **Phase enum delta:** v1.0 four-state `{ queued, draining, cancelling, completed }` → v1.1 three-state `{ queued, granted, cancelled }` (RC-A close — collapses redundant intermediate states; the user-prescribed shape takes precedence over Opus's proposed five-state `{ queued, residual_queued, draining, cancelling, completed }` split because the v1.1 mutex-owned `next_drain_head_` makes the `queued` ↔ `residual_queued` distinction unnecessary — both are cancellable-while-queued; the drain CAS to `granted` is the ownership-transfer terminus).
- **Section rewrites enumerated:** §1.1 (worst-case + 96-B layout + post-v1.0 risk note); §1.2 (scope addition for `next_drain_head_` + `slot_allocator`); §3.1 (three new primitive rows); §4.1 (class members + forward-declarations + static_assert reflow + `try_lock` move to `detail::`); §4.2 (phase enum + awaiter layout + slot_storage_ buffer + alignment static_assert reflow); §4.2.2 / §4.2.3 (await_suspend draining_ check + slot-allocator binding + lifetime-safety prose); §4.3 (§4.3.1 references §4.3.4; new §4.3.4 three-case slot-allocator storage sub-table); §4.4 (engaged-ctor private/friend-only); §4.5 (cancellation table + §4.5.1 four windows + §4.5.2 mutex-owned drain algorithm); §4.7.2 (`cancel_and_drain` reshape with code-shape sketch); §6.1.1 (per-case allocation prose); §6.2.2 (three new memory-ordering rows for `next_drain_head_`; two new for `draining_`; phase-CAS rows updated); §6.3 (row 4 split into drain-handoff vs resumed-coroutine first-checkpoint); §6.5 (`sync_lock_drained` variant); §9 (#10 split; #20 friend access; #21/#22/#23 added; intro "20 seams" → "23 seams"); §11 (hand-off list adds §D.3); Appendix D (§D.1 exact-text + resolution chain + strike `core/` plumbing; §D.2 exact-text format; §D.3 NEW for `[2d §7.4]`).

---

## Appendix D — Cross-doc drop-ins (NEW — declared by v1.0)

Per convergence rule 6 + the 2c v1.3 / 2d v0.4 / 2e v0.4 sibling-doc-edit precedent, sibling-doc text touched by this rewrite is surfaced as drop-in amendment language for the orchestrator to apply at sign-off. The 2f rewrite agent does not edit `2d-threading.md` directly. Per `[const §VI.5]`, every reference uses the exact `[DocAbbrev §X.Y.Z] Title` form; review-internal IDs (e.g., "RC#2", "Codex C-P1-3") are not carried into the sibling text.

### D.1 `[2d §4.5] fixpp::session::SessionConfig — session-level frozen-at-open knobs` — publish `Session::session_arena()` engine-internal accessor (RC#2 close; v1.1 / RC-D close — exact-text format)

**Tension:** 2f's session-side helper `async_lock_via_session_executor` (§4.3.2) recovers the per-session PMR resource from the awaiter's bound `session_executor`. The `[2d §4.8]` v0.4 wrapper publishes `session_ptr() noexcept -> Session*`; from there, the helper needs a `Session` member that returns the resource. `[2d §4.5]` publishes `SessionConfig::session_arena` as a config field; it does not publish a `Session` accessor. v0.1 of this doc invented `Session::session_arena()` under a false `[2d §4.5]` reuse citation (Opus N-P1-1). Round-1 close: queue the cross-doc edit explicitly. v1.1 / Codex C-P3-8 / Opus C-P3-8 / Opus N-P2-3 close: use exact-text "Replace this:" / "with this:" format and add the resolution chain reference per `[2d §4.4]` / `[arch §5.6]`.

**Replace this paragraph in `[2d §4.5]` (the `Session` class declaration that consumes `SessionConfig`):**

```cpp
class Session {
public:
    // … existing public surface (open, close, get_executor, etc.) …
private:
    // … existing private members (config, trace_slot_, executor_, etc.) …
};
```

**with this:**

```cpp
class Session {
public:
    // … existing public surface (open, close, get_executor, etc.) …

    // Engine-internal accessor (not part of the user-facing public surface;
    // exposed for the fixpp::session-layer helper
    // async_lock_via_session_executor per [2f §4.3.2] / [2f Appendix D §D.1]).
    // Returns the per-session PMR resource carried as
    // SessionConfig::session_arena per [2d §4.5]. noexcept; never returns
    // null — the constructor pre-conditions a non-null session_arena via
    // the [2d §4.4] resolution chain (SessionConfig::session_arena ?:
    // EngineConfig::default_session_resource ?:
    // std::pmr::get_default_resource()); per [arch §5.6] the resolved value
    // is frozen at session open and never swaps mid-session, so the
    // accessor's never-null contract holds for the session lifetime.
    [[nodiscard]] std::pmr::memory_resource* session_arena() const noexcept;

private:
    // … existing private members …
};
```

The accessor is **engine-internal** (callable from `fixpp::session/`; not part of `Session`'s documented public user-facing API). v1.1 / Codex C-P3-8 / Opus C-P3-8 close: the v1.0 prose "callable from `fixpp::session/` and `fixpp::core/` plumbing" violated `[arch §2.3]`'s leaf rule (`core/` cannot back-edge into `session/`); v1.1 strikes "and `fixpp::core/` plumbing" — `core::async_mutex` does not call this accessor (the session-side helper in `fixpp::session/` is the only caller). The orchestrator applies this edit at 2f sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2f RC#2.

### D.2 `[2d §4.7] Cancellation propagation API — two-phase close` — `async_mutex::lock` row's cancellation result rewording (RC#4 close; v1.1 / RC-D close — exact-text format)

**Tension:** `[2d §4.7]` v0.4 line 804's per-mode effect table row for `async_mutex::lock` reads "completes with `operation_aborted`" — using the ASIO completion-token shape. v0.1 of this doc reshaped that into `expected_t::unexpected{error::sync_lock_aborted}` (consistent with `[2d §6.5]`'s `cancellable_dispatch → dispatch_aborted` precedent and `[arch §5.3]`'s "no exceptions on the hot path" rule) **without** declaring a 2d cross-doc amendment (Codex C-P1-6 / Opus close). Round-1 close: queue the cross-doc edit explicitly.

**Replace this row in `[2d §4.7]`'s per-mode effect table** (do not modify the other 8+ rows):

```
| `async_mutex::lock` (in-flight waiter) | runs | cancelled (`operation_aborted`) | cancelled |
```

**with this** (v1.3 / RC-γ / Codex C-R3-P3-1 + Opus N-R3-P3-2 close — the row name in `[2d §4.7]`'s historical table is `async_mutex::lock`; 2f's published surface is `async_mutex::async_lock(...)`. The "Replace this:" body is byte-faithful to the upstream row name so the diff applies cleanly; the "with this:" body annotates the historical→2f mapping in the row text rather than renaming the row, preserving citation lineage):

```
| `async_mutex::lock` (historical row name; corresponds to `async_mutex::async_lock(...)` per `[2f §4.1.1]` v1.3) (in-flight waiter) | runs | cancelled — completes with `expected_t::unexpected{error::sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]` / `[2f §6.5]` | cancelled (same as graceful phase 2) |
```

**One-paragraph contract on the 2f-boundary cancellation outcome (drop-in addition — append immediately after the per-mode effect table, before the existing Notes section):**

> **`async_mutex::lock` (historical row name — corresponds to `async_mutex::async_lock(...)` per `[2f §4.1.1]` v1.3) cancellation outcome at the 2f boundary (driven by `[2f §4.5]` / `[2f §6.5]` / `[2f Appendix D §D.2]`).** `fixpp::sync::async_mutex::async_lock(...)` is a C++-only awaitable returning `asio::awaitable<expected_t<async_lock_guard>>`; on `cancellation_type::total` (or `terminal`, treated as `total` per `[2f §4.5]`) it removes the waiter from the LIFO list via the per-waiter `phase_` CAS protocol and completes the awaitable with `expected_t::unexpected{error::sync_lock_aborted}`. The wording "`operation_aborted`" elsewhere in `[2d §4.7]` continues to apply to ASIO completion-token-shaped cancellations on operations that surface their cancellation through ASIO's `error_code` channel; 2f's 2f-boundary outcome is `expected_t::unexpected` because the operation's value channel is `expected_t<async_lock_guard>` and `[arch §5.3]` forbids exceptions on the hot path. At the C ABI both shapes coalesce into `FIXPP_ERR_CANCELLED` per `[2d §6.7]`. The `[2d §6.5]` `cancellable_dispatch → expected_t::unexpected{dispatch_aborted}` precedent is the project-internal idiom this rewording matches.

The orchestrator applies this edit at 2f sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2f RC#4.

### D.3 `[2d §7.4] Locked contract surface — Awaitable mutex (2f)` — surface bullet's cancellation result rewording (NEW v1.1 / RC-D — Codex C-P2-5 / Opus N-P1-1 close)

**Tension:** Codex C-P2-5 / Opus N-P1-1 verified that `[2d §7.4]`'s second bullet (the *locked* executor-compat contract surface that 2f's status block cites as the binding source) still reads "completes with `operation_aborted`". §D.2's effect-table edit alone leaves two sibling contracts authoritative and inconsistent: `[2d §4.7]` (table) says `expected_t::unexpected{sync_lock_aborted}`; `[2d §7.4]` (surface) still says `operation_aborted`. v1.1 / RC-D queues the second drop-in to align both.

**Replace this bullet in `[2d §7.4]`'s contract surface for `async_mutex::lock`:**

```
- The mutex must honour `asio::cancellation_type::total` per `[SYN §3.2 Q6b]`
  item 3 — when cancellation is signalled, the waiter is removed from the
  LIFO list and completes with `operation_aborted`.
```

**with this** (v1.3 / RC-γ / Codex C-R3-P3-1 + Opus N-R3-P3-2 close — surface bullet text annotates the historical `async_mutex::lock` row name → 2f's `async_mutex::async_lock(...)` API mapping inline):

```
- The mutex (per [2f §4.1.1] v1.3 surface `async_mutex::async_lock(...)` —
  the `[2d §4.7]` table's historical row name `async_mutex::lock` maps to
  this 2f API) must honour `asio::cancellation_type::total` per
  `[SYN §3.2 Q6b]` item 3 — when cancellation is signalled, the waiter is
  removed from the LIFO list (via the per-waiter `phase_` CAS protocol per
  `[2f §4.5]`) and the awaitable completes with
  `expected_t::unexpected{error::sync_lock_aborted}` at the 2f boundary,
  mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]` / `[2f §6.5]`.
  The `[2d §6.5]` `cancellable_dispatch →
  expected_t::unexpected{dispatch_aborted}` precedent is the project-internal
  idiom this surface matches.
```

The orchestrator applies this edit at 2f sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2f RC#4 / RC-D.
