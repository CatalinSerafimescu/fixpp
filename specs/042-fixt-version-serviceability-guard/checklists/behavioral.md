# Behavioral Contract Checklist: FIXT version-registry serviceability guard

**Purpose**: Gate-B-audience requirements-quality review ("unit tests for the requirements"). Validates
that the 042 behavioral contract is completely, clearly, and consistently specified — NOT that the code
works. Scope deliberately excludes API-surface / ABI-layout families (no new public surface, FR-007).
**Created**: 2026-06-17 | **Feature**: [spec.md](../spec.md)

## Behavioral Contract Correctness

- [ ] CHK001 Is the exact failure disposition (`error::invalid_session_config`) specified for the
  unserviceable-configured-default case, rather than a vague "fails"? [Clarity, Spec §FR-001]
- [ ] CHK002 Is "fail closed" defined as occurring **before any observable state mutation or wire
  emission**, with the ordering relative to the sibling open()-guards stated? [Clarity, Spec §FR-002]
- [ ] CHK003 Is "serviceable" defined unambiguously (the engine registry has an application dictionary
  registered for the configured version) and tied to the same predicate the inbound path uses?
  [Clarity, data-model §Serviceability predicate / INV-042-1]
- [ ] CHK004 Are the guard's trigger conditions specified as an exact truth table over
  (`begin_string`, `default_appl_ver_id` present?, registry null?, registry serves default?), so no
  input combination is left undefined? [Completeness, data-model §Guard truth table]
- [ ] CHK005 Is the role applicability stated unambiguously as **role-agnostic** (both acceptor and
  initiator), with the rationale and the orthogonality to 033 FR-004a documented? [Clarity/Consistency,
  Spec §FR-008 / Clarifications]

## Non-Regression / Byte-Identity

- [ ] CHK006 Is the serviceable-configured-default path required to remain **byte-identical** to current
  behaviour, with a measurable non-regression criterion? [Measurability, Spec §FR-003 / SC-002]
- [ ] CHK007 Is the non-FIXT (`begin_string != "FIXT.1.1"`) case explicitly specified as unaffected,
  and is the mechanism (outer FIXT gate short-circuit) documented so the claim is verifiable? [Coverage,
  Spec §FR-004 / contract NG-2]
- [ ] CHK008 Are the two pre-existing open()-guard disjuncts (absent default; null registry) documented
  as unchanged, distinguishing the NEW disjunct from them? [Consistency, data-model truth table]

## Inbound Non-Deadness (033 FR-004a preserved)

- [ ] CHK009 Is it explicitly required that the inbound peer-advertised-`1137` reject
  (`Reject 35=3,371=1137,373=5`) stays live and is NOT made dead by the new open() guard? [Completeness,
  Spec §FR-005 / INV-042-2]
- [ ] CHK010 Is the distinction between "this side's own configured default" (open() guard) and "the
  peer's advertised version" (inbound runtime check) stated clearly enough to prevent conflating them?
  [Clarity, Spec Edge Cases / contract NG-3]
- [ ] CHK011 Is the obligation to **rewrite** the two inherited inbound witnesses (so this side's own
  default is serviceable) — and the prohibition on editing them green by dropping their inbound-reject
  assertions — specified, with the named tests and lines? [Completeness, research §D-2 / tasks T008]
- [ ] CHK012 Is the inbound-non-deadness witness required to use a **NEW three-distinct-version
  registry** (serves own default, not the peer's version), rather than a reuse of an existing witness?
  [Clarity, contract §W4 / research §D-2a]

## Witness & Coverage Discipline

- [ ] CHK013 Are W1 (acceptor fail-closed) and W2 (initiator fail-closed) specified as **distinct,
  separately mutation-tested** obligations, so the symmetric-API claim is directly witnessed on both
  arms (not inferred)? [Coverage, contract §W2 / FR-008]
- [ ] CHK014 Is the RED-first requirement (witnesses fail before the guard exists) and the mutation
  check (drop disjunct #3 ⇒ witnesses re-fail) specified as acceptance criteria? [Measurability,
  Spec §SC-001 / quickstart]
- [ ] CHK015 Is the §IX.1 coverage obligation on the **new disjunct's true arm** (covered lcov DA line +
  taken BRDA branch) stated as a measurable gate? [Measurability, plan Constitution Check §IX.1 / SC-001]
- [ ] CHK016 Is a serviceable-path success witnessed for **both** roles (isolated acceptor and isolated
  initiator open()-success), so role-agnostic non-regression is covered? [Coverage, contract §W3]

## No-New-Surface Assertion

- [ ] CHK017 Is it explicitly asserted that NO new public wire field / error slot / config field /
  codegen output / C-ABI symbol is introduced (reuse of `error::invalid_session_config` +
  `version_registry::get`)? [Completeness, Spec §FR-007]
- [ ] CHK018 Is the §XV.9 "no new awaitable include edge" claim documented and tied to a verification
  step (unfiltered Tier-1), given `open()` is in the awaitable corpus? [Traceability, plan §XV.9 / tasks T013]

## Dependencies & Assumptions

- [ ] CHK019 Is the assumption that `default_appl_ver_id` is already the resolved enum (no wire-string
  resolution at open()) documented and validated against the source field type? [Assumption, plan §Assumptions]
- [ ] CHK020 Is the L-033-5 close-out (flip to RESOLVED with a code reference) specified as a required
  deliverable, so the limitation does not silently persist? [Completeness, Spec §SC-004 / tasks T011]

## Notes

- All items test requirement QUALITY (completeness/clarity/consistency/measurability), not code behavior.
- Audience: Gate B reviewer. Depth: focused (single-US behavioral hardening feature).
