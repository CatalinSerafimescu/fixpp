# Phase 0 Research: atomic_shared_ptr — libc++ portability fallback integration

**Feature**: `046-atomic-shared-ptr` | **Date**: 2026-06-20

This feature integrates the **algorithm** of a design-locked primitive (validated 18/18 in the research harness `research/G19-fix-fpml-iso20022/atomic-shared-ptr/`), so Phase 0 is mostly *consolidation and reconciliation*, not open research. The genuine decisions were how to keep the fallback `std::mutex` out of the awaitable headers (D-2 — resolved by type-erasure, **no constitution amendment**), the consumer API-compatibility (D-5), and the libc++ build/dependency story (D-3/D-4) — all resolved below.

---

## D-1 — Adopt the locked primitive's algorithm; adapt only the lock packaging

- **Decision**: Bring `atomic_shared_ptr_detect.hpp` over **verbatim** and `atomic_shared_ptr.hpp` with its **algorithm unchanged** (native alias | 128-shard address-hashed mutex-guarded `shared_ptr` over the public API; CAS-equivalence; vendor-macro detection; memory-order honoring) into `include/fixpp/sync/` (the same path the harness targets). The **one adaptation** (D-2): the `std::mutex` shard table + the lock acquisition are **type-erased out of the header into `src/sync/atomic_shared_ptr.cpp`**; the header uses an opaque `detail::shard_guard(this)` RAII type instead of `std::lock_guard<std::mutex>`.
- **Rationale**: The design is locked and proven across (gcc/clang × libstdc++/libc++ × none/ASan+UBSan/TSan × native/forced-fallback) = 18/18; the algorithm is not re-derived. The lock-packaging adaptation is mechanical (where the mutex lives, not how it shards/locks) and is re-validated by this feature's own libc++ + forced-fallback sanitizer lanes — it is the enabler for the no-amendment outcome (D-2).
- **Alternatives considered**: Transplant libstdc++'s internal split-reference `atomic<shared_ptr>` onto libc++ — **rejected** (UB: libc++'s `shared_ptr` has a different control-block layout, compounded by `_LIBCPP_SHARED_PTR_TRIVIAL_ABI`). A lock-free DWCAS tagged-pointer design — rejected as out-of-scope re-research for a portability fallback.

## D-2 — Keep the fallback mutex out of awaitable headers by type-erasure → NO constitution amendment *(central design item)*

- **Decision**: **Type-erase the fallback lock into a `.cpp`** so `atomic_shared_ptr.hpp` contains **no `std::mutex` token (and no `#include <mutex>`) on either standard library**. The header declares only `namespace fixpp::sync::detail { class shard_guard { explicit shard_guard(const void* self) noexcept; ~shard_guard(); shard_guard(const shard_guard&) = delete; /* holds an opaque index/handle */ }; }`; `src/sync/atomic_shared_ptr.cpp` defines the 128-`std::mutex` shard table + the guard's ctor (hash `self` → lock shard) / dtor (unlock), compiled only when `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE == 0`. The fallback class methods do `detail::shard_guard g(this);` in place of `std::lock_guard<std::mutex>`. **No constitution amendment is made** — Article XI §3 / XV §9 stay satisfied as-is, and `tools/check_no_std_mutex_in_awaitable_headers.sh` passes under **both** libstdc++ and libc++ (the header is mutex-free on every path).
- **Rationale**: §XI.3 exists to prevent a `std::mutex` token (and a mutex held across a suspension) in coroutine-context headers. Two of the four consumers (`async_mutex.hpp`, `engine.hpp`) are awaitable headers, so the naive header-only fallback would breach §XI.3 on the libc++ path. Type-erasure removes the token from the header entirely, so the rule is honored **by construction** with no rule change — aligning with Article XX §1's spirit ("resolve conflicts by amending the rule **only** when a feature genuinely cannot satisfy it"; here a localized code change does). The lock semantics are unchanged: still a bounded O(1) synchronous lock, never held across a `co_await` (the guard is acquired and released entirely within a synchronous `load`/`store`). The native path is unaffected (pure alias, no `.cpp`, no guard).
- **Alternatives considered (this is the round-1 Gate A question)**:
  - **(REJECTED) Amend Article XI §3 with a bounded-mutex exemption** (adopt the harness header verbatim, std::mutex in the awaitable header on the libc++ path, exempted because the lock is synchronous/never-across-co_await/libc++-only; folded into Gate A per Article XX with a MINOR bump; precedent 035 §XV.4 / 043 §XII.5). Rejected because (i) it amends the constitution when a localized code change (type-erasure) achieves identical correctness — against Article XX §1's spirit; (ii) it leaves a **latent contradiction**: the corpus gate is preprocessor-based and green on Tier-1 (fallback preprocessed out) but would **fire under the libc++/macOS lane** unless separately taught the exemption — i.e., the granted exemption and the enforced gate disagree; (iii) it reverses 023's CHK046 by *override* rather than by removing the objection. Type-erasure has none of these costs. The only thing the amendment buys — the harness header stays byte-verbatim — is low value, since the change is mechanical and re-validated by the integration lanes anyway.
  - **(REJECTED) `fixpp::sync::async_mutex` in the fallback** — it is a *coroutine awaitable* mutex; `load`/`store` are synchronous non-coroutine calls with no `co_await` available, so it is the wrong primitive.
  - **(REJECTED) Rewire only the 2 non-awaitable consumers** — leaves the awaitable members as raw `std::atomic<std::shared_ptr>`, which still fail to compile on libc++ → feature delivers nothing.
  - **(REJECTED) A lock-free fallback** — does not exist (the reason the sharded-mutex fallback exists).

## D-3 — libc++ toolchain profile + dependency rebuild

- **Decision**: Add a `linux-clang-libc++` Conan profile (`compiler.libcxx=libc++`) and rebuild the full dependency set under libc++ (libc++ and libstdc++ are ABI-incompatible — they cannot be mixed in one link). The llvm toolchain on the build host ships a complete libc++ (headers + `libc++.{so,a}` + `libc++abi`), so the toolchain is present; only a new profile + a libc++ dep build is needed.
- **Rationale**: A clean libc++ link requires every TU and every dependency compiled with `-stdlib=libc++`. asio is header-only (no rebuild cost).
- **Alternatives considered**: Mix libc++ app code with libstdc++ deps — rejected (ABI-incompatible, link/runtime UB).

## D-4 — Dependencies under libc++: rebuild the full C++ set; scope out any that won't port this cycle

- **Decision** (Clarifications 2026-06-20): Rebuild the **full C++ dependency set** under libc++ — current `conanfile.py` (re-censused 2026-06-20, NOT trusting the stale memory): C++ deps = **opentelemetry-cpp/1.26.0** (+ its transitive protobuf/abseil/grpc — the heaviest risk), **quill/11.1.0**, **tomlplusplus/3.4.0**, **pugixml/1.15**, **crc32c/1.1.2**, **gtest/1.17.0**, **benchmark/1.9.5**; `asio/1.38.0` is header-only (free) and `openssl/3.6.2` is C (libc++-agnostic). If **any** C++ dependency fails to build under libc++ this cycle (OTel + its protobuf/abseil/grpc stack is the prime candidate), scope the `linux-clang-libc++` lane to a build configuration that **excludes the failing dependency's target** (e.g., the existing OTel-off configuration) and record that dependency's libc++ port as a tracked follow-up. The primitive integration MUST NOT be blocked on a dependency port.
- **Rationale**: libc++/libstdc++ are ABI-incompatible, so every C++ TU + dep must compile under libc++. The NFR-017 goal (the primitive + the four consumers building on libc++) is orthogonal to whether each heavyweight dep ports. Decoupling avoids holding the portability win hostage to a dependency issue. *(Memory-staleness note: the 2026-06-03 memory named only OTel; the live conanfile adds quill/tomlplusplus since — same staleness class that under-counted the consumers; re-censused here.)*
- **Alternatives considered**: Block on full dep parity under libc++ (rejected — couples unrelated risk); permanently disable the failing dep on libc++ (rejected — a lasting capability gap; the follow-up should close it). Whether a dep actually fails is an empirical implement-phase finding, not a pre-decided exclusion.

## D-5 — Consumer API-compatibility against the fallback's reduced surface *(verified)*

- **Decision**: Migrate all four members in place; no call-site logic changes are required. Verified usage (2026-06-20):
  | Consumer (member) | Header | Operations used | Awaitable header? |
  |---|---|---|---|
  | `async_mutex.hpp` (`drain_latch_ptr_`) | `core/sync/async_mutex.hpp` | `.load(acquire)`, `.store(v, release)`, `.store(nullptr, release)` | **Yes** |
  | `pinset` (`snapshot_`) | `tls/pinset.hpp` (ops in `src/tls/pinset.cpp`) | `.load(acquire|relaxed)`, `.store(v, release)` | No |
  | `transport_factory` (`cert_source_slot_`) | `transport/transport_factory.hpp` (ops in `.cpp`) | `.load(acquire)`, `.store(v, release)` | No |
  | `engine` (`reader_snapshot_`) | `session/engine.hpp` (ops in `src/session/engine.cpp`) | `.load(acquire)`, `.store(v, release)` | **Yes** (reverses 023 CHK046) |
- **Rationale**: Every call site uses only `load`/`store` with a `memory_order` argument — both fully supported by the fallback (which accepts and honors the order, providing at least the requested acquire/release via the lock). **No** consumer uses `exchange`, `compare_exchange`, the conversion operators, or `wait`/`notify` (the surface the fallback omits) — confirmed by an exhaustive grep. The fallback's reduced surface is therefore sufficient.
- **Edge note (no nested-lock hazard)**: `pinset` move/copy does `snapshot_.store(other.snapshot_.load(...), ...)`; C++ fully evaluates the inner `load` (lock acquired+released) before the outer `store`'s lock is taken, so the two shard locks are never held nested → no deadlock even when both operands hash to the same shard.

## D-6 — Census-regrowth guard (FR-005 / SC-004)

- **Decision**: Add `tools/check_no_raw_atomic_shared_ptr.sh` — a gate that fails if a raw `std::atomic<std::shared_ptr<…>>` (or `std::atomic_*` free-function form on a `shared_ptr`) appears in any project-owned header/source **other than** the primitive's own `atomic_shared_ptr.hpp`. Wire it into the same CI step set as the existing corpus gate.
- **Rationale**: Fixing today's four sites is necessary but not sufficient — a future edit re-introducing the raw form silently re-breaks libc++. The guard enforces **exact-set completeness** (per [[feedback_completeness_gate_exact_set_not_subset]] / [[feedback_census_all_handrolled_scanners_before_scoping_parse_fix]]); SC-004 mutation-tests it by injecting a raw re-introduction and asserting the gate fails.
- **Alternatives considered**: clang-tidy custom check — heavier to author/maintain than a focused grep gate; deferred as a possible later hardening.

## D-7 — CAS-equivalence & memory-order semantics (locked rationale, carried for Gate A)

- **Decision**: Keep the harness's CAS-equivalence exactly: a `compare_exchange` succeeds iff **both** stored-pointer equality (`expected.get() == value_.get()`) **and** owner equality (`!owner_before` both ways) hold; aliasing `shared_ptr`s with the same raw pointer but different control blocks compare **unequal**; default-null vs default-null compare equal. Memory-order arguments are accepted; the lock provides ≥ acquire/release; `failure_order()` downgrades release→relaxed / acq_rel→acquire for the failure path.
- **Rationale**: This matches P0718's observable semantics so the native and fallback paths are behaviorally interchangeable for the consumers (none of which actually use CAS today, but the equivalence must hold for the primitive to be a true drop-in and for the ported CAS tests to pass). Carried explicitly because it was a corrected point from the harness's round-1 brief.

## D-8 — Reverse feature 023's CHK046 prohibition (FR-013)

- **Decision**: Migrate `engine.hpp::reader_snapshot_` to `fixpp::sync::atomic_shared_ptr<const ReaderSnapshot>`. Record the reversal explicitly: 023's CHK046 ("avoid the unshipped `atomic_shared_ptr`; pin the standard primitive, whose `std::mutex` fallback is barred from the awaitable header") was correct **for the header-only fallback shape it assumed**; the D-2 **type-erasure removes that objection** (the migrated `engine.hpp` stays mutex-free), so the migration is now both legal (no §XI.3 breach) and necessary (libc++ build). Cross-reference 023's `checklists/concurrency.md` CHK046, `spec.md §FR-014`, `research.md`, `data-model.md`, `contracts/`, and `tasks.md` T023 so the prohibition is not left as a live contradiction.
- **Rationale**: A silent swap would leave 023's artifacts asserting the opposite; the reversal must be an auditable decision. Note it is grounded in the objection **no longer applying** (type-erased, mutex-free header), not in overriding the rule — a stronger basis than an exemption.

## D-9 — Force-fallback CI exercise on native toolchains (FR-010 / SC-006)

- **Decision**: Exercise the fallback path even on the native (libstdc++) toolchain by building a test configuration with `-DFIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK` and running the concurrency/primitive tests under sanitizers. The `FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE` override exists as a documented (untested) escape hatch.
- **Rationale**: Keeps the fallback continuously regression-protected without requiring a libc++ host on every run (the load-bearing harness acceptance was exactly the forced-fallback cells passing across all toolchains). The detect header already enforces the two force macros are mutually exclusive (`#error`).

---

## Resolved unknowns

All Technical-Context items are resolved; no `NEEDS CLARIFICATION` remains. The three clarified scope items (platform = Linux libc++ now + macOS deferred; Tier-2 opt-in; OTel-non-OTel scoping) are encoded in D-3/D-4 and the spec FRs.
