# Phase 1 Data Model: atomic_shared_ptr — libc++ portability fallback integration

**Feature**: `046-atomic-shared-ptr` | **Date**: 2026-06-20

> **REBASE CORRECTION (2026-06-22) — consumer set 4→3.** This file was authored for the original **four-consumer** design. 046 was later rebased onto merged **048** (PR #144), whose strand-local `cancel_and_drain` reap **removed** `core/sync/async_mutex.hpp drain_latch_ptr_`. The as-built production `std::atomic<std::shared_ptr<T>>` consumer set is therefore **three**: `tls/pinset.hpp snapshot_`, `transport/transport_factory.hpp cert_source_slot_`, `session/engine.hpp reader_snapshot_`. References below to "four" / "all four" / `drain_latch_ptr_` (incl. the E-4 table's row 1) are the **historical design record**; the authoritative as-built record is `spec/feature-catalogue.md` NFR-017 (+ NFR-016 E-5).

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

- 128 `std::mutex` in a function-local-static `std::array`, **defined in `src/core/sync/atomic_shared_ptr.cpp`** (one shared global table for all `atomic_shared_ptr` instances; the `.cpp` is **always compiled** into the promoted `fixpp_sync` STATIC/OBJECT target on every mode — see the always-ship-guard decision, research D-9 / contract CT-4) — NOT in the header.
- The header (`include/fixpp/core/sync/detail/atomic_shared_ptr.hpp`) declares only `detail::shard_guard` (RAII): `explicit shard_guard(const void* self) noexcept` (ctor hashes `self`, locks the shard, records the index in `shard_index_`), `~shard_guard()` (unlocks `shard_index_`), copy- **and move-deleted**. Its single header-visible member is `std::size_t shard_index_` (a plain integer — **not** a `std::mutex*`/`std::unique_lock`, which would re-introduce a banned token). Both ctor/dtor bodies live in the `.cpp`. So `<mutex>` and the `std::mutex` token are **absent from the header** → §XI.3/§XV.9 satisfied on every path (E-5). The `noexcept` ctor `std::terminate`s if `std::mutex::lock()` throws — deliberate `std::atomic`-no-throw match (Clarifications 2026-06-21 / contract New-C).
- Shard chosen by hashing the **object's address** (`this`) with a murmur-style finalizer (`x ^= x>>33; x *= 0xff51afd7ed558ccd; x ^= x>>33; & 127`) — unchanged from the harness; now computed inside the `.cpp` guard ctor.
- Consequence: distinct instances may share a shard (bounded false contention, never incorrectness); acceptable on the non-perf portability path. The `.cpp` is **always compiled** (always-ship-guard, D-9/CT-4); on native the `shard_guard` symbols it defines are **present-but-never-called** (the alias path never constructs a guard → the function-local-static lazy-init never fires), so native pays **zero runtime overhead** and SC-005 holds.

## E-4 — The four migrated members (state transitions = atomic publish/replace)

| # | Member | File (decl) | Ops (file) | Awaitable header | 023 note |
|---|---|---|---|---|---|
| 1 | `drain_latch_ptr_` | `include/fixpp/core/sync/async_mutex.hpp:249` | `load(acquire)` / `store(v|nullptr, release)` (same file) | **Yes** | — |
| 2 | `snapshot_` (`shared_ptr<const pin_snapshot>`) | `include/fixpp/tls/pinset.hpp:135` | `load(acquire|relaxed)` / `store(v, release)` (`src/tls/pinset.cpp`) | No | — |
| 3 | `cert_source_slot_` | `include/fixpp/transport/transport_factory.hpp:210` | `load(acquire)` / `store(v, release)` (`src/transport/transport_factory.cpp`) | No | — |
| 4 | `reader_snapshot_` (`shared_ptr<const ReaderSnapshot>`) | `include/fixpp/session/engine.hpp:466` | `load(acquire)` / `store(v, release)` (`src/session/engine.cpp:190/246/1592`) | **Yes** | **reverses CHK046** |

Each member's "state" is a single immutable `shared_ptr` snapshot, replaced wholesale via `store` after building the new value; readers `load` lock-free (native) or shard-locked (fallback). No partial mutation. Migration = change the member type `std::atomic<std::shared_ptr<X>>` → `fixpp::sync::atomic_shared_ptr<X>` + add the `#include "fixpp/core/sync/detail/atomic_shared_ptr.hpp"`; **no call-site *logic* change** (D-5), but the migration is **not comment-free** — see the stale-documentation census below.

### E-4a — Stale-documentation + lock-free-assumption census (Codex #8 / New-D)

"No call-site change" does **not** mean "no source change": several existing comments and any `is_lock_free`/perf-gate assumptions become false on the fallback path and MUST be corrected as part of the migration (they otherwise survive `/implement` as false records). The census (each must be reworded to "atomic snapshot load; native path lock-free status is implementation-defined, fallback path shard-locked"):

| Site | Current (false on fallback) | Action |
|---|---|---|
| `src/tls/pinset.cpp:8` | "Reader path: lock-free — single acquire-load on snapshot_." | reword (lock-free only on native) |
| `include/fixpp/tls/pinset.hpp:118-120` | `find()` doc: "Lookup on the handshake-hot path. **Lock-free** — acquire-load on snapshot_." (source-verified) | reword: lock-free **only on native**; on the libc++/forced-fallback path the `load(acquire)` takes a shard mutex (correctness/portability over speed — see plan:22) |
| `bench/tls/bench_pinset_snapshot_acquire.cpp` (`[2g §9 seam #4]`, soft gate `[const §VIII.2]`) | `detect_snapshot_ceiling_ns()` returns a **30 ns** "lock-free atomic<shared_ptr>" ceiling; branches on `#if defined(__cpp_lib_atomic_shared_ptr)` (L46-52, source-verified) | (a) the **30 ns lock-free ceiling is false on the fallback path** (the shard-locked `load` is not lock-free) → define a separate, **no-lock-free-ceiling** expectation for the libc++/forced-fallback lane; (b) **New-P3-2**: the detector MUST branch on the **fixpp selector** `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE`, **not** the raw `__cpp_lib_atomic_shared_ptr` feature-test macro — otherwise a forced-fallback build under libstdc++ (where `__cpp_lib_atomic_shared_ptr` is still defined) reports a **false-green** 30 ns lock-free ceiling. (Soft gate, so it won't hard-fail CI, but it mis-measures silently.) |
| `bench/tls/bench_pinset_find.cpp` (`[2g §9 seam #5]`, soft gate `[const §VIII.2]`) | combined **≤130 ns** ceiling ("snapshot_acquire ≤30 ns + scan_16 ≤100 ns"); same `#if defined(__cpp_lib_atomic_shared_ptr)` branch (L37-42, source-verified) | same two fixes as above — revise the libc++/forced-fallback ceiling (no 30 ns lock-free floor for the snapshot leg) and key the detector off `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE` (New-P3-2) |
| `include/fixpp/session/engine.hpp:460-462` | "Standard C++20 std::atomic<shared_ptr<>> — NO std::mutex in our headers … Not lock-free on all STLs … correctness does not depend on lock-freedom." | reword: the "Standard C++20 std::atomic" identity is literally wrong post-migration (it is now `fixpp::sync::atomic_shared_ptr`); keep the "no std::mutex in our headers" claim (still true via type-erasure) + the lock-freedom hedge |
| `include/fixpp/session/engine.hpp:119` | "Standard C++20 `std::atomic<std::shared_ptr<const ReaderSnapshot>>`" | reword to name `fixpp::sync::atomic_shared_ptr` |
| `include/fixpp/core/sync/async_mutex.hpp:21` (prose) | "lazy drain_latch_state via atomic shared_ptr" | reword to the primitive's name |
| `include/fixpp/transport/transport_factory.hpp:86-87` (prose) | "std::atomic<std::shared_ptr<…>>" | reword to the primitive's name |
| consumer **test suites** + the **V-6 perf gate** | any `static_assert(is_always_lock_free)` / perf-gate assumption of lock-freedom (the two pinset benches above are the concrete realization of this risk) | grep the four consumers' tests + V-6; confirm **none** asserts lock-freedom (the forced-fallback lane would violate it) — New-D |

Mutation-check: confirm the obsolete **023 CHK046 prohibition** ("avoid the unshipped `atomic_shared_ptr`; pin the standard primitive") is **absent** from active 023 artifacts after the FR-013 reversal (not just superseded in prose) — see research D-8.

## E-5 — Awaitable-header cleanliness (NO constitution change)

`atomic_shared_ptr.hpp` is **mutex-free on every standard library** (the lock is type-erased into the `.cpp`, E-3), so the two awaitable consumers (`async_mutex.hpp`, `engine.hpp`) that include it never gain a `std::mutex` token. Article XI §3 / XV §9 stay satisfied **as-is — no amendment, no exemption, no constitution version bump**. The existing `tools/check_no_std_mutex_in_awaitable_headers.sh` corpus gate passes under both libstdc++ and libc++ unchanged. Contract: `contracts/type-erased-lock-and-awaitable-cleanliness.md`. (The amendment alternative was considered and rejected — research D-2.)

## E-6 — `linux-clang-libc++` build profile + Tier-2 lane

- A Conan profile selecting `compiler.compiler=clang`, `compiler.libcxx=libc++`, `-stdlib=libc++` on compile+link; the dependency set rebuilt under libc++.
- A Tier-2 / opt-in CI lane (label-triggered, like `windows-msvc`): full library build + the concurrency-relevant test subset under available sanitizers, on the fallback path. OTel-non-OTel scoped per D-4.
- macOS lane (FR-011a) is the deferred follow-up — same shape, `macos-14` runner, not in this feature's acceptance.

## E-7 — Census-regrowth guard (`tools/check_no_raw_atomic_shared_ptr.sh`)

Input: project-owned headers + sources (exclude the primitive header + tests/third-party). Detects raw `std::atomic<std::shared_ptr` (and `std::atomic_{load,store,exchange,compare_exchange}` on a `shared_ptr`). Exit non-zero on any match outside `include/fixpp/core/sync/detail/atomic_shared_ptr.hpp`. Wired into the gate set; SC-004 mutation-tests it.
