# Process: Completeness & Traceability Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that the completeness-gate, catalogue/coverage-index, traceability, and feature-done requirements are themselves well-specified, unambiguous, and measurable — the process surface that prevents the 001-class end-gap. Tests whether the *process requirements* are well-written, not whether the process ran.
**Created**: 2026-05-16
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md) · **Tasks**: [tasks.md](../tasks.md) (T057/T058)
**Audience**: Reviewer at Gate A/B

## ID Scheme & Bidirectional Traceability

- [ ] CHK001 - Is a stable requirement ID scheme (FR-001..FR-018, SC-001..SC-008) established and used consistently across spec/plan/tasks? [Traceability, Spec Requirements]
- [ ] CHK002 - Is every FR- required to map to ≥1 task AND ≥1 SC-, with the bidirectional mapping an explicit obligation (not implied)? [Traceability, tasks.md T058]
- [ ] CHK003 - Is every owned catalogue row (W-001..W-014, OSS-006/008) required to trace to an FR/SC and a coverage-index entry? [Traceability, Plan §VI.4 / tasks.md T057]
- [ ] CHK004 - Is the FR-006 / FR-013 "covered-but-underspecified" status from `/analyze` reconciled or explicitly accepted, so traceability gaps are not silently inherited? [Consistency, /analyze report C1/U1]

## Catalogue / Coverage-Index Update Requirements

- [ ] CHK005 - Is the disposition of each owned catalogue row specified individually (which rows `backlog`/`in-progress`→`done`, which stay deferred), not as a blanket "flip all"? [Clarity, tasks.md T057]
- [ ] CHK006 - Is OSS-013's non-flip (post-1.0 v1.2, pattern-only) stated explicitly so it is not wrongly closed by the audit? [Conflict, tasks.md T057 / `[const §XVIII.2]`]
- [ ] CHK007 - Is the ordering requirement explicit — coverage-index entry exists *before* a row is declared done (`[const §VI.4]`) — rather than left to inference? [Clarity, Plan Constitution Check §VI.4]
- [ ] CHK008 - Is the per-row evidence set required by `[const §VI.6]` (matching `/specify` artifact, verifying tests, Gate A, Gate B) stated as a precondition to declaring a row done? [Completeness, `[const §VI.6]`]

## Completeness Audit Definition (T058)

- [ ] CHK009 - Are the three audit assertion sets (tasks `[X]`-or-waived; FR/SC ↔ landed test AND impl; catalogue ↔ coverage-index) each defined precisely enough to be objectively evaluated? [Measurability, tasks.md T058]
- [ ] CHK010 - Is "re-checked against the merged tree, not the plan" stated explicitly, so the audit cannot pass on planned-but-absent coverage? [Clarity, tasks.md T058]
- [ ] CHK011 - Is "100% or recorded waiver" defined — what constitutes a valid waiver (rationale format, approver), so a waiver is not an unbounded escape hatch? [Ambiguity, tasks.md T058 / `[const §XVII.2]`]
- [ ] CHK012 - Is the audit's load-bearing relationship to `/gate-b` stated (a non-100%/non-waived result blocks `/gate-b` exactly like a RED verify verdict)? [Consistency, tasks.md T058 / `[const §XVII.8]`]
- [ ] CHK013 - Is the audit's output location specified unambiguously (a `## Completeness` section in `<feature>-verify.md` OR a sibling `<feature>-completeness.md`) so the `/gate-b` pre-flight can deterministically find it? [Clarity, tasks.md T058 / gate-b pre-flight 4d]

## "Feature Complete" as a Measurable State

- [ ] CHK014 - Is "feature complete" defined as a measurable end-state (audit 100%/waived + verify GREEN/YELLOW + both gate records) rather than an informal judgement? [Measurability, tasks.md Notes / `[const §XVII.8]`]
- [ ] CHK015 - Is the cutover unblock claim (001 FLOAT + 003 reify) tied to a measurable success criterion (previously-2b-gated tests GREEN, zero frozen-stub-surface refs) rather than a narrative assertion? [Measurability, Spec SC-006]
- [ ] CHK016 - Are cross-doc items D-9..D-12 explicitly excluded from 004's completeness scope (confirmations owned by 2c/2d/2e, not 004 blockers), so the audit boundary is bounded? [Clarity, research / Plan Gate A]

## Gate Records & Evidence Pairing

- [ ] CHK017 - Is the gate-label evidence-pairing rule referenced (gate-{a,b}-done requires verify GREEN + Codex convergence record) so labels are evidence, not decoration? [Traceability, `[const §XVII.8]` / quickstart §8]
- [ ] CHK018 - Is the tracked vs local-only record split specified (tracked = phase-4 doc + `research/reviews/`; `.specify/decisions/` gitignored), so the completeness/verify records are filed in the right place? [Clarity, Plan / project record-layout]
- [ ] CHK019 - Is the distinction between the pre-`/tasks` Gate A *review record* and the PR-scoped gate *label* stated, so "Gate A done" is unambiguous about which event it refers to? [Ambiguity, quickstart §8 / Plan Gate A]
- [ ] CHK020 - Is the local-build approval obligation (`[const §XVII.7]` AskUserQuestion before Conan/CMake) recorded against the GREEN/Tier-1 tasks so it is not a silent assumption? [Assumption, tasks.md Notes G1]

## Process Traceability

- [ ] CHK021 - Does every process obligation above trace to a constitution article, a tasks.md task, or a gate-b pre-flight step, with no completeness/traceability requirement lacking an enforcement anchor? [Traceability, `[const §VI]`/`[const §XVII.8]` / tasks.md T057/T058]
