# Checklist: Security & Wire-Correctness Requirements Quality

**Feature**: `040-inbound-tag-overflow-hardening`
**Created**: 2026-06-15
**Audience**: Gate B reviewers
**Purpose**: "Unit tests for the requirements" — validate that the spec's threat model, helper
contract, disposition, completeness, and exclusion requirements are complete, clear, consistent, and
measurable BEFORE implementation. (Tests the requirements, not the code.)

## Threat Model & Scope Completeness

- [ ] CHK001 Is the forged-tag-overflow aliasing threat described with a concrete mechanism (uint32
  wrap → small-tag alias) rather than a vague "overflow risk"? [Clarity, Spec §Background]
- [ ] CHK002 Is the attacker class explicitly bounded (TLS-authenticated, CompID-bound per 015; no
  anonymous MITM) so severity (MED) is traceable to the threat model? [Traceability, Spec §Background/Assumptions]
- [ ] CHK003 Are ALL live-inbound tag scanners enumerated as an explicit, closed set (the census
  table), and is the basis for "complete" stated (idiom + non-idiom sweep)? [Completeness, Spec §Background]
- [ ] CHK004 Is each in-scope scanner's aliasable security tags listed (e.g. 34/49/52/56/1137) so a
  reviewer can judge security relevance per site? [Completeness, Spec §Background census]
- [ ] CHK005 Is the `build_replay_frame` exclusion justified by a stated reason (stored own-outbound,
  not inbound) rather than silently omitted? [Completeness, Spec §FR-008/Background site 6]
- [ ] CHK006 Are non-tag accumulators (value/length/seqnum/checksum) explicitly declared out of scope
  with examples, so a reviewer can confirm the boundary is intentional? [Clarity, Spec §Edge Cases]

## Helper Contract Clarity & Measurability

- [ ] CHK007 Is the bound value specified as exactly `0xFFFF` (16-bit tag space) with a rationale for
  why no legitimate tag is lost, rather than left as "a sensible limit"? [Clarity, Spec §Clarifications/FR-001]
- [ ] CHK008 Is "detect the overflow in-loop, before any multiply that could wrap" stated as a
  requirement (not just "add a bound")? [Clarity, Spec §FR-001]
- [ ] CHK009 Is the helper's return contract (bounded value + ok/overflow signal; MUST NOT embed
  disposition) specified unambiguously? [Clarity, Spec §FR-001, contracts/tag-scan-helper.md]
- [ ] CHK010 Is the digit-only precondition stated, AND is the consequence of violating it (a folded
  digit-check accepting a non-numeric tag) documented so it can't be "simplified" away? [Completeness, Spec §FR-007a, research D-3]
- [ ] CHK011 Is boundary correctness made measurable (65535 ok / 65536 reject / wrap-and-continue
  reject / zero-padded ok), and is a compile-time `static_assert` required (not just a runtime test)?
  [Measurability, Spec §FR-001, contracts §Compile-time guarantee]

## Disposition Preservation Consistency

- [ ] CHK012 Is each of the 5 sites' on-overflow disposition specified individually (Index
  `entries_.clear()`; Scan `done_`; session scanners `tag_ok=false`/skip), rather than a single
  generic "reject"? [Completeness, Spec §Clarifications, data-model disposition table]
- [ ] CHK013 Is the decision to KEEP per-site disposition (not a uniform whole-frame reject) recorded
  with its rationale (minimal blast radius), so the choice is auditable? [Traceability, Spec §Clarifications 2026-06-15]
- [ ] CHK014 Is it specified that a skipped/forged field can never be surfaced/queryable under the
  aliased tag (the security invariant), distinct from merely "rejected"? [Clarity, Spec §FR-004/FR-005, data-model invariant]
- [ ] CHK015 Are the requirements consistent on how a skipped required header field is handled
  downstream (existing missing-required-field handling provides frame-level rejection)? [Consistency, Spec §Clarifications]

## 038 Regression Vector & Cross-Feature Consistency

- [ ] CHK016 Is the 038 SendingTime-guard regression vector (a forged token aliasing 52 feeding the
  038 MaxLatency guard) called out as an explicit requirement to close, with traceability to the 038
  guard? [Traceability, Spec §US1/SC-002/Normative References]
- [ ] CHK017 Is the centralization decision (one helper vs N hand-rolled bounds) justified by the
  stated evidence (the one hand-rolled guard shipped wrong), making SC-004 a real requirement?
  [Consistency, Spec §SC-004, research D-1]

## Acceptance Criteria & Witness Coverage

- [ ] CHK018 Does every scanner site have a corresponding wrap-and-continue acceptance scenario /
  witness requirement (not just the central one)? [Coverage, Spec §US1/US2 Acceptance Scenarios, FR-007]
- [ ] CHK019 Are the canonical verified vectors (`429496729634`→34, `429496729649`→49) pinned as
  required proof tokens across the scanner set, rather than left as "e.g."? [Measurability, Spec §SC-001]
- [ ] CHK020 Is the non-digit negative witness (sites 4/5) specified as a required acceptance
  criterion, tied to the FR-007a precondition? [Coverage, Spec §FR-007a]
- [ ] CHK021 Is the deferral of the live cross-engine witness stated with its rationale and family
  (038 L-038-2), so the absence is a documented decision, not a gap? [Traceability, Spec §FR-007/Clarifications]
- [ ] CHK022 Is "conforming tags unchanged at every site" (no behavioral regression) a measurable
  acceptance criterion (boundary 65535, zero-padded, existing corpora)? [Measurability, Spec §FR-005/SC-003]

## Non-Functional & Boundary Requirements

- [ ] CHK023 Is the perf-neutrality requirement (no measurable throughput regression; helper inlines)
  stated measurably enough to witness against a benchmark baseline? [Measurability, Spec §FR-006]
- [ ] CHK024 Are the "no new error code / config / codegen / wire / C-ABI change" constraints stated
  as explicit requirements (so the reviewer can confirm scope discipline)? [Completeness, Spec §FR-009]
- [ ] CHK025 Is the exact-boundary edge case (accumulated value crossing `0xFFFF` mid-scan before any
  wrap; final wrapped value `≤0xFFFF` must still reject) specified, not just the naive case?
  [Edge Case, Spec §Edge Cases]

## Notes

- Items are requirements-quality questions for Gate B (do the requirements read complete/clear/
  consistent/measurable), NOT implementation tests. Dispositioned by `/speckit-checklist-audit`.
