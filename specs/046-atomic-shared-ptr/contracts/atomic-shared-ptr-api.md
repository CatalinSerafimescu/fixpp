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
- **C-4 (memory_order honored)**: order arguments are accepted on every operation; the fallback provides **at least** the requested acquire/release (lock is seq-cst-ish); `failure_order` maps release→relaxed, acq_rel→acquire.
- **C-5 (noexcept)**: all operations are `noexcept` (a `shared_ptr` copy can allocate only its control block on construction by the caller, not inside these ops; the ops only copy/swap existing `shared_ptr`s).

## Test obligations (ported from harness, run on the integrated library)

- T-API-1: forced-fallback build green on every supported toolchain (load-bearing).
- T-API-2: CAS-equivalence (aliasing-distinct-control-block ⇒ fail; null/null ⇒ succeed; expected-update on fail).
- T-API-3: publish/acquire ordering under TSan (writer release / reader acquire, no torn read).
- T-API-4: multi-instance isolation (distinct instances independent across shard collisions).
- T-API-5: native path is the std alias (compile-time `static_assert(std::is_same_v<...>)` under `NATIVE_ACTIVE`).
