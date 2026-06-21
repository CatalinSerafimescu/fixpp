# Checklist: Concurrency & Portability Requirements Quality — 046-atomic-shared-ptr

**Purpose**: Unit-tests-for-English — validate that the REQUIREMENTS (spec FR/SC, contracts CT-1..CT-4, data-model E-1..E-7) for the libc++ fallback integration are complete, clear, consistent, measurable, and cover the concurrency/portability edge cases. NOT implementation tests.
**Created**: 2026-06-21
**Feature**: [spec.md](../spec.md) · derived from FR-001..015, SC-001..006, contracts, data-model E-4a

## 1. Concurrency correctness of the type-erased fallback

- [ ] CHK001 Is the publish/acquire guarantee (a reader `load(acquire)` observes either the prior or the new fully-constructed `shared_ptr`, never torn/null) stated as a testable requirement for all four consumers? [Completeness, Spec §FR-008 / §INV-3 / data-model E-1]
- [ ] CHK002 Is the CAS-equivalence rule defined precisely enough to test (success iff stored-ptr equality AND owner equality; aliasing-same-raw-diff-ctrl-block ⇒ unequal; null-null ⇒ equal; `expected` updated on failure)? [Clarity, Spec §Edge Cases / research D-7 / contract atomic-shared-ptr-api]
- [ ] CHK003 Is the memory-order obligation distinguished between consumers — async-mutex-specific I-23/I-13/I-24 vs the plain release/acquire pair for the other three — rather than over-generalized? [Consistency, Spec §FR-008]
- [ ] CHK004 Are refcount-integrity requirements specified for the fallback (weak_ptr snapshot → `store(nullptr)` → expired; no leaked strong ref / UAF)? [Coverage, plan Integrated test inventory row 3]
- [ ] CHK005 Is the no-nested-lock / no-deadlock property documented where two operands may hash to the same shard (e.g. `pinset` move/copy `store(load())`)? [Edge Case, research D-5 edge note]
- [ ] CHK006 Is the `noexcept` contract of the shard-guard ctor (terminate-on-`lock()`-throw, matching `std::atomic`'s no-throw contract) stated as a requirement, not left implicit? [Clarity, data-model E-3 / Clarifications 2026-06-21]
- [ ] CHK007 Are the omitted P0718 surfaces (`wait`/`notify[_one|_all]`) explicitly declared out of scope WITH a stated basis (no consumer uses them)? [Completeness, Spec §FR-001 / data-model E-1 / D-5]

## 2. Awaitable-header mutex-cleanliness / no-amendment thesis

- [ ] CHK008 Is "the header carries no `std::mutex` token on either standard library" stated as an objectively checkable requirement (vs an aspiration)? [Measurability, Spec §FR-012 / contract CT-2]
- [ ] CHK009 Is the corpus-gate falsifiability requirement specified — that the libc++ leg must actually run under libc++ (positive `_LIBCPP_VERSION` probe) and RED on empty/misconfigured output, not silently pass? [Clarity, contract CT-1c / plan Project Structure]
- [ ] CHK010 Is the mutation-discrimination requirement (a project-local transitive include of a banned mutex type MUST make the gate fire, on BOTH stdlib legs) defined? [Coverage, contract CT-1d / New-P3-1]
- [ ] CHK011 Are the FR-014 "six banned mutex types" enumerated so the header-cleanliness check is unambiguous about what it forbids? [Clarity, Spec §FR-012/FR-014]
- [ ] CHK012 Is the no-amendment claim internally consistent across all artifacts (no doc still implies a constitutional amendment or exemption)? [Consistency, plan §Constitution Check / research D-2 / data-model E-5]

## 3. libc++ build / portability + OTel toggle realizability

- [ ] CHK013 Is "full functional suite passes at least once under libc++" given a precise, measurable definition of "full" under both the OTel-ENABLED primary and OTel-OFF fallback configurations? [Measurability, Spec §FR-007/FR-011/SC-002]
- [ ] CHK014 Is the OTel toggle specified as a realizable paired Conan+CMake option that fails configuration on disagreement (not a CMake-only flag that cannot drop an unconditional Conan requires)? [Clarity, Spec §FR-011 / research D-4]
- [ ] CHK015 Is the public-API-invariance requirement under OTel-OFF stated (only `engine.cpp` gated; `engine.hpp`/`engine_config.hpp` unchanged) with its dependency-clean basis? [Consistency, Spec §FR-011/FR-015 / research D-4]
- [ ] CHK016 Is the dependency set to rebuild under libc++ censused from the ACTUAL active Conan options (not a stale list), and is the per-dep scope-out path defined for any that won't port? [Completeness, research D-3/D-4]
- [ ] CHK017 Is the CI-lane tier (Tier-2 / opt-in, label-triggered) and ongoing scope (concurrency-relevant subset under sanitizers) specified with the subset enumerated? [Clarity, Spec §FR-011 / tasks T021]
- [ ] CHK018 Is the macOS lane (FR-011a) clearly scoped OUT of this feature's acceptance with an explicit sequencing condition (after the Linux lane is green)? [Coverage, Spec §FR-011a / Assumptions]

## 4. Consumer-migration completeness + census-regrowth

- [ ] CHK019 Is the consumer census exact (all four members named with file+line) and is exact-set completeness — not subset — the stated acceptance? [Completeness, Spec §FR-004/SC-004 / data-model E-4]
- [ ] CHK020 Is the census-regrowth guard's scope unambiguous (what it scans, what it excludes — the primitive header, tests, third-party)? [Clarity, Spec §FR-005 / data-model E-7]
- [ ] CHK021 Is the guard's discrimination requirement defined (it MUST fail on an injected raw `std::atomic<std::shared_ptr>` re-introduction — mutation-verified)? [Measurability, Spec §SC-004 / research D-6]
- [ ] CHK022 Is the reduced-surface sufficiency claim backed by a documented call-site census (only `load`/`store` used; no `exchange`/CAS/`wait`/`notify`)? [Assumption, research D-5]

## 5. Zero Tier-1 regression + always-ship-guard linkage

- [ ] CHK023 Is "zero Tier-1 regression" given a measurable definition (every existing gate's disposition unchanged; no new waivers attributable to the migration)? [Measurability, Spec §SC-005]
- [ ] CHK024 Is the native zero-overhead requirement stated AND reconciled with the always-compiled guard TU (symbols present-but-never-called; alias never constructs a guard)? [Consistency, Spec §FR-002/SC-005 / research D-9 / data-model E-3]
- [ ] CHK025 Is the always-ship-guard force-mode contract pinned (one target-level decision; guard symbols always link) rather than deferred to implementation? [Clarity, research D-9 / contract CT-4]
- [ ] CHK026 Is the forced-fallback link-resolution requirement (one link test per owning target: tls/transport/session) defined? [Coverage, plan inventory row +B / contract CT-4]
- [ ] CHK027 Is SC-006's toolchain scope (libstdc++ this cycle; MSVC forced-fallback an explicit deferred follow-up) stated consistently with the spec's narrowing? [Consistency, Spec §SC-006]

## 6. 023 CHK046 reversal + stale-documentation census

- [ ] CHK028 Is the 023 CHK046 reversal framed as "the objection is removed (mutex-free header)" rather than an override, with the cross-referenced 023 artifacts enumerated? [Clarity, Spec §FR-013 / research D-8]
- [ ] CHK029 Is the mutation-check requirement defined (the CHK046 prohibition must be ABSENT from active 023 artifacts post-reversal, not merely contradicted)? [Measurability, research D-8 / data-model E-4a]
- [ ] CHK030 Is the stale-documentation census exhaustive (each false-on-fallback comment + the two pinset benches enumerated with corrected wording)? [Completeness, data-model E-4a]
- [ ] CHK031 Is the bench-detector correctness requirement stated (branch on `FIXPP_ATOMIC_SHARED_PTR_NATIVE_ACTIVE`, not raw `__cpp_lib_atomic_shared_ptr`; no lock-free ceiling on the fallback lane)? [Clarity, data-model E-4a / New-P3-2]
- [ ] CHK032 Is there a requirement to confirm NO consumer test or perf gate asserts lock-freedom (which the forced-fallback lane would violate)? [Coverage, data-model E-4a / New-D]

## 7. Recording / completeness obligations

- [ ] CHK033 Is the FR-014 catalogue-row obligation specified to rewrite the STALE description (four-consumer reality + type-erasure mechanism + FR-008 correction), not merely flip the status field? [Completeness, Spec §FR-014]
- [ ] CHK034 Are the forward-tracking follow-ups (FR-011a macOS; OTel-under-libc++ if the OFF fallback was taken) required to be recorded so they are not silently forgotten? [Coverage, tasks T027 / analyze E1]
- [ ] CHK035 Is a feature-completeness audit (every FR/SC ↔ task ↔ catalogue, 100% or explicitly waived) defined as a Gate-B precondition? [Traceability, tasks T029 / const §XVII.8]

## Notes

- Items are requirement-quality questions (is X specified/clear/consistent/measurable?), not implementation tests.
- Traceability: ≥80% reference a Spec §/contract/data-model anchor or a `[Gap]`/`[Assumption]` marker.
- This checklist is the input to `/checklist-audit` (pipeline step 9) — each item gets a PASS / SPEC-FIXED / DD-DECIDED / WAIVED disposition before `/speckit-implement` is unblocked.
