---
description: "Task list — 046-atomic-shared-ptr (NFR-017 libc++ portability fallback integration)"
---

# Tasks: atomic_shared_ptr — libc++ portability fallback integration

**Input**: Design documents from `specs/046-atomic-shared-ptr/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-9), data-model.md (E-1..E-7), contracts/ (CT-1..CT-4, T-API, census)

**Tests**: REQUIRED — Threading/concurrency feature (§VII.3); the locked harness's CODEX-BRIEF §6 inventory is re-run against the type-erased shape (plan "Integrated test inventory"). Write tests before/with the code they validate.

**Build caps (durable)**: max `-j2`; sanitizer presets run STRICTLY ONE AT A TIME (WSL2 OOM). The 6-preset verify matrix runs one at a time.

**Organization**: by user story. US1 (P1) = build+run under libc++ (MVP). US2 (P2) = keep the fallback honest. US3 (P3) = zero Tier-1 regression.

---

## Phase 1: Setup (primitive drop-in + test scaffold)

- [ ] T001 [P] Copy the detect header from the locked harness `research/G19-fix-fpml-iso20022/atomic-shared-ptr/include/fixpp/sync/atomic_shared_ptr_detect.hpp` to `include/fixpp/core/sync/detail/atomic_shared_ptr_detect.hpp` — verbatim EXCEPT the include-guard macro + any include-path adjustment for the new location. Preserve the vendor-macro detection + the two mutually-exclusive `FIXPP_FORCE_*` overrides + the derived `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE` selector. [FR-001, D-1, E-2]
- [ ] T002 Integrate the type-erased main header at `include/fixpp/core/sync/detail/atomic_shared_ptr.hpp` from the harness `atomic_shared_ptr.hpp`: keep the algorithm (native alias branch | 128-shard address-hashed fallback class with the E-1 member table; CAS-equivalence; memory-order honoring) but TYPE-ERASE the lock — the header declares only `namespace fixpp::sync::detail { class shard_guard { explicit shard_guard(const void* self) noexcept; ~shard_guard(); shard_guard(const shard_guard&)=delete; shard_guard(shard_guard&&)=delete; private: std::size_t shard_index_; }; }` and the fallback methods use `detail::shard_guard g(this);` in place of `std::lock_guard<std::mutex>`. The header MUST NOT `#include <mutex>` nor name any of the six banned mutex types (FR-012). [FR-001/002/003/012, E-1/E-3/E-5, D-1/D-2]
- [ ] T003 Add the new atomic_shared_ptr test files into the EXISTING `tests/sync/` directory (it already holds the 006 async_mutex tests — do NOT recreate it) and register the new targets in its `CMakeLists.txt`; confirm `tests/tls/`, `tests/transport/`, `tests/session/` exist for the consumer witnesses. Follow the repo's existing test-target conventions. [plan Project Structure; analyze F1]

---

## Phase 2: Foundational (out-of-line lock + build-graph promotion — BLOCKS all consumers)

**⚠️ CRITICAL**: no consumer migration (US1) can link until T004–T006 land.

- [ ] T004 Create `src/core/sync/atomic_shared_ptr.cpp`: the 128-`std::mutex` function-local-static shard table + `shard_guard::shard_guard(const void* self) noexcept` (murmur-finalizer hash of `self` → lock the shard, store the index in `shard_index_`) and `~shard_guard()` (unlock `shard_index_`). The `noexcept` ctor `std::terminate`s if `std::mutex::lock()` throws (deliberate `std::atomic` no-throw match). This is the ONLY project TU that names `std::mutex` for this primitive; it is ALWAYS compiled on every mode (always-ship-guard). [E-3, D-2/D-9, FR-012]
- [ ] T005 Edit `src/core/sync/CMakeLists.txt`: promote `fixpp_sync` INTERFACE→STATIC (or OBJECT) UNCONDITIONALLY (its own comment "promote to STATIC iff a .cpp is added"); add `atomic_shared_ptr.cpp`; keep the INTERFACE include dirs. [D-9, plan Structure Decision]
- [ ] T006 Add the link edges so the fallback's out-of-line `shard_guard` symbols resolve in every consumer's TU: `fixpp_tls` (pinset.cpp), `fixpp_transport` (transport_factory.cpp), `fixpp_session` (engine.cpp) MUST link the now-compiled `fixpp_sync`. (`async_mutex.hpp` is already inside `fixpp_sync` — no new edge.) [D-9, plan Project Structure]

**Checkpoint**: primitive + lock TU + build graph ready — consumers can migrate.

---

## Phase 3: User Story 1 — Build and run fixpp under libc++ (Priority: P1) 🎯 MVP

**Goal**: fixpp compiles and its functional suite passes under `-stdlib=libc++` (the fallback path), which is impossible today.

**Independent Test**: configure the `linux-clang-libc++` profile, build the full library + tests, run the suite green; detection reports the fallback active.

### Tests for User Story 1

- [ ] T007 [P] [US1] Primitive signature static-assert test `tests/sync/atomic_shared_ptr_signature_static_assert` (inventory row 1: all P0718 methods minus wait/notify, both CAS overload forms, `exchange`) + native alias-identity `tests/sync/atomic_shared_ptr_native_alias_identity` (row +A: `static_assert(is_same_v<atomic_shared_ptr<T>, std::atomic<std::shared_ptr<T>>>)` on native). [SC-001, FR-001/002]
- [ ] T008 [P] [US1] Per-consumer publish/acquire witnesses (inventory row 6-consumers; concurrent writer `store(v, release)` HB reader `load(acquire)`, never torn/null): `async_mutex_drain_latch_publish_acquire` (`tests/sync/`), `pinset_snapshot_publish_acquire` (`tests/tls/`), `transport_factory_cert_source_publish_acquire` (`tests/transport/`), `engine_reader_snapshot_publish_acquire` (`tests/session/`) — each runnable on forced-fallback + libc++ under TSan. [SC-003, FR-008]

### Implementation for User Story 1

- [ ] T009 [US1] Migrate `include/fixpp/core/sync/async_mutex.hpp` `drain_latch_ptr_` (L249) `std::atomic<std::shared_ptr<…>>` → `fixpp::sync::atomic_shared_ptr<…>`; add `#include "fixpp/core/sync/detail/atomic_shared_ptr.hpp"`. No call-site logic change (load(acquire)/store(v|nullptr,release)). [FR-004, D-5, E-4]
- [ ] T010 [P] [US1] Migrate `include/fixpp/tls/pinset.hpp` `snapshot_` (L135) + include; ops stay in `src/tls/pinset.cpp` unchanged. [FR-004, D-5, E-4]
- [ ] T011 [P] [US1] Migrate `include/fixpp/transport/transport_factory.hpp` `cert_source_slot_` (L210) + include; ops stay in `src/transport/transport_factory.cpp` unchanged. [FR-004, D-5, E-4]
- [ ] T012 [US1] Migrate `include/fixpp/session/engine.hpp` `reader_snapshot_` (L466) + include; ops stay in `src/session/engine.cpp` (190/246/1592). This is the member 023 CHK046 forbade — the reversal record is T026. [FR-004/FR-013, D-5/D-8, E-4]
- [ ] T013 [US1] Add the `linux-clang-libc++` Conan profile (`compiler.libcxx=libc++`, `-stdlib=libc++` compile+link) + matching CMake preset; full dependency set rebuilt under libc++ (asio header-only = free). [FR-011, E-6, D-3]
- [ ] T014 [US1] Add the paired OTel toggle so the lane is realizable: a new Conan option `fixpp:with_otel` (default `True`; moves `opentelemetry-cpp` into a conditional `def requirements(self)`) + a matching CMake `FIXPP_BUILD_OTEL` (default ON) that FAIL configuration if they disagree; gate `add_subdirectory(src/otel)` + the `fixpp_otel` link edges (session/capi) + `src/session/engine.cpp`'s `#include <fixpp/otel/providers.hpp>` and its `tracer/meter->shutdown()` calls (engine.cpp:33/1424/1427) under `#if FIXPP_BUILD_OTEL` (engine.cpp shutdown calls are near L1432-1436 — verify exact lines at implement; plan's 1424/1427 is Gate-A-era drift, analyze F3/C1). The public headers `engine.hpp` and `engine_config.hpp` MUST NOT gain any `#if FIXPP_BUILD_OTEL` guard or otherwise change (FR-015 — verified dependency-clean: tracer/meter are shared_ptr-to-forward-declared, no `opentelemetry/*` include). [FR-011, D-4, FR-015]
- [ ] T015 [US1] Build the full library + test binaries under the libc++ lane with **OTel ENABLED** (primary acceptance — port `opentelemetry-cpp` under libc++); zero compile/link errors; detection reports fallback active. If OTel proves unbuildable under libc++ this cycle, fall back to the OTel-OFF config (T014) and record the OTel-under-libc++ port as a tracked follow-up. [SC-001, FR-006/FR-011]
- [ ] T016 [US1] Run the FULL functional suite once under the libc++ build; assert dispositions equal the libstdc++ build (no new failures attributable to the migration). [SC-002, FR-007]

**Checkpoint**: fixpp builds and passes under libc++ — MVP delivered.

---

## Phase 4: User Story 2 — Keep the libc++ fallback path honest over time (Priority: P2)

**Goal**: the fallback path is continuously regression-protected (forced-fallback on native + a Tier-2 libc++ lane), and the full primitive is validated against the type-erased shape.

**Independent Test**: trigger the libc++ lane (green build + sanitizers); separately run a native build with force-fallback and see the fallback exercised green.

### Tests for User Story 2

- [ ] T017 [P] [US2] Full primitive test inventory (tests/sync/) against the type-erased shape — rows 2,3,4,5,7,9,10: `atomic_shared_ptr_single_thread_roundtrip`, `_refcount_integrity`, `_contention_stress`, `_cas_equivalence` (3-discriminator: distinct-object FAIL / aliasing-same-raw-diff-ctrl FAIL / shared-ownership SUCCEED / null-null SUCCEED / expected-update on fail), `_publish_acquire_ordering`, `_linearizability`, `_allocator_pressure`, `_lock_free_reporting` (false on fallback), `_randomized_stress` (~30s, env-tunable). [SC-003, FR-009, D-7]
- [ ] T018 [P] [US2] Feature-detection probe test `tests/sync/atomic_shared_ptr_detection_probe` (row 8): correct yes/no per cell — native (libstdc++) + libc++ + both forced overrides. [FR-010, E-2]
- [ ] T019 [P] [US2] Forced-fallback LINK tests per owning target (row +B): `tls_forced_fallback_link`, `transport_forced_fallback_link`, `session_forced_fallback_link` — build the whole config in fallback mode and assert the `shard_guard` symbols resolve. [D-9, CT-4, Codex #4]

### Implementation for User Story 2

- [ ] T020 [US2] Wire the forced-fallback build config (`-DFIXPP_FORCE_ATOMIC_SHARED_PTR_FALLBACK`) on the native libstdc++ toolchain as a single target-level mode applied uniformly (always-ship-guard); run the concurrency/primitive tests under ASan, UBSan, TSan — green, ONE sanitizer preset at a time. [SC-003/SC-006, FR-009/FR-010, D-9]
- [ ] T021 [US2] Stand up the Tier-2 / opt-in (label-triggered, like `windows-msvc`) `linux-clang-libc++` CI lane: full library build + the concurrency-relevant test subset under available sanitizers on the fallback path; OTel-scoped per D-4; enable LSan where the host allows. The "concurrency-relevant subset" = the CTest `sync` label scope: all `tests/sync/` targets + the four per-consumer publish/acquire witnesses from T008 + the forced-fallback link tests from T019 (label each accordingly). [FR-011, E-6, analyze B2]

**Checkpoint**: the fallback path is honest on every supported native toolchain + the libc++ lane.

---

## Phase 5: User Story 3 — Zero regression to existing Tier-1 toolchains (Priority: P3)

**Goal**: libstdc++/MSVC-STL builds are byte-identical; the §XV.9 awaitable-mutex corpus gate passes under both stdlibs; no new waivers.

**Independent Test**: build on libstdc++, confirm the primitive resolves to the std alias, run the full Tier-1 gate matrix — all dispositions unchanged.

### Tests for User Story 3

- [ ] T022 [P] [US3] CT-1 corpus-gate hardening — edit `tools/check_no_std_mutex_in_awaitable_headers.sh` (three changes, Codex #1): (1) drop `|| true` so it fails-closed on a `-E` error; (2) add a `-stdlib=*` parser branch forwarding it into the include flags (today it falls to the L77 else → a "header not found" WARNING, not an error, so the libc++ leg silently preprocesses under host libstdc++); (3) use the preset's configured clang for the libc++ leg. Add a NEW libc++-leg ctest registered ONLY in the `linux-clang-libc++` preset (CT-1c): runs the script with `-stdlib=libc++` over `async_mutex.hpp` + `engine.hpp`, asserts each emitted the `asio/awaitable.hpp` marker (empty output ⇒ RED), AND a positive `_LIBCPP_VERSION`-active probe (preprocess `#include <version>` with `-dM`, RED if `_LIBCPP_VERSION` absent). Add the mutation fixture CT-1d (a project-local transitive include of a banned mutex type ⇒ the gate MUST fire) on BOTH the libstdc++ leg and the libc++ leg. The existing Tier-1 `check_no_std_mutex_corpus` membership is unchanged (engine.hpp already registered). [FR-012, SC-005, CT-1, plan Project Structure, New-P3-1]

### Implementation for User Story 3

- [ ] T023 [US3] Run the FULL existing Tier-1 gate matrix on the default libstdc++ build (the 6-preset sanitizer/coverage matrix, the §XV.9 corpus gate, ABI hygiene) and confirm every disposition is unchanged from before the migration — zero regression, no new waivers attributable to this feature. Native detection reports the std alias. [SC-005, US3 acceptance]

**Checkpoint**: Tier-1 path proven unperturbed.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T024 [P] Add the census-regrowth guard `tools/check_no_raw_atomic_shared_ptr.sh` (rejects any raw `std::atomic<std::shared_ptr<…>>` or `std::atomic_{load,store,exchange,compare_exchange}` on a `shared_ptr` in project-owned headers/sources outside `include/fixpp/core/sync/detail/atomic_shared_ptr.hpp`); ctest-register it alongside the corpus gate; mutation-test it (inject a raw re-introduction ⇒ gate fails). [FR-005, SC-004, D-6, E-7]
- [ ] T025 [P] Stale-documentation + lock-free-assumption census (E-4a) — reword: `src/tls/pinset.cpp:8`, `include/fixpp/tls/pinset.hpp:118-120` (find() "lock-free" → lock-free only on native; shard-locked on fallback), `include/fixpp/session/engine.hpp:119` + `460-462` (it is now `fixpp::sync::atomic_shared_ptr`, not "Standard C++20 std::atomic"; keep the no-std::mutex-in-headers claim), `include/fixpp/core/sync/async_mutex.hpp:21`, `include/fixpp/transport/transport_factory.hpp:86-87`. Fix BOTH pinset benches (`bench/tls/bench_pinset_snapshot_acquire.cpp`, `bench/tls/bench_pinset_find.cpp`): (a) no 30 ns / 130 ns lock-free ceiling on the libc++/forced-fallback lane; (b) branch the detector on `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE`, NOT raw `__cpp_lib_atomic_shared_ptr` (New-P3-2). Grep the four consumers' tests + the V-6 perf gate and confirm NONE asserts lock-freedom. [Codex #8/New-D/New-P3-2, E-4a]
- [ ] T026 Reverse 023 CHK046 (FR-013) — **NOT [P]: its engine.hpp comment-absence mutation-check depends on T025's engine.hpp rewording (analyze F2)**: update the active 023 artifacts — `specs/023-engine-session-strand/checklists/concurrency.md` CHK046 + `spec.md §FR-014` + research/data-model/contracts — to record the reversal (objection removed by type-erasure, not overridden). Mutation-check: grep the active 023 artifacts and confirm the prohibition ("avoid the unshipped `atomic_shared_ptr`; pin the standard primitive") is ABSENT, not merely contradicted; and that no engine.hpp comment still claims the member is `std::atomic`. [FR-013, D-8]
- [ ] T027 [P] Discharge the two recording obligations (FR-014): update `spec/feature-catalogue.md` NFR-017 row backlog→delivered AND rewrite its stale description to the FOUR-consumer reality (`drain_latch_ptr_`, `pinset.snapshot_`, `transport_factory.cert_source_slot_`, `engine.reader_snapshot_`) + the type-erasure mechanism (lock in `src/core/sync/atomic_shared_ptr.cpp`, header mutex-free, no amendment) + the FR-008 I-23/I-13 = async-mutex-only correction; update `specs/006-async-mutex/research.md` D-4 pointer to reference this feature. Add the coverage-index note + any Behaviors-&-Limitations rows per the catalogue convention. **Also record two forward-tracking follow-ups** (analyze E1 + escalation-2): (a) FR-011a macOS Tier-2 lane (deferred, sequenced after the Linux lane is green); (b) IF the OTel-OFF fallback was taken in T015, the OTel-under-libc++ port as a tracked follow-up — point both at REMAINING-WORK.md / a future feature. [FR-014, New-E]
- [ ] T028 Run `quickstart.md` validation end-to-end: libc++ build (OTel ENABLED primary + OTel-OFF fallback recipe), force-fallback on native, the `-L sync` lane, the unfiltered full-suite acceptance command, and both corpus + census gates green. [quickstart, SC acceptance]
- [ ] T029 Feature-completeness audit (the /gate-b precondition): map every task ↔ FR-001..015 / SC-001..006 ↔ catalogue NFR-017; assert 100% covered or each gap explicitly waived. [const §XVII.8, feedback_feature_completeness_gate]

---

## Dependencies & Execution Order

- **Setup (T001–T003)**: start immediately; T001/T003 are [P]. T002 depends on T001 (detect header).
- **Foundational (T004–T006)**: depends on Setup; BLOCKS all user stories. T004 → T005 → T006 (build graph order).
- **US1 (T007–T016)**: depends on Foundational. Tests T007/T008 first. Migrations T009–T012 ([P] except T009/T012 touch awaitable headers — verify corpus gate after each). T013 (profile) → T014 (OTel toggle) → T015 (build) → T016 (run).
- **US2 (T017–T021)**: depends on Foundational + the migrated consumers (US1 T009–T012) for the consumer/link tests. Tests T017–T019 [P] first. T020 (forced-fallback) → T021 (lane).
- **US3 (T022–T023)**: depends on US1 migrations (the headers must be migrated before the corpus gate is re-proven). T022 (gate edits) before T023 (full Tier-1 matrix).
- **Polish (T024–T029)**: T024/T025/T027 are [P] (different files). **T026 is NOT [P] — it depends on T025** (its engine.hpp comment-absence check runs after T025 rewords engine.hpp; analyze F2). T028 after the lanes exist (US1/US2). T029 last (the completeness gate, /gate-b precondition).

## Implementation Strategy

- **MVP** = Setup + Foundational + US1 (fixpp builds & runs under libc++).
- Then US2 (keep it honest), US3 (prove zero Tier-1 regression), then Polish (guards, censuses, recording).
- Commit after each task or logical group. Build `-j2`; sanitizer presets one at a time.

## Notes

- The no-amendment thesis and the XV §1 PASS disposition are settled (Gate A) — do NOT reopen them; the work is integration + re-validation.
- Two awaitable headers (`async_mutex.hpp`, `engine.hpp`) must stay mutex-free — re-run the corpus gate after T009/T012.
- The primitive's CAS/`exchange` is tested at the primitive level only (no consumer uses it); consumers witness publish/acquire (load/store) only.
