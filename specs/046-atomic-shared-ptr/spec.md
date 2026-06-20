# Feature Specification: atomic_shared_ptr — libc++ portability fallback integration

**Feature Branch**: `046-atomic-shared-ptr`
**Created**: 2026-06-20
**Status**: Draft
**Input**: User description: "Start atomic_shared_ptr (NFR-017) — integrate the proven `fixpp::sync::atomic_shared_ptr<T>` fallback primitive into fixpp so the library builds and runs on libc++ (and, by extension, macOS/FreeBSD), where the standard `std::atomic<std::shared_ptr<T>>` (P0718) is unavailable. The isolated research harness is DONE (18/18 across gcc/clang × libstdc++/libc++ × sanitizers × native/forced-fallback). This feature lands the primitive in the library, rewires all production raw `std::atomic<std::shared_ptr<T>>` consumers, and adds an ongoing libc++ regression lane. Folds in the required Article XV §9 amendment (sharded-mutex fallback in awaitable headers) per Article XX, and reverses feature 023's CHK046 prohibition for the engine reader-snapshot. NFR-017; 006 research.md D-4 follow-up."

## Overview

fixpp uses the C++20 standard primitive `std::atomic<std::shared_ptr<T>>` (P0718) in four production members to publish immutable shared-state snapshots lock-free for readers. This primitive is **only usable on standard libraries that implement P0718** — today's build matrix (libstdc++, MSVC-STL) does. **libc++ does not** ship a working P0718 specialization: it resolves to the primary `std::atomic<T>` template, trips `is_trivially_copyable`, and hard-errors at instantiation (compounded by `_LIBCPP_SHARED_PTR_TRIVIAL_ABI`). As a direct consequence, **fixpp does not compile under `-stdlib=libc++` at all today** — which blocks the entire libc++/macOS/FreeBSD reach.

A drop-in replacement primitive, `fixpp::sync::atomic_shared_ptr<T>`, has already been designed, implemented, and validated in an isolated research harness (`research/G19-fix-fpml-iso20022/atomic-shared-ptr/`, **18/18 PASS** across the full toolchain × sanitizer × native/forced-fallback matrix). It resolves to the standard primitive on platforms that support P0718 (zero-cost alias, no behavior change) and to an address-hash-sharded, mutex-guarded `shared_ptr` implementation where P0718 is absent. The primitive's design, API surface, detection strategy, and CAS-equivalence semantics are **locked** by the harness.

This feature performs the **fixpp integration**: it lands the primitive in the library, migrates all four production consumers, makes the library build-and-pass-tests under libc++, and stands up a regression lane so the libc++ path cannot silently rot. Two of the four consumers live in **awaitable headers** (`async_mutex.hpp`, `engine.hpp`), where the sharded-mutex fallback's `std::mutex` collides with the categorical Article XV §3/§9 ban on `std::mutex` in coroutine-context headers — so this feature also carries the **constitutional amendment** that reconciles the bounded, never-held-across-`co_await`, libc++-only fallback with that rule, and reverses the deliberate opposite decision feature 023 made for the engine reader-snapshot.

This is **pre-GA, non-Linux portability** work. It changes no runtime behavior on the existing Tier-1 toolchains.

## Clarifications

### Session 2026-06-20

- Q: Which platforms must 046 actually VALIDATE (vs merely enable)? → A: **Linux libc++ only for this feature's core scope, with macOS sequenced as an explicit follow-up AFTER the Linux lane is passing** (user-refined 2026-06-20: land + prove the Linux `linux-clang-libc++` lane first, then add the `macos-14`+ runner). Apple clang's libc++ exercises the same fallback path, so macOS is low-risk once Linux is green; FreeBSD validation stays a further follow-on. The primitive integration enables all three.
- Q: What CI tier should the libc++ lane(s) be? → A: **Tier-2 / opt-in label** (like the existing `windows-msvc` opt-in), promotable to blocking later — does not put a full libc++ dep-rebuild on every PR's critical path.
- Q: How to handle the OTel-cpp dependency if it fails to build under libc++ this cycle? → A: **Scope the lane to the non-OTel build configuration** (exclude the OTel-dependent target) and record OTel-under-libc++ as a tracked follow-up — do not block the primitive integration on an orthogonal dependency port.
- Q: What test scope must run under libc++? → A: **Full functional suite at least once (acceptance, SC-002); the ongoing regression lane runs the concurrency-relevant subset under sanitizers** — controls per-run cost while keeping the fallback honest.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Build and run fixpp under libc++ (Priority: P1)

A developer (or a downstream consumer targeting Apple/BSD platforms) configures a fixpp build with the LLVM libc++ standard library (`-stdlib=libc++`). The library compiles cleanly and its existing test suite passes — the same correctness the libstdc++ build delivers — without any source edits on their side.

**Why this priority**: This is the irreducible MVP and the entire point of NFR-017. Today fixpp fails to *compile* under libc++ because of the four raw `std::atomic<std::shared_ptr<T>>` members. Migrating them to `fixpp::sync::atomic_shared_ptr<T>` is the single change that converts "does not build" into "builds and works." Every other story depends on this one existing.

**Independent Test**: Configure a libc++ toolchain profile, build the full fixpp library + test binaries, and run the test suite. Observe a clean compile and a green test run (the fallback path is active, confirmed by the detection macro reporting `0`). Delivers value standalone: fixpp is usable on libc++.

**Acceptance Scenarios**:

1. **Given** a libc++ toolchain, **When** the fixpp library is built, **Then** it compiles with zero errors (today it fails at the `std::atomic<std::shared_ptr<T>>` instantiations) and the detection reports the fallback path is active.
2. **Given** a libc++ build of fixpp, **When** the existing functional test suite runs, **Then** it passes with the same dispositions as the libstdc++ build (no new failures, no behavior divergence introduced by the snapshot-publishing members).
3. **Given** an established session on a libc++ build, **When** readers concurrently load a published snapshot while a writer republishes it, **Then** every reader observes either the prior or the new fully-constructed snapshot (never a torn or null value) — the same publish/acquire guarantee the standard primitive gives.

---

### User Story 2 - Keep the libc++ fallback path honest over time (Priority: P2)

A maintainer needs assurance that the libc++ reach, once won, does not silently regress as the library evolves. A dedicated regression lane builds fixpp under libc++ and exercises the fallback path under the sanitizer matrix on every change; additionally, the fallback path is forced-on even under the native toolchains so it cannot rot unnoticed between libc++ runs.

**Why this priority**: Portability that is not continuously verified decays. The harness proved the primitive once in isolation; this story keeps the *integrated* library honest. It depends on P1 (there must be something to regress against) but adds durability, not new capability.

**Independent Test**: Trigger the libc++ regression lane and observe a green build + sanitizer run on the fallback path. Separately, run a native-toolchain build with the force-fallback override set and observe the fallback path exercised green. Both are independently runnable.

**Acceptance Scenarios**:

1. **Given** a CI change, **When** the libc++ regression lane runs, **Then** it builds the full library and runs the concurrency-relevant tests on the fallback path under the available sanitizers, green.
2. **Given** a native (libstdc++) toolchain, **When** the force-fallback override is set, **Then** the build selects the sharded-mutex implementation and the concurrency tests pass — proving the fallback works independently of whether a libc++ host is present.
3. **Given** the integrated primitive, **When** its publish/acquire ordering and CAS-equivalence semantics are tested, **Then** the integrated library reproduces the harness's load-bearing acceptance criteria (forced-fallback green on every supported toolchain).

---

### User Story 3 - Zero regression to the existing Tier-1 toolchains (Priority: P3)

An operator or maintainer on the existing supported toolchains (libstdc++, MSVC-STL) sees no change whatsoever: default builds, runtime behavior, performance characteristics, and all existing constitution gates remain byte-identical. The new primitive resolves to the standard `std::atomic<std::shared_ptr<T>>` on these platforms, and the constitutional amendment that unblocks the libc++ fallback does not weaken the `std::mutex`-in-awaitable-headers rule on the native path.

**Why this priority**: The migration must be a pure portability *addition*. A portability win that perturbs the validated Tier-1 path would be a net loss. This is the safety constraint that bounds the whole feature; it depends on P1's mechanism (the alias path) being correct.

**Independent Test**: Build on the default libstdc++ toolchain and confirm the primitive resolves to the standard alias (detection macro reports native). Run the full existing gate matrix (sanitizers, coverage, the §XV.9 awaitable-mutex corpus gate, ABI checks) and confirm all dispositions are unchanged from before the migration.

**Acceptance Scenarios**:

1. **Given** the default libstdc++ toolchain, **When** fixpp is built, **Then** `fixpp::sync::atomic_shared_ptr<T>` is the standard `std::atomic<std::shared_ptr<T>>` (an alias) and no `std::mutex` is introduced on this path.
2. **Given** the migrated library on a native toolchain, **When** the §XV.9 awaitable-mutex corpus gate runs (preprocessing under the native stdlib), **Then** it passes exactly as before — the fallback's `std::mutex` is preprocessed out and never appears in a project-owned region on the native path.
3. **Given** the full existing Tier-1 gate matrix, **When** it runs against the migrated library, **Then** every gate's disposition is unchanged (no new waivers, no coverage loss, no ABI break attributable to the migration).

---

### Edge Cases

- **Aliasing `shared_ptr`s in CAS**: two `shared_ptr`s with the same raw pointer but different control blocks MUST compare unequal in `compare_exchange`; default-null vs default-null MUST compare equal. (Locked harness CAS-equivalence: success iff stored-ptr equality AND owner equality.)
- **Memory-order honoring**: even though the fallback mutex is effectively seq-cst, a caller-passed `memory_order` (acquire/release) MUST still produce correct publish/acquire visibility — guards against a future "optimization" silently breaking the I-23/I-13 orderings the original consumers rely on.
- **Detection lying**: header-side SFINAE/`requires` probing is infeasible (libc++'s `static_assert` hard-errors past SFINAE). Detection MUST be vendor-macro gated with safe-default-to-fallback, plus two explicit overrides (force-fallback for the regression lane; force-native escape hatch).
- **A new raw `std::atomic<std::shared_ptr<T>>` reappearing**: a future edit re-introducing the raw form in a project header would silently re-break libc++. The feature must guard against regrowth of the census (not merely fix today's four sites).
- **OTel-under-libc++ build failure**: the libc++ lane requires every dependency rebuilt under libc++ (libc++/libstdc++ are ABI-incompatible — cannot mix). The watch-item is the OpenTelemetry-cpp dependency; if it fails to build under libc++, the lane is blocked on a dependency issue orthogonal to the primitive.
- **`wait`/`notify` surface absence**: the locked primitive omits P0718's `wait`/`notify[_one|_all]`. If any consumer relied on them, that consumer cannot migrate as-is. (Census confirms none of the four do.)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The library MUST provide `fixpp::sync::atomic_shared_ptr<T>` as a library-internal primitive, integrated from the locked research-harness headers (`atomic_shared_ptr.hpp` + `atomic_shared_ptr_detect.hpp`), preserving the harness's locked API surface (P0718 minus `wait`/`notify[_one|_all]`), detection strategy, and CAS-equivalence semantics.
- **FR-002**: On standard libraries that implement P0718 (libstdc++ with `__cpp_lib_atomic_shared_ptr ≥ 201711L`, MSVC-STL), the primitive MUST resolve to `std::atomic<std::shared_ptr<T>>` as a zero-overhead alias, introducing no `std::mutex` and no behavior or performance change.
- **FR-003**: On standard libraries without a working P0718 (libc++ / `_LIBCPP_VERSION`), the primitive MUST resolve to the address-hash-sharded, mutex-guarded `shared_ptr` fallback implemented purely on the public `shared_ptr` API (never transplanting libstdc++'s internal `atomic<shared_ptr>` representation).
- **FR-004**: All four production consumers of raw `std::atomic<std::shared_ptr<T>>` MUST be migrated to `fixpp::sync::atomic_shared_ptr<T>`: `core/sync/async_mutex.hpp` (`drain_latch_ptr_`), `tls/pinset.hpp` (pin-snapshot), `transport/transport_factory.hpp` (`cert_source_slot_`), and `session/engine.hpp` (`reader_snapshot_`). After migration, **no production header or source file may contain a raw `std::atomic<std::shared_ptr<…>>`** outside the primitive's own header.
- **FR-005**: A census-regrowth guard MUST fail the build/gate if a raw `std::atomic<std::shared_ptr<…>>` re-appears in any project-owned header or source outside the primitive header — enforcing exact-set completeness, not merely fixing today's four sites.
- **FR-006**: The library MUST compile cleanly under an LLVM libc++ toolchain (`-stdlib=libc++`), exercising the fallback path — the capability that does not exist today.
- **FR-007**: Under libc++, the **full** existing fixpp functional test suite MUST pass **at least once** (acceptance) with dispositions equivalent to the libstdc++ build, with **no behavior divergence** attributable to the migrated snapshot-publishing members. (The *ongoing* lane scope is FR-011.)
- **FR-008**: The fallback path MUST honor caller-supplied `memory_order` arguments to produce the publish/acquire visibility the four consumers depend on (I-23/I-13 orderings), verified by ordering tests on the integrated library.
- **FR-009**: The fallback path MUST be exercisable under the sanitizer matrix (ASan/UBSan/TSan) green on the integrated library, reproducing the harness's load-bearing forced-fallback acceptance.
- **FR-010**: A force-fallback override MUST allow the fallback path to be selected and tested on a native (P0718-supporting) toolchain, so the fallback is continuously exercised without requiring a libc++ host; a force-native override MUST exist as a documented escape hatch.
- **FR-011**: A `linux-clang-libc++` build profile MUST be added (a new toolchain profile selecting `compiler.libcxx=libc++` with the dependency set rebuilt under libc++), wired as a **Tier-2 / opt-in** CI lane (label-triggered, like the existing `windows-msvc` lane; not blocking on every PR). The lane MUST build the library + run the **concurrency-relevant test subset** under the available sanitizers on the fallback path on an ongoing basis. If the OTel-cpp dependency fails to build under libc++ in this cycle, the lane MUST be scoped to the **non-OTel build configuration** (excluding the OTel-dependent target) and the OTel-under-libc++ build recorded as a tracked follow-up — the primitive integration MUST NOT be blocked on it.
- **FR-011a** *(DEFERRED follow-up — sequenced after FR-011 is passing, user-refined 2026-06-20)*: A **macOS** CI lane (a `macos-14`+ runner with macOS Conan profile(s), Tier-2 / opt-in, exercising the same fallback path since Apple clang's libc++ lacks a working P0718 specialization) is a **committed follow-up**, NOT in this feature's core delivery. It is gated on the Linux `linux-clang-libc++` lane (FR-011) being green first, then adds the macOS profiles + dep set + runner under the same OTel-non-OTel scoping and concurrency-subset ongoing scope. Tracked as a forward obligation; FreeBSD remains a further follow-on. *(The design MUST NOT preclude macOS — the primitive's fallback already covers it — but standing up the macOS lane is out of this feature's acceptance.)*
- **FR-012**: This feature MUST carry the **Article XV §3/§9 amendment** that permits the `fixpp::sync::atomic_shared_ptr` sharded-mutex fallback to appear in awaitable headers, **strictly scoped** to: a bounded O(1) lock held only for the duration of a single synchronous `shared_ptr` load/store/CAS, **never held across a `co_await`**, and **active only on the non-Tier-1 libc++ fallback path**. The amendment MUST NOT relax the ban for any other `std::mutex` use, and MUST keep the native-path corpus gate passing unchanged. (Constitutional amendment folded into Gate A per Article XX.)
- **FR-013**: This feature MUST reverse feature 023's CHK046 prohibition for `engine.hpp::reader_snapshot_` (which deliberately pinned the standard primitive and forbade `fixpp::sync::atomic_shared_ptr`), recording the reversal as an explicit decision and updating 023's affected artifacts/anchors — not a silent swap.
- **FR-014**: The two recording obligations carried for NFR-017 MUST be discharged: the `spec/feature-catalogue.md` NFR-017 row updated from backlog to delivered with evidence, and the `specs/006-async-mutex/research.md` D-4 follow-up pointer updated to reference this feature.
- **FR-015**: The migration MUST introduce no new public API type, no new wire/error/config/codegen/C-ABI surface, and no new third-party dependency; the primitive is a library-internal sync utility.

### Key Entities

- **`fixpp::sync::atomic_shared_ptr<T>`**: the integrated primitive. Native path = alias to `std::atomic<std::shared_ptr<T>>`; fallback path = address-hash-sharded mutex-guarded `shared_ptr` over the public API. API = P0718 minus `wait`/`notify[_one|_all]`.
- **Detection macro set**: `FIXPP_HAS_STD_ATOMIC_SHARED_PTR` (+ the resolved `*_NATIVE_ACTIVE` selector) and the two overrides `FIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK` / `FIXPP_FORCE_ATOMIC_SHARED_PTR_NATIVE`. Vendor-macro gated, safe-default-to-fallback.
- **The four migrated consumers**: `drain_latch_ptr_` (async_mutex.hpp, awaitable), pin-snapshot (pinset.hpp), `cert_source_slot_` (transport_factory.hpp), `reader_snapshot_` (engine.hpp, awaitable). Two are awaitable-header members subject to §XV.9.
- **`linux-clang-libc++` toolchain profile + regression lane**: the build configuration (libc++ stdlib + libc++-rebuilt deps) and the CI lane that exercises the fallback path under sanitizers.
- **Census-regrowth guard**: a gate that rejects any new raw `std::atomic<std::shared_ptr<…>>` in project-owned code.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The full fixpp library and its test binaries compile under the Linux `linux-clang-libc++` lane (FR-011) with zero errors — a state currently impossible (the library does not build under libc++ at all). *(macOS lane compile is the FR-011a deferred follow-up, not part of this feature's acceptance.)*
- **SC-002**: The **full** fixpp functional test suite passes **at least once** on the libc++ (fallback) build with the same pass/fail dispositions as the libstdc++ build — zero new failures attributable to the migration; the ongoing Tier-2 Linux lane then runs the concurrency-relevant subset under sanitizers (FR-011).
- **SC-003**: The fallback path passes the ASan/UBSan/TSan sanitizer matrix green on the integrated library (matching the harness's forced-fallback acceptance), and the publish/acquire-ordering and CAS-equivalence behaviors are witnessed on the integrated consumers.
- **SC-004**: 100% of the production raw `std::atomic<std::shared_ptr<T>>` occurrences (the four census'd consumers) are migrated, and the census-regrowth guard demonstrably fails on an injected raw re-introduction (exact-set completeness, mutation-verified).
- **SC-005**: Every existing Tier-1 gate (sanitizers, coverage thresholds, the §XV.9 awaitable-mutex corpus gate, ABI hygiene) retains an unchanged disposition on the default libstdc++ build after the migration — zero regression, no new waivers attributable to this feature.
- **SC-006**: The forced-fallback path is exercised green on every supported native toolchain (not only on a libc++ host), so the fallback is continuously regression-protected.

## Assumptions

- **Platform validation scope** *(resolved — Clarifications 2026-06-20, user-refined)*: This feature's core scope validates **Linux libc++ only** (toolchain present on the build host). The **macOS `macos-14`+ runner is a committed follow-up sequenced AFTER the Linux lane is green** (FR-011a — deferred, not in acceptance). **FreeBSD** is a further follow-on. The primitive enables all three; the design must not preclude macOS/BSD.
- **libc++ CI-lane tier** *(resolved — Clarifications 2026-06-20)*: Both libc++ lanes are **Tier-2 / opt-in** (label-triggered, like `windows-msvc`), promotable to blocking later. See FR-011/FR-011a.
- **Dependency rebuild under libc++** *(resolved — Clarifications 2026-06-20)*: libc++ and libstdc++ are ABI-incompatible, so the lanes rebuild the full dependency set under libc++. asio is header-only (free); the **OpenTelemetry-cpp** dependency is the build watch-item. If OTel cannot build under libc++ this cycle, the lane scopes to the **non-OTel build configuration** and records OTel-under-libc++ as a tracked follow-up — the primitive integration is not blocked on it. See FR-011.
- **The primitive design is locked** by the research harness (API, sharded-mutex implementation, vendor-macro detection, CAS-equivalence, memory-order honoring); this feature integrates it rather than re-designing it. The harness headers are drop-in-copyable.
- **ASan leak detection caveat**: the harness ran ASan with `detect_leaks=0` (WSL2 ptrace restriction); the integrated libc++ lane should enable LSan where the host allows it, but a WSL2-bound run inherits the same caveat (refcount-integrity tests cover the leak class via `weak_ptr` snapshots).
- **The census is four consumers** (verified 2026-06-20: `async_mutex.hpp`, `pinset.hpp`, `transport_factory.hpp`, `engine.hpp`) — one more than the prior project-memory note recorded (which predated the 023 `reader_snapshot_` addition); the `session.hpp:170` reference is a comment only, not a consumer.

## Dependencies

- **Research harness** `research/G19-fix-fpml-iso20022/atomic-shared-ptr/` (DONE, 18/18) — source of the locked primitive headers and the validated test ideas.
- **Constitution Article XV §3/§9** (the `std::mutex`-in-awaitable-headers ban) — amended by this feature (Gate A, Article XX), enforced by `tools/check_no_std_mutex_in_awaitable_headers.sh`.
- **Feature 023** (`reader_snapshot_` / CHK046) — its prohibition reversed by this feature.
- **Feature 006** (`async_mutex` / `drain_latch_ptr_`) — the originally-planned consumer; its research.md D-4 names this as the follow-up.
- **`spec/feature-catalogue.md` NFR-017** + project memory `project_libcxx_atomic_shared_ptr_followup` — the tracked backlog row + cross-session state.
