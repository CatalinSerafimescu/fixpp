# Implementation Plan: atomic_shared_ptr — libc++ portability fallback integration

**Branch**: `046-atomic-shared-ptr` | **Date**: 2026-06-20 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/046-atomic-shared-ptr/spec.md`

## Summary

Integrate the **harness-validated** `fixpp::sync::atomic_shared_ptr<T>` primitive into the fixpp library so the library compiles and passes its tests under libc++ (today it does not build under libc++ at all). On P0718-capable standard libraries (libstdc++, MSVC-STL) the primitive is a **zero-overhead alias** to `std::atomic<std::shared_ptr<T>>`; on libc++ it resolves to an address-hash-sharded, mutex-guarded `shared_ptr` fallback over the public `shared_ptr` API, with the **lock type-erased into a `.cpp`** so the header carries no `std::mutex`. The work migrates **all four** production raw `std::atomic<std::shared_ptr<T>>` consumers to the primitive, adds a census-regrowth guard, stands up a Tier-2 opt-in `linux-clang-libc++` regression lane, and reverses feature 023's CHK046 prohibition for the engine reader-snapshot.

**No constitutional amendment** (decided 2026-06-20): two consumers (`async_mutex.hpp`, `engine.hpp`) are awaitable headers where a header-resident `std::mutex` would breach Article XI §3 / XV §9. Rather than amend that rule with a bounded-mutex exemption, the fallback's 128-mutex shard table + an opaque RAII lock guard are moved **out-of-line into `src/core/sync/atomic_shared_ptr.cpp`** (the header declares only the guard), so `atomic_shared_ptr.hpp` contains no `std::mutex` token on **either** standard library — the awaitable-header gate passes under libc++ too, with no rule change. This honors the project's "amend only when a code change cannot achieve the same correctness" principle and *removes* (rather than overrides) 023's CHK046 objection.

**Technical approach**: the primitive's *algorithm* is adopted from the locked 18/18 research harness (sharding, CAS-equivalence, vendor-macro detection, memory-order honoring — unchanged). The lock packaging is a **structural lock-path adaptation, fully re-validated** (NOT "mechanical"): the harness held `std::lock_guard<std::mutex>` **inline** in every op with a function-local-static shard table **in the header**; type-erasure moves the lock behind an **out-of-line** `detail::shard_guard` (a cross-TU call on every fallback op) and relocates the shard table's function-local-static into the `.cpp` — a different inlining/linkage/initialization context the harness's 18/18 never exercised. The full CODEX-BRIEF §6 test inventory is therefore re-run against the type-erased shape (see "Integrated test inventory" below), via the integration's own libc++ + forced-fallback sanitizer lanes. The remaining engineering: (1) verify each of the four consumers' exact call-site usage against the fallback's surface (verified: `load`/`store` only — no `exchange`/CAS/`wait`/`notify`); (2) the libc++ toolchain profile + full C++ dependency rebuild, with per-dependency scope-out if any dep will not build under libc++ this cycle (OTel + its protobuf/abseil/grpc stack the prime risk).

## Technical Context

**Language/Version**: C++23 (`-std=c++23`; project standard, matches the existing build + corpus gate)
**Primary Dependencies**: standard library only for the primitive (libstdc++ ≥ P0718 / MSVC-STL native; libc++ fallback). Build/infra: a new `linux-clang-libc++` Conan profile (`compiler.libcxx=libc++`) + full dependency set rebuilt under libc++ (asio header-only = free; **OpenTelemetry-cpp** = the build watch-item). No new third-party runtime dependency.
**Storage**: N/A
**Testing**: ctest/gtest; the harness's validated test ideas ported to the integrated library (CAS-equivalence, memory-order publish/acquire, multi-instance isolation, forced-fallback acceptance); ASan/UBSan/TSan matrix; the new census-regrowth guard; the existing §XI.3/§XV.9 awaitable-mutex corpus gate.
**Target Platform**: Linux libstdc++ (Tier-1, unchanged), Linux libc++ (Tier-2 opt-in, **new** — validated here), Windows-MSVC (Tier-2, unchanged); macOS libc++ (FR-011a deferred follow-up), FreeBSD (further follow-on). The primitive enables all.
**Project Type**: C++ library (FIX engine) — internal sync utility addition + build-infra.
**Performance Goals**: native (Tier-1) path **zero overhead** — the primitive is a type alias, byte-identical codegen; fallback path optimizes for **correctness/portability over speed** (libc++ is a portability target, not a perf target). On the fallback path **every** op takes a shard lock — not only the writer's `store`, but **every reader `load` too**, including `pinset::find()` which is documented lock-free on the handshake-hot path (`pinset.hpp:118-120`). This is accepted: libc++ is not a perf target, and `find()` runs per-*handshake*, not per-*message*. (Corrected from the earlier "non-hot republish path" framing, which reasoned only about the writer — Codex #5; the reader lock is real. The per-message in-memory hot path is untouched, and on Tier-1 the primitive is the std alias — unchanged.)
**Constraints**: no new public API type, no wire/error/config/codegen/C-ABI surface (FR-015); **no constitution change** (FR-012 type-erasure keeps the fallback `std::mutex` out of awaitable headers); **zero Tier-1 regression** (SC-005); the corpus gate stays green under both libstdc++ and libc++.
**Scale/Scope**: 1 primitive (1 detect header [verbatim except include-path/guard-macro] + 1 type-erased main header, both under `core/sync/detail/` + 1 new `src/core/sync/.cpp`), 4 migrated consumers (2 of them awaitable headers), 1 census-regrowth guard, 1 Tier-2 CI lane, **0 constitution amendments**, 1 prior-feature decision reversal (023 CHK046), 2 recording obligations (catalogue NFR-017, 006 research D-4).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Mandatory-trigger classification (Appendix A)**: **Threading / concurrency** — this changes a concurrency primitive used in coroutine-context headers. Triggers **all four** controls: `/speckit-clarify` (DONE), `/speckit-analyze` (pending step 6), **Codex Gate A** (pending — a normal feature review; **no amendment to ratify**), **user `/plan` sign-off** (pending). Not a Security trigger (TLS behavior unchanged; `cert_source_slot_` migration is representation-only). Not an ABI/wire/error/codegen/FSM trigger (FR-015). **No constitution amendment** — the §XI.3 collision is resolved by code (FR-012 type-erasure), not by rule change.

| Article / Gate | Status | Notes |
|---|---|---|
| **XI §3 — `std::mutex` banned in awaitable headers** | **PASS (via FR-012 type-erasure)** | The fallback's `std::mutex` + shard table live out-of-line in `src/core/sync/atomic_shared_ptr.cpp`; `core/sync/detail/atomic_shared_ptr.hpp` declares only an opaque RAII guard and carries **no `std::mutex` token on either standard library**. So no awaitable header (`async_mutex.hpp`, `engine.hpp`) acquires a banned mutex symbol — under libstdc++ **or** libc++. No exemption, no amendment. (Alternative — a bounded-mutex §XI.3 exemption — considered and rejected; research D-2.) Proof is the CT-1 libc++-leg gate run, fail-closed on `-E` error (New-A). |
| **XV §9 — banned-pattern pointer to XI §3** | PASS | Unchanged; satisfied as-is (the header is mutex-free). |
| **XV §1 — no per-message/hot-path heap alloc** | PASS | Article XV §1 is **heap-alloc-scoped** (`.specify/constitution.md` — bans `new`/`delete` between parse and `fromApp`, **not** locking): a shard-lock acquire + a `shared_ptr` refcount bump is neither a `new` nor a `delete`, so the fallback's lock does **not** engage XV §1. Independently, the migration is a **pure Tier-1 alias** (byte-identical codegen on the path XV §1 actually gates), so a Tier-1 gate cannot be newly violated by a change that changes nothing on Tier-1. The fallback's per-`load` shard lock (incl. `pinset::find()`, plan:22) is real but is on the libc++ non-perf portability path and runs per-handshake, not per-message. On Tier-1 the primitive is the std alias (unchanged). |
| **II §4 — no global compiler-version pin** | PASS | Detection is vendor-macro gated (`_LIBCPP_VERSION` / `__GLIBCXX__ + __cpp_lib_atomic_shared_ptr` / `_MSC_VER`) with safe-default-to-fallback; no compiler-version pin. |
| **II §3 / III §3 — Tier-2 toolchain profiles** | PASS | The new `linux-clang-libc++` Conan profile is a **Tier-2 / opt-in** addition (FR-011), blessed by Article II §3 (Tier-2 additions). Article III §3 enumerates profiles but carries no closed-set / amendment-required language (contrast XII §5), so adding a Tier-2 profile is not a MUST violation and needs no amendment. (analyze D1.) |
| **IX §1 — coverage (lcov DA/BRDA)** | PASS (planned) | Fallback-path lines (incl. the new `.cpp` guard) covered by the ported harness tests + forced-fallback lane (SC-003/SC-006); native alias is trivially covered by existing consumer tests. |
| **XV §12 — no LGPL deps** | PASS | No new dependency; the primitive is original (sharded mutex over the public `shared_ptr` API). |
| **FR-015 — no new public/wire/error/codegen/C-ABI surface** | PASS | Both new headers live under `include/fixpp/core/sync/**detail/**`, which [arch §9.1] declares **not part of the public API surface** (`\internal`-scoped, excluded from the supported API / Doxygen stability surface). They are **physically installed** (install ships the whole `include/` tree), but that is the install mechanism — not the API contract. Established precedent: the public, installed `session/message_store.hpp` already `#include`s `session/detail/has_flush_for_session_close.hpp` (source-verified `message_store.hpp:28`) — a public header **could not** include a header absent from the install tree, which proves detail headers physically ship; both are registered in the §XV.9 corpus gate, so a public/awaitable header including a `core/sync/detail/` header is existing practice, not a new install or API break. (Note: arch §9.1:549 says "Detail headers are excluded from the install set" — that wording is imprecise; they are physically installed but excluded from the *supported API / Doxygen* surface, as the message_store precedent proves.) Consumers are private members; the new `.cpp` is an internal TU. |
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
include/fixpp/core/sync/detail/
├── atomic_shared_ptr.hpp          # NEW — harness algorithm, ADAPTED: lock type-erased (header declares only detail::shard_guard; no <mutex>, no std::mutex token). Under detail/ ⇒ physically installed but excluded from the supported API/Doxygen surface per [arch §9.1] ⇒ not new public API (FR-015).
└── atomic_shared_ptr_detect.hpp   # NEW — verbatim from the locked harness EXCEPT include-path/guard-macro adjusted for the new location (vendor-macro detection + 2 force overrides)

src/core/sync/atomic_shared_ptr.cpp # NEW — out-of-line shard table (128 std::mutex) + detail::shard_guard lock/unlock; ALWAYS compiled on every mode (always-ship-guard, research D-9 / contract CT-4) — native pays zero overhead because the symbols are never called (function-local-static lazy-init never fires). Added to the fixpp_sync target (promoted INTERFACE→STATIC UNCONDITIONALLY per its own CMakeLists comment "promote to STATIC iff a .cpp is added").

src/core/sync/CMakeLists.txt        # EDIT — promote fixpp_sync INTERFACE→STATIC (or OBJECT); add atomic_shared_ptr.cpp; keep INTERFACE include dirs.
include/fixpp/core/sync/async_mutex.hpp        # MIGRATE drain_latch_ptr_   (awaitable header — stays mutex-free; already inside fixpp_sync — no new link edge)
include/fixpp/tls/pinset.hpp                   # MIGRATE pin_snapshot        (non-awaitable; ops in src/tls/pinset.cpp)
include/fixpp/transport/transport_factory.hpp  # MIGRATE cert_source_slot_  (non-awaitable; ops in src/transport/transport_factory.cpp)
include/fixpp/session/engine.hpp               # MIGRATE reader_snapshot_    (awaitable header — stays mutex-free; reverses 023 CHK046; ops in src/session/engine.cpp)

# Link edges (the TUs that instantiate the fallback class + reference the out-of-line shard_guard symbols):
#   fixpp_tls (pinset.cpp), fixpp_transport (transport_factory.cpp), fixpp_session (engine.cpp) MUST link the promoted (compiled) fixpp_sync.
#   async_mutex.hpp lives INSIDE fixpp_sync, so it needs NO new edge.
#   Each of the three owning targets gets a forced-fallback LINK test (CT-4 / Codex #4) proving the guard symbols resolve.

tests/sync/                                     # NEW — ported harness tests against the integrated primitive (test inventory: §"Integrated test inventory" below)
tools/check_no_raw_atomic_shared_ptr.sh         # NEW — census-regrowth guard (FR-005); ctest-registered alongside check_no_std_mutex_corpus
tools/check_no_std_mutex_in_awaitable_headers.sh # EDIT (CT-1a, three changes — Codex #1) — (1) drop `|| true` (fail-closed on -E error); (2) ADD a `-stdlib=*` parser branch forwarding it into INCLUDE_FLAGS (today it falls to the `else` at L77 → appended to HEADERS → "header not found" WARNING at L160, NOT an -E error, so the libc++ leg silently preprocesses under host libstdc++); (3) use the preset's configured clang for the libc++ leg, not merely the first compiler on PATH. The EXISTING Tier-1 `check_no_std_mutex_corpus` corpus (engine.hpp already registered, 039 US4) is unchanged in membership.

# NEW libc++-leg ctest (registered ONLY in the linux-clang-libc++ preset, NOT in the existing corpus test) — CT-1c:
#   runs check_no_std_mutex_in_awaitable_headers.sh with -stdlib=libc++ (now forwarded, see the script EDIT) + libc++ include path over async_mutex.hpp + engine.hpp,
#   asserting each supplied header emitted the asio/awaitable.hpp marker (falsifiability: a libc++ misconfig → empty output → must RED, not silent-pass),
#   AND a positive _LIBCPP_VERSION-active probe: preprocess `#include <version>` with the same -stdlib=libc++ <preset-clang> + -dM and RED-fail if `_LIBCPP_VERSION` is absent from the macro dump (proves the leg actually ran under libc++, not host libstdc++).
#   + a mutation fixture (CT-1d): a project-local transitive include that introduces a banned mutex type → the gate MUST fire — run on BOTH the libstdc++ leg AND (once the -stdlib forwarding lands) the libc++ leg, so the discrimination proof lives on the same path as CT-1c (New-P3-1).

# NO constitution change. Conan/CI: new linux-clang-libc++ profile + Tier-2 opt-in lane (build-infra paths per repo convention).
```

**Structure Decision**: Single C++ library. The primitive lands under the **actual** sync module — `include/fixpp/core/sync/` (headers) + `src/core/sync/` (build), the location of `fixpp_sync` and `async_mutex.hpp` ([arch §3]/[arch §4.1]: `fixpp::sync` lives physically under `core/`). The two new headers go under `include/fixpp/core/sync/detail/`, which is **physically installed but not part of the supported API / Doxygen stability surface** per [arch §9.1] — so FR-015's "no new public API type" holds (see the Constitution Check FR-015 row). The detect header is verbatim from the harness except its include-path/guard-macro; the main header keeps the harness's validated *algorithm* but **type-erases the lock** (the `std::mutex` shard table + guard bodies move to a new `src/core/sync/atomic_shared_ptr.cpp`, **always compiled** on every mode per the always-ship-guard decision — research D-9 / CT-4 — with the native path paying zero overhead because the alias never constructs a `shard_guard`, so the symbols are present-but-never-called). Adding a `.cpp` requires **promoting `fixpp_sync` from INTERFACE to a compiled target unconditionally** (STATIC/OBJECT — its own `CMakeLists.txt` comment already plans for exactly this: *"promote to STATIC iff a .cpp is added"*) and the three TUs that instantiate the fallback (pinset.cpp / transport_factory.cpp / engine.cpp) linking it; async_mutex.hpp is already inside `fixpp_sync`. Migrations are in-place edits to four existing member declarations + one include-path change each (no call-site logic change). The new gate and CI profile follow the repo's existing `tools/` + Conan-profile conventions. No new module, no layer-graph change (`core/sync` is already a base layer).

## Integrated test inventory (re-validates the type-erased shape; reproduces CODEX-BRIEF §6)

The locked harness's acceptance was **10 named obligations + ≥2 reviewer extras** (CODEX-BRIEF §6.1–§6.10). Because type-erasure is a structural lock-path change (Summary / research D-1), the **complete** inventory is re-run against the integrated, out-of-line-guard shape — not the 5 broad tests the contract previously listed. **Primitive-level** tests (`tests/sync/`) exercise the `atomic_shared_ptr<T>` surface directly (incl. CAS/`exchange`, which **no consumer uses**); **consumer-level** tests witness publish/load only.

| # | CODEX-BRIEF §6 obligation | Level | Named test (tests/sync/) | Preset / lane | Sanitizer |
|---|---|---|---|---|---|
| 1 | API conformance (compile-time signatures: all P0718 methods, both CAS overload forms, `exchange`) | primitive | `atomic_shared_ptr_signature_static_assert` | all (compile) | — |
| 2 | Single-thread `load`/`store`/`exchange` round-trips; CAS success + failure (`expected` updated); default = empty | primitive | `atomic_shared_ptr_single_thread_roundtrip` | linux-clang-asan + forced-fallback **+ libc++** | ASan |
| 3 | Refcount integrity under contention (weak_ptr snapshot → store(nullptr) → expired; no leaked strong ref / UAF) | primitive | `atomic_shared_ptr_refcount_integrity` | forced-fallback | ASan |
| 4 | Concurrent correctness — contention stress, no torn read / UAF / race | primitive | `atomic_shared_ptr_contention_stress` | forced-fallback + libc++ | TSan; ASan+UBSan |
| 5 | CAS three-discriminator equivalence (distinct-object FAIL / aliasing-same-raw-diff-ctrl-block FAIL / shared-ownership SUCCEED / null-null SUCCEED / expected-update on fail) | primitive | `atomic_shared_ptr_cas_equivalence` | forced-fallback | UBSan |
| 6 | Publish/acquire ordering (release store / acquire load; reader never sees partial Payload) | primitive **and** consumer | `atomic_shared_ptr_publish_acquire_ordering` (primitive, `tests/sync/`); **one named consumer publish/acquire-ordering witness per migrated member** (see row 6-consumers below) | forced-fallback + libc++ | TSan |
| 6-consumers | Per-consumer publish/acquire witness (concurrent writer `store(v, release)` HB reader `load(acquire)` on the migrated member; never torn/null) | consumer | `async_mutex_drain_latch_publish_acquire` (**tests/sync/**, `drain_latch_ptr_`); `pinset_snapshot_publish_acquire` (**tests/tls/**, `snapshot_`); `transport_factory_cert_source_publish_acquire` (**tests/transport/**, `cert_source_slot_`); `engine_reader_snapshot_publish_acquire` (**tests/session/**, `reader_snapshot_`) | forced-fallback + libc++ | TSan |
| 7 | Linearizability spot-check + allocator-pressure stress | primitive | `atomic_shared_ptr_linearizability`, `atomic_shared_ptr_allocator_pressure` | forced-fallback | ASan; TSan |
| 8 | Feature-detection probe reports correct yes/no per cell | primitive | `atomic_shared_ptr_detection_probe` | native (libstdc++) + libc++ + both forced overrides | — |
| 9 | `is_lock_free()` / `is_always_lock_free` report `false` on fallback (record-only on native) | primitive | `atomic_shared_ptr_lock_free_reporting` | forced-fallback + native | — |
| 10 | ~30s randomized mixed-op stress (tunable via env), no deadlock/leak | primitive | `atomic_shared_ptr_randomized_stress` | forced-fallback | ASan; TSan |
| +A | Alias identity on native (`static_assert(is_same_v<…, std::atomic<std::shared_ptr<T>>>)`) | primitive | `atomic_shared_ptr_native_alias_identity` | native | — |
| +B | Forced-mode **link** test per owning target (guard symbols resolve under `-DFIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK`) | consumer | `tls_forced_fallback_link`, `transport_forced_fallback_link`, `session_forced_fallback_link` | forced-fallback | — |

**US/SC mapping** (every row's "Preset / lane" column is reconciled to this mapping — Codex #4): US1 (build+run under libc++) → rows 2,4,6,6-consumers,8 on the `linux-clang-libc++` lane (SC-001/SC-002) — each of these rows now carries **libc++** in its lane column. US2 (keep fallback honest) → rows 3,4,7,10 + the forced-fallback lane on native (SC-003/SC-006) + row +B link tests (Codex #4). US3 (zero Tier-1 regression) → row +A + the unchanged Tier-1 gate matrix (SC-005) + CT-1 corpus gate (libstdc++ leg unchanged; libc++ leg added). **SC-003 consumer witnessing is the named row 6-consumers tests** — one per migrated member (async_mutex `tests/sync/`, pinset `tests/tls/`, transport `tests/transport/`, engine `tests/session/`), each mapped to the forced-fallback + libc++ lanes under TSan; consumers witness **publish/acquire (load/store) only** (they use no CAS), while CAS/`exchange` equivalence is witnessed at the **primitive level** (rows 5,6) — see contract C-3-note and the SC-003 correction in spec.md.

## Complexity Tracking

**No constitution violations** — the §XI.3 awaitable-mutex collision is resolved by code (FR-012 type-erasure), not by amendment, so no justification table is required. The alternatives that *were* weighed (and rejected) are recorded in research **D-2**: a bounded-mutex §XI.3 exemption (rejected — amending the constitution when a localized code change achieves the same correctness violates the Article XX §1 spirit and would leave a latent gate/exemption contradiction under the libc++ lane); rewiring only the 2 non-awaitable consumers (rejected — the awaitable members would not compile under libc++, so the feature would deliver nothing); a lock-free fallback (does not exist); `async_mutex` in the fallback (wrong shape — it is a coroutine awaitable mutex; the primitive's `load`/`store` are synchronous). The one structural cost — the primitive becomes header + `.cpp` on the fallback path (vs the harness's header-only form) — is not a constitution concern; it is re-validated by the integration's libc++ + forced-fallback sanitizer lanes.

## Gate A

- Round 1 applied 2026-06-21: Codex P1=1 P2=7 P3=2; Opus post-judging P1=1 P2=10 P3=4; rewrite addresses root causes 1-3 (file-placement to core/sync, re-validation scope, scope/recording staleness). Reviews: research/reviews/codex_046-atomic-shared-ptr_gate_a_review.md, research/reviews/opus_046-atomic-shared-ptr_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-21: Codex P1=0 P2=5 P3=1; Opus post-judging P1=0 P2=5 P3=3; rewrite closes root causes 1-2 (ground claims in source: CT-1 -stdlib parsing, OTel-OFF realizability incl. public engine.hpp coupling, pinset.hpp/bench census; pin force-mode always-ship-guard + name consumer tests). Reviews: research/reviews/codex_046-atomic-shared-ptr_gate_a_2_review.md, research/reviews/opus_046-atomic-shared-ptr_gate_a_2_adversarial_review.md.
- Round 3 converged 2026-06-21: Codex P1=0 P2=2 P3=2; Opus post-judging P1=0 P2=0 P3=4 (both Codex P2s downgraded to P3 — acceptance normatively defined; quickstart example-command gaps only). 4 P3 doc fixes applied at close-out. Reviews: research/reviews/codex_046-atomic-shared-ptr_gate_a_3_review.md, research/reviews/opus_046-atomic-shared-ptr_gate_a_3_adversarial_review.md.
