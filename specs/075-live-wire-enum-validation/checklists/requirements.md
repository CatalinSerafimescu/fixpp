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

- [x] **No [NEEDS CLARIFICATION] markers remain** — **RESOLVED 2026-07-14** (Clarifications session, 5 questions): Q1 = all ten dictionaries (⇒ Constitution disposition (a), amend Article I §1 → v0.7); Q2 = rides the existing `validate_inbound_messages` flag, no sub-flag; Q3 = enum-check all inbound messages incl. Logon (QuickFIX parity); Q4 = dedupe duplicate codes, fail closed on a missing `enum` attribute (QuickFIX parity); Q5 = parity proven by a checked-in golden generated from a real-QuickFIX differential run. All five folded into FR-001/FR-010/FR-013/FR-014/FR-017/FR-018, the Edge Cases, and SC-009/SC-011 — not left only in the Clarifications section.
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
- **Anti-regression floor**: FR-003's empty-store-accepts rule is the single guard against a reject-everything regression. It must be pinned directly, not inferred from a green suite. QuickFIX implements the same guard (`DataDictionary.h:493-496`). It is also what keeps **FIXT11 working at all** (its `MsgType` has zero codes — FR-016).
- **FR-008 (empty value) is OPEN, and it is the cautionary tale for this whole spec.** It was first written as "empty → out-of-domain → reason 5, matching QuickFIX" — asserted parity, never verified the way FR-003/004/006 were. It is **wrong**: `m_checkFieldsHaveValues` defaults to `true`, so `checkHasValue` front-runs `checkValue` and QuickFIX yields reason **4**, which fixpp has no slot for. Routing empty values through the new enum check would *manufacture* a divergence. `/plan` must pick (a) exclude empty from the enum check (presumptive), (b) add a reason-4 slot, or (c) document the divergence. **The lesson generalizes: any remaining "matches QuickFIX" claim not backed by the FR-018 golden should be treated as unverified.**
- **Parity is proven by data, not by prose** (FR-018). SC-009 as originally worded could have been satisfied by a test encoding *my reading* of QuickFIX — self-referential, and it would enshrine any misreading of `isFieldValue`'s tokenizer. The golden table is generated from a real-QuickFIX differential run and checked in; CI must not depend on the gitignored `reference-engines/` tree.
- **Three ex-assumptions are now source-verified facts** against QuickFIX (empty-store-accepts; single-space tokenization with no empty-token skipping; out-of-domain → SessionRejectReason 5). They moved out of Assumptions into Clarifications → *Reference-engine confirmations*, and are pinned as FR-003 / FR-004+FR-014 / FR-006. SC-009 makes divergence from the reference engine a defect.
- **L-069-1 is NOT retired** by this feature (different path: typed codegen, not `table_view`). Restate it as still-open at close-out.

## Design risks to carry into `/plan` (not spec defects — landmines)

- **`table_view` ownership contract — a DECISION, not a detail.** `table_view` is today **fully self-owning**: `valid_`, `types_`, `group_members_` hold owned values, and `add_valid_tag` even copies the msg_type key (`valid_[std::string{msg_type}]`). It aliases **nothing** external, and `dictionary.hpp:193-205` documents that "the returned `table_view` **owns its tables**". Adding code sets forces a choice `/plan` MUST make explicitly: **(a)** store `string_view`s into the Dictionary's metadata pool — the *first* time `table_view` aliases external memory, coupling its lifetime to the Dictionary and **contradicting the stated owns-its-tables contract**; or **(b)** `table_view` owns copies of the code bytes in its own storage (a config-time allocation, which is off the hot path and therefore cheap). This is **distinct** from the two interning notes below (those are loader→Dictionary); this one is Dictionary→`table_view`, and it is the one that touches a documented contract. Note `Session` holds the validator (and thus the `table_view`) while `cfg_.dictionary` is a separate object — (a) would make validator lifetime silently depend on dictionary lifetime.
- **String-pool lifetime in `XmlLoader` (highest-probability subtle bug).** `EnumValueRef::value` / `::description` are `string_view`s that **alias the metadata-handle's name-string pool** (`dictionary.hpp:64-67`). `OrchestraLoader` interns into that pool. `XmlLoader` MUST route the `<value enum= description=>` strings through the **same intern path** — pointing them at the pugixml document's own strings yields views that dangle the moment the doc is destroyed at end of load. `XmlLoader` already interns field names (`Dictionary::field_name()` works on legacy dicts), so the path exists; confirm value/description reuse it rather than adding a second, shorter-lived pool. Cf. `[[feedback_typeerased_voidptr_staticcast_constrain_producer]]` and the aliasing-lifetime family of prior lessons.
- **`table_view` cannot currently express "multi-value"** (FR-005). Its per-tag `types_` map stores the collapsed 7-value `field_type`, into which `MultiStringValue`/`MultiCharValue` both fold as `String`. FR-004's tokenization is therefore **not satisfiable from the existing table** — the enum-domain table must carry the multi-value bit (or the finer `field_data_type`) per tag. Any plan that assumes `field_type_of()` suffices is wrong.
- **Hot-path allocation.** FR-007 forbids per-field allocation. The tokenizer must operate on `std::span<const std::byte>` / `string_view` slices of the wire buffer — no `std::string` materialization, no `std::vector<std::string>` of tokens. QuickFIX's own implementation allocates a `std::string` per token (`DataDictionary.h:269`); we match its **semantics**, explicitly **not** its allocation behavior.
- **Dictionary memory growth.** FIX50SP2 adds 668 enum-backed fields / 5565 codes to the metadata handle. Confirm at `/plan` that the arena/pool sizing absorbs this for every dictionary; an under-sized fixed arena would surface as a silent truncation or an alloc failure at load. Cf. `[[feedback_fixed_arena_over_reserve_silent_loss_larger_stl]]`.
