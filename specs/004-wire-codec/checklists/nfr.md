# NFR & Safety Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that the non-functional / safety requirements (allocation, DoS, lifetime, noexcept, ABI-invisibility, performance) are complete, quantified, measurable, and internally consistent — *before* implementation. Tests the requirements, not the code.
**Created**: 2026-05-16
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md)
**Audience**: Reviewer at Gate A/B (pre-implementation requirements sign-off)

## Allocation Discipline

- [ ] CHK001 - Is the zero-allocation window bounded by a single, precisely-defined start and end point used identically across FR-012, SC-002, and the three-arena model? [Consistency, Spec FR-012/SC-002]
- [ ] CHK002 - Is "between the start of parse and the return of `fromApp`" defined unambiguously (what event is "start of parse"; whose `fromApp`)? [Clarity, Spec FR-012]
- [ ] CHK003 - Are the three arenas' lifetimes (per-message / session / writer-scratch) each specified with an explicit owner and reset trigger, with no arena left ambiguous? [Completeness, Spec Key Entities / Plan Constraints]
- [ ] CHK004 - Is the requirement that allocation-bearing trait specializations use the supplied arena (not heap) stated as a testable obligation rather than guidance? [Measurability, Spec FR-012]
- [ ] CHK005 - Is "Iter mode is zero-allocation end-to-end" stated as a distinct, separately-verifiable requirement from the Index-path arena bound? [Completeness, Spec FR-003/Plan]
- [ ] CHK006 - Are the worst-case arena footprint figures (e.g. ≈80 KiB at the 4096-entry cap; validator ≤~600 B) specified with their derivation basis so they are auditable? [Measurability, Plan PMR accounting]

## DoS / Capacity Bounds

- [ ] CHK007 - Are all four DoS caps (frame size, offset-table occurrences, group entries/instance, tag range) each specified with a default value AND a distinct error code for the exceed path? [Completeness, Spec FR-015]
- [ ] CHK008 - Is each cap's value stated once as a single source of truth (no drift between spec, plan, data-model)? [Consistency, Spec FR-015 / Plan / data-model]
- [ ] CHK009 - Is "occurrence space" vs "distinct-tag space" for the offset-table cap defined explicitly so 4096 is unambiguous? [Ambiguity, Spec FR-002/FR-015]
- [ ] CHK010 - Is the per-group-instance cap explicitly scoped as per-instance (not aggregate) in the requirement text? [Clarity, Spec Edge Cases]
- [ ] CHK011 - Is "bounded memory and no crash when exceeded" stated as an objectively verifiable acceptance condition? [Measurability, Spec FR-015/SC-003]
- [ ] CHK012 - Are the caps documented as caller-tunable bounds (not FIX-spec invariants), including the explicit "rejects conformant venue traffic on day one" consequence? [Completeness, Spec Assumptions]

## Lifetime & Flyweight Safety

- [ ] CHK013 - Is the flyweight lifetime contract (view lifetime ⊆ originating buffer) stated as a requirement, not only an entity description? [Completeness, Spec FR-016/Key Entities]
- [ ] CHK014 - Are the debug-trap behaviour and the release-strip behaviour of the generation token specified as two distinct, separately-testable requirements? [Clarity, Spec FR-016]
- [ ] CHK015 - Is the `[[clang::lifetimebound]]` obligation specified with its accepted enforcement gap (MSVC) so the requirement is not over-claimed? [Consistency, Spec FR-016/Assumptions]
- [ ] CHK016 - Is "capturing a view past `fromApp`" classified with a defined outcome for both release (UB) and debug (trap)? [Coverage, Spec Key Entities / data-model]

## noexcept & Exception Trapping

- [ ] CHK017 - Is the `noexcept` obligation scoped to a named surface set (5 primitives + `View`) and a named window, not stated generically? [Clarity, Spec FR-013]
- [ ] CHK018 - Is the behaviour of a throwing third-party trait wrapper specified as "trap, not propagate" with a defined trap mechanism reference? [Completeness, Spec FR-013/Plan §Constraints]
- [ ] CHK019 - Is the trap-vs-propagate requirement measurable (a test can assert no exception escapes)? [Measurability, Spec FR-013]

## ABI Invisibility

- [ ] CHK020 - Is "no C++ types through the C ABI / no `extern "C"` wire symbols" stated as a verifiable requirement with a named check (`nm`)? [Measurability, Spec FR-014/Plan §X.2]
- [ ] CHK021 - Is abidiff's non-applicability explicitly recorded (not silently omitted) so the absent gate is intentional? [Completeness, Plan §IX.5 / research D-13]
- [ ] CHK022 - Are the new `core::error` variants distinguished from the C-ABI `fixpp_error_t` so the additive-slot requirement is unambiguous about which surface it touches? [Ambiguity, data-model Error mapping]

## Performance Requirements

- [ ] CHK023 - Are the per-operation latency ceilings each bound to a named workload and measurement condition (compiler, build, cache state)? [Clarity, Plan Performance Goals]
- [ ] CHK024 - Is the ±5% regression budget specified against a named baseline artifact location? [Measurability, Plan §VIII.2]
- [ ] CHK025 - Is hffix parity explicitly scoped as a v1.0 release gate (not a this-PR blocker), removing ambiguity about when it gates? [Consistency, Plan / research D-14]
- [ ] CHK026 - Are the coverage thresholds (≥90% line / ≥80% branch) stated against a named module path so "touched modules" is unambiguous? [Clarity, Plan §IX.1]
- [ ] CHK027 - Is the debug generation-counter cost requirement quantified with a decision threshold (e.g. >2× release ⇒ fallback) rather than left qualitative? [Measurability, research D-8]

## Traceability

- [ ] CHK028 - Does every NFR above trace to at least one SC- success criterion or a named gate, with no orphan NFR lacking a measurable outcome? [Traceability, Spec SC-001..SC-008]
