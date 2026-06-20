# Phase 1 Data Model: atomic_shared_ptr — libc++ portability fallback integration

**Feature**: `046-atomic-shared-ptr` | **Date**: 2026-06-20

This feature adds a sync primitive and migrates four members; there is no domain/wire data model. The "entities" below are the integrated type, its detection inputs, the migrated members, and the gate/build artifacts.

## E-1 — `fixpp::sync::atomic_shared_ptr<T>` (the primitive)

Two resolutions selected at preprocess time by `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE`:

- **Native (`==1`, libstdc++ ≥ P0718 / MSVC-STL)**: `using atomic_shared_ptr = std::atomic<std::shared_ptr<T>>;` — exact std type, zero overhead.
- **Fallback (`==0`, libc++ / forced)**: a class with `value_type = std::shared_ptr<T>` and:

| Member | Signature | Notes |
|---|---|---|
| ctors | `()`, `(std::nullptr_t)`, `(value_type)` | default-null; value ctor moves |
| copy | `= delete` (copy ctor + copy assign) | matches `std::atomic` |
| `operator=(value_type)` | `void … noexcept` | seq-cst store |
| `operator value_type()` | `… const noexcept` | seq-cst load |
| `is_lock_free()` / `is_always_lock_free` | `false` | honest non-lock-free |
| `store(value_type, memory_order = seq_cst)` | `noexcept` | shard-locked assign |
| `load(memory_order = seq_cst) const` | `noexcept` | shard-locked copy |
| `exchange(value_type, memory_order = seq_cst)` | `noexcept` | shard-locked swap |
| `compare_exchange_weak/strong(value_type&, value_type, …)` | 2-order + 1-order overloads, `noexcept` | weak delegates to strong |

**Omitted** (vs P0718): `wait` / `notify_one` / `notify_all` — documented gap; **no consumer uses them** (D-5).

**Invariants**:
- INV-1: On the native path the type IS `std::atomic<std::shared_ptr<T>>` (alias identity), so any std-conformant usage compiles unchanged.
- INV-2: The fallback never exposes a non-public `shared_ptr` representation; all access is via the public `shared_ptr` API.
- INV-3: A `load` never returns a torn/partial value; a concurrent `store`/`load` returns either the prior or the new fully-constructed `shared_ptr` (publish/acquire).

## E-2 — Detection inputs (`atomic_shared_ptr_detect.hpp`)

`FIXPP_HAS_STD_ATOMIC_SHARED_PTR` resolved (first match wins): force-fallback → `0`; force-native → `1`; `_LIBCPP_VERSION` → `0`; `__GLIBCXX__ && __cpp_lib_atomic_shared_ptr ≥ 201711L` → `1`; `_MSC_VER && __cpp_lib_atomic_shared_ptr ≥ 201711L` → `1`; else → `0` (safe default). The two `FIXPP_FORCE_*` macros are mutually exclusive (`#error` if both defined). Derived selector `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE = (HAS==1 && !FORCE_FALLBACK)`.

## E-3 — Shard table + type-erased lock (fallback internals)

- 128 `std::mutex` in a function-local-static `std::array`, **defined in `src/sync/atomic_shared_ptr.cpp`** (one shared global table for all `atomic_shared_ptr` instances) — NOT in the header.
- The header declares only `detail::shard_guard` (opaque RAII): `explicit shard_guard(const void* self) noexcept` (ctor hashes `self`, locks the shard, stores the index), `~shard_guard()` (unlocks), copy-deleted. Both bodies live in the `.cpp`. So `<mutex>` and the `std::mutex` token are **absent from the header** → §XI.3/§XV.9 satisfied on every path (E-5).
- Shard chosen by hashing the **object's address** (`this`) with a murmur-style finalizer (`x ^= x>>33; x *= 0xff51afd7ed558ccd; x ^= x>>33; & 127`) — unchanged from the harness; now computed inside the `.cpp` guard ctor.
- Consequence: distinct instances may share a shard (bounded false contention, never incorrectness); acceptable on the non-perf portability path. The `.cpp` is compiled only when `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 0` (empty TU on native).

## E-4 — The four migrated members (state transitions = atomic publish/replace)

| # | Member | File (decl) | Ops (file) | Awaitable header | 023 note |
|---|---|---|---|---|---|
| 1 | `drain_latch_ptr_` | `include/fixpp/core/sync/async_mutex.hpp:249` | `load(acquire)` / `store(v|nullptr, release)` (same file) | **Yes** | — |
| 2 | `snapshot_` (`shared_ptr<const pin_snapshot>`) | `include/fixpp/tls/pinset.hpp:135` | `load(acquire|relaxed)` / `store(v, release)` (`src/tls/pinset.cpp`) | No | — |
| 3 | `cert_source_slot_` | `include/fixpp/transport/transport_factory.hpp:210` | `load(acquire)` / `store(v, release)` (`src/transport/transport_factory.cpp`) | No | — |
| 4 | `reader_snapshot_` (`shared_ptr<const ReaderSnapshot>`) | `include/fixpp/session/engine.hpp:466` | `load(acquire)` / `store(v, release)` (`src/session/engine.cpp:190/246/1592`) | **Yes** | **reverses CHK046** |

Each member's "state" is a single immutable `shared_ptr` snapshot, replaced wholesale via `store` after building the new value; readers `load` lock-free (native) or shard-locked (fallback). No partial mutation. Migration = change the member type `std::atomic<std::shared_ptr<X>>` → `fixpp::sync::atomic_shared_ptr<X>` + add the `#include "fixpp/sync/atomic_shared_ptr.hpp"`; **no call-site change** (D-5).

## E-5 — Awaitable-header cleanliness (NO constitution change)

`atomic_shared_ptr.hpp` is **mutex-free on every standard library** (the lock is type-erased into the `.cpp`, E-3), so the two awaitable consumers (`async_mutex.hpp`, `engine.hpp`) that include it never gain a `std::mutex` token. Article XI §3 / XV §9 stay satisfied **as-is — no amendment, no exemption, no constitution version bump**. The existing `tools/check_no_std_mutex_in_awaitable_headers.sh` corpus gate passes under both libstdc++ and libc++ unchanged. Contract: `contracts/type-erased-lock-and-awaitable-cleanliness.md`. (The amendment alternative was considered and rejected — research D-2.)

## E-6 — `linux-clang-libc++` build profile + Tier-2 lane

- A Conan profile selecting `compiler.compiler=clang`, `compiler.libcxx=libc++`, `-stdlib=libc++` on compile+link; the dependency set rebuilt under libc++.
- A Tier-2 / opt-in CI lane (label-triggered, like `windows-msvc`): full library build + the concurrency-relevant test subset under available sanitizers, on the fallback path. OTel-non-OTel scoped per D-4.
- macOS lane (FR-011a) is the deferred follow-up — same shape, `macos-14` runner, not in this feature's acceptance.

## E-7 — Census-regrowth guard (`tools/check_no_raw_atomic_shared_ptr.sh`)

Input: project-owned headers + sources (exclude the primitive header + tests/third-party). Detects raw `std::atomic<std::shared_ptr` (and `std::atomic_{load,store,exchange,compare_exchange}` on a `shared_ptr`). Exit non-zero on any match outside `include/fixpp/sync/atomic_shared_ptr.hpp`. Wired into the gate set; SC-004 mutation-tests it.
