# Specification Quality Checklist: 003-dictionary-codegen

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-05-15
**Feature**: [spec.md](../spec.md)

## Content Quality

- [~] No implementation details (languages, frameworks, APIs) — **EXCEPTION (design-anchored Phase 4 spec)**, see note
- [x] Focused on user value and business needs
- [~] Written for non-technical stakeholders — **EXCEPTION (design-anchored Phase 4 spec)**, see note
- [x] All mandatory sections completed

> **Honest-checkbox note (Gate A round 1, Codex P3-1 / Opus Confirm @ P3).** The two items above are marked `[~]` (explicit exception), **not** `[x]`: this spec is overtly implementation-bound — it names `tools/codegen/fixpp-codegen`, `wire::MessageView<Index>`, `dict::reify_as`, `owning_message_handle`, per-header filenames, and exact C++ namespaces/target names (spec §1, §4, §7). That is a deliberate **project convention** for design-anchored Phase 4 / Spec-Kit features over a *signed-off design doc* (`2c-codegen.md` v1.3): C++ type/namespace names from the design doc are the spec's domain vocabulary and the contract the feature must meet — but the literal checklist statements ("no implementation details", "written for non-technical stakeholders") are false as worded, so they are recorded as an explicit exception rather than a silent pass. The one genuinely open implementation decision (codegen host-tool language) is explicitly deferred to /plan (F1, /clarify Q2), not baked in.

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [~] Success criteria are technology-agnostic (no implementation details) — **EXCEPTION (design-anchored Phase 4 spec)**, see Content-Quality note
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [~] Dependencies and assumptions identified — **corrected Gate A r1**: `version_profile`/`resolve_application_version`/`field_traits` were mis-identified as 002-merged; now restated as 003-owned **blocking** upstream surface (spec §8 "Upstream dependency audit"), resolved at re-`/plan`

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [~] No implementation details leak into specification — **EXCEPTION (design-anchored Phase 4 spec)**, see Content-Quality note

## Notes

- Three load-bearing scope decisions resolved inline during /specify (Clarifications Session 2026-05-15): Q1 message/conformance scope → all 4 versions + headline-CI/exhaustive-nightly corpus; Q2 codegen host-tool language → deferred to /plan (F1); Q3 Validator.hpp → emit + shape-test, defer behavior. `/clarify` marker count is **zero**.
- Acceptance criteria are grouped AC-G (typed messages), AC-V (Fields/Validator/NormativeReferences), AC-R (reify bridge), AC-D (runtime dispatch), AC-X (version_registry shape), AC-C (CMake targets), AC-T (determinism/threading) — each one testable, each tied to a `[2c §...]` anchor and/or a `[2c §9]` test seam.
- NFRs (NFR-003-1..8) are measurable (latency ns/µs ceilings, compile-time seconds, allocation counts, byte-identical determinism) and carry an explicit verification seam each.
- Five follow-ups (F1–F5) each have a concrete trigger; R6 flags the `wire::MessageView` build-ordering risk for Gate A.
- **Gate A round 1 (2026-05-15) outcome:** NOT converged in this pass. Three root causes (RC#1 unshipped+unassigned `version_profile`/`resolve_application_version`/`field_traits` surface → 003-owned, re-`/plan`; RC#2 inherited 2c §4.1.3/§4.7 `decimal_t::from_chars` incoherent with merged 2a/001 → 2c reopen, not bundle-fixable; RC#3 open `dict/`→`wire/` layer amendment) recorded in `plan.md` `## Gate A` and reflected throughout the bundle. `blocked_on_replan: yes` in spec/plan front-matter gates `/tasks` until 2c is reopened and `/plan` re-run.
