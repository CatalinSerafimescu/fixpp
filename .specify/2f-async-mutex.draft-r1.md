# Design Doc 2f — Awaitable Mutex `fixpp::sync::async_mutex`

> **Status:** Draft v0.1 — pre-Gate A
> **Date:** 2026-05-08
> **Owner:** Opus (Phase 2 design author)
> **Headers:** `fixpp::sync::async_mutex` (`include/fixpp/core/sync/async_mutex.hpp`); RAII guard `fixpp::sync::async_lock_guard` (same header); CI-side enforcement of `[const §XV.9]` (`tools/check_no_std_mutex_in_awaitable_headers.sh` — grep gate; clang-tidy custom check is the post-v1 path per §10 Q3).
> **Inherits:**
> - `[const §VIII.5]` Performance Budgets & Benchmarks — Allocator policy on the hot path (zero `new`/`delete` between parse and `fromApp`); 2f extends the discipline to the contended `async_lock` path because the v1.0 store-write mutex sits on the outbound dispatch chain.
> - `[const §XI.1]` Concurrency & Coroutines — `asio::awaitable<T>` is the session/transport composition primitive; 2f's `async_lock` returns `asio::awaitable<expected_t<async_lock_guard>>`.
> - `[const §XI.2]` Concurrency & Coroutines — ASIO native cancellation slots end-to-end; 2f honours `cancellation_type::total` per §4.5.
> - `[const §XI.3]` Concurrency & Coroutines — Awaitable mutex required in coroutine context; **direct mandate** for this doc.
> - `[const §XI.5]` Concurrency & Coroutines — Hot-path lock policy; the store-write path always uses mutex regardless of `SessionConfig::lock_policy`, so 2f is the lock for that path.
> - `[const §XI.6]` Concurrency & Coroutines — Coroutine frame allocation HALO-first + per-awaiter PMR fallback; 2f's awaiter is HALO-eligible by construction (item (1) of the six-item list) with PMR fallback per §4.3.
> - `[const §XIV.2]` Pluggable Interfaces — ≤5 pure-virtual on plugin interfaces. **`async_mutex` is NOT a plugin** (no virtual surface; value type owned by the consumer); §3 records this explicitly so the next reviewer does not waste a finding on it.
> - `[const §XV.9]` Banned Patterns — `std::mutex` in coroutine context is banned; 2f is the only legal mutex shape, and 2f names the CI enforcement mechanism in §6 / §9.
> - `[arch §3]` Public Namespaces — `fixpp::sync` lives physically under `core/`; the header path is `include/fixpp/core/sync/async_mutex.hpp`.
> - `[arch §4.1]` `core` — `fixpp::sync::async_mutex` is listed in the core public surface; 2f delivers the detail.
> - `[arch §5.1]` Executor model — `asio::any_io_executor` primitive, per-session strand default, coroutine composition, HALO-first; 2f's awaiter completes on the awaiter's bound executor (the `session_executor` wrapper class per `[2d §4.8]` for in-session callers).
> - `[arch §5.2]` Allocator policy — public API is PMR-aware, per-session `memory_resource` carried via the executor and `SessionConfig`; 2f's PMR fallback path consumes the per-session resource.
> - `[arch §5.5]` Lifetime model — `async_mutex` is a value-typed, non-movable owned type held by its consumer; the `async_lock_guard` is a flyweight bound to the mutex's lifetime via `[[clang::lifetimebound]]`.
> - `[arch §6]` Plugin Pattern — applies only when there is a virtual surface user code subclasses; **2f does not expose one** (§3 / §1.2).
> - `[arch §10]` row 2f — Awaitable mutex (six-item design list per `[SYN §3.2 Q6b]`); cross-cutting hooks: §5.1 executor model; §4.1 surface.
> - `[arch §11]` row 2 — Coroutine HALO firing on inbound dispatch path across the compiler matrix; **co-owned by 2d and 2f**; verification work tracked in §10.
> - `[SYN §3.2 Q6a]` — Cancellation propagation model (DECIDED — ASIO native cancellation slots end-to-end); 2f honours `cancellation_type::total`.
> - `[SYN §3.2 Q6b]` — Awaitable mutex (DECIDED — own implementation in `fixpp::sync`); the **six-item design list** is the operating spec for §4 / §6.
> - `[2a §4.2]` `trap_throw` pattern — the no-terminate-on-PMR-throw mechanism; 2f's PMR fallback path routes throws through this helper.
> - `[2a §6.5]` Latency Tier 1 ceiling idiom — 2f mirrors this idiom in §6.3.
> - `[2b §6.4]` Lifetime contract on flyweights — 2f's `async_lock_guard` carries `[[clang::lifetimebound]]` per the same precedent.
> - `[2c §6.7]` Per-doc-prefix discipline (`FIXPP_ERR_DICT_*`) — 2f adopts the same shape with prefix `FIXPP_ERR_SYNC_*`.
> - `[2d §4.5]` `SessionConfig` — per-session PMR resource (`session_arena`) consumed by 2f's fallback path; `lock_policy` enum recorded but not consumed by 2f directly (the store-write callsite cap in `[const §XI.5]` is what binds 2f to the store path).
> - `[2d §4.7]` Cancellation propagation API — two-phase close + per-mode effect table; the row at line 804 (`async_mutex::lock`) is 2f's inherited contract: phase-1 graceful runs the wait normally, phase-2 cancels with `operation_aborted`, `terminal` cancels with `operation_aborted`, `partial` is dropped from v1.0.
> - `[2d §4.8]` `fixpp::core::session_executor` — project-owned wrapper class (NOT an alias to `any_io_executor`); 2f's awaitable completes on the awaiter's bound `session_executor` for in-session callers.
> - `[2d §6.5]` `cancellable_dispatch` primitive — 2f's `async_lock` is **NOT** this primitive; it is the lower-level mutex the primitive uses internally for any internal serialisation. 2f's cancellation outcome (`error::sync_lock_aborted`) joins `[2d §6.7]`'s `dispatch_aborted` and `clock_sleeps_cancelled` in the `FIXPP_ERR_CANCELLED` group.
> - `[2d §6.7]` C-ABI coalescing precedent (`FIXPP_ERR_THREAD_*`); 2f introduces `FIXPP_ERR_SYNC_*` as a peer.
> - `[2d §7.4]` **Locked contract surface** for 2f — the executor-compat surface 2f must satisfy:
>   - completion on the awaiter's bound executor;
>   - honour `cancellation_type::total` (waiter removed from LIFO list, completes with `operation_aborted`);
>   - `dispatch` vs `post` policy with default `dispatch`; per-mutex override is 2f's call.
> - `[2d §10] Q1` — `async_lock` signature DEFERRED to 2f; contract locked at `[2d §7.4]`. **2f closes this here in §4.1.**
> - `[2e §3.1]` Inherited primitives — store-write mutex is a `fixpp::sync::async_mutex`; 2e is 2f's first downstream consumer.
> - `[2e §4.2]` `MemoryStore` — single per-instance `fixpp::sync::async_mutex` per `[const §XI.3]`; the writer mutex per `[2e §6.4]`.
> - `[2e §6.4]` Writer mutex contract — every `MessageStore` mutating method serialises on the per-store-instance `fixpp::sync::async_mutex`; **2f sign-off is the named hard hand-off gate** for 2e implementation per `[2e §3.1]`.
> - `[2e §10] Q8` — 2f signature deferred; **2f closes this here in §4.1.**
> - `[const §VI.5]` Spec Coverage Discipline — exact-citation rule; this appendix's structure obeys it.
>
> **Cites:** `[SYN §3.2 Q6a]`, `[SYN §3.2 Q6b]`, `[const §XI.3]`, `[const §XV.9]`, `[const §VIII.5]`, `[const §XI.6]`, `[2d §7.4]`. Per `[const §VI.5]` the Normative References section is bound to spec sources (`[FIX-SL §...]`, `[FIXT §...]`, `[FIXS §...]`); **2f's primary drivers are engineering judgment, not a specific FIX spec section, and no `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies.** This is recorded in Appendix B per the precedent set by `architecture.md` Appendix B's closing note (line 678) and `[2d Appendix B] §B.2`.
>
> **Catalogue rows owned:** **NFR-016** (NEW row) — Awaitable mutex `fixpp::sync::async_mutex`. Drop-in language for `library/spec/feature-catalogue.md` and `library/spec/coverage-index.md` is in §11; the orchestrator applies the amendment at sign-off per `[2d §11]` precedent (the rewrite agent does not edit those files in this draft). Appendix A claims the row.
>
> **Convergence log pointer:** see Appendix C; populated after Gate A reviews. v0.1 is the pre-Gate-A draft.

---

## 1. Goals

1. **Deliver `fixpp::sync::async_mutex` per the six-item design list** in `[SYN §3.2 Q6b]`: (1) waiter embedded in the awaiter object; (2) PMR-aware fallback for type-erased completion handlers; (3) ASIO cancellation slot support (`cancellation_type::total` removes the waiter and completes with `operation_aborted`); (4) per-mutex `dispatch` vs `post` policy with default `dispatch`; (5) safe destructor semantics; (6) tests covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters policy, contention stress, TSan + ASan clean. Each item lands at a named subsection in §4 or §6 and a named test seam in §9.

2. **Honour the executor-compat contract from `[2d §7.4]`** — the `async_lock` awaitable completes on the awaiter's bound executor (the `session_executor` wrapper for in-session callers per `[2d §4.8]`); cancellation flows through ASIO native slots; `dispatch` is the default completion policy, `post` is the per-mutex opt-in. The contract is locked at 2d's level; 2f's job is the signature, the internals, and the test seams that prove the contract is satisfied.

3. **Unblock `[2e §6.4]` writer-mutex contract and `[const §XI.3]` mandate.** The store-write path (per `[const §XI.5]`'s callsite cap) and any post-v1 store/audit/replication impl that wants in-coroutine mutual exclusion all consume `fixpp::sync::async_mutex`; v1.0 ships seqnum-counter, store-writer, and pinset-rotation use cases (per `[SYN §3.2 Q6b]`). 2f sign-off is the named hand-off gate for 2e implementation per `[2e §3.1]`.

### 1.1 Magnitude domain — what `async_mutex` is sized for

- **Worst-case waiter depth.** v1.0 hot paths (seqnum counter, per-store writer mutex, pinset rotation) all run inside a single session serialisation domain (per `[2d §4.8]`'s wrapper-class shape under both threading modes). Under that discipline contention is **structurally zero**: at most one coroutine ever calls `async_lock` on a given mutex instance at a time, so the `async_lock` always takes the uncontended fast path and the LIFO waiter list is empty in steady state. Worst-case waiter depth on any v1.0 hot path is therefore **O(1)** (one in-flight, none queued); the mutex is defence-in-depth against a session-serialisation-domain violation (the user's `direct_executor` attestation is incorrect, the FSM has a strand-cross-over bug, or a future post-v1 impl posts work onto a foreign executor without rebinding). **Cross-domain pathological case** (deliberate stress under §9 seam **"Contention stress (≥10⁴ coroutines)"**): O(N) with N = number of concurrent acquirers; each waiter is one cache-line node in the LIFO list, total memory ≈ N × 64 B. The mutex's atomic state is O(1) regardless of N.

- **Embedded-waiter size budget — one cache line, ≈ 64 B.** The awaiter object lives in the caller's coroutine frame; its layout is: `async_mutex* owner_` (8 B) + intrusive `next_` pointer for the LIFO list (8 B) + `std::coroutine_handle<>` continuation (16 B on most ABIs — pointer + a non-zero promise offset slot) + `asio::cancellation_slot` registration storage (≈ 16 B — `std::function<void()>`-shaped cancel handler bound to the slot) + `bool completion_policy_` (1 B) + alignment padding to a cache line (≈ 16 B). Total ≤ 64 B — fits one cache line, fits in any reasonable coroutine frame's free space. The size is named here so HALO-firing analysis (§6.4) can cite it.

- **Cancellation slot integration cost.** Per the awaiter's `await_suspend` contract (§4.2), the cancellation handler is registered exactly once on suspend and de-registered exactly once on resume — no per-poll overhead. The handler is small (≈ 32 B closure: `async_mutex* + waiter*`) and is stored alongside the awaiter inside the coroutine frame so it is HALO-friendly by the same argument as the awaiter itself. Registration cost: one atomic write to the cancellation state's slot list (≤ 5 ns on warm cache). De-registration cost: identical. Both are charged into the §6.3 contended-enqueue Tier 1 ceiling.

### 1.2 Scope boundary — what 2f owns vs what it doesn't

2f **OWNS**:

- The `fixpp::sync::async_mutex` class definition (header location, atomic state encoding, construction, destruction, copy/move semantics, `async_lock(...)` member, `unlock()` semantics, `try_lock()` semantics).
- The awaiter type (`async_mutex::lock_awaiter`, exposition-only — declared `private` and `friend`-accessible to `async_mutex`; not part of the user surface beyond the `async_lock` return-shape contract).
- The RAII guard (`async_lock_guard`) plus the `release()` / `unlock()` discipline.
- The executor-compat surface satisfying `[2d §7.4]`: completion on the awaiter's bound executor, `cancellation_type::total` honoured, `dispatch` default with per-mutex `post` override.
- The PMR-aware fallback path for type-erased completion handlers (§4.3).
- The destructor semantics (§4.7) — pre-conditioned destructor + debug assert.
- The CI enforcement mechanism for `[const §XV.9]` (§6, §9 seam **"`std::mutex`-in-coroutine-context CI gate"**).
- The test seams that cover the (3), (5), and (6) items of the six-item list (cancellation, destructor-with-waiters, the test list itself).

2f **does NOT own**:

- `async_shared_mutex` / `async_recursive_mutex` / `async_timed_mutex` — out-of-scope per `[SYN §3.2 Q6b]` and `[2e §1.1]`'s exclusive-only retire (§2 non-goals).
- The consuming sites: the seqnum counter (owner = Phase-4 session-module spec); the store writer mutex contract (`[2e §6.4]`); pinset rotation (owner = `2g`).
- The `cancellable_dispatch` primitive (`[2d §6.5]`) — 2f's `async_lock` is the lower-level mutex `cancellable_dispatch` may use internally for any internal serialisation; it is **not** the same primitive.
- The `session_executor` wrapper-class shape (`[2d §4.8]`) — 2f consumes the wrapper as the awaiter's bound executor type, but does not define it.
- The C ABI shape for any of the above — 2f is C++ only (§5).

---

## 2. Non-goals

The following are **out of scope** for v1.0 and are explicitly retired here so the next reviewer does not waste a finding asking for them:

- **No `async_shared_mutex` / RW-mutex.** Per `[SYN §3.2 Q6b]` the v1.0 use cases (seqnum counter, store writes, pinset rotation) need only the basic exclusive form; per `[2e §1.1]` 2e has already retired the RW-mutex variant in favour of exclusive-only. Post-v1 follow-up if an audit/replication consumer surfaces.
- **No `async_recursive_mutex`.** Per `[const §XV.9]` (no carve-out) and `[SYN §3.2 Q6b]` (recursion is not a v1.0 use case). 2e v0.4 explicitly removed the v0.1 "transitional `std::recursive_mutex` adapter" per `[2e §7.4]`; 2f confirms there is no recursive variant.
- **No `async_timed_mutex`.** Timed acquire (`try_lock_for`, `try_lock_until`) is not a v1.0 use case; the Phase-4 session-module spec's heartbeat / SendingTime windows use `Clock::sleep_until` per `[2d §4.1]`, not a timed mutex. Post-v1 if a consumer surfaces.
- **No fairness modes beyond LIFO-pop + FIFO-drain.** The cppcoro / Lewis-Baker algorithm gives FIFO fairness *within a drain cycle* (the LIFO list is reversed on `unlock`). Adding a strict-FIFO mode (e.g., a ticket-lock alternative) would need a separate algorithm; v1.0 ships LIFO-push + FIFO-drain only. The `dispatch` vs `post` policy (item 4) covers the HFT/fairness-sensitive sites' need.
- **No pluggable allocator beyond the inherited per-session PMR resource.** `async_mutex` itself does not carry a `memory_resource*` field; the PMR fallback path (§4.3) draws from the awaiter's bound `session_executor`'s per-session resource (per `[2d §4.5]` / `[2d §8]`). A future post-v1 audit-tee mutex with its own arena is a different type, not a knob on this one.
- **No spinning fallback.** Per `[const §XI.5]` the store-write path always uses mutex regardless of `SessionConfig::lock_policy`; 2f is the lock for that path and cannot spin (the store-write path has unbounded disk-wait). Per `[2d §7.4]` cancellation must work; a spin loop without a cancellation check is incompatible. 2f provides no `lock_policy::spin` shape.
- **No `co_await std::mutex` adapter.** Banned by `[const §XV.9]`; 2f provides no carve-out, no transitional wrapper, no escape hatch. The CI gate (§9) refuses any header under `include/fixpp/...` that contains both `<mutex>` (or `std::mutex`) and `asio::awaitable<...>`.
- **No extension-point `concept` / CRTP for user-derived async-mutex types in v1.0.** `async_mutex` is a closed, value-typed, non-virtual class. Users who want a custom mutex shape ship their own type; they do not subclass or specialise this one. Per `[arch §6]` plugin pattern review: `async_mutex` is **NOT** a plugin (no virtual interface user code subclasses); the ≤5 pure-virtual rule does not bind here. §3 records this explicitly.

---

## 3. Inherited surface

Per `[const §VI.5]`, every primitive 2f leans on must trace to its owning section. This section is the exhaustive enumeration; Codex docks points for missing inherits, and the precedent from `[2e §3.1]` is to enumerate everything.

From `[const §XI.3]` (Concurrency & Coroutines — Awaitable mutex required in coroutine context):

> Awaitable mutex required in coroutine context. `fixpp::sync::async_mutex` (own implementation, BSL-1.0 algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.** Enforced by clang-tidy custom check or grep gate.

**Implication for 2f:** this is the direct mandate; the entire doc operationalises this article. The grep gate / clang-tidy custom check is named in §6 / §9 seam **"`std::mutex`-in-coroutine-context CI gate"**.

From `[const §XI.5]` (Hot-path lock policy):

> Hot-path lock policy: per-session policy with hard-coded callsite caps. Default = mutex. Spin opt-in via session config. Store-write path always uses mutex regardless of policy.

**Implication for 2f:** the store-write callsite cap means `fixpp::sync::async_mutex` is the lock type on the v1.0 store-write hot path regardless of `SessionConfig::lock_policy`. 2f therefore inherits the hot-path zero-allocation discipline (`[const §VIII.5]`) on the contended path, not just the uncontended path. §6.1 / §6.4 carry this through.

From `[const §XI.6]` (HALO-first frame allocation):

> Coroutine frame allocation: HALO-first. PMR fallback per-awaiter where HALO doesn't fire.

**Implication for 2f:** the awaiter (item 1 of the six-item list) is HALO-eligible by construction; the PMR fallback path (item 2) consumes the per-session `memory_resource` from the awaiter's bound `session_executor`. §4.3 spells the mechanism precisely.

From `[const §XV.9]` (Banned Patterns — `std::mutex` in coroutine context):

**Implication for 2f:** 2f is the *only* legal mutex shape for any header that includes `asio::awaitable<...>`. The CI enforcement mechanism (named §6 / §9) is 2f's deliverable, not just the mutex itself.

From `[const §VIII.5]` (Allocator policy on the hot path):

> Allocator policy on the hot path: zero `new`/`delete` between parse and `fromApp` callback.

**Implication for 2f:** the contended-acquire path on the store-write mutex sits *outside* the parse → `fromApp` window (the store-write fires post-`fromApp` on the outbound dispatch chain), but the constitutional discipline is symmetric: the contended path must satisfy "zero global-heap allocation" to be a viable choice. The waiter-embedded design (item 1) achieves this by construction; the PMR fallback (item 2) achieves it by drawing from the per-session arena. **`async_mutex` ships zero global `new`/`delete` on every code path** including the contended one. §6.1 records this.

From `[arch §3]` (Public Namespaces) — `fixpp::sync` lives under `core/` physically:

> `fixpp::sync` | `async_mutex`, future awaitable utilities | `core` | Lives under `core` physically; named separately to keep its discipline visible.

**Implication for 2f:** the header lives at `include/fixpp/core/sync/async_mutex.hpp`, not `include/fixpp/sync/async_mutex.hpp`. The namespace is `fixpp::sync` (consistent with `[arch §3]`).

From `[arch §4.1]` (`core` public surface):

> `fixpp::sync::async_mutex` — owned by **2f**. The only mutex shape allowed in coroutine context `[const §XI.3]`.

**Implication for 2f:** the row exists; 2f delivers the detail.

From `[arch §5.1]` (Executor model):

- `asio::any_io_executor` primitive; engine never picks a concrete executor.
- Per-session strand (under `per_session_strand`) or attested executor (under `direct_executor`) — 2d's `session_executor` wrapper holds either.
- `asio::awaitable<T>` is the return type of every async session/transport entry point.
- ASIO native cancellation slots end-to-end.
- No `co_await` of `std::mutex`.
- HALO-first frame allocation; PMR fallback per-awaiter.

**Implication for 2f:** the `async_lock` return type is `asio::awaitable<expected_t<async_lock_guard>>`; the awaitable completes on the awaiter's bound executor (which is a `session_executor` wrapper for in-session callers); cancellation flows through ASIO slots; the awaiter is HALO-eligible.

From `[arch §5.2]` (Allocator policy):

- Public API is PMR-aware; per-session `memory_resource*` carried on `SessionConfig`.
- Hot-path discipline: zero `new`/`delete` between parse and `fromApp`.

**Implication for 2f:** PMR fallback path consumes the per-session resource; no global heap on any code path.

From `[arch §5.5]` (Lifetime model):

- Owned types follow standard value semantics; copy is deleted, move is enabled where natural.
- `[[clang::lifetimebound]]` on view-returning constructors and accessors.

**Implication for 2f:** `async_mutex` is non-copyable and **non-movable** (a locked-mutex move is unsafe; an unlocked-mutex move is safe but the awaiter's `owner_` back-pointer would have to be patched, which is brittle — see §4.1 for the justification). `async_lock_guard` is movable and carries `[[clang::lifetimebound]]` on its constructor.

From `[arch §6]` (Plugin Pattern — ≤5 pure-virtual rule):

**Implication for 2f:** `async_mutex` exposes **zero pure-virtual methods** because it is a closed, value-typed, non-virtual class. It is **not** a plugin interface; the ≤5 cap does not bind. §3 / §1.2 / §2 record this; the next reviewer should not waste a finding asking for a plugin justification.

From `[arch §10]` row 2f (handoff):

> 2f — Awaitable mutex — `fixpp::sync::async_mutex` (six-item design list per `[SYN §3.2 Q6b]`) — Cross-cutting hooks: §5.1; §4.1 surface.

**Implication for 2f:** the row exists; 2f delivers it.

From `[arch §11]` row 2 (HALO firing on inbound dispatch path):

> Coroutine HALO firing on inbound dispatch path across our compiler matrix. Owner: **2d**, **2f**. Disposition: Verify by spike `[SYN §3.2 Q6]`.

**Implication for 2f:** 2f co-owns the HALO-firing verification spike with 2d. The spike covers the dispatch path's awaiter (2d's `cancellable_dispatch` node) and the lock-acquire awaiter (2f's `lock_awaiter`); both must elide on the default Linux/Clang, Linux/GCC, Windows/MSVC toolchains for the budget to hold. §10 Open Questions tracks this as a still-open spike.

From `[SYN §3.2 Q6a]` (Cancellation propagation model — DECIDED — ASIO native cancellation slots end-to-end):

**Implication for 2f:** `cancellation_type::total` removes the waiter from the LIFO list and completes with `operation_aborted`; no parallel `stop_token`. §4.5 spells the per-type behaviour.

From `[SYN §3.2 Q6b]` (Awaitable mutex — DECIDED — own implementation in `fixpp::sync`):

**The six-item design list is the operating spec for §4 / §6. Verbatim, with section pointers:**

1. **Waiter embedded in the awaiter object** (cppcoro-style), not heap-allocated. → §4.2.
2. **PMR-aware fallback** for the rare cases where embedding isn't possible. → §4.3.
3. **ASIO cancellation slot support** — `cancellation_type::total` removes the waiter, completes with `operation_aborted`. → §4.5.
4. **`dispatch` vs `post` policy** on completion, configurable per-mutex (default `dispatch`). → §4.6.
5. **Safe destructor semantics** — drain or assert; spec it explicitly. → §4.7.
6. **Tests** covering FIFO fairness, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan. → §9 (seams 3, 4, 5, 6, 7, 8).

From `[2a §4.2]` (`trap_throw` pattern — the no-terminate-on-PMR-throw mechanism):

**Implication for 2f:** the PMR fallback path's `allocate(...)` throw routes through `fixpp::core::detail::trap_throw` and surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}`. §4.3 / §6.5 spell this.

From `[2a §6.5]` (Tier 1 latency-ceiling idiom):

**Implication for 2f:** §6.3 mirrors the per-component breakdown style for the four `async_lock` / `unlock` ceilings.

From `[2b §6.4]` (lifetime-bound flyweight discipline):

**Implication for 2f:** `async_lock_guard` is a flyweight bound to `async_mutex`'s lifetime; `[[clang::lifetimebound]]` on the constructor surfaces caller-side misuse (e.g., binding to a temporary).

From `[2c §6.7]` (per-doc-prefix discipline):

**Implication for 2f:** the C-ABI coalescing prefix is `FIXPP_ERR_SYNC_*` (peer of `FIXPP_ERR_DECIMAL_*` / `FIXPP_ERR_WIRE_*` / `FIXPP_ERR_DICT_*` / `FIXPP_ERR_THREAD_*` / `FIXPP_ERR_STORE_*`). §6.5 introduces it; §5 confirms the C-ABI surface is delegated.

From `[2d §4.5]` (`SessionConfig` field list):

- `executor_override` (nullable) → resolved executor reaches 2f via the awaiter's bound `session_executor`.
- `lock_policy` (recorded; not consumed by 2f directly — `[const §XI.5]` callsite cap binds 2f to the store path regardless).
- `clock_override` / `effective_clock` per `[2d §7.9]` — not consumed by 2f's v1.0 surface (no timed acquire); recorded for forward-compat with a post-v1 `async_timed_mutex`.
- `session_arena` (`SessionConfig::session_arena` per `[2d §8]`) — the PMR resource the fallback path draws from.

From `[2d §4.7]` (Cancellation propagation API — two-phase close + per-mode effect table):

The row at line 804 (`async_mutex::lock`) is 2f's inherited contract:

| `graceful` (phase 1) | `graceful` (phase 2) | `terminal` |
|---|---|---|
| runs | cancelled (`operation_aborted`) | cancelled |

**Implication for 2f:** during phase 1 of graceful close, in-flight `async_lock` waiters continue to run (they are NOT pre-cancelled by phase 2's eventual root total — phase 1 uses a child cancellation state). Phase 2's root `cancellation_type::total` propagates to in-flight `async_lock` waiters; 2f removes them from the LIFO list and completes with `operation_aborted` per item (3) of the six-item list. `terminal` close skips phase 1 and goes directly to the same total-cancel behaviour. `partial` is **dropped** from v1.0 per `[2d §4.7]`'s effect-table footer; 2f matches the same retirement.

From `[2d §4.8]` (`fixpp::core::session_executor` — project-owned wrapper class):

**Implication for 2f:** the awaiter's bound executor for in-session callers is a `session_executor` value (not an `asio::any_io_executor` — round-3 root cause #1). 2f's `async_lock` awaitable completes by binding to the executor returned from `co_await asio::this_coro::executor`, which **is** the wrapper for in-session callers. The PMR fallback path (§4.3) reaches the per-session `memory_resource` through the wrapper's `session_ptr()` member-function accessor (the same mechanism `[2d §4.6]`'s `session_local<T>` uses), then through `Session*` to the session's stored `SessionConfig::session_arena`.

From `[2d §6.5]` (`cancellable_dispatch`):

**Implication for 2f:** 2f's `async_lock` is **NOT** `cancellable_dispatch`. `cancellable_dispatch` is a higher-level primitive that posts a handler to a session executor with cancellation-aware reaping; `async_lock` is the lower-level mutex `cancellable_dispatch` (or any other engine internal that needs a lock) may use as a building block. The two primitives compose; they are not the same shape.

From `[2d §6.7]` (C-ABI coalescing precedent):

**Implication for 2f:** `error::sync_lock_aborted` (§6.5) joins `[2d §6.7]`'s `dispatch_aborted` and `clock_sleeps_cancelled` in the `FIXPP_ERR_CANCELLED` group at the C ABI; runtime errors take the new `FIXPP_ERR_SYNC_*` prefix.

From `[2d §7.4]` (executor-compat surface — **the locked contract for 2f**):

> 2d locks the executor-compat surface that `fixpp::sync::async_mutex` (2f) must satisfy:
> - The mutex's `async_lock` awaitable must complete on the awaiter's bound executor, not on a foreign executor. If the awaiter is bound to the session strand, the completion runs on the strand.
> - The mutex must honour `asio::cancellation_type::total` per `[SYN §3.2 Q6b]` item 3 — when cancellation is signalled, the waiter is removed from the LIFO list and completes with `operation_aborted`.
> - The mutex's `dispatch` vs `post` policy (per `[SYN §3.2 Q6b]` item 4) defaults to `dispatch`. Choosing `post` adds an executor hop on completion; HFT/fairness-sensitive sites pick this. 2d locks the *default* as `dispatch`; per-mutex override is 2f's call.

**Implication for 2f:** §4.1 / §4.5 / §4.6 each implement one bullet; §9 has named seams (#11 "Executor-compat", #4 "Cancellation mid-wait", #12 "`dispatch` vs `post` policy effect on completion") that prove satisfaction.

From `[2d §10] Q1` (signature deferred to 2f; contract locked at §7.4):

**Implication for 2f:** **2f closes this open question in §4.1** by picking the signature `awaitable<expected_t<async_lock_guard>> async_lock(...)`.

From `[2e §3.1]` (Inherited primitives — store-write mutex):

| `fixpp::sync::async_mutex` | mutex class | **2f** (not yet drafted) — contract via `[2d §7.4]` executor-compat surface. Hand-off gate: 2f sign-off required before 2e implementation. | §6.4. |

**Implication for 2f:** **2f sign-off is the named hard hand-off gate for 2e implementation.** 2f delivering this contract unblocks 2e's tasks layer.

From `[2e §6.4]` (Writer-mutex contract on `MessageStore`):

> Every `MessageStore` mutating method serialises against the per-store-instance writer mutex regardless of `SessionConfig::lock_policy`. The mutex is `fixpp::sync::async_mutex` (per `[const §XI.3]`).

**Implication for 2f:** the mutex is *per-store-instance*, not per-session; one `async_mutex` is held by each `MessageStore` impl (`MemoryStore`, `FileStore`, custom). 2f does not constrain how many instances the engine holds — the consumer owns the lifetime.

From `[2e §10] Q8` (2f signature deferred):

**Implication for 2f:** **2f closes this open question in §4.1** with the same `async_lock` signature that closes `[2d §10] Q1`.

### 3.1 Inherited primitives — exhaustive list

| Primitive | Origin | Hand-off form | Used in 2f §… |
|---|---|---|---|
| `asio::awaitable<T>` | `[const §XI.1]` / `[arch §5.1]` | ASIO type | §4.1 (return type), §4.2 (awaiter type). |
| `asio::cancellation_slot` | `[const §XI.2]` / `[SYN §3.2 Q6a]` | ASIO type | §4.2 (awaiter wires the slot), §4.5 (per-type behaviour). |
| `asio::cancellation_type` | `[const §XI.2]` / `[2d §4.7]` | ASIO type | §4.5 (per-type behaviour). |
| `expected_t<T>` | `[arch §4.1]` / `[arch §5.3]` | template alias = `std::expected<T, fixpp::core::error>` | §4.1 (return type), §6.5 (variants). |
| `fixpp::core::error` | `[arch §5.3]` | tagged enum | §6.5 (error variants). |
| `fixpp::core::session_executor` (project-owned wrapper class) | `[2d §4.8]` v0.4 | value-typed wrapper holding either `asio::strand` (per_session_strand) or attested `any_io_executor` (direct_executor); typed `session_ptr()` member accessor | §4.1 (awaitable's bound executor type), §4.3 (PMR resource recovery), §6.1 (threading). |
| `Session*` accessor on `session_executor` | `[2d §4.8]` v0.4 | public member function | §4.3 (PMR fallback path reaches the per-session resource through the wrapper). |
| Per-session PMR resource (`SessionConfig::session_arena`) | `[2d §4.5]` / `[2d §8]` | `std::pmr::memory_resource*` carried on `SessionConfig` | §4.3 (PMR fallback allocation source). |
| `[[clang::lifetimebound]]` annotation discipline | `[2b §6.4]` / `[2b §6.6]` | C++23 attribute | §4.4 (`async_lock_guard` ctor). |
| `fixpp::core::detail::trap_throw` | `[2a §4.2]` | helper that converts a thrown PMR `bad_alloc` into an `expected_t` failure without terminating | §4.3 / §6.5 (`error::sync_lock_alloc_failed`). |
| `cancellable_dispatch` | `[2d §6.5]` | higher-level primitive that *uses* `async_mutex`-shaped locks internally; called by callers, not internally by 2f | §1.2 (scope boundary). |
| `error::dispatch_aborted` (peer cancellation variant) | `[2d §6.7]` | `expected_t::unexpected` outcome | §6.5 (`error::sync_lock_aborted` joins the same C-ABI group `FIXPP_ERR_CANCELLED`). |

**Hand-off gates:**
- 2f sign-off is the hard hand-off gate for **2e implementation** per `[2e §3.1]`.
- 2f sign-off unblocks **2g** (pinset rotation) and the **Phase-4 session-module spec** (seqnum counter).
- 2d v0.4 is sign-off-applied; 2f consumes the wrapper-class shape from `[2d §4.8]` directly.

This document refines the inherited surface; it does **not** diverge from any sibling-doc contract.

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
// Algorithm: lifted (with BSL-1.0 attribution) from avast/asio-mutex — the
// classic Lewis-Baker / cppcoro lock-free design:
//
//   std::atomic<uintptr_t> state_:
//     state_ == not_locked         (= 1)              — free.
//     state_ == locked_no_waiters  (= 0)              — held, LIFO list empty.
//     state_ == <pointer-to-waiter>                   — held, head of LIFO list.
//
// On lock (uncontended): CAS(state_, not_locked → locked_no_waiters).
// On lock (contended):   CAS(state_, head_old → my_waiter); my_waiter.next_ = head_old.
// On unlock:             exchange(state_, locked_no_waiters); if list non-null,
//                        reverse to FIFO and resume each waiter on its bound
//                        executor under `dispatch` (default) or `post` (per-mutex
//                        override).
//
// The single biggest architectural difference from upstream avast/asio-mutex
// (item 1 of [SYN §3.2 Q6b]'s six-item list): the waiter lives INSIDE the
// awaiter object inside the caller's coroutine frame; no per-acquisition
// `new`/`delete`. PMR fallback per §4.3 for type-erased completion handlers.
namespace fixpp::sync {

// Per-mutex completion policy — item 4 of [SYN §3.2 Q6b]'s six-item list.
// Default `dispatch`; HFT / fairness-sensitive sites pick `post`. Locked at
// the `dispatch` default by [2d §7.4]; per-mutex override is 2f's call.
enum class completion_policy : std::uint8_t {
    // Run the resumed coroutine inline on the unlocking thread when the
    // unlocking thread is already on the resumed coroutine's bound executor
    // (e.g., the session strand the waiter was bound to). Falls through to
    // a post when the executor's `dispatch` semantics demand it (e.g., the
    // unlocking thread is not on the bound executor). The default; matches
    // the [2d §7.4] surface lock.
    dispatch = 0,

    // Always post the resumed coroutine through the bound executor; one
    // executor hop per resumption regardless of caller thread. HFT and
    // fairness-sensitive sites pick this when they want every waiter to
    // pay an identical hop so latency variance across waiters is bounded.
    // Cost: ≈ 25 ns extra per resumption (§6.3).
    post = 1,
};

class async_mutex {
public:
    // Construct an unlocked mutex with the default completion policy.
    constexpr async_mutex() noexcept = default;

    // Construct an unlocked mutex with an explicit completion policy.
    explicit constexpr async_mutex(completion_policy cp) noexcept
        : policy_(cp) {}

    // Non-copyable, non-movable (root cause: a movable mutex would have to
    // patch every in-flight awaiter's `owner_` back-pointer atomically with
    // the move, which is fragile under cancellation. The seqnum counter / store
    // writer / pinset rotation all hold the mutex by-value at a stable address
    // for the lifetime of their owner, so non-movable is no operational cost.
    // Future post-v1 use cases that need movability can switch to a
    // `std::unique_ptr<async_mutex>` shape on the consumer side.).
    async_mutex(async_mutex const&)            = delete;
    async_mutex(async_mutex&&)                 = delete;
    async_mutex& operator=(async_mutex const&) = delete;
    async_mutex& operator=(async_mutex&&)      = delete;

    // Destructor — pre-conditioned per §4.7. Debug builds assert that the
    // mutex is unlocked AND the LIFO waiter list is empty; release builds
    // assume the precondition was met (UB if violated, with the §9 seam
    // **"Destructor-with-waiters policy enforcement"** holding the line).
    // Callers MUST drain the mutex before destruction. The store-writer /
    // seqnum-counter / pinset-rotation v1.0 callsites all hold the mutex at
    // session-or-store lifetime and drain naturally (the session FSM's
    // graceful close completes all in-flight async_lock acquires before
    // destroying the consumer that owns the mutex).
    ~async_mutex();

    // Acquire the mutex. The signature closes [2d §10] Q1 and [2e §10] Q8.
    //
    // Returns an awaitable that completes when the mutex is acquired (with
    // an `async_lock_guard` that releases on destruction) or with
    // `expected_t::unexpected{error::sync_lock_aborted}` if cancellation
    // wins before the acquire linearisation point (per §4.5).
    //
    // The awaitable's completion runs on the awaiter's bound executor per
    // [2d §7.4] — the `session_executor` wrapper for in-session callers. The
    // completion policy (per-mutex `policy_`) decides whether the resumption
    // is `dispatch`-shaped (run inline if already on the bound executor) or
    // `post`-shaped (always one hop) — see §4.6.
    //
    // No completion-token parameter (e.g., `asio::use_awaitable`) on the
    // public surface — the awaiter type is internal; users get the
    // `awaitable<expected_t<async_lock_guard>>` shape directly. Rationale:
    // every existing engine call site uses `co_await mutex.async_lock();`
    // and doesn't care about `use_future` / `as_tuple` / etc. Adding the
    // token parameter would force the awaiter type to be a public template,
    // which inflates the surface for zero gain on the v1.0 use cases. The
    // alternative direct-awaiter shape (`co_await mutex` without an explicit
    // member call) was rejected because the signature has to surface the
    // `expected_t` outcome cleanly — having a named member-function gives
    // the call site one stable place where `[[nodiscard]]` and the cancel
    // contract are documented.
    //
    // Cancellation: see §4.5 per-cancellation_type table. Honours `total`
    // (waiter removed from LIFO list, completes with sync_lock_aborted);
    // `partial` is dropped from v1.0 per [2d §4.7]; `terminal` is treated
    // as `total` per §4.5.
    [[nodiscard]] asio::awaitable<expected_t<async_lock_guard>>
        async_lock() noexcept;

    // Non-blocking try-acquire. Returns true if the mutex was acquired (the
    // caller MUST then release via a matching unlock() call or by
    // constructing an `async_lock_guard` that adopts the locked state — see
    // §4.4). Returns false if the mutex is held. Never suspends, never
    // throws, never allocates. Useful for opportunistic single-thread
    // bookkeeping in tests and for the §9 seam **"Destructor-with-waiters
    // policy enforcement"** drain-on-test path.
    [[nodiscard]] bool try_lock() noexcept;

    // Release the mutex. Drains the LIFO waiter list (atomic exchange to
    // not_locked or locked_no_waiters depending on the list state, then
    // FIFO-reverse and resume each waiter under the per-mutex policy).
    // Public so the test seams and certain internal callsites (e.g., a
    // try_lock() success that doesn't construct an async_lock_guard) can
    // release; the v1.0 hot path should always use async_lock_guard's
    // destructor.
    //
    // Precondition (debug-asserted): the mutex is held (state_ != not_locked).
    // UB in release if violated.
    void unlock() noexcept;

    // Query the current completion policy. Const, noexcept, lock-free.
    [[nodiscard]] completion_policy policy() const noexcept { return policy_; }

private:
    // Algorithm state per the cppcoro shape. The encoding is:
    //   not_locked         := uintptr_t{1}  (any non-null odd value works;
    //                                         we pick 1 so a uintptr that
    //                                         carries a low-bit-set flag
    //                                         remains distinguishable from
    //                                         a real waiter pointer, which
    //                                         is always at least 8-byte
    //                                         aligned).
    //   locked_no_waiters  := uintptr_t{0}.
    //   <pointer-to-waiter>:= the head of the LIFO list (>= 8).
    static constexpr uintptr_t not_locked        = 1;
    static constexpr uintptr_t locked_no_waiters = 0;

    std::atomic<uintptr_t> state_ {not_locked};
    completion_policy      policy_ {completion_policy::dispatch};

    // Friend declaration so the awaiter (declared in detail; fwd-declared
    // here) can manipulate state_ atomically without going through the
    // public unlock() entry point.
    friend class detail::async_mutex_awaiter;
};

}  // namespace fixpp::sync
```

#### 4.1.1 Why `awaitable<expected_t<async_lock_guard>>` over the alternatives

The signature decision closes both `[2d §10] Q1` and `[2e §10] Q8`. Two viable alternatives were considered and rejected:

- **(a) `awaitable<async_lock_guard> async_lock()` — no `expected_t`.** Cancellation would have to surface as `asio::error::operation_aborted` thrown from the awaitable, breaking the `[arch §5.3]` "no exceptions on the hot path" rule. Rejected.

- **(b) `awaitable<async_lock_guard> async_lock(asio::completion_token_for<...> auto token = asio::use_awaitable)` — token-shaped.** Forces the awaiter type to be a public template parameterised on the token; inflates the surface for zero gain on the v1.0 callsites, all of which do `co_await mutex.async_lock();` and never pass a non-default token. Per `[const §XI.1]` the engine standardises on `asio::awaitable<T>` as the composition primitive — the token-shape is unnecessary indirection. Rejected.

- **(c) `co_await mutex` — direct-awaiter shape (the cppcoro idiom).** Requires `async_mutex` itself to be an awaitable; the signature has to surface `expected_t<async_lock_guard>` cleanly under the awaiter's `await_resume()`, which mixes the lock-acquisition surface with the awaiter surface and makes `[[nodiscard]]` non-obvious to the caller (you would discard the awaitable, not the guard). Rejected for documentation clarity reasons; the named member-function (`mutex.async_lock()`) gives one stable place where the `[[nodiscard]]` and cancellation contracts live.

**Chosen:** `awaitable<expected_t<async_lock_guard>> async_lock() noexcept;` — completion on awaiter's bound executor (per `[2d §7.4]`), honours `total` (per `[SYN §3.2 Q6b]` item 3), default `dispatch` (per `[2d §7.4]` lock; per-mutex override per item 4 via the constructor's `completion_policy` argument). The signature is identical regardless of completion-policy because the policy lives on the mutex instance, not in the call.

### 4.2 The waiter awaiter type — embedded in the caller's coroutine frame (item 1)

The waiter is declared in `fixpp::sync::detail::async_mutex_awaiter` (private; not part of the user surface beyond `async_lock`'s return-shape contract):

```cpp
// include/fixpp/core/sync/async_mutex.hpp (continued)
//
// fixpp::sync::detail::async_mutex_awaiter — the waiter object that lives
// INSIDE the caller's coroutine frame. The single biggest architectural
// difference from upstream avast/asio-mutex (item 1 of [SYN §3.2 Q6b]'s
// six-item list).
//
// HALO-eligibility (per [const §XI.6]): the awaiter's storage is embedded in
// the coroutine frame produced by the caller's `co_await mutex.async_lock()`;
// no escape to the heap, no captures of size > the §1.1 64-byte budget. HALO
// elides the coroutine frame's heap allocation when the caller's coroutine
// frame is itself bounded by its caller — the typical hot-path case. The
// HALO-firing verification spike per [arch §11] row 2 is the §10 open
// question; see §10 Q1.
//
// Layout (see §1.1 for the per-field budget; total ≤ 64 B = one cache line):
//   async_mutex*                owner_;
//   detail::async_mutex_awaiter* next_;             // intrusive LIFO link.
//   std::coroutine_handle<>      continuation_;
//   asio::cancellation_slot      cancel_slot_;       // bound at suspend.
//   completion_policy            policy_;
//   std::pmr::memory_resource*   pmr_fallback_;      // null in the embedded path; non-null in §4.3 fallback.
//   /* alignment padding */
//
// Awaiter shape (conceptually):
//
//   bool await_ready() noexcept {
//       // Try the uncontended fast path: CAS state_ from not_locked to
//       // locked_no_waiters. If the CAS succeeds, we own the mutex without
//       // suspending — await_resume is called next without await_suspend.
//       uintptr_t expected = async_mutex::not_locked;
//       return owner_->state_.compare_exchange_strong(
//           expected, async_mutex::locked_no_waiters,
//           std::memory_order_acquire, std::memory_order_relaxed);
//   }
//
//   void await_suspend(std::coroutine_handle<> h) noexcept {
//       continuation_ = h;
//       // Bind the cancellation slot from the awaiter's cancellation_state
//       // and register the cancel handler — single relaxed-atomic write to
//       // the slot's handler slot, ≤ 5 ns warm-cache.
//       auto state = h.promise().get_cancellation_state();  // exposition
//       cancel_slot_ = state.slot();
//       cancel_slot_.assign([this](asio::cancellation_type type) {
//           // §4.5: total → remove from LIFO list, complete with
//           //                error::sync_lock_aborted.
//           //       partial → drop from v1.0 surface (treated as no-op
//           //                 per [2d §4.7]).
//           //       terminal → treated as total per §4.5.
//           if (type == asio::cancellation_type::total ||
//               type == asio::cancellation_type::terminal) {
//               this->cancel();
//           }
//           // partial: no-op.
//       });
//
//       // Push self onto the LIFO list via CAS retry loop.
//       uintptr_t old_state = owner_->state_.load(std::memory_order_acquire);
//       for (;;) {
//           if (old_state == async_mutex::not_locked) {
//               // Race won by us; we got the lock between await_ready's
//               // CAS-fail and now. CAS to locked_no_waiters and resume.
//               if (owner_->state_.compare_exchange_weak(
//                       old_state, async_mutex::locked_no_waiters,
//                       std::memory_order_acquire, std::memory_order_relaxed)) {
//                   cancel_slot_.clear();
//                   continuation_.resume();  // hot resume; HALO-friendly.
//                   return;
//               }
//               continue;  // retry on stale `old_state`.
//           }
//           // The list head (or locked_no_waiters); link self in.
//           next_ = reinterpret_cast<detail::async_mutex_awaiter*>(
//               old_state == async_mutex::locked_no_waiters
//                   ? nullptr
//                   : reinterpret_cast<detail::async_mutex_awaiter*>(old_state));
//           uintptr_t new_state = reinterpret_cast<uintptr_t>(this);
//           if (owner_->state_.compare_exchange_weak(
//                   old_state, new_state,
//                   std::memory_order_release, std::memory_order_acquire)) {
//               return;  // suspended; unlock() will resume us under policy.
//           }
//           // Loop on stale `old_state`.
//       }
//   }
//
//   expected_t<async_lock_guard> await_resume() noexcept {
//       cancel_slot_.clear();
//       if (cancelled_) {
//           // Cancellation won the race per cancel().
//           return expected_t<async_lock_guard>{
//               std::unexpected{error{error_code::sync_lock_aborted}}};
//       }
//       // We own the mutex — return the guard.
//       return expected_t<async_lock_guard>{
//           std::in_place, async_lock_guard{owner_, /*adopt_locked=*/true}};
//   }
//
//   void cancel() noexcept;  // §4.5: walks the LIFO list, removes self,
//                            // then resumes self with cancelled_=true.
```

#### 4.2.1 What happens when `await_ready` returns true (uncontended fast path)

`await_ready` performs one CAS — `state_: not_locked → locked_no_waiters`. On success, `await_suspend` is **not** called and the coroutine never suspends; `await_resume` runs immediately and returns the `async_lock_guard`. **Zero allocation**, **zero cancellation-slot wiring**, **zero atomic operations beyond the single CAS**, no executor hop. The Tier 1 ceiling for this path is ≤ 30 ns warm-cache (§6.3).

The cancellation slot is **not** registered on the fast path — there is no waiter to cancel, and the awaiter completes synchronously before any cancellation could land. This is consistent with `[2d §4.7]` (`async_mutex::lock` runs during phase 1 of graceful close; phase 2's `total` finds nothing to cancel).

### 4.3 The PMR-aware fallback path (item 2)

The waiter-embedded design (item 1) requires the awaiter to live in the caller's coroutine frame. There is one class of callers where that does not hold:

**Type-erased completion handlers** — `asio::any_completion_handler<...>` and any composed operation whose intermediate state is type-erased. When a user's library wraps a `co_await mutex.async_lock()` into a higher-order primitive that erases the awaitable into a generic completion-handler shape, the awaiter object's storage is not the caller's coroutine frame; it is a heap- or PMR-allocated control block managed by the type-erasure machinery.

Other paths where the embedded shape is not directly available — composed operations crossing executor boundaries via `asio::co_spawn` onto a foreign executor that re-enters via `asio::bind_executor`, certain SWIG/Python re-entries through `bindings/python` — fall through to the same fallback path because they end up routing through a type-erased completion handler at the boundary.

#### 4.3.1 The rule

> If the awaiter cannot be constructed in-place in the coroutine frame (because the awaitable is being consumed by a type-erased completion handler that allocates its control block separately), the implementation allocates the awaiter from the per-session `std::pmr::memory_resource` carried by the awaiter's bound `session_executor` — never from the global heap. The resource is acquired via `session_executor::session_ptr()->session_arena()`. Outside any session serialisation domain (engine bootstrap, listener accept, control-plane handlers — i.e., the awaiter's bound executor is **not** a `session_executor` value), the mutex's `async_lock` is **not on the v1.0 hot path** (no v1.0 use case acquires a mutex from outside a session) and the implementation may allocate from `EngineConfig::default_session_resource` per `[2d §4.4]` — the engine-default monotonic resource. Both branches route through `fixpp::core::detail::trap_throw` (per `[2a §4.2]`); allocation failure surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}` (§6.5) without termination.

#### 4.3.2 Mechanism — precise specification

The mechanism (the part Codex will hunt):

1. **Detecting the path.** The implementation detects type-erased completion at the awaitable's `co_await` site by inspecting whether the awaiter's storage can be embedded in the consuming coroutine frame. The compiler-level affordance is the same one HALO uses: when `co_await mutex.async_lock()` is consumed by a type-erased handler, the awaitable's promise type does not have a stable in-frame location and the embedded layout cannot be used. The implementation provides a single `co_await` overload that delegates to a `detail::make_awaiter(this)` factory; the factory tries the embedded shape first (the default), and if the `co_await` site is type-erased the factory falls through to the PMR-allocated shape. (The detection is at the awaitable's `operator co_await()` level, not at runtime; type-erasure is a compile-time fact at the `co_await` site.)

2. **Acquiring the per-session resource.** When the fallback path fires inside a session:
   - `co_await asio::this_coro::executor` returns the awaiter's bound executor — for in-session callers this is a `fixpp::core::session_executor` value (per `[2d §4.8]`).
   - Static type-recovery on the executor (the same mechanism `[2d §4.6]`'s `current_trace_context` awaiter uses to find the session) yields the wrapper.
   - `session_executor::session_ptr()` (the public member-function accessor — round-3 root cause #1) yields the typed `Session*`.
   - `Session::session_arena()` (an engine-internal accessor on the `Session` object that returns the resource the session was opened with — `SessionConfig::session_arena` per `[2d §4.5]`) yields the `std::pmr::memory_resource*`.
   - The fallback path allocates the awaiter from that resource via `std::pmr::polymorphic_allocator<detail::async_mutex_awaiter>{resource}`.

3. **Engine-fallback when there is no session.** When the awaiter's bound executor is **not** a `session_executor` value (engine bootstrap, listener accept, control-plane handlers — i.e., the `[2d §4.6]` "wrapper-type recovery miss" case), the fallback path allocates from `EngineConfig::default_session_resource` per `[2d §4.4]` — reachable via the engine handle the calling code holds. **No v1.0 hot path takes this branch** (every v1.0 use case acquires a mutex from inside a session); the branch is for forward-compat with post-v1 audit / replication / gRPC handlers.

4. **Lifetime of the fallback awaiter.** The awaiter is owned by the type-erasure machinery; on `await_resume` the awaiter de-allocates itself back to the same PMR resource. The de-allocation is a no-op for monotonic resources (the v1.0 default) — the per-session arena resets at session destruction.

5. **No global-heap touch.** Both branches route through PMR resources owned by the engine or session; the global heap is never touched. The §9 seam **"PMR fallback exercise"** verifies via `mallocnesia` interceptor that the fallback path produces zero `new`/`delete`/`malloc` calls.

The fallback path is **strictly opt-in by the caller's shape**: the v1.0 hot path callsites (`async_lock` inside a coroutine bound to the session strand, with no type-erasure at the `co_await` site) take the embedded path by construction. No runtime configuration switch.

### 4.4 The RAII lock guard

```cpp
namespace fixpp::sync {

// async_lock_guard — the RAII handle returned by async_lock's awaitable
// completion. Movable, non-copyable, releases on destruction.
//
// Lifetime contract: bound to the originating async_mutex's lifetime. The
// guard MUST NOT outlive its mutex; the [[clang::lifetimebound]] on the
// constructor surfaces caller-side misuse (e.g., binding to a temporary
// async_mutex). Per [2b §6.4] precedent.
class async_lock_guard {
public:
    // Adopt-locked constructor — used only by async_mutex::async_lock's
    // awaiter (`friend`). User code obtains a guard exclusively via
    // co_await mutex.async_lock() returning the guard wrapped in expected_t.
    async_lock_guard(async_mutex* mutex [[clang::lifetimebound]],
                     bool adopt_locked) noexcept
        : mutex_(adopt_locked ? mutex : nullptr) {}

    // Move semantics: the guard is movable; the source guard becomes empty.
    async_lock_guard(async_lock_guard&& other) noexcept
        : mutex_(std::exchange(other.mutex_, nullptr)) {}
    async_lock_guard& operator=(async_lock_guard&& other) noexcept {
        if (this != &other) {
            release();
            mutex_ = std::exchange(other.mutex_, nullptr);
        }
        return *this;
    }
    async_lock_guard(async_lock_guard const&)            = delete;
    async_lock_guard& operator=(async_lock_guard const&) = delete;

    // Destructor — releases the mutex if owned.
    ~async_lock_guard() noexcept { release(); }

    // Explicit early release — sometimes useful when the user wants to
    // shorten the critical section before the guard's natural end-of-scope.
    // After release(), the guard is empty; subsequent destruction is a no-op.
    void release() noexcept {
        if (mutex_) {
            mutex_->unlock();
            mutex_ = nullptr;
        }
    }

    // Query owner state — true if this guard holds the mutex.
    [[nodiscard]] bool owns_lock() const noexcept { return mutex_ != nullptr; }

private:
    async_mutex* mutex_;
};

}  // namespace fixpp::sync
```

The guard is a flyweight (`sizeof(async_lock_guard) == sizeof(async_mutex*)` = 8 B); per `[arch §5.5]` lifetime-classes discipline it does not own the mutex (`async_mutex` is the owner; the consumer that constructed the mutex is the owner of the mutex). The `[[clang::lifetimebound]]` on the constructor flags caller-side misuse on Clang; partial on GCC; ignored on MSVC per `[arch §5.5]`.

### 4.5 Cancellation contract (item 3)

The cancellation contract follows `[2d §4.7]`'s per-mode effect table at line 804 (the `async_mutex::lock` row); this section spells the per-`asio::cancellation_type` behaviour 2f's awaiter implements.

| `asio::cancellation_type` | 2f awaiter behaviour |
|---|---|
| `total` | Remove the waiter from the LIFO list (atomic LIFO unlink under a small CAS retry loop) and complete the awaitable with `expected_t::unexpected{error::sync_lock_aborted}`. The waiter has **not yet entered the critical section** — the cancellation lands strictly between `await_suspend` and the awaiter's resumption from a previous holder's `unlock()`. The mutex's `state_` is unaffected by the unlink (the unlink rewrites the awaiter's predecessor's `next_` pointer; the head pointer is untouched unless the cancelled waiter is the head, in which case the head pointer CAS-replaces with the cancelled waiter's `next_`). No `unlock()` is implied — the cancelled waiter never held the lock. |
| `partial` | **Dropped from v1.0 surface.** Per `[2d §4.7]` ("`partial` is dropped from v1.0 public surface"), 2f matches the same retirement: a `partial` cancellation signal on a mutex waiter is treated as a **no-op** (the cancellation handler returns without unlinking; the waiter continues to wait). Rationale: `partial` semantics on a mutex acquire are not well-defined — there is no "partial acquisition" of a mutex. The session FSM uses `total` for graceful-close phase 2 and `terminal` for `Session::close(terminal)`; neither path issues `partial`. Future reintroduction would require the per-component effect table extension at sign-off. |
| `terminal` | **Treated as `total`.** Per `[2d §4.7]` ("`terminal` skips phase 1") the closest analogue for an awaitable mutex is "treated as `total`" — the waiter has not yet entered a critical section, so there is nothing to skip-phase-1 over. The behaviour matches `total` exactly: unlink + complete with `error::sync_lock_aborted`. |

#### 4.5.1 What happens to a waiter that has already begun a critical section

This is the question Codex will hunt. The answer is: **a waiter that has begun a critical section is not a waiter** — it is a holder. Cancellation on a holder is **not in 2f's surface** because:

1. The cancellation slot is *only* registered while the awaiter is suspended in `await_suspend` (per §4.2 — `cancel_slot_.assign(...)` happens at suspend, `cancel_slot_.clear()` happens on resume). Once `await_resume` returns the `async_lock_guard`, the slot is cleared and the holder's coroutine continues without any cancellation-on-mutex contract.
2. Cancellation on the holder's *outer* coroutine is a different concern: ASIO's `cancellation_type::total` propagates to the holder's next `co_await` checkpoint — not to the mutex it holds. The holder is responsible for releasing the mutex before suspending on a cancellable op, which the RAII guard discipline (§4.4) enforces by construction (the guard releases on scope exit; if the holder's coroutine is cancelled and unwinds, the guard's destructor runs and releases the mutex).
3. Therefore, item (3) of the six-item list explicitly says "removes the waiter from the linked list" — it is purely a wait-list removal, not a critical-section abort.

The §9 seam **"Cancellation mid-wait"** verifies all three rows. The §9 seam **"`std::mutex`-in-coroutine-context CI gate"** prevents users from accidentally introducing the alternative (a `std::mutex` whose holder's coroutine could be cancelled and leave the mutex permanently locked); 2f's RAII guard makes the analogous problem unreachable on its surface.

### 4.6 `dispatch` vs `post` policy (item 4)

Per `[2d §7.4]`, the default is `dispatch`; per-mutex override is 2f's call.

#### 4.6.1 How the policy is set

**Constructor argument.** The `completion_policy` enum is passed to the `async_mutex` constructor; defaults to `completion_policy::dispatch`. **Not** a template parameter (because the consumer holds the mutex by value at a stable address; templating would force every consumer to choose the policy at compile time and would break the `MessageStore` contract where the policy might want to differ between `MemoryStore` (test, dispatch is fine) and `FileStore` (production, possibly `post` for fairness). **Not** a runtime field that the user can flip mid-life — the policy is frozen at construction (the `policy_` member is `const`-initialised by the constructor; no setter). Mid-life flip would require a memory-ordering audit that the v1.0 surface declines.

```cpp
async_mutex m1;                                  // default — completion_policy::dispatch.
async_mutex m2{completion_policy::dispatch};     // explicit dispatch.
async_mutex m3{completion_policy::post};         // HFT/fairness-sensitive site.
```

#### 4.6.2 Cost difference

- **`dispatch`** — when `unlock()` runs on the same executor the resumed waiter is bound to (the typical hot-path case: the unlocking coroutine is on the session strand, the waiter is on the same strand), the waiter's continuation runs **inline** on the unlocking thread. No executor hop. Cost: ≈ 0 ns above the `unlock()`'s own atomic exchange (§6.3 row "`unlock` contended").
  - When `unlock()` runs on a different executor than the waiter's bound (cross-thread unlock — happens only in pathological / test scenarios under v1.0's single-domain discipline), `dispatch` falls through to a `post` (ASIO's standard `dispatch` semantics).
- **`post`** — every resumption goes through one `asio::post(executor, handler)` hop, regardless of caller thread. Cost: ≈ 25 ns extra per resumption (§6.3 row "Cross-strand `dispatch` handoff" precedent from `[2d §6.3]` — though the value is 25 ns warm-cache, not 250 ns; the cross-thread case has its own §6.3 ceiling). HFT / fairness-sensitive sites pick `post` so every waiter pays the same cost (no advantage to the waiter that happens to be co-scheduled with the unlocking thread).

The v1.0 default is `dispatch`; 2e's writer mutex on `MemoryStore`/`FileStore` keeps the default. A future high-frequency seqnum counter or pinset-rotation site may pick `post`; that is a per-callsite decision documented at the consumer's design level, not 2f's.

### 4.7 Destructor semantics (item 5)

The choice from the brief's three options:

- **(a) Drain (debug + release).** Destructor blocks until the LIFO list is empty. **Rejected** — the destructor runs synchronously (no `co_await` available); blocking on a coroutine drain inside a destructor introduces a re-entrancy hole (the waiter's resumption can land on the same thread that is destructing the mutex). Worse, "blocks until drained" without a deadline gives the consumer no exit when a waiter's executor has stopped — the destructor hangs.
- **(b) Assert (debug) + drain (release).** Debug fires `assert(no_waiters)`; release silently drains. **Rejected** — the silent release-mode drain is exactly the upstream avast/asio-mutex's silent UB-in-release pathology that `[SYN §3.2 Q6b]` item 5 cited as unacceptable. The "wakes waiters with `operation_aborted` or just leaks" decision in release mode is unspecifiable without a detailed memory-ordering proof, and either choice is a bad surprise.
- **(c) Pre-conditioned destructor.** The user MUST drain the mutex before destruction; debug-only assert; release UB if violated; documented as a precondition.

**Chosen: (c) — pre-conditioned destructor.**

#### 4.7.1 Mechanism

```cpp
async_mutex::~async_mutex() {
    // Precondition (debug-asserted): the mutex is fully drained.
    //  - state_ == not_locked  (no holder, no waiters).
    //  - The consumer has cancelled or completed every in-flight async_lock
    //    waiter before allowing the owning object to be destroyed.
    assert(state_.load(std::memory_order_acquire) == not_locked &&
           "fixpp::sync::async_mutex destroyed with waiters or while held; "
           "drain the mutex before destruction (e.g., complete graceful close "
           "before destroying the consumer that owns this mutex).");
    // Release: no operation. UB if the precondition is violated.
}
```

**Per build mode:**

| Build | Behaviour on `~async_mutex()` with waiters or held |
|---|---|
| Debug (`assert` enabled) | Fires `assert` and aborts — visible bug, immediate stack trace, no silent corruption. |
| Release (`assert` disabled) | UB — the destructor returns; the waiters' coroutine handles point at freed memory; their resumption (if any subsequent `unlock()` is called on the dead mutex, or if a cancellation slot fires) accesses a destroyed object. |

#### 4.7.2 Why this shape

1. **The precondition is satisfiable in v1.0 by construction.** Every v1.0 consumer holds the mutex at session-or-store lifetime; the session FSM's graceful close (per `[2d §4.7]`) drains every in-flight `async_lock` waiter (phase 2's `total` cancels them, completing each with `error::sync_lock_aborted`) **before** destroying the owning object. The store-write mutex on `[2e §4.2]` / `[2e §4.3]` is owned by the `MessageStore` impl, which is owned by the `Session` via `unique_ptr<MessageStore>`; the session's drain ordering (per `[2e §6.2.1]`) guarantees the mutex is unlocked before the `~MessageStore()` call.
2. **The debug assert holds the invariant.** Per `[const §VII]` testing discipline, the §9 seam **"Destructor-with-waiters policy enforcement"** exercises a deliberately-violating shape (a test that destructs a mutex while a waiter is parked) and verifies the debug-build assert fires.
3. **The release UB is documented.** Users who write a custom consumer that destroys the mutex without draining are violating a documented precondition; the release-mode UB is the same shape as `std::vector::operator[](out_of_range)` UB — the standard library accepts this shape because the precondition is testable in debug.
4. **Drain is unsafe inside a destructor.** A drain that blocks the destructor thread would have to re-enter the executor that the cancelled waiter is bound to, which can be the same executor the destructor is called from (the session strand under v1.0's single-domain discipline). The re-entrancy is a deadlock waiting to happen.
5. **Alternative (b)'s silent release-mode drain is incompatible with `[const §VIII.5]` zero-allocation discipline.** A drain inside a destructor would have to allocate a temporary list to walk the LIFO under cancellation; the only allocator available at destruction time is the global heap (the per-session arena may already be partially destructed). 2f's discipline does not admit a global-heap touch even in destructors.

The contract summary:

> **`async_mutex` precondition for destruction:** the mutex is unlocked (`state_ == not_locked`) and has no waiters in its LIFO list. Debug builds fire an `assert` if violated; release builds are UB. The session FSM's graceful close (per `[2d §4.7]`) satisfies the precondition by construction; users implementing custom consumers MUST drain the mutex before destruction, e.g., by completing all in-flight `async_lock` awaiters (cancellation path in graceful phase 2 is the canonical pattern).

---

## 5. Public C ABI

**Delegated to 2i.** `fixpp::sync::async_mutex` is C++ only and does not cross the C ABI boundary. Per `[const §X]` and `[arch §4.10]`, the C ABI cannot expose templates or coroutine types; an `awaitable<expected_t<async_lock_guard>>` is both. 2f records that no symbol or type is owed to 2i.

The error variants 2f introduces (§6.5) — `error::sync_lock_aborted`, `error::sync_lock_alloc_failed` — *do* surface at the C ABI under the `FIXPP_ERR_*` enum that 2i locks; per the per-doc-prefix discipline established by `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]`, 2f's prefix is `FIXPP_ERR_SYNC_*`. The cancellation variant joins the existing `FIXPP_ERR_CANCELLED` group per `[const §XI.2]`.

---

## 6. Behavioural contract

### 6.1 Allocation, exceptions, threading, cancellation

#### 6.1.1 Allocation

- **Uncontended fast path** — zero allocation. `await_ready` runs one CAS and returns true; `await_resume` constructs the `async_lock_guard` (8 B, a single pointer copy onto the coroutine frame's return slot). No PMR call, no global heap.
- **Contended path under HALO** — zero allocation. The waiter is embedded in the caller's coroutine frame (per item 1 of `[SYN §3.2 Q6b]`); the frame itself is HALO-elided when the caller's frame is bounded by its caller (the typical hot-path case per `[const §XI.6]`). The cancellation slot's handler closure (≈ 32 B) is stored alongside the awaiter in the same frame.
- **PMR fallback path** — one allocation from the per-session `memory_resource` (per §4.3). No global heap. Allocation failure routes through `fixpp::core::detail::trap_throw` per `[2a §4.2]` and surfaces as `expected_t::unexpected{error::sync_lock_alloc_failed}` (§6.5).
- **Per `[const §VIII.5]`** — zero `new`/`delete` between parse and `fromApp` extends to the contended `async_lock` path on the store-write mutex (which sits on the post-`fromApp` outbound dispatch chain per `[const §XI.5]`). 2f satisfies the discipline on every code path; the §9 seam **"PMR fallback exercise"** under `mallocnesia` interceptor verifies.

#### 6.1.2 Exceptions

- All public methods are `noexcept`. The PMR fallback path's `allocate(...)` may throw `std::bad_alloc` from the underlying resource; `trap_throw` (per `[2a §4.2]`) converts the throw into an `expected_t::unexpected{error::sync_lock_alloc_failed}` without terminating. No exception crosses the coroutine boundary.
- Per `[arch §5.3]` "no exceptions on the hot path" — 2f satisfies the rule by construction.

#### 6.1.3 Threading

- **The mutex itself is thread-safe.** `state_` is `std::atomic<uintptr_t>`; every load/store is a CAS or atomic exchange. No data races on the mutex's own state regardless of where `async_lock` / `unlock` are called from.
- **The v1.0 invariant** — every consuming site of `async_mutex` runs inside a single session serialisation domain (per `[2d §4.8]`'s wrapper-class shape under both `per_session_strand` and `direct_executor` modes). Under that discipline, contention is structurally zero (§1.1); the mutex is defence-in-depth.
- **Cross-domain pathological case** (§9 seam **"Cross-strand acquire"**): two consumers on different domains acquire the same mutex; the second arrival suspends on the LIFO list and wakes FIFO-fairly within the drain cycle (the LIFO push is wait-free; the unlock exchange is wait-free; only the FIFO drain is sequenced — see §6.2 for the state machine).
- **`unlock` on a different executor than the holder.** Per `[2d §7.4]`, the awaiter completes on its own bound executor (not on the unlocking thread). If `unlock` is called from a different executor than the waiter is bound to, the waiter is resumed via `dispatch` (which falls through to `post` if the unlocking thread is not on the bound executor) or always via `post` (per the `completion_policy` field, §4.6).

#### 6.1.4 Cancellation

Per §4.5: `total` removes the waiter and completes with `error::sync_lock_aborted`; `partial` is no-op (dropped from v1.0); `terminal` is treated as `total`.

Cancellation outcome is surfaced through the awaitable's `expected_t::unexpected{error::sync_lock_aborted}` outcome — observable in the same shape as `[2d §6.5]`'s `cancellable_dispatch → awaitable<expected_t<void>>` and `[2e §6.1.4]`'s `store → awaitable<expected_t<void>>` cancellation contract. The C-ABI mapping joins the `FIXPP_ERR_CANCELLED` group per `[const §XI.2]`.

### 6.2 The atomic state machine

```
                   ┌──────────────┐
                   │  not_locked  │   = uintptr_t{1}
                   │  (free; no   │
                   │   waiters)   │
                   └──────┬───────┘
                          │ CAS(1 → 0) on async_lock fast path
                          ▼
                   ┌──────────────┐
                   │   locked_    │   = uintptr_t{0}
                   │ no_waiters   │
                   │ (held; LIFO  │
                   │  empty)      │
                   └──┬───┬───┬───┘
                      │   │   │
        unlock() ────┐│   │   ▼ async_lock under contention
        exchange     ▼│   │  CAS(0 → &waiter) and waiter.next_ = nullptr
        (0 → 1) ────► not_locked again (uncontended unlock, no drain).
                      │
                      │ (or, if list non-null on exchange:)
                      │
                      ▼
                   ┌──────────────┐
                   │  &head       │   = pointer to LIFO head waiter
                   │  (held;      │     (>= 8, 8-byte aligned)
                   │  list of    │
                   │  waiters)   │
                   └──┬───┬───┬───┘
                      │   │   │
        async_lock ───┘   │   │
        (more waiters)    │   │
        CAS(&old_head     │   │
            → &new)       │   │
                          │   ▼
        unlock() ─────────┘  (if list non-empty:)
        exchange (head → 0)  reverse the LIFO chain to FIFO,
                             resume each in order under
                             the per-mutex completion policy.
```

- **Lock CAS** (`async_lock` fast path): `compare_exchange_strong(state_, not_locked → locked_no_waiters, acq_rel)`. Wait-free.
- **Suspend CAS** (`async_lock` contended path): `compare_exchange_weak(state_, old_head → &my_waiter, release / acquire)` with retry on stale `old_head`. Wait-free per CPU; bounded retry under cross-CPU contention.
- **Unlock exchange** (`unlock`): `state_.exchange(locked_no_waiters, acq_rel)`. Wait-free.
- **FIFO drain** (post-unlock, list non-empty): walk the LIFO chain forward and reverse each `next_` pointer to produce a FIFO-ordered list, then resume each waiter under the per-mutex `completion_policy` (`dispatch` runs inline if the unlocking thread is on the waiter's bound executor; `post` always hops). Sequenced — the unlocking thread holds no lock during the drain (the `state_` exchange has already released `state_` to either `locked_no_waiters` or the new head if a concurrent push lands during the drain), but only the unlocking thread walks its drained slice.

The LIFO push is wait-free on a single CPU (one CAS per attempt, retry on stale state); the unlock exchange is wait-free; only the FIFO drain is sequenced (within each unlock cycle, the resumption order is deterministic and FIFO; across drain cycles, the LIFO push order between cycles is arbitrary). This delivers FIFO fairness *within a drain cycle* — item 6's first sub-test (§9 seam **"FIFO fairness across drain cycles"**) verifies the property.

### 6.3 Latency Tier 1 ceilings

Per the 2a v0.3 §6.5 / 2b v0.2 §6.6 / 2d v0.4 §6.3 idiom: Linux/Clang/x86_64 warm-cache, named workload. CI fails on >5% regression vs the previous tagged release.

| Operation | Workload | Ceiling | Per-component breakdown |
|---|---|---|---|
| `async_lock` uncontended | Single CAS fast path; awaiter not constructed; `async_lock_guard` returned via `await_resume`. | ≤ 30 ns | CAS atomic on warm L1 ≈ 5 ns + awaiter `await_ready` true + `async_lock_guard` ctor (single pointer copy) ≈ 5 ns + `co_await` resume cost (HALO-elided) ≈ 5 ns + dispatch boilerplate ≈ 5 ns; head-room for guard return. |
| `async_lock` contended (waiter suspends) | Waiter pushed onto LIFO list; cancellation slot registered; coroutine handle captured. | ≤ 80 ns | Initial CAS-fail ≈ 10 ns + awaiter construction in coroutine frame (HALO-elided) ≈ 10 ns + cancellation-slot bind (one atomic write to slot's handler list) ≈ 5 ns + LIFO push CAS retry (1–2 attempts under low contention) ≈ 15 ns + suspend boilerplate ≈ 30 ns; head-room for cache-line-sized awaiter layout copy. |
| `unlock` uncontended | Empty LIFO list; atomic exchange and return. | ≤ 10 ns | One atomic exchange ≈ 5 ns + return-from-method boilerplate ≈ 5 ns. |
| `unlock` contended (drains N waiters) | LIFO list reversed to FIFO; each waiter resumed under `dispatch` (default). | ≤ 30 ns + (≤ 50 ns per waiter resumption) | Atomic exchange ≈ 5 ns + LIFO-to-FIFO reversal (walk list) ≈ 5 ns per waiter — bookkeeping ≈ 30 ns total for the head; per-waiter resumption: cancellation-slot clear ≈ 5 ns + executor `dispatch` (run inline if same executor; ≈ 0 ns on hot strand) or `post` (≈ 25 ns one hop) + coroutine resume ≈ 20 ns = ≤ 50 ns per waiter. |

Per `[2d §6.3]` and `[const §VIII]` the ceilings cite warm-cache regression bars; the §9 seam **"Uncontended-acquire latency Tier 1"** and **"Contended-enqueue latency Tier 1"** bench-harness rows fail CI on >5% drift. The contended-resumption-per-waiter rate is bench-harness-soft (the per-waiter cost is bounded but cumulative regressions on long drain cycles trigger investigation, not auto-fail).

### 6.4 HALO discipline

Per `[2d §6.6]` clock-contract precedent and `[const §XI.6]`'s HALO-first discipline:

- The `async_lock` awaiter is **HALO-eligible** by construction:
  - The awaiter's storage (≤ 64 B per §1.1) lives in the caller's coroutine frame, which itself is bounded by the caller (the session FSM coroutine, the store's `MessageStore::store` awaitable, or the pinset rotation handler).
  - No escape to the heap on the contended path (the LIFO link is a `next_` field inside the awaiter's frame, not a separate allocation).
  - No captures of size > the §1.1 64-byte budget (the cancellation-slot handler closure is ≈ 32 B, fits in the same cache line).
- HALO firing on the inbound dispatch path (per `[arch §11]` row 2) is the verification spike co-owned with 2d. Both 2d's `cancellable_dispatch` node and 2f's `lock_awaiter` must elide on the default Linux/Clang, Linux/GCC, Windows/MSVC toolchains for the `[const §VIII.5]` zero-allocation discipline to hold without the PMR fallback firing.
- §10 Q1 records the open spike work; §9 seam **"HALO firing for awaiter under default toolchains"** is the verification harness across the three toolchains.

### 6.5 Errors introduced by this design

Per the per-doc-prefix discipline established by `[2a §6.7]` (`FIXPP_ERR_DECIMAL_*`), `[2b §6.7]` (`FIXPP_ERR_WIRE_*`), `[2c §6.7]` (`FIXPP_ERR_DICT_*`), `[2d §6.7]` (`FIXPP_ERR_THREAD_*`), `[2e §6.7]` (`FIXPP_ERR_STORE_*`): 2f adopts the prefix **`FIXPP_ERR_SYNC_*`** for its C-ABI mapping target, owned by 2i.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `sync_lock_aborted` | §4.5 / §4.2 — cancellation won the race against the waiter's resumption from `unlock()`. The waiter was unlinked from the LIFO list before entering the critical section. | Cancellation outcome — joins `[2d §6.7] dispatch_aborted` and `[2d §6.7] clock_sleeps_cancelled` and `[2e §6.7] store_cancelled` in the cancellation group at the C ABI (`FIXPP_ERR_CANCELLED` per `[const §XI.2]`). The FSM treats this distinct from runtime errors: cancellation = no state change, no recovery action. |
| `sync_lock_alloc_failed` | §4.3 — the PMR fallback path's `allocate(...)` threw `std::bad_alloc` (per-session arena exhausted, or engine-default resource exhausted). Trapped via `fixpp::core::detail::trap_throw` per `[2a §4.2]`. | Configuration / capacity error — operator raises the per-session arena cap, or the consumer fixes a leak. The mutex itself is unaffected (the awaiter was never constructed; the lock is still in whatever state it was before the failed acquire). |

(2 variants. 2f does not introduce an `error::async_mutex_destroyed_with_waiters` variant because the destructor shape (§4.7) is pre-conditioned — debug `assert` + release UB; no `expected_t` form is needed because the destructor does not return a value. The `[2d §6.7] dispatch_aborted` variant is **reused** for any cross-doc cancellation hand-off; 2f's `sync_lock_aborted` is the 2f-layer name that maps to the same C-ABI group.)

C-ABI mapping (delegated to **2i**) per the per-doc-prefix discipline:

- runtime / capacity → **`FIXPP_ERR_SYNC_RUNTIME`**: `sync_lock_alloc_failed`.
- cancellation → **`FIXPP_ERR_CANCELLED`** per `[const §XI.2]` (joining `[2d §6.7] dispatch_aborted`, `[2d §6.7] clock_sleeps_cancelled`, `[2e §6.7] store_cancelled`): `sync_lock_aborted`.

Final coalescing is 2i's call. The `FIXPP_ERR_SYNC_*` prefix matches the per-doc-prefix discipline 2b v0.2 / 2c v1.3 / 2d v0.4 / 2e v0.4 established. v0.1 of this doc does NOT introduce a `FIXPP_ERR_SYNC_CONFIG` group because there is no configuration error — `async_mutex` has no construction-time invariant beyond the default `completion_policy`, and the policy enum is exhaustive at the type level.

### 6.6 Enforcement of `[const §XV.9]` — `std::mutex`-in-coroutine-context CI gate

Per `[const §XV.9]`, plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`. Per `[const §XI.3]` ("Enforced by clang-tidy custom check or grep gate"), 2f names the enforcement mechanism:

**v1.0: grep gate** — `tools/check_no_std_mutex_in_awaitable_headers.sh`, run in Tier 1 CI per `[const §IX.4]`. The gate scans every header under `include/fixpp/...` and `src/` for the conjunction `<mutex>` (or `std::mutex` declaration) AND `asio::awaitable` / `<asio/awaitable.hpp>`. Hits fail the build with a documentation pointer to `[const §XV.9]` and `[2f §4]`.

**Post-v1: clang-tidy custom check** — a project-owned check `fixpp-no-std-mutex-in-coroutine-context` enforced through `clang-tidy`'s plugin mechanism. The check is more precise than grep (catches `std::mutex` declared as a member of a class whose method returns `awaitable<...>`, which the grep gate would miss because the `<mutex>` and `<asio/awaitable.hpp>` may live in separate translation units). v1.0 ships the grep gate as the line-of-defence; the clang-tidy check is post-v1 work. §10 Q3 records the choice.

The §9 seam **"`std::mutex`-in-coroutine-context CI gate"** verifies the gate fires on a deliberately-violating fixture (a header with both `<mutex>` and `asio::awaitable<...>`).

---

## 7. Integration with adjacent modules

### 7.1 MessageStore (2e) — direct client of the writer mutex

Per `[2e §6.4]` writer-mutex contract: every `MessageStore` mutating method (`store`, `retrieve`, `next_seqnum`, `reset`) serialises against the per-store-instance writer mutex; the mutex is `fixpp::sync::async_mutex` (per `[const §XI.3]`).

The lock-acquisition shape used by `MemoryStore::store` / `retrieve` / `next_seqnum` / `reset` is:

```cpp
asio::awaitable<expected_t<void>> MemoryStore::store(seqnum_t seq,
                                                    std::span<const std::byte> frame,
                                                    direction_t dir) noexcept {
    auto guard_or_err = co_await writer_mutex_.async_lock();
    if (!guard_or_err) {
        // sync_lock_aborted (cancellation) — bubble through to the caller's
        // expected_t shape per [2e §6.1.4] cancellation contract.
        co_return std::unexpected(error{error_code::store_cancelled});
    }
    auto guard = std::move(*guard_or_err);  // RAII; releases on scope exit.

    // Verify seqnum order, copy bytes into the slab, advance the entry
    // index — all per [2e §4.1] / [2e §4.2] contract.
    // …

    co_return {};
}
```

The guard's RAII release (per §4.4) means the critical section is exactly the body between `async_lock` and the awaitable's natural completion; no manual `unlock()` call is needed on the consumer's hot path.

**2f sign-off is the named hard hand-off gate for 2e implementation** per `[2e §3.1]` last bullet — once 2f v0.1 lands and Codex Gate A clears, 2e implementation can begin. The integration test seam is in `tests/session/test_memory_store_round_trip.cpp` and `tests/session/test_file_store_crash_survival.cpp` (already inventoried at `[2e §9]`).

### 7.2 Pinset rotation (2g) — second client per `[SYN §3.2 Q6b]`

Per `[SYN §3.2 Q6b]`'s v1.0 use cases: pinset rotation. The `Pinset::add(cert)` / `Pinset::remove(cert)` API per `[arch §4.6]` is independently thread-safe (owned by 2g); under the v1.0 pattern it acquires a per-`Pinset` `fixpp::sync::async_mutex` for the add-then-remove sequence. 2g's design doc owns the pinset's exact shape; 2f records the consumption pattern.

The shape mirrors 2e's:

```cpp
asio::awaitable<expected_t<void>> Pinset::add(cert_t cert) noexcept {
    auto guard_or_err = co_await rotation_mutex_.async_lock();
    if (!guard_or_err) co_return std::unexpected(error{error_code::pinset_cancelled});
    auto guard = std::move(*guard_or_err);
    // … add the cert to the pinset, validate, etc. …
    co_return {};
}
```

### 7.3 Seqnum counter (Phase-4 session-module spec) — third client

Per `[SYN §3.2 Q6b]`'s v1.0 use cases: seqnum counter. The session FSM's outbound-seqnum increment runs on the session strand under v1.0's single-domain discipline; the mutex is **defence-in-depth** there (contention is structurally zero per §1.1). The Phase-4 session-module spec (not yet drafted) consumes `fixpp::sync::async_mutex` for the seqnum bookkeeping; 2f records the forward dependency.

### 7.4 Threading + Clock (2d) — supplier (executor-compat surface)

2f consumes the **executor-compat surface** locked at `[2d §7.4]`:

- The mutex's `async_lock` awaitable completes on the awaiter's bound executor — for in-session callers, the `session_executor` wrapper class per `[2d §4.8]`. The completion policy (per-mutex `dispatch` / `post`) decides the inline-vs-hop trade-off.
- `cancellation_type::total` is honoured (waiter unlinked, `error::sync_lock_aborted` returned) per `[SYN §3.2 Q6b]` item 3.
- The default completion policy is `dispatch` per `[2d §7.4]`; per-mutex override (HFT/fairness-sensitive sites pick `post`) is 2f's call (constructor argument, §4.6.1).

2f does **not** consume the `Clock` surface (no timed acquire in v1.0; per §2 non-goals). 2f does **not** consume the `cancellable_dispatch` primitive (`[2d §6.5]`); 2f's `async_lock` is the lower-level mutex `cancellable_dispatch` may use internally for any internal serialisation.

### 7.5 C ABI (2i) — none

`async_mutex` is C++ only. The error variants 2f introduces (`sync_lock_aborted`, `sync_lock_alloc_failed`) surface at the C ABI under the `FIXPP_ERR_SYNC_*` prefix and the existing `FIXPP_ERR_CANCELLED` group; 2i locks the symbol shape. No mutex handle, no awaitable type, no completion policy enum crosses the C ABI.

### 7.6 Phase-4 session-module spec — names the seqnum-counter and lock-policy consumers

The Phase-4 session-module spec consumes 2f's surface:

- **Seqnum counter** — per `[SYN §3.2 Q6b]` item 1 of the v1.0 use cases. The session FSM's outbound-seqnum increment under per-session strand serialisation is lock-protected by an `async_mutex`; the mutex is **defence-in-depth** under the v1.0 single-domain discipline.
- **Lock-policy consumer** — per `[const §XI.5]` the store-write callsite cap binds 2e to mutex regardless of `SessionConfig::lock_policy`; the Phase-4 spec governs how `lock_policy` is consumed for any non-store-write site. 2f provides the only legal mutex shape.

The Phase-4 spec is not yet drafted; 2f records the forward dependency.

---

## 8. PMR — recap

`fixpp::sync::async_mutex` itself does **NOT** carry a `std::pmr::memory_resource*` field. The mutex's atomic state (`std::atomic<uintptr_t>`) and policy field (`completion_policy`) are stack/instance-allocated by the consumer; no PMR allocation at construction. The PMR fallback path (§4.3) inherits the per-session resource from the awaiter's bound `session_executor` per `[2d §4.8]` — the wrapper's `session_ptr()` member-function accessor recovers the typed `Session*`, which carries the `SessionConfig::session_arena` per `[2d §4.5]`.

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| Caller's coroutine frame (HALO-friendly) | the caller's coroutine's lifetime | the `async_mutex_awaiter` waiter object (≤ 64 B, one cache line) on the contended path; the cancellation-slot handler closure (≈ 32 B) | coroutine's own destruction (HALO frees the frame at the consumer's natural end) |
| `SessionConfig::session_arena` (per `[2d §4.5]` / `[2d §8]`) | session lifetime | the PMR-fallback awaiter when the embedded shape is unavailable (type-erased completion handlers per §4.3) | session destruction |
| `EngineConfig::default_session_resource` (per `[2d §4.4]`) | engine lifetime | the engine-fallback awaiter when the awaiter's bound executor is not a `session_executor` value (engine bootstrap, listener accept, control-plane handlers — non-v1.0-hot-path consumers) | engine destruction |

**Lifetime classes for non-arena objects:**

- **`async_mutex` instance** — consumer-controlled lifetime (per-`MessageStore`-instance for the writer mutex; per-`Pinset` for rotation; per-counter-site for the seqnum counter). Non-copyable, non-movable per §4.1; held by-value at a stable address. Pre-conditioned destruction per §4.7.
- **`async_lock_guard`** — flyweight; lifetime bounded by the originating `async_mutex`. Movable; releases the mutex on destruction (per §4.4). `[[clang::lifetimebound]]` on the constructor surfaces caller-side misuse.
- **`async_mutex_awaiter`** — embedded in the caller's coroutine frame on the hot path; allocated from a PMR resource on the type-erased fallback path. Ownership is the awaiter's container (the coroutine frame, or the type-erasure machinery's control block).

Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp`, **extended here to the contended-acquire path** because the v1.0 store-write mutex sits on the post-`fromApp` outbound dispatch chain. The waiter-embedded design (item 1) and the PMR fallback (item 2) jointly satisfy the discipline; the §9 seam **"PMR fallback exercise"** verifies under `mallocnesia` interceptor that no global-heap allocation occurs on either path.

---

## 9. Test seams

Per `[arch §10]` requirement (4) and `[const §VII]`. v0.1 ships **14 seams** (≥ 12 required by the brief; the six-item list's item (6) maps to seams 3, 4, 5, 6, 7, 8 — six of them — and the broader test plan inflates to 14 to cover the per-section contracts in §4 and §6). Seams are referenced by name; ordinals may shift across review rounds (per `[2d §9]` precedent on name-based cross-referencing).

1. **Uncontended-acquire latency Tier 1.** Google Benchmark on `async_mutex::async_lock` uncontended (single CAS fast path; awaiter not constructed). Verify the §6.3 ceiling ≤ 30 ns warm-cache; CI fails on >5% regression. Lives in `bench/sync/bench_async_mutex_uncontended.cpp`.

2. **Contended-enqueue latency Tier 1.** Google Benchmark on `async_mutex::async_lock` contended (waiter pushed onto LIFO list; cancellation slot registered). Verify the §6.3 ceiling ≤ 80 ns warm-cache; CI fails on >5% regression. Variant for `unlock` uncontended (≤ 10 ns) and `unlock` contended drain (≤ 30 ns + ≤ 50 ns/waiter). Lives in `bench/sync/bench_async_mutex_contended.cpp`.

3. **FIFO fairness across drain cycles** (item (6) of `[SYN §3.2 Q6b]`). Spawn N=64 coroutines that all `co_await mutex.async_lock()` on a shared mutex; each waits for the lock, immediately releases, records its acquisition order. Verify within each drain cycle (the waiters that arrived between consecutive `unlock()` exchanges) the resumption order is FIFO (matches the LIFO push order reversed). Lives in `tests/sync/test_fifo_fairness.cpp`.

4. **Cancellation mid-wait** (item (6); item (3) verification). For each `cancellation_type` (`total`, `terminal`), park a coroutine in `async_lock` (the holder is a sibling coroutine that will not release for 1 s); fire the cancellation slot from a different thread; verify the parked coroutine completes with `expected_t::unexpected{error::sync_lock_aborted}` within ≤ 100 µs. Verify the LIFO list is now empty (the cancelled waiter was unlinked). Variant for `partial` — verify the parked coroutine continues to wait (no-op, dropped from v1.0 per §4.5). Verify the holder's `unlock()` post-cancellation does not crash (the cancelled waiter is no longer in the list, the unlock exchange finds either an empty list or a different waiter). Lives in `tests/sync/test_cancellation_mid_wait.cpp`.

5. **Destructor-with-waiters policy enforcement** (item (6); item (5) verification). Construct an `async_mutex`, hold it via `try_lock()` success, then attempt to destroy it — verify debug builds fire the `assert` with the documented message ("destroyed with waiters or while held; drain the mutex before destruction"). Variant: park a waiter in `async_lock` (without cancelling), then attempt destruction — verify the same assert fires (the LIFO list is non-empty). Variant: properly drain (unlock, then cancel the waiter, then destruct) — verify destruction succeeds in both debug and release. Lives in `tests/sync/test_destructor_with_waiters.cpp`.

6. **Contention stress (≥10⁴ coroutines)** (item (6)). Spawn 10⁴ coroutines that each `co_await mutex.async_lock()`, perform a 1 ns critical section (atomic increment of a shared counter), release. Run on `asio::thread_pool` with N = vCPU threads. Verify (a) all 10⁴ coroutines complete; (b) the shared counter reads exactly 10⁴; (c) no waiter is lost (the LIFO list is empty at end-of-test); (d) total runtime is bounded by the atomic-CAS-rate × 10⁴. Lives in `tests/sync/test_contention_stress.cpp`.

7. **TSan clean under stress** (item (6)). Build the contention-stress test under `linux-clang-tsan` (per `[const §III.3]`); verify zero TSan reports on a 10⁴-iteration run. Catches any data race introduced in the LIFO push / unlock exchange / FIFO drain. Lives in `tests/sync/test_tsan_clean.cpp` (the same source as **"Contention stress"** but built under the TSan profile).

8. **ASan clean (no leaks, no use-after-free) under stress** (item (6)). Build the contention-stress test under `linux-clang-asan`; verify zero ASan reports on a 10⁴-iteration run. Catches any leak introduced by a cancelled waiter (the unlinked-from-LIFO awaiter must be properly destructed by the coroutine resume path, not leaked on the heap or PMR). Variant under `linux-clang-ubsan` for any UB. Lives in `tests/sync/test_asan_clean.cpp`.

9. **HALO firing for awaiter under default toolchains** (co-owned with 2d's `[arch §11]` row 2 spike). Compile `tests/sync/test_halo_firing.cpp` under each of the three default toolchains (Linux/Clang, Linux/GCC, Windows/MSVC), exercise an in-session `async_lock` from a session FSM coroutine, dump the assembly via `llvm-objdump --disassemble`, verify the awaiter's coroutine frame is HALO-elided (no `operator new` call inside the inlined `async_lock` body). Failure is **non-fatal** for v1.0 — the PMR fallback path (§4.3) catches the gap; the seam logs a warning and reports the failing toolchain so the gap can be tracked. Lives in `tests/sync/test_halo_firing.cpp`.

10. **PMR fallback exercise.** Construct a type-erased `asio::any_completion_handler<expected_t<async_lock_guard>(...)>` that wraps `co_await mutex.async_lock()`; run it under `tools/check_alloc.py` + `mallocnesia` interceptor (Linux/Clang Tier 1). Verify zero global-heap allocations on the fallback path; verify the awaiter is allocated from `SessionConfig::session_arena` (tracked via a custom PMR resource that increments a counter per `allocate(...)`). Catches a global-heap leak on the fallback path or a wrong-resource selection. Variant: under `EngineConfig::default_session_resource` for the engine-bootstrap path. Lives in `tests/sync/test_pmr_fallback.cpp`.

11. **Executor-compat: completion runs on awaiter's bound executor.** Construct an `async_mutex` and a `session_executor` wrapper (per `[2d §4.8]`) over a strand-shaped executor; spawn a coroutine bound to the `session_executor` that does `co_await mutex.async_lock()`; the unlocking thread is on a *different* executor. Verify the awaiter resumes on the original `session_executor`'s thread (recover the thread ID via `std::this_thread::get_id()` inside the resumed coroutine). Variant: under both `per_session_strand` and `direct_executor` modes — the wrapper's `session_ptr()` member-function accessor must work in both, and the resumption must land on the wrapper's bound thread regardless. Lives in `tests/sync/test_executor_compat.cpp`.

12. **`dispatch` vs `post` policy effect on completion.** Construct two mutexes — one with `completion_policy::dispatch` (default), one with `completion_policy::post`. Park a waiter on each from a session strand; trigger `unlock()` from the same session strand. Verify the `dispatch` mutex resumes the waiter inline on the unlocking thread (zero executor hop; ≈ 0 ns above unlock cost). Verify the `post` mutex resumes the waiter via one explicit hop (≈ 25 ns extra). The cost is asserted against §6.3's per-component breakdown via Google Benchmark; CI fails on >5% regression. Lives in `tests/sync/test_dispatch_vs_post.cpp`.

13. **Cross-strand acquire** (FIFO-fair drain across two strands). Construct an `async_mutex`; spawn two coroutines bound to two different strands (call them strand A and strand B) — coroutine A acquires, coroutine B parks on the LIFO list, coroutine A releases. Verify coroutine B resumes on strand B (not strand A) per `[2d §7.4]`. Variant: 100 coroutines split across 2 strands; verify FIFO fairness within each drain cycle on each strand. Lives in `tests/sync/test_cross_strand_acquire.cpp`.

14. **`std::mutex`-in-coroutine-context CI gate** (`[const §XV.9]` enforcement). Place a deliberately-violating header in `tests/fixtures/header_with_std_mutex_and_awaitable.hpp` (declares both `<mutex>` and `asio::awaitable<...>`); run `tools/check_no_std_mutex_in_awaitable_headers.sh` (the v1.0 grep gate per §6.6); verify the gate fires with the documented `[const §XV.9]` and `[2f §4]` documentation pointers. Variant: a non-violating header (only `<mutex>` and no awaitable, or only awaitable and no mutex) — verify the gate does NOT fire. Lives in `tests/sync/test_no_std_mutex_ci_gate.cpp` (the test driver) and `tools/check_no_std_mutex_in_awaitable_headers.sh` (the gate itself).

---

## 10. Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **HALO firing on inbound dispatch path across compiler matrix.** Per `[arch §11]` row 2 — does HALO elide the `async_lock` awaiter's coroutine frame allocation on Linux/Clang, Linux/GCC, Windows/MSVC default toolchains? Co-owned with 2d's `cancellable_dispatch` spike. The PMR fallback path (§4.3) catches the gap, so failure is non-fatal for v1.0; the spike informs whether v1.x can drop the fallback for the embedded path on a given toolchain. | OPEN — verification spike at 2f implementation start; Linux/Clang first (highest-confidence HALO support per `[const §II.2]`), then Linux/GCC, then Windows/MSVC. The §9 seam **"HALO firing for awaiter under default toolchains"** is the harness. | 2f + 2d co-owned per `[arch §11]` row 2 |
| 2 | **PMR resource acquisition in type-erased fallback path.** §4.3 specifies the mechanism precisely (the awaiter's bound `session_executor` exposes `session_ptr()` per `[2d §4.8]`; the typed `Session*` exposes `session_arena()` for the per-session resource). The open question is whether the `Session::session_arena()` accessor exists on `Session` today or is owed as a 2d cross-doc edit. Per `[2d §4.5]` `SessionConfig::session_arena` is the source field; `Session` reads it at session open and stores it in a private member. The accessor `Session::session_arena()` is **not** on the published `[2d §4.5]` surface; it is engine-internal. **No 2d cross-doc edit is required if `Session::session_arena()` is admitted as engine-internal accessor on the same axis as `Session::trace_slot_` (§4.6 of 2d). If a published accessor is needed (e.g., for user-side tooling that wants to allocate on the session arena directly), that is a 2d Appendix D drop-in.** v0.1 marks the accessor as engine-internal (no Appendix D drop-in); §10 Q2 is closed unless Codex Gate A pushes back. | CLOSED in v0.1 with the engine-internal accessor; reopens to Appendix D drop-in if Gate A demands a published surface. | 2f |
| 3 | **CI enforcement: clang-tidy custom check vs grep gate.** Both work; per `[const §XI.3]` either is admitted. v1.0 ships the **grep gate** (§6.6) — simpler, faster on every PR, no clang-tidy plugin maintenance cost. The clang-tidy custom check is more precise (catches separate-translation-unit cases the grep gate misses) and is post-v1 work. | CLOSED for v1.0 — grep gate. clang-tidy custom check is post-v1 follow-up. | 2f |
| 4 | **Closes `[2d §10] Q1`** — `async_lock` signature locked at §4.1 (`awaitable<expected_t<async_lock_guard>> async_lock() noexcept`). | CLOSED in v0.1. | 2f closes |
| 5 | **Closes `[2e §10] Q8`** — 2f signature delivered (same as Q4 above). | CLOSED in v0.1. | 2f closes |

---

## 11. Hand-off

**Docs unblocked by 2f sign-off (downstream):**

- **2e implementation** — the writer-mutex contract on `MessageStore` per `[2e §6.4]` is now satisfied by 2f's surface; `MemoryStore::store` / `retrieve` / `next_seqnum` / `reset` all consume `async_mutex` per §7.1. **2f sign-off is the named hard hand-off gate from `[2e §3.1]` last bullet.** 2e's design v0.4 carries forward into implementation immediately on 2f sign-off.
- **2g** (TLS `cert_source` + pinset rotation) — pinset rotation can use `async_mutex` per `[SYN §3.2 Q6b]` v1.0 use cases (§7.2). 2g's Codex Gate A may proceed once 2f signs off.
- **Phase-4 session-module spec** (not yet drafted) — seqnum counter (§7.3) consumes `async_mutex`; the spec's coroutine FSM design can reference 2f's signature directly.

**Catalogue + coverage-index amendments owed at sign-off** (drop-in language pattern from `[2d §11]` / `[2c App D]`; the orchestrator applies these during the sign-off commit, not the 2f rewrite agent):

- Add **NFR-016** to `library/spec/feature-catalogue.md` (one row, mirroring the NFR-015 row format from `feature-catalogue.md` line 225):

  > **NFR-016** | OFFICIAL | nfr | Awaitable mutex `fixpp::sync::async_mutex` — own implementation (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with not_locked/locked_no_waiters/pointer-to-LIFO encoding); waiter embedded in the awaiter object inside the caller's coroutine frame (zero global-heap allocation on the contended path); PMR-aware fallback for type-erased completion handlers via `SessionConfig::session_arena`; ASIO `cancellation_type::total` removes the waiter and completes with `error::sync_lock_aborted`; per-mutex `dispatch`/`post` completion policy with default `dispatch`; pre-conditioned destructor (debug `assert`, release UB if violated); the only legal mutex shape in coroutine context per `[const §XI.3]` (CI-enforced via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate per `[const §XV.9]`). | all | `[2f §4.1] / [arch §1.1]` | backlog | `.specify/2f-async-mutex.md` v0.1 | — | — | — |

- Add a corresponding entry to `library/spec/coverage-index.md` linking `[2f §4.1]` and `[arch §1.1]` (concurrency primitives promise) to **NFR-016**.

- Update `[arch §11]` row 2 disposition note to reference 2f as one of the two HALO-spike co-owners (no text change to the row itself; the existing "Owner: **2d**, **2f**" is intact). No `architecture.md` text edit is required — the row already names 2f.

- **No 2d cross-doc Appendix D drop-in is owed** under §10 Q2's v0.1 disposition (the `Session::session_arena()` accessor stays engine-internal). If Gate A demands a published accessor, the cross-doc edit is declared at convergence time.

The amendments are **not** applied by 2f itself; per `[2c App D]` / `[2d §11]` precedent, the rewrite agent does not edit `feature-catalogue.md`, `coverage-index.md`, or `architecture.md` directly. The orchestrator (parent session) applies the amendment text during the sign-off commit.

---

## Appendix A — Catalogue row coverage

This doc owns one new catalogue row.

### A.1 Owned

| Row | Family | What 2f covers | Status |
|---|---|---|---|
| **NFR-016** (NEW) | NFR — Awaitable mutex `fixpp::sync::async_mutex` | The class definition, the awaiter type, the RAII guard, the executor-compat surface satisfying `[2d §7.4]`, the PMR fallback path, the cancellation contract per `cancellation_type`, the per-mutex `dispatch`/`post` policy, the pre-conditioned destructor, the test seams covering items (3) (5) (6) of `[SYN §3.2 Q6b]`'s six-item list, and the CI grep gate enforcing `[const §XV.9]`. | Claimed by 2f; row to be added to `feature-catalogue.md` at sign-off (§11). |

### A.2 Cross-references

(No application-message rows like A-XXX, no W-XXX wire rows, no D-XXX dictionary rows are owned or touched by 2f. Per the per-doc-prefix discipline established by `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]`: 2f's claim is bounded to the awaitable-mutex primitive; the row owners that *consume* the primitive — 2e for S-011..S-014 store-side rows, the Phase-4 session-module spec for the seqnum counter, 2g for pinset rotation — discharge their own rows.)

---

## Appendix B — Normative References

Per `[const §VI.5]`, every `/specify` artifact lists the exact `[DocAbbrev §X.Y.Z] Title` references that inform it.

### B.1 Coverage-index normative references

2f's design is **engineering judgment, not spec-driven** — there is no `[FIX-SL §...]`, `[FIXT §...]`, or `[FIXS §...]` section that bears on the awaitable-mutex algorithm or its surface. The table below lists the constitution / architecture / synthesis / sibling-doc references that inform the design.

| Source | Title (exact) | Where applied |
|---|---|---|
| `[const §VI.5]` | Spec Coverage Discipline (the 100% FIX Rule) — exact-citation rule | this appendix's structure |
| `[const §VII]` | Testing Requirements — every plugin needs a mock + test seam (applies here even though `async_mutex` is not a plugin: §9 ships test seams per the spirit of the article) | §9 |
| `[const §VIII.5]` | Performance Budgets & Benchmarks — Allocator policy on the hot path | §1.2, §6.1, §6.4, §8 |
| `[const §XI.1]` | Concurrency & Coroutines — `asio::awaitable<T>` composition primitive | §4.1, §3 |
| `[const §XI.2]` | Concurrency & Coroutines — Cancellation: ASIO native cancellation slots end-to-end | §4.5, §6.5, §3 |
| `[const §XI.3]` | Concurrency & Coroutines — Awaitable mutex required in coroutine context (the **direct mandate** for this doc) | every section |
| `[const §XI.5]` | Concurrency & Coroutines — Hot-path lock policy (store-write callsite cap) | §1.1, §3, §6.1, §7.1 |
| `[const §XI.6]` | Concurrency & Coroutines — HALO-first frame allocation | §1.1, §6.4, §3 |
| `[const §XIV.2]` | Pluggable Interfaces — ≤5 pure-virtual on plugin interfaces (recorded that `async_mutex` is NOT a plugin) | §1.2, §2, §3 |
| `[const §XV.9]` | Banned Patterns — `std::mutex` in coroutine context | §6.6, §9, §3 |
| `[const §XVII.1]` | Codex Review Gates — Gate A required for design docs | this doc requires Gate A before `/tasks` |
| `[arch §1.1]` | Goals — concurrency primitives promise | NFR-016 row drop-in |
| `[arch §3]` | Public Namespaces — `fixpp::sync` lives under `core/` physically | §4.1 (header path), §3 |
| `[arch §4.1]` | `core` module surface — `fixpp::sync::async_mutex` listed | §3 |
| `[arch §5.1]` | Executor model | §4.1, §6.1, §3 |
| `[arch §5.2]` | Allocator policy — PMR-aware public API | §4.3, §8, §3 |
| `[arch §5.5]` | Lifetime model — value-typed, non-movable; `[[clang::lifetimebound]]` discipline | §4.1, §4.4, §3 |
| `[arch §6]` | Plugin Pattern — applies only to virtual surfaces (recorded that 2f has none) | §1.2, §2, §3 |
| `[arch §10]` | Hand-off to Design Docs 2a–2m — row 2f | §3 |
| `[arch §11]` | Open Architectural Questions — row 2 (HALO firing co-owned 2d/2f) | §6.4, §10 Q1 |
| `[SYN §3.2 Q6a]` | Cancellation propagation model (DECIDED — ASIO native cancellation slots) | §4.5, §6.1.4, §3 |
| `[SYN §3.2 Q6b]` | Awaitable mutex (DECIDED — own implementation in `fixpp::sync`); the **six-item design list** | every section that delivers an item; §1, §2, §3 |
| `[2a §4.2]` | `trap_throw` pattern (no-terminate-on-PMR-throw) | §4.3, §6.5, §3 |
| `[2a §6.5]` | Latency Tier 1 ceiling idiom | §6.3, §3 |
| `[2b §6.4]` | Lifetime contract on flyweights | §4.4, §3 |
| `[2b §6.6]` | Allocation, exceptions, threading; three-arena pinning; view-escape rule | §4.4, §8 |
| `[2c §6.7]` | C-ABI coalescing groups precedent (`FIXPP_ERR_DICT_*`) | §6.5, §3 |
| `[2d §4.5]` | `SessionConfig` field list — `session_arena`, `lock_policy` | §4.3, §8, §3 |
| `[2d §4.7]` | Cancellation propagation API — two-phase close + per-mode effect table (the row at line 804 for `async_mutex::lock` is 2f's inherited contract) | §4.5, §3 |
| `[2d §4.8]` | `fixpp::core::session_executor` — project-owned wrapper class | §4.1, §4.3, §6.1, §7.4, §3 |
| `[2d §6.5]` | `cancellable_dispatch` — higher-level primitive 2f does NOT implement | §1.2, §3 |
| `[2d §6.7]` | C-ABI coalescing groups precedent (`FIXPP_ERR_THREAD_*`); `dispatch_aborted` is the cancellation peer | §6.5, §3 |
| `[2d §7.4]` | Executor-compat surface — **the locked contract for 2f** | §4.1, §4.5, §4.6, §7.4, §3 |
| `[2d §10] Q1` | `async_lock` signature DEFERRED to 2f — closed in §4.1 | §4.1, §10 Q4 |
| `[2e §3.1]` | Inherited primitives — store-write mutex; **2f sign-off is hand-off gate** | §3, §11 |
| `[2e §4.2]` | `MemoryStore` writer mutex — `fixpp::sync::async_mutex` per `[const §XI.3]` | §7.1, §3 |
| `[2e §6.4]` | Writer-mutex contract on `MessageStore` | §7.1, §3 |
| `[2e §10] Q8` | 2f signature deferred — closed in §4.1 | §4.1, §10 Q5 |

### B.2 Engineering-judgment citations (non-normative, inline at point of use)

Per `architecture.md` Appendix B's closing note (line 678) and `[2d Appendix B] §B.2`'s precedent: design decisions whose primary driver is engineering judgment rather than a specific spec section — **the `async_mutex` algorithm itself, the awaiter shape, the destructor pre-condition, the per-mutex completion policy, the LIFO-push + FIFO-drain semantics, the cancellation behaviour per `asio::cancellation_type`, the PMR fallback mechanism, the `[const §XV.9]` CI grep gate** — cite `[const §X.y]` / `[arch §X.y]` / `[SYN §3.x Q#]` / `[2X §X.y]` inline at point of use; they are not spec normatives and are intentionally omitted from §B.1.

**No `[FIX-SL §...]`, `[FIXT §...]`, or `[FIXS §...]` reference applies to 2f's design.** The awaitable-mutex primitive is not described in any FIX session-layer, FIXT, or FIXS spec section; it is a project-owned engineering primitive driven by `[const §XI.3]`'s mandate, `[const §XV.9]`'s ban, `[SYN §3.2 Q6b]`'s six-item design list, and `[2d §7.4]`'s locked executor-compat contract surface.

---

## Appendix C — Convergence log

> To be populated after Codex Gate A. Round-by-round Codex finding count, Opus adversarial finding count, root causes, and per-finding resolutions will land here. v0.1 is the pre-Gate-A draft.
