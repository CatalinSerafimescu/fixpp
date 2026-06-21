# Contract: `fixpp::sync::atomic_shared_ptr<T>` API

**Feature**: `046-atomic-shared-ptr` | Library-internal sync utility (NOT public API / NOT C-ABI).

## Surface (both resolutions MUST satisfy)

- Construct: default (null), from `nullptr`, from `std::shared_ptr<T>` (move).
- Non-copyable (copy ctor + copy assign deleted), matching `std::atomic`.
- `store(value, memory_order = seq_cst) noexcept`
- `load(memory_order = seq_cst) const noexcept` → `std::shared_ptr<T>`
- `exchange(value, memory_order = seq_cst) noexcept` → previous
- `compare_exchange_weak` / `compare_exchange_strong` — both `(expected&, desired, success, failure)` and `(expected&, desired, order = seq_cst)` overloads, `noexcept`
- `operator=(value) noexcept` (seq-cst store), `operator value_type() const noexcept` (seq-cst load)
- `is_lock_free() const noexcept`, `static constexpr is_always_lock_free`
- **NOT provided**: `wait`, `notify_one`, `notify_all` (documented gap; no consumer depends on them).

## Behavioral contract

- **C-1 (alias identity)**: when `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 1`, the type is *exactly* `std::atomic<std::shared_ptr<T>>` — drop-in, zero overhead.
- **C-2 (publish/acquire)**: a `load` concurrent with a `store` returns either the prior or the new fully-constructed `shared_ptr`, never torn/partial; the writer's construction of the pointee happens-before a reader that observes the new pointer (honors `release` store / `acquire` load).
- **C-3 (CAS-equivalence)**: `compare_exchange` succeeds iff `expected.get() == current.get()` **AND** same ownership (`!owner_before` both directions). Same-raw-pointer/different-control-block ⇒ unequal. Null vs null ⇒ equal. On failure, `expected` is updated to the current value.
  - **C-3-note (consumer scope)**: **none of the four migrated consumers uses `compare_exchange` or `exchange`** — they use `load`/`store` only (D-5). CAS/`exchange` equivalence is therefore witnessed at the **primitive level** (`tests/sync/`), and the SC-003 phrase "witnessed on the integrated consumers" applies to **publish/acquire (load/store) ordering only**; CAS is a primitive-level obligation (see the SC-003 correction in spec.md). The CAS surface is still tested in full so the primitive remains a true P0718 drop-in.
- **C-4 (memory_order honored)**: order arguments are accepted on every operation; the fallback provides **at least** the requested acquire/release (lock is seq-cst-ish); `failure_order` maps release→relaxed, acq_rel→acquire.
- **C-5 (noexcept — incl. the type-erased guard boundary)**: all operations are `noexcept`. The ops themselves only copy/swap existing `shared_ptr`s (no allocation inside them). **After type-erasure**, each op constructs `detail::shard_guard g(this);`, whose `noexcept` ctor calls the out-of-line `std::mutex::lock()` (not standard-`noexcept`); if `lock()` throws, the `noexcept` boundary `std::terminate`s — **by design**, matching `std::atomic`'s no-throw contract (a shard-lock failure is unrecoverable; proceeding without the lock would be a data race). This `terminate`-on-lock-failure is a recorded decision (spec Clarifications 2026-06-21 / contract `type-erased-lock-and-awaitable-cleanliness.md` New-C), not an oversight. `noexcept` is retained; it is **not** dropped.

## Test obligations (ported from harness, run on the integrated library)

These reproduce the **full** CODEX-BRIEF §6 inventory (the harness's 10 named obligations + ≥2 extras), not a 5-test subset — a fallback could otherwise silently lose `exchange`, an overload, round-trip behavior, refcount integrity, or allocator-pressure safety while still passing. The concrete named-test table + preset/sanitizer/lane mapping lives in **plan.md "Integrated test inventory"**; the obligations are:

- T-API-1: forced-fallback build green on every supported toolchain **this feature validates** (libstdc++ now; MSVC forced-fallback deferred per SC-006 narrowing — spec Clarifications 2026-06-21) — load-bearing (inventory rows 1–10).
- T-API-2: **compile-time signature coverage** — every P0718 method (incl. `exchange` and **both** CAS overload forms `(expected&, desired, success, failure)` and `(expected&, desired, order)`) exists with the correct signature via `static_assert` / `requires` (inventory row 1).
- T-API-3: single-thread `load`/`store`/`exchange` round-trips; CAS success **and** failure paths with `expected`-update; default = empty `shared_ptr` (inventory row 2).
- T-API-4: CAS three-discriminator equivalence (distinct-object ⇒ fail; aliasing-same-raw/diff-control-block ⇒ fail; shared-ownership ⇒ succeed; null/null ⇒ succeed; expected-update on fail) (inventory row 5).
- T-API-5: refcount integrity / no-UAF under contention (weak_ptr snapshot → `store(nullptr)` → `expired()`), ASan-clean (inventory row 3).
- T-API-6: publish/acquire ordering under TSan (writer release / reader acquire, no torn read) — primitive **and** the four consumers' load/store (inventory row 6).
- T-API-7: allocator-pressure + linearizability stress; ~30s randomized mixed-op stress (tunable), ASan/TSan-clean, no deadlock/leak (inventory rows 7, 10).
- T-API-8: feature-detection probe reports the correct path per cell; `is_lock_free()`/`is_always_lock_free` report `false` on fallback (inventory rows 8, 9).
- T-API-9: native path is the std alias (compile-time `static_assert(std::is_same_v<...>)` under `NATIVE_ACTIVE`) (inventory row +A).
- T-API-10: forced-mode **link** test per owning target — `tls`/`transport`/`session` build in fallback mode and the out-of-line `shard_guard` symbols resolve (Codex #4 / CT-4; inventory row +B).
