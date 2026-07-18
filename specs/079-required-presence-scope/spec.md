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

fixpp derives required-ness in more than one place: the runtime dictionary-driven validator and the generated typed `validate_<Msg>` validators. After this fix the two tiers must return the same accept/reject verdict for the same message, for every version that **has** a typed validator tier — **v44 / v50sp2 / vlatest**. **FIX42 has no typed builder/validator tier** (`emit_builders`/`emit_validator` are skipped at the codegen driver `tools/codegen/fixpp-codegen/main.cpp:132` `if (ir.ns != "v42")` — L-077-1/#196, because FIX 4.2 `NumInGroup=INT` yields 0 typed groups), so FIX42 gets **runtime-only** real-frame coverage (US1/US2) plus the census-vs-IR-**structure** leg (the `MessageIR` top-level list exists for v42), NOT two-tier verdict agreement. Where a typed tier exists, a message accepted by one tier and rejected by the other is a defect.

**Why this priority**: The candidate fix touches only the runtime path (loaders + validator). The generated typed validators derive required-ness through a third projection (codegen IR, `ir.cpp`). Whether the typed tier *also* needs a change is an **empirical scope question resolved at planning / Gate A** (see FR-007 and Assumptions), not a foregone conclusion — the issue asserts the typed validators already enforce group-member required-ness per entry, so the typed tier may already be correct for the group-scope case. This story exists to force that question to be answered with evidence and to prevent a half-restructure that leaves the tiers disagreeing.

**Independent Test**: For each affected message, run the same conforming frame (US1) and the same malformed frame (US2) through BOTH the runtime validator and the generated typed validator; assert identical verdicts. The gap, if any, localizes the codegen leg.

**Acceptance Scenarios**:

1. **Given** a sides-less `TradeCaptureReport`, **When** validated by both the runtime and the generated typed `validate_<Msg>` validator, **Then** both accept.
2. **Given** any affected message, **When** validated by both tiers, **Then** their accept/reject verdicts match.

---

### User Story 4 - Required-set derivation matches an independent oracle across every dictionary (Priority: P1)

The correctness of the fix cannot be established by a handful of examples. The message-level required set that the validator requires at the top level must equal, for every message in every vendored dictionary, an independently-derived expected set — and that independent derivation must be cross-checked against the QuickFIX reference engine so it encodes the AND-rule correctly rather than fixpp's own reading of it.

**Why this priority**: This is the leg that distinguishes "correct" from "passes my three examples". It is the tightest constraint and the primary guard against both over- and under-correction repo-wide. It is P1 alongside US1 because without it the fix is unproven.

**Independent Test**: A non-circular census — an independent raw-XML walker computes the expected message-level required set per message as **full-ancestor-chain component-AND composition**: a field is message-level-required iff its own `required='Y'` AND every enclosing componentRef usage on the path to the message root is `required='Y'`, AND it is not enclosed by any group — **EXCEPT StandardHeader/StandardTrailer fields (tags 8/9/34/35/49/52/56/10), which are treated as structurally-always-required and are NEVER dropped even when a message references the header/trailer componentRef with default (optional) presence** (parity-tolerance note: QuickFIX `DataDictionary.cpp:510/:522` ANDs only the *immediate* enclosing component, not the full ancestor chain; the vendored dicts contain 0 nested-optional-component sites, so this divergence never bites). It is compared for **exact set equality** against `table_view::required_fields(msg_type)` — the exact set the runtime validator's Step-2 required-field scan iterates (Step-2's literal input; `dict_` is a `table_view` held by value in the validator, so this accessor IS the probe surface, not a sibling projection). Step-2 skips exactly tags {8,9,10} (framer-guaranteed), so the census compares the pre-skip `required_fields()` span on both sides — which also verifies 8/9/10 are present, so a header/trailer carve-out regression is caught. This holds across all 10 dictionaries, and against the codegen **IR data-structure** top-level required list (the `MessageIR` projection — available for every version including FIX42, whose emitted validator tier is absent per L-077-1/#196). The expected side is additionally reconciled against quickfix-cpp 1.16.0 (`DataDictionary::isRequiredField`, already vendored under `reference-engines/`) on the affected messages.

**Acceptance Scenarios**:

1. **Given** all 10 vendored dictionaries, **When** the independent walker's expected top-level required set is compared to the shipped runtime required set per message, **Then** they are exactly set-equal for every message (no extra, no missing).
2. **Given** the affected messages (PositionReport, TradeCaptureReport, and the ~19 FIX50SP2 / FIX44 / FIX42 messages the issue names), **When** each dictionary's QuickFIX per-message **required set** (captured via `DataDictionary::isRequiredField`, which encodes the component AND-rule) is compared to the census oracle, **Then** the sets are exactly equal (Contract 2 — the QuickFIX gate is required-**set** parity, NOT a QuickFIX frame-validation harness; QuickFIX covers the 9 QuickFIX-schema dicts including FIX42, no vlatest/Orchestra row). The conforming-accept / malformed-reject frame verdicts stay as Contract-4 behavioral corroboration on fixpp's own runtime validator.

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
- **FR-004**: The runtime validator MUST check, for each instance of a present repeating group, that the instance carries every member that is `required='Y'` inside that group, and reject the message (surfacing the offending tag) when an instance omits one. Enforcement MUST be **universal** — it applies for **every** per-group required-member count, fail-closed (Article XV), never silently skipping the check for large groups. The design is the **dynamic-width** required-member check (the validator already runs a linear `req_bit` scan; only the fixed-width mask is 64-bounded — that mask MUST be widened to a dynamic width at /implement, replacing the candidate's ≤64 fail-open skip). A census MUST additionally assert the shipped **maximum** per-group required-member count across all 10 dictionaries so the "groups carry only 0–3 required members" assumption cannot silently rot.
- **FR-005**: The message-level required-set derivation MUST leave component-usage handling unchanged — a field required inside a component keeps its existing message-level treatment (the vendored dicts contain no optional-component-with-required-field configuration at message level, so there is nothing to correct here; this feature narrows only *group* scope). The header/trailer required fields MUST NOT be dropped.
- **FR-006**: The change MUST be confined to validation/dictionary derivation — no C-ABI change (frozen 1.5.0), and the read/reify tier and its goldens MUST be unaffected.
- **FR-007**: The generated typed `validate_<Msg>` validators MUST agree with the runtime validator on accept/reject for every affected message **in the versions that have a typed tier (v44 / v50sp2 / vlatest)**; FIX42 has no typed validator (L-077-1/#196) and is covered runtime-only + census-vs-IR-structure. No codegen change is expected — the generated top-level required check already excludes group members (`group_no_tag==0` filter) and Phase 0 found no optional-component over-require sites — so this is a *verification* requirement, not a change: a two-tier agreement test MUST confirm the verdicts match, and the census (FR-009) MUST cover the codegen IR projection so any latent over-require would surface as RED.
- **FR-008**: **All** read/reify goldens MUST stay byte-identical (v44 / v42 / vt11 **and** v50sp2 / vlatest). The candidate changes only the loader `required_out` accumulation, not `FieldRef.rule` or the IR inputs the read/reify tier consumes, so no read golden may change; there is no census-justified golden-delta allowance.
- **FR-009**: A non-circular completeness census MUST assert exact set-equality between an independently-derived expected top-level required set (full-ancestor-chain component-AND + StandardHeader/StandardTrailer carve-out; see US4) and `table_view::required_fields(msg_type)` — the validator's Step-2 pre-skip input (Step-2 skips exactly tags {8,9,10}, so both sides compare the pre-skip span, which also verifies 8/9/10 are present) — per message, across all 10 dictionaries — covering both the runtime derivation and the codegen **IR data-structure** projection (the `MessageIR` top-level required list, which exists for every version including FIX42; the emitted validator tier is NOT the census surface). The census MUST be a **durable checked-in** subtest/tool (not a planning-time throwaway) that classifies the StandardHeader/StandardTrailer hits explicitly and reports the measured 0 genuine optional-component over-require sites, failing on synthetic optional-component insertion.
- **FR-009a**: The census MUST additionally assert per-group required-member set-equality in **two distinct legs** (the bare and context stores have different contracts, so requiring both to equal every per-context oracle is unsatisfiable — FIX44 tag 295 NoQuoteEntries is reused with divergent direct-required members, `{}` vs `{299}`, and the bare store is a single value keyed on `no_tag` alone): **(1)** the shipped **context** store `group_required_members(msg_type, parent_path, no_tag)` == the independent walker's **per-context** required set, exact set-equality both directions, for **every** `(msg_type, parent_path, no_tag)` in all 10 dictionaries — this is the PRIMARY pin, it drives FR-004 per-instance rejection; and **(2)** the shipped **bare** store `group_required_members(no_tag)` == the **global first-seen** variant for `no_tag` (reached by the validator only on a context miss) — the bare store is a fallback, NOT required to equal every per-context oracle. Both legs cross-checked against QuickFIX's per-group required members (`DataDictionary.cpp:560/:570`) where available. This is a distinct census leg from the message-level set-equality (FR-009); a wrong or incomplete per-context store, or an omitted per-group required member, would otherwise ship undetected (FR-004 must not rest on SC-002's example frames alone).
- **FR-010**: The independent oracle MUST be cross-checked against quickfix-cpp 1.16.0 by **required-set parity** — QuickFIX's per-message required set (via `DataDictionary::isRequiredField`, which encodes the component AND-rule) MUST equal the census oracle's set (Contract 2), confirming the oracle encodes the AND-rule faithfully. This is a set-parity gate, NOT a QuickFIX frame-validation harness; the affected-message frame accept/reject verdicts remain fixpp-side Contract-4 corroboration.

### Key Entities

- **Message-level required set**: the set of tags the validator requires at the top level of a message. Contaminated today by group-scoped requireds; the unit of the census's set-equality assertion.
- **Per-group required-member set**: the members that are `required='Y'` inside a given repeating group; queryable per group, checked per instance.
- **Component usage (AND-rule)**: a field's effective required-ness = field `required='Y'` AND enclosing component-usage `required='Y'`; unchanged by this feature.
- **Independent required-set oracle**: a raw-XML walker (not the loader code path) that computes the expected top-level required set, reconciled against QuickFIX.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A conforming message that omits only optional groups is accepted by strict validation. Verified by **runtime** real-frame regressions on the named messages (PositionReport AP, TradeCaptureReport AE) plus one representative conforming frame per affected version (FIX44 / FIX50SP2 / FIX42 — FIX42 is runtime-only, no typed tier per US3/SC-004); repo-wide coverage of the full affected set is carried by the census (SC-003).
- **SC-002**: A malformed group instance (an instance omitting an intra-group required member) is rejected with the offending tag surfaced, for at least one representative message per affected version. The per-instance check enforces for **every** required-member count (dynamic-width, no ≤64 fail-open skip — FR-004), and a census asserts the shipped **maximum** per-group required-member count across all 10 dicts (so the small-count assumption cannot rot).
- **SC-003**: The non-circular census reports exact set-equality (0 extra, 0 missing tags) for the message-level required set of every message in all 10 dictionaries, for both the runtime derivation and the codegen **IR data-structure** projection (the `MessageIR` top-level list — present for every version including FIX42; the emitted validator tier is not the census surface). It is proven to fail (RED) by **two** independent injections: (a) reverting the `in_group` gate (group-member leak restored), and (b) inserting a **synthetic optional-component-required field** — the full-component-AND walker drops it while the loader keeps it, so the census goes RED even though the real corpus has **0** genuine optional-component sites. This makes the "scope narrowing does not narrow verification" safety-net claim genuinely load-bearing rather than vacuous.
- **SC-003a**: The per-group required-member census (FR-009a) reports exact set-equality, both directions, in two legs: **(1)** the shipped **context** store `group_required_members(msg_type, parent_path, no_tag)` == the independent walker's per-context required set for every `(msg_type, parent_path, no_tag)` in all 10 dicts (the PRIMARY pin driving FR-004); and **(2)** the shipped **bare** store `group_required_members(no_tag)` == its global first-seen fallback value (NOT required to equal every context — reused tags like FIX44 295 diverge per context). Cross-checked against QuickFIX per-group required members where available — proven RED by an injected/omitted per-context required member.
- **SC-004**: The runtime validator and the generated typed validator return identical accept/reject verdicts for every affected message (both conforming and malformed frames), **for the versions that have a typed tier — v44 / v50sp2 / vlatest only**. FIX42 is excluded: it has no typed `validate_<Msg>` (L-077-1/#196; `main.cpp:132`), so it is covered runtime-only (SC-001/SC-002) plus census-vs-IR-structure (SC-003).
- **SC-005**: quickfix-cpp 1.16.0 agrees with the census oracle on the affected messages by **required-set parity** — QuickFIX's per-message required set (`DataDictionary::isRequiredField`) equals the oracle's set (Contract 2). No frame is validated through QuickFIX; the conforming-accept / malformed-reject frame verdicts are corroborated on fixpp's **own runtime** validator (Contract 4).
- **SC-006**: **All** read/reify goldens are byte-identical before and after — v44 / v42 / vt11 **and** v50sp2 / vlatest. The candidate touches only loader `required_out`, not `FieldRef.rule` or the IR inputs the read/reify tier consumes, so no golden may change (no delta allowance).
- **SC-007**: The control message (NewOrderSingle without Symbol) is unaffected — its message-level required set never contained Symbol and still does not (no over-correction).
- **SC-008**: The two-tier agreement test confirms the runtime and generated typed validators return identical verdicts for the affected messages **that have a typed tier (v44 / v50sp2 / vlatest; not FIX42 — SC-004)** with **no codegen change** — corroborating the Phase-0 finding that the codegen tier never over-required (group members excluded by `group_no_tag==0`; 0 optional-component sites). If this test unexpectedly fails, it localizes a codegen leg that Phase 0 missed.

## Assumptions

- The candidate on `177a0535` is a **starting hypothesis**; the spec's verification (census, parity, tier-agreement) is authoritative and may require changing the candidate.
- The codegen IR leg is **out of scope** — Phase 0 (static raw-XML enumeration across all 10 dicts) found 0 optional-component over-require sites and the codegen already excludes group members from the top-level required check. The two-tier agreement test guards this conclusion; if it fails, a codegen leg is added.
- quickfix-cpp 1.16.0 is available under `reference-engines/` (vendored, gitignored) for the parity leg, consistent with the 069/075 parity precedent (derive a golden, link no QuickFIX in CI).
- Strict inbound validation (`validate_inbound_messages`) remains opt-in and default-off; this feature changes only what that path accepts/rejects, not its default.
- The per-instance group required check reuses 072's nesting-aware `consume_group` structure rather than introducing a new group walker.

## Normative References

Per Article VI §5, the exact coverage-index and behaviour-record entries that inform this spec:

- **`[FIX50SP2 §3] Message validator — required fields, type conformance, enum values, group structure`** — `spec/coverage-index.md:189` (catalogue row W-014). The `required fields` and `group structure` clauses of this row are the behaviour this feature corrects: the runtime validator must require exactly the top-level required fields (no group-scoped leakage) and must enforce per-instance group required members.
- **`[FIX50SP2 §3.2] Repeating groups (NoXxx delimiter, ordered field list, nested groups)`** — `spec/coverage-index.md:184` (catalogue rows W-006 / W-007 / D-010; 063/072 group-membership + nesting precedent). Governs the per-instance required-member semantics of FR-004 and the nesting-aware `consume_group` reuse. The componentRef AND-rule the census oracle (US4/FR-009) encodes is the required-field clause of the §3 validator row above (W-014).
- **`spec/behaviors-and-limitations.md` L-067-1** — the adjacent *under*-require non-bug (a field required only inside a *required* component being treated Optional); this feature is the opposite (*over*-require) direction and must not touch the L-067-1 path.
- **`spec/behaviors-and-limitations.md` L-077-1** — no typed `build_<Msg>`/`validate_<Msg>` for `fixpp::v42` (descoped, issue #196); the basis for the FIX42 runtime-only tier carve-out (US3 / SC-004 / SC-008).
