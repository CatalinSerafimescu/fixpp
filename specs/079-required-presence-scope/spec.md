# Feature Specification: Runtime validator required-presence scoping

**Feature Branch**: `079-required-presence-scope`

**Created**: 2026-07-18

**Status**: Draft

**Issue**: fixpp#201

**Input**: The runtime dictionary-driven validator's message-level required-field set is contaminated with fields that are `required='Y'` only *inside an optional repeating group*. Under strict inbound validation (`validate_inbound_messages=true`) this false-rejects conforming messages that legitimately omit the optional group. A second defect on the same path: repeating-group instances are not checked for their own required members, so a malformed group instance is wrongly accepted.

## Context & provenance

A prior investigation (fable-assessments 6.1) independently **reproduced** this against the shipped `libfixpp_dictionary.a` on the real vendored dictionaries. A candidate runtime-only fix exists on this branch (commit `177a0535`, authored by a prior agent): loader scoping + a per-instance group required check + an additive per-group required-member store. **This spec treats that candidate as a hypothesis to be independently verified, not a diff to ratify** — the required-set derivation is re-derived independently by the census (US4) and cross-checked against QuickFIX (also US4), so the gates test the fix rather than enshrine it.

This is distinct from **L-067-1** (a field required only inside a *required component* being *under*-required — QuickFIX AND-rule parity, a non-bug, not recoverable from any vendored dictionary — reworded in `spec/behaviors-and-limitations.md`). This feature is the real, reproduced *presence-scope* bug adjacent to it: the *over*-require direction, where a field required only inside an optional container (group **or** component) wrongly enters the message-level required set.

## Clarifications

### Session 2026-07-18

- Q: Is the fix scoped to optional-group leakage only, or also optional-component over-require (issue fix item 3, componentRef AND)? → A (initial): **Both**, premised on the issue's "~6 codegen over-require sites" being real. **Superseded by the Phase-0 finding below.**
- Q: How broad should the behavioral real-frame accept/reject regressions be, given the census already proves exhaustive set-equality? → A: **Named + one-per-version** — real frames for the named messages (PositionReport AP, TradeCaptureReport AE) plus one representative conforming (and, where applicable, one malformed) frame per affected version (FIX44 / FIX50SP2 / FIX42); the census carries repo-wide exhaustiveness.
- Q (Phase-0 verification, planning): Do optional-component over-require sites actually exist in the vendored dictionaries? → A: **No — the component leg is VACUOUS (L-067-1 redux).** A static raw-XML enumeration across all 10 dicts found **0** genuine optional-component over-require sites: the 9 QuickFIX-schema dicts have none (FIX50SP2's only two components carrying a directly-required field — `MDStatisticParameters`, `PostTradePayment` — are never used with optional presence at message level); the Orchestra/vlatest dict's 11 apparent hits are all StandardHeader/StandardTrailer fields (8/9/34/35/49/52/56/10) flagged only because those messages reference the header/trailer `componentRef` with default (optional) presence — they are structurally always-required and must NOT be dropped. **Scope reverts to group-only, runtime-only** (the candidate's fix). The codegen tier needs no change: `emit_builders` already filters top-level items by `group_no_tag==0`, so group members never enter the top-level required check. The exact-set census + two-tier agreement are retained as the **safety net** — they would go RED if any optional-container over-require existed, so the scope narrowing does not blind the verification.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Conforming inbound message that omits an optional group is accepted (Priority: P1)

An operator runs an inbound session with strict validation on (`validate_inbound_messages=true`). A counterparty sends a well-formed message that legitimately omits an optional repeating group — e.g. a FIX44 `PositionReport` (AP) with no `NoUnderlyings`, or a FIX50SP2 `TradeCaptureReport` (AE) with no `NoSides`. The engine must accept it.

**Why this priority**: This is the shipped correctness defect — the strict path false-rejects conforming production traffic. It is opt-in and default-off, but prominent since the live enum-validation work, and a per-release QuickFIX interop gate would eventually trip on the `PositionReport` case. Fixing it is the reason the feature exists.

**Independent Test**: Load each real vendored dictionary; construct/parse a conforming instance of each affected message that omits the optional group; assert the runtime validator returns *accept* (no `wire_required_field_missing`). Fully testable with the dictionary + validator alone; no session or transport needed.

**Acceptance Scenarios**:

1. **Given** the FIX44 dictionary and strict validation, **When** a `PositionReport` (AP) with no `NoUnderlyings` group is validated, **Then** it is accepted (previously rejected with `wire_required_field_missing(732)`).
2. **Given** the FIX50SP2 dictionary and strict validation, **When** a `TradeCaptureReport` (AE) with no `NoSides` group is validated, **Then** it is accepted (previously rejected on tag 54).
3. **Given** any dictionary, **When** a message that IS genuinely missing a *top-level* required field is validated, **Then** it is still rejected (no over-correction — the fix must not stop rejecting real omissions).
4. **Control — Given** the FIX44 dictionary, **When** a `NewOrderSingle` (D) without `Symbol`(55) is validated, **Then** the message-level required set does NOT contain 55 (QuickFIX AND-rule parity; confirms the top-level derivation stays correct).

---

### User Story 2 - Malformed repeating-group instance is rejected (Priority: P2)

A counterparty sends a message whose repeating group is present but one instance omits a member that is `required='Y'` inside the group. QuickFIX rejects this; fixpp strict validation currently accepts it. The engine must reject the malformed instance.

**Why this priority**: A genuine "malformed message wrongly accepted" hole — the inverse failure mode of US1. Lower than P1 because it under-rejects rather than breaking conforming traffic, but it is a real strictness gap on the same path and closing it is what makes the group-scope handling *complete* rather than merely permissive.

**Independent Test**: Construct a group with ≥2 instances where a later instance omits a required member; assert the runtime validator rejects with the offending tag reported. Testable against the dictionary + validator directly.

**Acceptance Scenarios**:

1. **Given** a dictionary and a message with a repeating group whose second instance omits an intra-group `required='Y'` member, **When** validated strictly, **Then** it is rejected and the missing tag is surfaced.
2. **Given** a message whose every group instance carries all its required members, **When** validated strictly, **Then** it is accepted (no false reject introduced by the per-instance check).

---

### User Story 3 - The two validation tiers agree on the same message (Priority: P2)

fixpp derives required-ness in more than one place: the runtime dictionary-driven validator and the generated typed `validate_<Msg>` validators. After this fix the two tiers must return the same accept/reject verdict for the same message, for every app-bearing version — otherwise a message accepted by one tier is rejected by the other.

**Why this priority**: The candidate fix touches only the runtime path (loaders + validator). The generated typed validators derive required-ness through a third projection (codegen IR, `ir.cpp`). Whether the typed tier *also* needs a change is an **empirical scope question resolved at planning / Gate A** (see FR-007 and Assumptions), not a foregone conclusion — the issue asserts the typed validators already enforce group-member required-ness per entry, so the typed tier may already be correct for the group-scope case. This story exists to force that question to be answered with evidence and to prevent a half-restructure that leaves the tiers disagreeing.

**Independent Test**: For each affected message, run the same conforming frame (US1) and the same malformed frame (US2) through BOTH the runtime validator and the generated typed validator; assert identical verdicts. The gap, if any, localizes the codegen leg.

**Acceptance Scenarios**:

1. **Given** a sides-less `TradeCaptureReport`, **When** validated by both the runtime and the generated typed `validate_<Msg>` validator, **Then** both accept.
2. **Given** any affected message, **When** validated by both tiers, **Then** their accept/reject verdicts match.

---

### User Story 4 - Required-set derivation matches an independent oracle across every dictionary (Priority: P1)

The correctness of the fix cannot be established by a handful of examples. The message-level required set that the validator actually probes must equal, for every message in every vendored dictionary, an independently-derived expected set — and that independent derivation must be cross-checked against the QuickFIX reference engine so it encodes the AND-rule correctly rather than fixpp's own reading of it.

**Why this priority**: This is the leg that distinguishes "correct" from "passes my three examples". It is the tightest constraint and the primary guard against both over- and under-correction repo-wide. It is P1 alongside US1 because without it the fix is unproven.

**Independent Test**: A non-circular census — an independent raw-XML walker computes the expected top-level required set per message (QuickFIX AND-rule: field `required='Y'` AND every enclosing component-usage on the path to the root `required='Y'`, with all group members excluded from the message level); it is compared for **exact set equality** against the *shipped* required set the runtime validator probes, across all 10 dictionaries, and against the codegen IR projection. The expected side is additionally reconciled against quickfix-cpp 1.16.0 (already vendored under `reference-engines/`) on the affected messages.

**Acceptance Scenarios**:

1. **Given** all 10 vendored dictionaries, **When** the independent walker's expected top-level required set is compared to the shipped runtime required set per message, **Then** they are exactly set-equal for every message (no extra, no missing).
2. **Given** the affected messages (PositionReport, TradeCaptureReport, and the ~19 FIX50SP2 / FIX44 / FIX42 messages the issue names), **When** the same frames run through quickfix-cpp 1.16.0, **Then** the accept/reject verdicts match fixpp's.

---

### Edge Cases

- A field that is `required='Y'` both at top level AND inside an optional group (present in two roles) — must remain top-level-required.
- Nested groups: a required member inside a group that is itself inside another group — per-instance required-ness applies at the correct nesting level (reuses 072's nesting-aware `consume_group`).
- A required field inside a component (not a group) — keeps its existing message-level treatment; the fix must NOT drop these (Phase 0: no optional-component-with-required-field configuration exists at message level in the vendored dicts).
- StandardHeader / StandardTrailer required fields (BeginString, MsgType, CheckSum, …) — MUST stay required even where the Orchestra dict references the header/trailer `componentRef` with default (optional) presence; the group-scope fix must not touch this path.
- Empty group (count present, zero instances) vs absent group — both are legitimate omissions of the group members.
- Legacy dictionaries (v42/v44/vt11) whose read goldens must stay byte-identical — the required-set change must not perturb the read/reify tier.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The message-level required-field set derived by the runtime dictionary loaders MUST contain only fields required at the top level of the message — a field that is `required='Y'` solely inside a repeating group MUST NOT appear in it. (Optional-component scope was investigated at Phase 0 and found vacuous across all 10 dicts, so no component-usage change is made; see Clarifications.)
- **FR-002**: The loaders MUST preserve each group's required-member information in a queryable form so the validator can enforce it per group instance (the message-level set losing the group members must not lose the data entirely).
- **FR-003**: The runtime validator's required-field scan MUST remain scoped to top-level fields, so a legitimately-absent optional group no longer fails the message.
- **FR-004**: The runtime validator MUST check, for each instance of a present repeating group, that the instance carries every member that is `required='Y'` inside that group, and reject the message (surfacing the offending tag) when an instance omits one.
- **FR-005**: The message-level required-set derivation MUST leave component-usage handling unchanged — a field required inside a component keeps its existing message-level treatment (the vendored dicts contain no optional-component-with-required-field configuration at message level, so there is nothing to correct here; this feature narrows only *group* scope). The header/trailer required fields MUST NOT be dropped.
- **FR-006**: The change MUST be confined to validation/dictionary derivation — no C-ABI change (frozen 1.5.0), and the read/reify tier and its goldens MUST be unaffected.
- **FR-007**: The generated typed `validate_<Msg>` validators MUST agree with the runtime validator on accept/reject for every affected message. No codegen change is expected — the generated top-level required check already excludes group members (`group_no_tag==0` filter) and Phase 0 found no optional-component over-require sites — so this is a *verification* requirement, not a change: a two-tier agreement test MUST confirm the verdicts match, and the census (FR-009) MUST cover the codegen IR projection so any latent over-require would surface as RED.
- **FR-008**: Legacy read goldens (v44 / v42 / vt11) MUST stay byte-identical; any golden change is confined to the affected v50sp2 / vlatest sites and MUST be justified against the census.
- **FR-009**: A non-circular completeness census MUST assert exact set-equality between an independently-derived expected top-level required set and the shipped required set the validator probes, per message, across all 10 dictionaries — covering both the runtime derivation and the codegen IR projection.
- **FR-010**: The affected-message verdicts MUST be cross-checked against quickfix-cpp 1.16.0 to confirm the independent oracle encodes the AND-rule faithfully.

### Key Entities

- **Message-level required set**: the set of tags the validator requires at the top level of a message. Contaminated today by group-scoped requireds; the unit of the census's set-equality assertion.
- **Per-group required-member set**: the members that are `required='Y'` inside a given repeating group; queryable per group, checked per instance.
- **Component usage (AND-rule)**: a field's effective required-ness = field `required='Y'` AND enclosing component-usage `required='Y'`; unchanged by this feature.
- **Independent required-set oracle**: a raw-XML walker (not the loader code path) that computes the expected top-level required set, reconciled against QuickFIX.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A conforming message that omits only optional groups is accepted by strict validation. Verified by real-frame regressions on the named messages (PositionReport AP, TradeCaptureReport AE) plus one representative conforming frame per affected version (FIX44 / FIX50SP2 / FIX42); repo-wide coverage of the full affected set is carried by the census (SC-003).
- **SC-002**: A malformed group instance (an instance omitting an intra-group required member) is rejected with the offending tag surfaced, for at least one representative message per affected version.
- **SC-003**: The non-circular census reports exact set-equality (0 extra, 0 missing tags) for the message-level required set of every message in all 10 dictionaries, for both the runtime derivation and the codegen IR projection — and is proven to fail (RED) when a group-scoped required is deliberately reintroduced. (Because the census is scope-agnostic — it compares full required *sets* — it would equally catch an optional-component over-require, serving as the safety net for the narrowed scope.)
- **SC-004**: The runtime validator and the generated typed validator return identical accept/reject verdicts for every affected message (both conforming and malformed frames).
- **SC-005**: quickfix-cpp 1.16.0 agrees with fixpp on the affected messages (accept the conforming frames, reject the malformed ones).
- **SC-006**: The v44 / v42 / vt11 read goldens are byte-identical before and after; any v50sp2 / vlatest golden delta is limited to census-justified sites.
- **SC-007**: The control message (NewOrderSingle without Symbol) is unaffected — its message-level required set never contained Symbol and still does not (no over-correction).
- **SC-008**: The two-tier agreement test confirms the runtime and generated typed validators return identical verdicts for the affected messages with **no codegen change** — corroborating the Phase-0 finding that the codegen tier never over-required (group members excluded by `group_no_tag==0`; 0 optional-component sites). If this test unexpectedly fails, it localizes a codegen leg that Phase 0 missed.

## Assumptions

- The candidate on `177a0535` is a **starting hypothesis**; the spec's verification (census, parity, tier-agreement) is authoritative and may require changing the candidate.
- The codegen IR leg is **out of scope** — Phase 0 (static raw-XML enumeration across all 10 dicts) found 0 optional-component over-require sites and the codegen already excludes group members from the top-level required check. The two-tier agreement test guards this conclusion; if it fails, a codegen leg is added.
- quickfix-cpp 1.16.0 is available under `reference-engines/` (vendored, gitignored) for the parity leg, consistent with the 069/075 parity precedent (derive a golden, link no QuickFIX in CI).
- Strict inbound validation (`validate_inbound_messages`) remains opt-in and default-off; this feature changes only what that path accepts/rejects, not its default.
- The per-instance group required check reuses 072's nesting-aware `consume_group` structure rather than introducing a new group walker.
