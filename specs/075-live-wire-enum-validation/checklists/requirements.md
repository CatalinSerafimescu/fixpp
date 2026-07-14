# Specification Quality Checklist: Live-Wire Enum-Value Validation

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-14
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *source anchors are cited as evidence for WHAT is broken, not as prescribed HOW; no mechanism (storage layout, function signature, tokenizer implementation) is mandated*
- [x] Focused on user value and business needs — *an operator's strict-validating session currently accepts out-of-domain enum values; the reject path is built and dead*
- [x] Written for non-technical stakeholders — *Context + user stories are readable without C++; the census table quantifies the stake*
- [x] All mandatory sections completed

## Requirement Completeness

- [x] **No [NEEDS CLARIFICATION] markers remain** — **RESOLVED 2026-07-14** (Clarifications session): Q1 = all ten dictionaries (⇒ Constitution disposition (a), amend Article I §1 → v0.7); Q2 = rides the existing `validate_inbound_messages` flag, no sub-flag; Q3 = enum-check all inbound messages incl. Logon (QuickFIX parity, source-verified). All three folded into FR-001/FR-010/FR-013/FR-014 and the Edge Cases, not left only in the Clarifications section.
- [x] Requirements are testable and unambiguous — *FR-001..FR-013 each name an observable outcome; FR-003/FR-004/FR-008/FR-009 pin the exact accept/reject boundary*
- [x] Success criteria are measurable — *SC-002 pins exact counts (245 fields / 1708 codes for FIX44) against the shipped XML; SC-001/SC-003 demand mutation-discriminating witnesses*
- [x] Success criteria are technology-agnostic — *stated as accept/reject outcomes, reject reasons, counts, and "no regression", not as function names*
- [x] All acceptance scenarios are defined — *3 stories, 9 scenarios*
- [x] Edge cases are identified — *8 recorded, incl. the two highest-risk: multi-value tokenization and pre-`interpret_logon` Logon validation*
- [x] Scope is clearly bounded — *explicit Non-Goals section; L-069-1 called out as NOT retired*
- [x] Dependencies and assumptions identified — *Assumptions section; Constitution Check section flags the Article I §1 / XVIII §5 interaction*

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows — *P1 single-value reject, P1 multi-value tokenization (co-equal: getting it wrong breaks conformant traffic), P2 legacy-dict enum data*
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- **Clarifications RESOLVED** (2026-07-14). Q1 = all ten dictionaries; Q2 = existing flag, no sub-flag; Q3 = all messages incl. Logon (QuickFIX parity).
- **Carries a constitution amendment.** Q1 = all-ten settles the Constitution Check on disposition **(a)**: amend Article I §1 (MINOR, v0.6 → v0.7) to narrow the FIX-Latest post-1.0 carve-out to *typed codegen + ApplExtID + session negotiation*. Must ride this branch with a Sync Impact Report, ratified at Gate A, with the feature-catalogue **D-011** row corrected in the same pass. `/plan` MUST NOT proceed as if this is a no-constitution-change feature.
- **Highest-risk requirement**: FR-004 + FR-014 (multi-value tokenization). A naive whole-string domain check does not merely under-validate — it **false-rejects conformant traffic** on 8 shipped FIX44 tags (`ExecInst(18)` et al.). Story 2 is P1, co-equal with Story 1, for this reason.
- **Second-highest risk**: FR-013. Enum-checking Logon (pre-`interpret_logon`) means a peer with an out-of-domain admin enum **cannot establish a session**. Accepted deliberately for QuickFIX parity — must be pinned by a direct test (SC-008) and recorded as an operator-facing B-row, never left as an emergent surprise.
- **Anti-regression floor**: FR-003's empty-store-accepts rule is the single guard against a reject-everything regression. It must be pinned directly, not inferred from a green suite. QuickFIX implements the same guard (`DataDictionary.h:493-496`).
- **Three ex-assumptions are now source-verified facts** against QuickFIX (empty-store-accepts; single-space tokenization with no empty-token skipping; out-of-domain → SessionRejectReason 5). They moved out of Assumptions into Clarifications → *Reference-engine confirmations*, and are pinned as FR-003 / FR-004+FR-014 / FR-006. SC-009 makes divergence from the reference engine a defect.
- **L-069-1 is NOT retired** by this feature (different path: typed codegen, not `table_view`). Restate it as still-open at close-out.
