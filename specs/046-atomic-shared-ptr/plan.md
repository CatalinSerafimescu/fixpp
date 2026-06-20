# Implementation Plan: atomic_shared_ptr — libc++ portability fallback integration

**Branch**: `046-atomic-shared-ptr` | **Date**: 2026-06-20 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/046-atomic-shared-ptr/spec.md`

## Summary

Integrate the **harness-validated** `fixpp::sync::atomic_shared_ptr<T>` primitive into the fixpp library so the library compiles and passes its tests under libc++ (today it does not build under libc++ at all). On P0718-capable standard libraries (libstdc++, MSVC-STL) the primitive is a **zero-overhead alias** to `std::atomic<std::shared_ptr<T>>`; on libc++ it resolves to an address-hash-sharded, mutex-guarded `shared_ptr` fallback over the public `shared_ptr` API, with the **lock type-erased into a `.cpp`** so the header carries no `std::mutex`. The work migrates **all four** production raw `std::atomic<std::shared_ptr<T>>` consumers to the primitive, adds a census-regrowth guard, stands up a Tier-2 opt-in `linux-clang-libc++` regression lane, and reverses feature 023's CHK046 prohibition for the engine reader-snapshot.

**No constitutional amendment** (decided 2026-06-20): two consumers (`async_mutex.hpp`, `engine.hpp`) are awaitable headers where a header-resident `std::mutex` would breach Article XI §3 / XV §9. Rather than amend that rule with a bounded-mutex exemption, the fallback's 128-mutex shard table + an opaque RAII lock guard are moved **out-of-line into `src/sync/atomic_shared_ptr.cpp`** (the header declares only the guard), so `atomic_shared_ptr.hpp` contains no `std::mutex` token on **either** standard library — the awaitable-header gate passes under libc++ too, with no rule change. This honors the project's "amend only when a code change cannot achieve the same correctness" principle and *removes* (rather than overrides) 023's CHK046 objection.

**Technical approach**: the primitive's *algorithm* is adopted from the locked 18/18 research harness (sharding, CAS-equivalence, vendor-macro detection, memory-order honoring — unchanged); the only structural adaptation is the type-erasure of the lock (header-only → header + `.cpp`), re-validated by the integration's own libc++ + forced-fallback sanitizer lanes. The remaining engineering: (1) verify each of the four consumers' exact call-site usage against the fallback's surface (verified: `load`/`store` only — no `exchange`/CAS/`wait`/`notify`); (2) the libc++ toolchain profile + full C++ dependency rebuild, with per-dependency scope-out if any dep will not build under libc++ this cycle (OTel + its protobuf/abseil/grpc stack the prime risk).

## Technical Context

**Language/Version**: C++23 (`-std=c++23`; project standard, matches the existing build + corpus gate)
**Primary Dependencies**: standard library only for the primitive (libstdc++ ≥ P0718 / MSVC-STL native; libc++ fallback). Build/infra: a new `linux-clang-libc++` Conan profile (`compiler.libcxx=libc++`) + full dependency set rebuilt under libc++ (asio header-only = free; **OpenTelemetry-cpp** = the build watch-item). No new third-party runtime dependency.
**Storage**: N/A
**Testing**: ctest/gtest; the harness's validated test ideas ported to the integrated library (CAS-equivalence, memory-order publish/acquire, multi-instance isolation, forced-fallback acceptance); ASan/UBSan/TSan matrix; the new census-regrowth guard; the existing §XI.3/§XV.9 awaitable-mutex corpus gate.
**Target Platform**: Linux libstdc++ (Tier-1, unchanged), Linux libc++ (Tier-2 opt-in, **new** — validated here), Windows-MSVC (Tier-2, unchanged); macOS libc++ (FR-011a deferred follow-up), FreeBSD (further follow-on). The primitive enables all.
**Project Type**: C++ library (FIX engine) — internal sync utility addition + build-infra.
**Performance Goals**: native (Tier-1) path **zero overhead** — the primitive is a type alias, byte-identical codegen; fallback path optimizes for **correctness over speed** (libc++ is a portability target, not a perf target — the lock is a bounded `shared_ptr` copy on a non-hot republish path).
**Constraints**: no new public API type, no wire/error/config/codegen/C-ABI surface (FR-015); **no constitution change** (FR-012 type-erasure keeps the fallback `std::mutex` out of awaitable headers); **zero Tier-1 regression** (SC-005); the corpus gate stays green under both libstdc++ and libc++.
**Scale/Scope**: 1 primitive (1 verbatim detect header + 1 type-erased main header + 1 new `.cpp`), 4 migrated consumers (2 of them awaitable headers), 1 census-regrowth guard, 1 Tier-2 CI lane, **0 constitution amendments**, 1 prior-feature decision reversal (023 CHK046), 2 recording obligations (catalogue NFR-017, 006 research D-4).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Mandatory-trigger classification (Appendix A)**: **Threading / concurrency** — this changes a concurrency primitive used in coroutine-context headers. Triggers **all four** controls: `/speckit-clarify` (DONE), `/speckit-analyze` (pending step 6), **Codex Gate A** (pending — a normal feature review; **no amendment to ratify**), **user `/plan` sign-off** (pending). Not a Security trigger (TLS behavior unchanged; `cert_source_slot_` migration is representation-only). Not an ABI/wire/error/codegen/FSM trigger (FR-015). **No constitution amendment** — the §XI.3 collision is resolved by code (FR-012 type-erasure), not by rule change.

| Article / Gate | Status | Notes |
|---|---|---|
| **XI §3 — `std::mutex` banned in awaitable headers** | **PASS (via FR-012 type-erasure)** | The fallback's `std::mutex` + shard table live out-of-line in `src/sync/atomic_shared_ptr.cpp`; `atomic_shared_ptr.hpp` declares only an opaque RAII guard and carries **no `std::mutex` token on either standard library**. So no awaitable header (`async_mutex.hpp`, `engine.hpp`) acquires a banned mutex symbol — under libstdc++ **or** libc++. No exemption, no amendment. (Alternative — a bounded-mutex §XI.3 exemption — considered and rejected; research D-2.) |
| **XV §9 — banned-pattern pointer to XI §3** | PASS | Unchanged; satisfied as-is (the header is mutex-free). |
| **XV §1 — no per-message/hot-path heap alloc** | PASS | The fallback lock/`shared_ptr` copy is on the snapshot-**republish** control path, not the per-message in-memory hot path; on Tier-1 the primitive is the std alias (unchanged). libc++ is not a perf target. |
| **II §4 — no global compiler-version pin** | PASS | Detection is vendor-macro gated (`_LIBCPP_VERSION` / `__GLIBCXX__ + __cpp_lib_atomic_shared_ptr` / `_MSC_VER`) with safe-default-to-fallback; no compiler-version pin. |
| **IX §1 — coverage (lcov DA/BRDA)** | PASS (planned) | Fallback-path lines (incl. the new `.cpp` guard) covered by the ported harness tests + forced-fallback lane (SC-003/SC-006); native alias is trivially covered by existing consumer tests. |
| **XV §12 — no LGPL deps** | PASS | No new dependency; the primitive is original (sharded mutex over the public `shared_ptr` API). |
| **FR-015 — no new public/wire/error/codegen/C-ABI surface** | PASS | `fixpp::sync::atomic_shared_ptr` is a library-internal sync utility; consumers are private members. The new `.cpp` is an internal TU. |
| **023 CHK046 — reader_snapshot_ pinned to std primitive** | **REVERSE (FR-013)** | 023 forbade `atomic_shared_ptr` here because its fallback would have put `std::mutex` in the awaitable header. FR-012 type-erasure **removes that objection** (mutex-free header), so the reversal is grounded in the objection no longer applying — not an override. Recorded as an explicit decision; 023 artifacts cross-referenced. |

**Gate verdict**: PASS to proceed — **no constitution violations** (the §XI.3 collision is resolved by code, not amendment; research D-2). Gate A is a normal Threading-trigger feature review with no amendment to sign off.

## Project Structure

### Documentation (this feature)

```text
specs/046-atomic-shared-ptr/
├── plan.md              # This file
├── research.md          # Phase 0 — D-1..D-9 (adopt primitive algorithm, type-erase the lock [no amendment], libc++ profile, dep scoping, consumer API-compat, census guard, CAS rationale, CHK046 reversal, force-fallback CI)
├── data-model.md        # Phase 1 — E-1..E-7 (primitive API, detection macros, type-erased shard table+.cpp, the 4 consumers' call-sites, awaitable-cleanliness, CI lane config, census guard)
├── quickstart.md        # Phase 1 — build under libc++, force-fallback, run the lane
├── contracts/
│   ├── atomic-shared-ptr-api.md         # the primitive's API + CAS-equivalence + memory-order contract
│   ├── type-erased-lock-and-awaitable-cleanliness.md # the header-mutex-free contract + .cpp/guard boundary (replaces the amendment contract)
│   └── census-regrowth-guard.md         # the gate contract (fails on a new raw std::atomic<std::shared_ptr>)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
include/fixpp/sync/
├── atomic_shared_ptr.hpp          # NEW — harness algorithm, ADAPTED: lock type-erased (header declares only detail::shard_guard; no <mutex>, no std::mutex token)
└── atomic_shared_ptr_detect.hpp   # NEW — copied verbatim from the locked harness (vendor-macro detection + 2 force overrides)

src/sync/atomic_shared_ptr.cpp     # NEW — out-of-line shard table (128 std::mutex) + detail::shard_guard lock/unlock; compiled only on the fallback path

include/fixpp/core/sync/async_mutex.hpp        # MIGRATE drain_latch_ptr_   (awaitable header — stays mutex-free)
include/fixpp/tls/pinset.hpp                   # MIGRATE pin_snapshot        (non-awaitable)
include/fixpp/transport/transport_factory.hpp  # MIGRATE cert_source_slot_  (non-awaitable)
include/fixpp/session/engine.hpp               # MIGRATE reader_snapshot_    (awaitable header — stays mutex-free; reverses 023 CHK046)

tests/sync/                                     # NEW — ported harness tests against the integrated primitive
tools/check_no_raw_atomic_shared_ptr.sh         # NEW — census-regrowth guard (FR-005)
tools/check_no_std_mutex_in_awaitable_headers.sh # EXISTING — re-run; passes under BOTH libstdc++ and libc++ (header is mutex-free); no change

# NO constitution change. Conan/CI: new linux-clang-libc++ profile + Tier-2 opt-in lane (build-infra paths per repo convention).
```

**Structure Decision**: Single C++ library. The primitive lands under the existing `include/fixpp/sync/` tree: the detect header is copied verbatim; the main header keeps the harness's validated *algorithm* but **type-erases the lock** (the `std::mutex` shard table + guard bodies move to a new `src/sync/atomic_shared_ptr.cpp`, compiled only on the fallback path; the native path stays a pure header alias with no `.cpp` needed). Migrations are in-place edits to four existing member declarations (no call-site change). The new gate and CI profile follow the repo's existing `tools/` + Conan-profile conventions. No new module, no layer-graph change (`sync` is already a base layer). The primitive is library-internal, so adding a `.cpp` to the library build is invisible to consumers.

## Complexity Tracking

**No constitution violations** — the §XI.3 awaitable-mutex collision is resolved by code (FR-012 type-erasure), not by amendment, so no justification table is required. The alternatives that *were* weighed (and rejected) are recorded in research **D-2**: a bounded-mutex §XI.3 exemption (rejected — amending the constitution when a localized code change achieves the same correctness violates the Article XX §1 spirit and would leave a latent gate/exemption contradiction under the libc++ lane); rewiring only the 2 non-awaitable consumers (rejected — the awaitable members would not compile under libc++, so the feature would deliver nothing); a lock-free fallback (does not exist); `async_mutex` in the fallback (wrong shape — it is a coroutine awaitable mutex; the primitive's `load`/`store` are synchronous). The one structural cost — the primitive becomes header + `.cpp` on the fallback path (vs the harness's header-only form) — is not a constitution concern; it is re-validated by the integration's libc++ + forced-fallback sanitizer lanes.
