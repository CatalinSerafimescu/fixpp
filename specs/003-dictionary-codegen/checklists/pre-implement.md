# Pre-Implementation Requirements-Quality Checklist: 003-dictionary-codegen

**Purpose**: Formal pre-`/speckit-implement` reviewer gate — "unit tests for the requirements". Validates that the spec's requirements are complete, clear, consistent, measurable, and coverage-complete across all four high-risk domains (reify bridge & cross-strand; runtime dispatch & version resolution; determinism & build-graph hygiene; performance & allocation NFRs) **before** any code is written. Tests the *requirements*, not the implementation.
**Created**: 2026-05-15
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · companion: [requirements.md](requirements.md) (spec-quality gate)
**Scope**: All domains · Formal depth · Audience: pre-implement reviewer

## Requirement Completeness

- [ ] CHK001 - Are requirements defined for every artifact the codegen tool emits (Messages/Fields/Validator/Reify/NormativeReferences + the two `_dispatch/` headers), each with at least one binding AC? [Completeness, Spec §4.1–§4.4]
- [ ] CHK002 - Is the full set of `owning_<Msg>` move-semantics requirements documented (custom `noexcept` move, source+dest cache reset, lazy `view()` rebuild, copy-deleted), not just asserted in prose? [Completeness, Spec §4.3 AC-R2/AC-R4]
- [ ] CHK003 - Are requirements specified for every error slot the feature introduces (the six `core::error` 23–28 variants) including which condition maps to which slot? [Completeness, Spec §4.8 AC-VP6, §4.4 AC-D7]
- [ ] CHK004 - Is the ApplVerID(1128)→C++ `application_version` mapping documented exhaustively (every wire value, empty, and non-parsing input), not by example? [Completeness, Spec §4.8 AC-VP3]
- [ ] CHK005 - Are requirements for the curated CI conformance subset fully enumerated (what MUST be included) rather than left to sampling judgement? [Completeness, Spec §4.1 AC-G12]
- [ ] CHK006 - Are the CMake target-graph requirements complete for all seven targets (`v42/v44/v50sp2/vt11/all_versions/runtime/dispatch`) including their dependency edges? [Completeness, Spec §4.6 AC-C1]
- [ ] CHK007 - Does the spec define the determinism guarantee's full scope (which emitted files are golden-anchored vs. only byte-stable) rather than leaving `Reify.hpp`/`_dispatch` coverage implicit? [Gap, Spec §4.7 AC-T1, §6 NFR-003-7]
- [ ] CHK008 - Are requirements documented for the `version_registry` shape boundary (what ships vs. what is deferred to F3/2d) precisely enough to implement the shape without the ownership model? [Completeness, Spec §4.5 AC-X3]

## Requirement Clarity & Measurability

- [ ] CHK009 - Is every latency ceiling quantified with an unambiguous unit, percentile/condition, and regression budget (string/int/char ≤20 ns; decimal ≤75 ns; `field_value` ≤25 ns; reify bounds)? [Measurability, Spec §6 NFR-003-1/3]
- [ ] CHK010 - Is "zero allocation on the read path" stated with precise boundaries (which accessors, which decimal trait) so it is objectively verifiable? [Clarity, Spec §6 NFR-003-4, §4.1 AC-G4a]
- [ ] CHK011 - Is the "≤ 4 PMR allocations per `reify_as`" budget defined with an itemised account of what each allocation is, so an implementer can check conformance? [Measurability, Spec §4.3 AC-R7, plan data-model PMR accounting]
- [ ] CHK012 - Is the single-version compile-time ceiling (≤ 3 s) distinguished as load-bearing vs. the ≤ 15 s all-versions "soft" ceiling with a defined measurement method? [Clarity, Spec §6 NFR-003-2]
- [ ] CHK013 - Is "byte-identical across runs and machines" defined with the exact invariant (sorted, locale-independent, bytewise) so determinism is testable rather than aspirational? [Measurability, Spec §6 NFR-003-7, Assumption A4]
- [ ] CHK014 - Is AC-V4's Length+Data pair-table location resolved to a single artifact, or does "(in Validator.hpp or Fields.hpp, /plan-locked)" leave an unresolved ambiguity in the spec text? [Ambiguity, Spec §4.2 AC-V4]
- [ ] CHK015 - Is NFR-003-6 (`[[clang::lifetimebound]]`/`[[nodiscard]]` emitted unconditionally) tied to a measurable verification artifact, or only to "static inspection"? [Measurability, Spec §6 NFR-003-6]
- [ ] CHK016 - Is the `owning_<Msg>` thread-safety claim stated unambiguously (single-strand-only; the only safe concurrent pattern is reify-A→move→consume-B) rather than as a blanket "thread-safe" term? [Clarity, Spec §4.7 AC-T3]
- [ ] CHK017 - Is "fail-loud default arm" for the application dispatch switch defined with the exact returned error and triggering condition? [Clarity, Spec §4.4 AC-D5/AC-D7]

## Requirement Consistency

- [ ] CHK018 - Are the decimal-accessor requirements consistent across AC-G4, AC-G4a, NFR-003-4, and data-model Entity 1 (all PMR-mandatory `decimal_t::parse(bytes, mr)`, `mr` a parameter not a member)? [Consistency, Spec §4.1, §6]
- [ ] CHK019 - Is the flyweight `sizeof == one pointer` invariant stated consistently everywhere the decimal `mr` parameter is introduced (no contradiction implying an `mr` member)? [Consistency, Spec §4.1 AC-G7/AC-G4]
- [ ] CHK020 - Are the `owning_message_traits<Msg>` / `owning_message_t<Msg>` resolvent requirements consistent between AC-G7a, `contracts/reify.hpp`, and `contracts/generated_message.hpp` (the canonical 2c v1.4 §4.8 external-trait form, no `Msg::owning_type` residue)? [Consistency, Spec §4.1 AC-G7a]
- [ ] CHK021 - Is `resolved_message_version`'s authoritative definition single-sourced (declared in `version_profile.hpp` per AC-VP1, consumed-not-redeclared by `reify.hpp` per AC-R6) with no conflicting duplicate shape? [Consistency, Spec §4.8 AC-VP1, §4.3 AC-R6]
- [ ] CHK022 - Are the version-coverage requirements consistent between the "full message set emitted" rule and the "curated CI subset / exhaustive nightly" rule (no implied partial-header contradiction)? [Consistency, Spec §1, §4.1 AC-G12, Assumption A3]
- [ ] CHK023 - Is the `get<1128>()` / `get<35>()` frozen-contract dependency stated consistently across AC-R8, AC-D2, AC-D3, and the R6 drift-guard seam (#18) with no divergent expectations? [Consistency, Spec §4.3/§4.4, §9 seam #18]
- [ ] CHK024 - Are scope boundaries consistent between spec §5 (out of scope) and the ACs/seams (e.g., behavioral validation excluded but `Validator.hpp` shape-tested; overlay excluded but `field_value` forwarder shipped)? [Consistency, Spec §5, §4.2 AC-V3, §4.1 AC-G6]

## Acceptance Criteria Quality

- [ ] CHK025 - Does every AC family (G/V/R/D/X/C/T/VP/FT) have at least one acceptance criterion that is objectively pass/fail without interpreting implementation behavior? [Acceptance Criteria, Spec §4]
- [ ] CHK026 - Are the negative/exclusion ACs (AC-FT2 decimal NOT a field_traits specialisation; AC-VP4 C++ index NOT reused; AC-G9/G10 messages NOT emitted) framed as verifiable assertions rather than absence-of-evidence? [Acceptance Criteria, Spec §4.8 AC-FT2/AC-VP4, §4.1 AC-G9/G10]
- [ ] CHK027 - Is AC-G7a's compile-time shape-oracle expressed as a checkable criterion (ill-formed alias on mis-wire) rather than a runtime expectation? [Measurability, Spec §4.1 AC-G7a]
- [ ] CHK028 - Are the reify error-path ACs (AC-R7 OOM→`dict_reify_oom`; AC-R8 msg-type mismatch; AC-R6 `as<Msg>()` nullptr on mismatch) each tied to a distinct, observable return value? [Acceptance Criteria, Spec §4.3]
- [ ] CHK029 - Is AC-C4 (configure-time / build-tree-only / `INTERFACE_INCLUDE_DIRECTORIES` / source-tree-clean) bound to a concrete acceptance mechanism (the named CTest target) rather than asserted narratively? [Acceptance Criteria, Spec §4.6 AC-C4]
- [ ] CHK030 - Are the AC-D4 worked-example resolution outcomes (Logon→vt11, NOS=9→v50sp2, NOS=6→v44, OCR→v50sp2 default, Heartbeat→vt11) each individually assertable? [Acceptance Criteria, Spec §4.4 AC-D4]

## Scenario & Edge-Case Coverage

- [ ] CHK031 - Are requirements defined for the FIXT `default_appl == Unknown` + no-ApplVerID edge (must yield `dict_unresolved_application_version`, NOT `dict_reify_unknown_msg_type`)? [Edge Case, Spec §3 Edge Cases, §4.4 AC-D6]
- [ ] CHK032 - Are requirements defined for the runtime-XML-only resolved-version negative path via a hand-built synthetic `MessageView` (no `FIX43.xml` dependency)? [Coverage, Spec §3.6, §4.4 AC-D5]
- [ ] CHK033 - Are requirements defined for `std::move` of an `owning_<Msg>` *after* its lazy `view()` cache was populated (both caches reset; moved-to rebuilds; concurrent reads UB)? [Edge Case, Spec §3 Edge Cases, §4.3 AC-R4]
- [ ] CHK034 - Are requirements defined for PMR allocation failure inside the `noexcept` reify path (trap_throw → `dict_reify_oom`, no terminate)? [Exception Flow, Spec §3 Edge Cases, §4.3 AC-R7, §9 seam #16]
- [ ] CHK035 - Are requirements defined for the allocating-trait decimal substitution (`cpp_dec_float`) edge — heap traffic confined to caller `mr`, never raw `new`/`delete`? [Edge Case, Spec §4.1 AC-G4a, §6 NFR-003-4]
- [ ] CHK036 - Are requirements defined for source XML declaring out-of-locked-set messages (FIX-Latest A-035..A-065 → warning, not emitted; A-014..A-034 → not typed)? [Coverage, Spec §3 Edge Cases, §4.1 AC-G9/AC-G10]
- [ ] CHK037 - Are requirements defined for capturing a typed flyweight past the originating view's lifetime (release-UB, debug-trap via generation counter; supported escape is `reify_as`)? [Edge Case, Spec §3 Edge Cases, §4.1 AC-G8]
- [ ] CHK038 - Are the cross-strand handoff requirements coverage-complete (reify on A → move → A's arena reset → B reads stable values; original traps post-reset under TSan)? [Coverage, Spec §3.2, §4.3 AC-R5, §9 seam #12]
- [ ] CHK039 - Is the recovery/no-owner scenario class addressed (resolved version has no codegen owner) without silently misdispatching? [Recovery, Spec §4.4 AC-D5, §3.6]

## Dependencies, Assumptions & Conflicts

- [ ] CHK040 - Is the upstream-surface ownership unambiguous post-RC#1 (003 owns `version_profile`/`resolve_application_version`/`field_traits`/`decode_field`; 002 ships enums only) with no residual "002-shipped" claim? [Assumption, Spec §8 Upstream dependency audit, Assumption A6]
- [ ] CHK041 - Is the dict↔wire bridge dependency documented as a header-only dual-compile carve-out (no module link edge, no `wire↔dictionary` cycle) rather than an unstated layer violation? [Dependency, Spec §6 NFR-003-8, §11 R6]
- [ ] CHK042 - Is the vendored frozen `wire::MessageView<Index>` stub's contract scope and 2b-swap/drift-guard mechanism specified precisely enough to bound the R6 risk? [Assumption, Spec §11 R6, Assumption A2]
- [ ] CHK043 - Are all deferred items (F1–F6) given a concrete, non-speculative trigger so scope exclusions are validated rather than open-ended? [Assumption, Spec §10]
- [ ] CHK044 - Is the "no new Conan dependency / no second XML parser" assumption (F1 Candidate A) stated as a verifiable constraint rather than a design aspiration? [Assumption, Spec §10 F1, plan §"Primary Dependencies"]
- [ ] CHK045 - Are the conformance-corpus data-source assumptions (public QuickFIX/exchange samples only; no proprietary message data) documented as a validated constraint? [Assumption, Spec Assumption A5, §9 seam #1]
- [ ] CHK046 - Is the residual R3/R4 golden-coverage gap (no golden for `Reify.hpp`/`_dispatch`) explicitly recorded as an accepted scope decision rather than an unflagged blind spot? [Conflict, plan §"Round 1 — new contract-test / residual-risk items" N-P3-2]

## Notes

- Check items off as completed: `[x]`; record findings inline. An item "fails" if the requirement it interrogates is missing, ambiguous, inconsistent, or unmeasurable — the fix is to amend `spec.md`/`plan.md`, not the (not-yet-written) code.
- This is a **requirements-quality** gate, not a test plan. Items ask "is X specified well?", never "does X work?".
- Traceability: 46/46 items carry a `[Spec §…]` reference or a `[Gap]/[Ambiguity]/[Conflict]/[Assumption]` marker (100%, exceeds the ≥80% minimum).
- Companion `requirements.md` covers the standard Spec-Kit spec-quality gate (content quality / completeness / readiness + Gate-A history); this file is the deeper per-domain pre-implement audit.
- Bundle state at creation: Gate A converged replan-loop round 3 (commit 3824bb5); `/speckit-analyze` clean (0 CRITICAL/HIGH; LOW items CHK014/CHK015 mirror analyze findings A1/U1). Ready for `/speckit-implement` once this gate is worked through.
