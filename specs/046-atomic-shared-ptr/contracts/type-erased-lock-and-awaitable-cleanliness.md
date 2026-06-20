# Contract: type-erased lock + awaitable-header cleanliness

**Feature**: `046-atomic-shared-ptr` | Replaces the (rejected) Article XI §3 amendment. **No constitution change.**

## Goal

Keep `atomic_shared_ptr.hpp` free of `std::mutex` (and the FR-014 six banned mutex types) on **every** standard library, so the two awaitable consumers that include it (`async_mutex.hpp`, `engine.hpp`) never breach Article XI §3 / XV §9 — without amending the constitution.

## Header/`.cpp` boundary

- **Header (`include/fixpp/sync/atomic_shared_ptr.hpp`)**:
  - Native path: `using atomic_shared_ptr = std::atomic<std::shared_ptr<T>>;` (no lock at all).
  - Fallback path: the `atomic_shared_ptr<T>` class with `load`/`store`/`exchange`/`compare_exchange` whose bodies acquire `fixpp::sync::detail::shard_guard g(this);` (RAII) instead of `std::lock_guard<std::mutex>`.
  - **MUST NOT** `#include <mutex>` or name any of the six banned mutex types.
  - Declares `namespace detail { class shard_guard { public: explicit shard_guard(const void* self) noexcept; ~shard_guard(); shard_guard(const shard_guard&) = delete; shard_guard& operator=(const shard_guard&) = delete; private: /* opaque: shard index/handle */ }; }`.
- **TU (`src/sync/atomic_shared_ptr.cpp`)** (compiled only when `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 0`):
  - Defines the 128-`std::mutex` function-local-static shard table + the address-hash (murmur finalizer).
  - Defines `shard_guard::shard_guard(const void*)` (hash → lock) and `~shard_guard()` (unlock).
  - This is the **only** project TU that names `std::mutex` for this primitive.

## Behavioral equivalence to the harness

- Same sharding (128, address-hash with high-bit mixing), same lock discipline (one shard lock per op, released on return), same CAS-equivalence and memory-order behavior. The lock is held only across a synchronous `shared_ptr` op; **never across a `co_await`**.

## Acceptance

- **CT-1**: `tools/check_no_std_mutex_in_awaitable_headers.sh` passes for `async_mutex.hpp` + `engine.hpp` when preprocessed under **both** libstdc++ **and** libc++ (header carries no `std::mutex` token on either) — the load-bearing no-amendment check.
- **CT-2**: a `static_assert`/grep confirms `atomic_shared_ptr.hpp` does not `#include <mutex>` nor name a banned mutex type.
- **CT-3**: the fallback's correctness tests (CAS-equivalence, publish/acquire ordering under TSan, forced-fallback acceptance) pass with the type-erased guard — proving the adaptation preserved the harness semantics.
- **CT-4**: native build links with **no** `atomic_shared_ptr.cpp` symbols required (the `.cpp` is empty/absent on the native path); fallback build links the guard symbols.
