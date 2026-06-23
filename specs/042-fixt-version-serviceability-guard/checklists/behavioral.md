# Behavioral Contract Checklist: FIXT version-registry serviceability guard

**Audited 2026-06-22 — 20/20 PASS (step-9 gate satisfied for GA).**

**Purpose**: Gate-B-audience requirements-quality review ("unit tests for the requirements"). Validates
that the 042 behavioral contract is completely, clearly, and consistently specified — NOT that the code
works. Scope deliberately excludes API-surface / ABI-layout families (no new public surface, FR-007).
**Created**: 2026-06-17 | **Feature**: [spec.md](../spec.md)

## Behavioral Contract Correctness

- [x] CHK001 Is the exact failure disposition (`error::invalid_session_config`) specified for the
  unserviceable-configured-default case, rather than a vague "fails"? [Clarity, Spec §FR-001]
  — PASS: spec.md FR-001 names `error::invalid_session_config` verbatim; confirmed at session.cpp:1006
  (`co_return std::unexpected(error::invalid_session_config)` in the new disjunct #3).
- [x] CHK002 Is "fail closed" defined as occurring **before any observable state mutation or wire
  emission**, with the ordering relative to the sibling open()-guards stated? [Clarity, Spec §FR-002]
  — PASS: FR-002 states "MUST occur at open()-time, before any observable state mutation or wire
  emission (fail-closed, consistent with the sibling FQ-1 / security-profile / credential open()-guards)";
  data-model INV-042-3 codifies the ordering; source confirms the disjunct is in the same pre-mutation
  guard block (session.cpp:972-1007) before any state assignment; W1/W2 assert no frame emitted + not
  Active.
- [x] CHK003 Is "serviceable" defined unambiguously (the engine registry has an application dictionary
  registered for the configured version) and tied to the same predicate the inbound path uses?
  [Clarity, data-model §Serviceability predicate / INV-042-1]
  — PASS: data-model.md defines `serviceable(v) := registry != nullptr && registry->get(v).has_value()`
  and explicitly states "Identical to the inbound runtime check at session.cpp:2194-2195 (one notion of
  serviceability)"; INV-042-1 encodes this; source at session.cpp:1005 and :2281 confirm same call.
- [x] CHK004 Are the guard's trigger conditions specified as an exact truth table over
  (`begin_string`, `default_appl_ver_id` present?, registry null?, registry serves default?), so no
  input combination is left undefined? [Completeness, data-model §Guard truth table]
  — PASS: data-model.md §Guard truth table has 4 exhaustive rows; outer gate explicitly stated as
  collapsing the other variables when false ("guard skipped entirely (non-FIXT byte-identical, FR-004)");
  all FIXT-branch combinations covered; no combination undefined.
- [x] CHK005 Is the role applicability stated unambiguously as **role-agnostic** (both acceptor and
  initiator), with the rationale and the orthogonality to 033 FR-004a documented? [Clarity/Consistency,
  Spec §FR-008 / Clarifications]
  — PASS: spec.md Clarifications (2026-06-17) records the settled Q&A with full rationale; FR-008
  mandates role-agnostic with no role gate; orthogonality to 033 FR-004a stated in FR-008 and research
  D-2; 033 FR-004a verified acceptor-scoped at specs/033-fixt-fix50sp2-session/spec.md:100.

## Non-Regression / Byte-Identity

- [x] CHK006 Is the serviceable-configured-default path required to remain **byte-identical** to current
  behaviour, with a measurable non-regression criterion? [Measurability, Spec §FR-003 / SC-002]
  — PASS: FR-003 requires "byte-identically to current behaviour"; SC-002 states "0 regressions across
  the existing session/FIXT test suite and the live interop matrix"; contract NG-1 restates; W3 in
  contracts (both roles) is the measurable witness; the guard's serviceable path falls through to the
  existing success path byte-identically by construction.
- [x] CHK007 Is the non-FIXT (`begin_string != "FIXT.1.1"`) case explicitly specified as unaffected,
  and is the mechanism (outer FIXT gate short-circuit) documented so the claim is verifiable? [Coverage,
  Spec §FR-004 / contract NG-2]
  — PASS: FR-004 requires all non-FIXT sessions "entirely unaffected (byte-identical open() behaviour)";
  spec Edge Cases documents the mechanism ("guard is gated on the FIXT begin-string"); contract NG-2
  restates; verifiable at session.cpp:1003 (`cfg_.begin_string == "FIXT.1.1"` outer gate).
- [x] CHK008 Are the two pre-existing open()-guard disjuncts (absent default; null registry) documented
  as unchanged, distinguishing the NEW disjunct from them? [Consistency, data-model truth table]
  — PASS: data-model truth table rows #1/#2 labeled "existing" and row #3 labeled "NEW (prod-reachable)"
  in the New? column; spec Edge Cases states "those two disjuncts are unchanged; this feature adds the
  ... disjunct"; session.cpp:974-999 comments label all three arms with #3 as "(042, production-reachable)".

## Inbound Non-Deadness (033 FR-004a preserved)

- [x] CHK009 Is it explicitly required that the inbound peer-advertised-`1137` reject
  (`Reject 35=3,371=1137,373=5`) stays live and is NOT made dead by the new open() guard? [Completeness,
  Spec §FR-005 / INV-042-2]
  — PASS: FR-005 is explicit: "MUST remain live and unchanged ... does not replace, weaken, or make dead
  the inbound peer-version check"; INV-042-2 encodes this as an invariant; SC-003 provides the measurable
  outcome; inbound path at session.cpp:2263-2281 is unchanged.
- [x] CHK010 Is the distinction between "this side's own configured default" (open() guard) and "the
  peer's advertised version" (inbound runtime check) stated clearly enough to prevent conflating them?
  [Clarity, Spec Edge Cases / contract NG-3]
  — PASS: spec Edge Cases states "The new open()-time guard validates this side's own default_appl_ver_id
  ... the existing inbound runtime check on the peer-advertised DefaultApplVerID(1137) ... remains live";
  FR-005 restates the same distinction; contract NG-3 names it; INV-042-2 encodes the "different axis"
  claim.
- [x] CHK011 Is the obligation to **rewrite** the two inherited inbound witnesses (so this side's own
  default is serviceable) — and the prohibition on editing them green by dropping their inbound-reject
  assertions — specified, with the named tests and lines? [Completeness, research §D-2 / tasks T008]
  — PASS: FR-008 names both tests — W3_Unserviceable1137_AcceptorRejectsWithVII_NotActive
  (test_fixt_logon_establishment.cpp:887) and W_Unserviceable1137_ToAdminObserved_ValueIsIncorrect_
  Disconnected (:1302) — with "MUST be rewritten ... NOT edited-green by dropping their inbound-reject
  assertions"; research D-2/D-2a specify the three-version-registry shape; tasks T008 carries the
  obligation; source at :888-898 confirms rewrite executed with "042 rewrite" comment.
- [x] CHK012 Is the inbound-non-deadness witness required to use a **NEW three-distinct-version
  registry** (serves own default, not the peer's version), rather than a reuse of an existing witness?
  [Clarity, contract §W4 / research §D-2a]
  — PASS: contract W4 states "a NEW three-distinct-version-registry witness (NOT a reuse of the existing
  033/038 inbound reject witness)" with the exact shape (registry {v44, v50sp2}, own default = v44,
  peer advertises v50sp1 absent); research D-2a specifies the same; W4_042_InboundNonDeadness_
  PeerUnserviceableSurvives at test_fixt_logon_establishment.cpp:1598 implements it.

## Witness & Coverage Discipline

- [x] CHK013 Are W1 (acceptor fail-closed) and W2 (initiator fail-closed) specified as **distinct,
  separately mutation-tested** obligations, so the symmetric-API claim is directly witnessed on both
  arms (not inferred)? [Coverage, contract §W2 / FR-008]
  — PASS: contract §W1 and §W2 carry separate mutation clauses; FR-008 cites
  [feedback_symmetric_api_claim_unreachable_arm] requiring the initiator arm be directly witnessed;
  tasks T002/T003 are separate with separate RED confirmations (:1489, :1511); source confirms two
  separate test functions at test_fixt_logon_establishment.cpp:1503 and :1538.
- [x] CHK014 Is the RED-first requirement (witnesses fail before the guard exists) and the mutation
  check (drop disjunct #3 ⇒ witnesses re-fail) specified as acceptance criteria? [Measurability,
  Spec §SC-001 / quickstart]
  — PASS: SC-001 encodes the behavioral gate (100% rejection of unserviceable sessions); quickstart.md
  §RED-first specifies all three steps explicitly; tasks T002/T003 record "RED confirmed" and T009
  records "W1+W2 RED on mutation (27 others GREEN); restored + all 29 GREEN."
- [x] CHK015 Is the §IX.1 coverage obligation on the **new disjunct's true arm** (covered lcov DA line +
  taken BRDA branch) stated as a measurable gate? [Measurability, plan Constitution Check §IX.1 / SC-001]
  — PASS: plan Constitution Check §IX.1 states the obligation ("MUST be covered by a RED-first /
  mutation-tested witness ... no waiver anticipated"); quickstart.md §Coverage states "must show a
  covered lcov DA line + taken BRDA branch ([const §IX.1])"; coverage-index.md §4.3.7 records "DA
  covered by W1_042+W2_042 (acceptor+initiator open-fail)".
- [x] CHK016 Is a serviceable-path success witnessed for **both** roles (isolated acceptor and isolated
  initiator open()-success), so role-agnostic non-regression is covered? [Coverage, contract §W3]
  — PASS: contract W3 requires "Pin both role arms explicitly: a serviceable acceptor open()-success AND
  an isolated serviceable initiator open()-success"; W3_042_Serviceable_BothRolesOpenSucceed at
  test_fixt_logon_establishment.cpp:1570 constructs separate FixtSetup per role and asserts both succeed.

## No-New-Surface Assertion

- [x] CHK017 Is it explicitly asserted that NO new public wire field / error slot / config field /
  codegen output / C-ABI symbol is introduced (reuse of `error::invalid_session_config` +
  `version_registry::get`)? [Completeness, Spec §FR-007]
  — PASS: FR-007 is explicit and exhaustive across all five surface kinds; plan §Constraints and
  §Scale/Scope repeat this; tasks notes confirm; catalogue S-043 records "No new wire field / error slot
  / config field / codegen / C-ABI surface (FR-007)".
- [x] CHK018 Is the §XV.9 "no new awaitable include edge" claim documented and tied to a verification
  step (unfiltered Tier-1), given `open()` is in the awaitable corpus? [Traceability, plan §XV.9 / tasks T013]
  — PASS: plan Constitution Check §XV.9 documents "version_registry.hpp already included in session.cpp
  and already called inside on_inbound_frame (:2195) — no new include edge"; tasks T013 ties to
  /speckit-verify unfiltered Tier-1; session.cpp:64 confirms `#include <fixpp/dict/version_registry.hpp>`
  unconditionally present pre-042.

## Dependencies & Assumptions

- [x] CHK019 Is the assumption that `default_appl_ver_id` is already the resolved enum (no wire-string
  resolution at open()) documented and validated against the source field type? [Assumption, plan §Assumptions]
  — PASS: spec.md §Assumptions states "SessionConfig::default_appl_ver_id already holds the resolved
  application-version value (not a raw wire string), so the open()-time check needs no wire-string
  resolution step"; data-model.md confirms type `std::optional<dict::application_version>` (citing
  SessionConfig:455); source at session.cpp:119 confirms same type.
- [x] CHK020 Is the L-033-5 close-out (flip to RESOLVED with a code reference) specified as a required
  deliverable, so the limitation does not silently persist? [Completeness, Spec §SC-004 / tasks T011]
  — PASS: SC-004 specifies "L-033-5 is dischargeable ... limitation row can be marked RESOLVED with a
  code reference"; tasks T011 makes it a concrete deliverable; spec/behaviors-and-limitations.md:1441
  shows "L-033-5 — RESOLVED by 042 (2026-06-17)" with full code + witness reference.

## Notes

- All items test requirement QUALITY (completeness/clarity/consistency/measurability), not code behavior.
- Audience: Gate B reviewer. Depth: focused (single-US behavioral hardening feature).

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 20 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **20** |

Anchors spot-verified: spec.md FR-001..008 / SC-001..004 / Clarifications / §Assumptions; data-model.md
§Guard truth table / §Serviceability predicate / INV-042-1..3; contracts/open-serviceability-guard.md
§W1..W4 / NG-1..4; research.md D-1..D-5; session.cpp:972-1007 (guard block) / :1003-1006 (disjunct #3)
/ :2263-2281 (inbound path unchanged); test_fixt_logon_establishment.cpp W1_042 :1503 / W2_042 :1538 /
W3_042 :1570 / W4_042 :1598 / inherited-rewrite :887; spec/behaviors-and-limitations.md:1441 L-033-5
RESOLVED; spec/feature-catalogue.md S-043; spec/coverage-index.md §4.3.7; constitution.md §IX.1 / §XII.5;
033 spec.md:100 FR-004a acceptor-scoped (orthogonality confirmed) — all resolve in signed-off artifacts.
