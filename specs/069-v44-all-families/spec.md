# Feature Specification: v44 all-families typed codegen coverage

**Feature Branch**: `069-v44-all-families`
**Created**: 2026-07-11
**Status**: Draft
**Input**: User description: "v44 all-families typed codegen coverage — extend the existing 067 codegen writer-emitter from the 33 OFFICIAL FIX44 MsgTypes to EVERY application message in the FIX44 dictionary (measured 33 → 86), generating the same build_/validate_/Args/reify artifacts per message, v44 namespace only, application messages only, verified by differential round-trip at breadth."

## Context & Motivation

fixpp already parses, validates, and serializes **every** FIX message across all nine legacy versions via its runtime-XML layer (dictionary-driven, `view.get(tag)` + `wire::body_builder`). What is partial is the **typed compile-time convenience layer**: feature `067-codegen-writer-emitter` generates typed `build_<Msg>()` / `validate_<Msg>()` builders for only the **33 OFFICIAL** FIX44 application MsgTypes. The remaining **50 in-scope** application messages the FIX44 dictionary defines (Collateral, Position, TradeCapture, Confirmation, SecurityList, TradingSessionList, MarketDefinition, List-handling, Registration, and more) have no typed builder — a developer must drop to the generic runtime path for them.

This feature closes that gap on the **one representative v44 namespace**: it widens the existing writer-emitter to generate the same typed artifacts for **all in-scope** FIX44 application messages (the 50 not-yet-typed families), so the legacy typed-ergonomics chapter is complete before the successor Orchestra / FIX-Latest direction. A measure-first spike (`../remaining-work/v44-all-families-measure-spike.md`) confirmed emission is mechanical from the existing dictionary model, with a bounded build-time cost and no architectural blocker.

> **Count basis (authoritative).** The FIX44 dictionary the codegen consumes (`dictionaries/FIX44.xml`) defines **93** `<message>` entries = **85 `msgcat='app'`** + **8 `msgcat='admin'`** (the 7 session types Heartbeat/TestRequest/ResendRequest/Reject/SequenceReset/Logout/Logon **+ XMLnonFIX `35=n`**, which is `admin` and is therefore auto-excluded, never emitted). Selection keys on `msgcat` (not a msgtype allowlist). Of the N-002/N-003 exclusion set `{BE, BF, BW, BX, BY}`, only **BE and BF exist in FIX44** (BW/BX/BY are FIX 5.0 messages, absent here — their exclusion is a harmless no-op). **In-scope = 85 app − 2 (BE, BF) = 83** (33 already-typed OFFICIAL + **50 new**). The spike's earlier "86" figure was **msgtype-based** (93 minus the 7 session types, still counting XMLnonFIX + BE/BF); its build-cost measurements are retained, but the authoritative msgcat-based in-scope count is **83**.

## Clarifications

### Session 2026-07-11

- Q: Default build coverage for v44 typed builders (CMake selection default)? → A: **Full-family by default** — the stock build generates all in-scope application builders; a cost-sensitive build can opt DOWN to OFFICIAL-only. Best serves "close the legacy chapter" (families present in the default artifact).
- Q: Scope of enum value-domain validation in generated validators? → A: **Out of scope** — generated validators enforce required-field presence + type conformance only (matching 067); enum value-domain stays unbacked and is recorded as an explicit limitation.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Typed build/validate for any FIX44 application message (Priority: P1)

A developer integrating fixpp wants to construct and validate any FIX44 application message — not only the 33 OFFICIAL ones — using the typed builder API instead of hand-assembling tag/value pairs on the generic runtime path. Today, if they need TradeCaptureReport, PositionReport, CollateralInquiry, SecurityList, or any of the other 50 in-scope application messages, no typed builder exists.

**Why this priority**: This is the feature's core value — completing typed coverage of the FIX44 application message set. Without it, the legacy typed layer stays partial and the "close the chapter" goal is unmet.

**Independent Test**: Pick any previously-uncovered family (e.g. TradeCaptureReport `35=AE`), populate its typed argument struct, invoke its generated builder, and assert the produced wire bytes are well-formed and carry the seeded fields; invoke its generated validator and confirm required-field enforcement.

**Acceptance Scenarios**:

1. **Given** the FIX44 dictionary defines an in-scope application message M (any of the **83 in-scope = 85 `msgcat='app'` minus BE (UserRequest) and BF (UserResponse)**; the 8 `msgcat='admin'` messages — the 7 session types + XMLnonFIX `35=n` — are auto-excluded, and BW/BX/BY are absent from FIX44), **When** the codegen runs with full-family coverage enabled, **Then** a typed builder, validator, argument struct, and typed read-back are generated for M.
2. **Given** a generated builder for a previously-uncovered family, **When** a developer supplies typed field values and calls it, **Then** it emits conformant FIX44 wire bytes for that MsgType.
3. **Given** a message with a required field omitted, **When** the generated validator runs, **Then** it fails closed and reports the missing required field — identical in behavior to the 33 OFFICIAL messages.
4. **Given** the 33 previously-covered OFFICIAL messages, **When** full-family coverage is generated, **Then** their generated output is byte-for-byte unchanged (no regression to existing builders).

---

### User Story 2 - Every generated builder is verified, not just emitted (Priority: P1)

A maintainer must trust that 83 mechanically-generated builders are correct without hand-writing 83 golden fixtures — and without a mass positive-fixture push that would silently enshrine any emitter bug.

**Why this priority**: Breadth without verification is worse than no breadth — it ships latent bugs behind a "typed" label. The verification methodology is what makes wide coverage trustworthy, so it is co-equal P1 with the coverage itself.

**Independent Test**: Run the differential harness — for every generated application message, seed its argument struct, build wire bytes, parse them back through the independent runtime-XML path, and assert each seeded field reads back with its exact value. The harness passes for all covered messages or names the divergent ones.

**Acceptance Scenarios**:

1. **Given** every generated application-message builder, **When** the differential round-trip harness runs, **Then** each builder's output parses back through the runtime path with all seeded fields matching exactly.
2. **Given** a small exemplar-per-family subset, **When** their output is compared to externally-authored (reference-engine) golden wire bytes, **Then** the bytes match — anchoring the round-trip against an external oracle so builder and parser cannot be co-wrong.
3. **Given** the emitter's completeness pin, **When** codegen runs, **Then** the pin asserts the exact set of emitted messages equals the intended set (no silent drop, no silent extra).

---

### User Story 3 - Build cost of wide coverage is bounded and selectable (Priority: P2)

An operator building fixpp — especially in the resource-constrained CI matrix (multiple sanitizer presets on limited hardware) — must not be forced to pay the full compile-time cost of all 83 in-scope builders when they only need the OFFICIAL set, while CI must still generate and verify the full set.

**Why this priority**: The spike measured a +19 s/TU compile cost (2.57×) for the full header. The default is full-family (families present by default — see Clarifications), so a cost-sensitive build needs an opt-DOWN path to OFFICIAL-only to bound its compile cost. A selection control provides that without foreclosing full coverage. It is P2 because it constrains *how* the P1 value is delivered, not *whether*.

**Independent Test**: Configure the build with each selection value; confirm the generated coverage matches the selection (full-family default vs OFFICIAL-only opt-down), and that a build opting down to OFFICIAL-only regenerates exactly today's 33-builder output.

**Acceptance Scenarios**:

1. **Given** the default build (no override), **When** codegen runs, **Then** all 83 in-scope application-message builders are generated.
2. **Given** the coverage selection control set to OFFICIAL-only, **When** codegen runs, **Then** only the 33 builders are generated (today's behavior, today's cost).
3. **Given** the project CI, **When** it runs, **Then** at least one preset generates and verifies the full-family set so the wide coverage is continuously proven.

---

### Edge Cases

- **Session/admin MsgTypes** (Heartbeat, TestRequest, ResendRequest, Reject, SequenceReset, Logout, Logon): excluded — these are owned by the session layer, not the application writer-emitter, and must NOT receive generated application builders.
- **Session-FSM application messages** N-002/N-003 (UserRequest/UserResponse, ApplicationMessageRequest family): excluded — they require session-FSM dispatch (a different work class) and remain the separate v1.0-tagging gate.
- **Messages with no required fields** (e.g. some request messages): the validator must accept an all-optional message without spuriously failing.
- **Deeply-nested / group-heavy families** (e.g. TradeCaptureReport, MassQuote-class): the emitter must produce correct nested-group serialization for families more complex than any in the 33; the round-trip harness must exercise the nesting.
- **Enum-valued fields**: no enum value-domain tables exist in the current model. Enum-domain validation is therefore either explicitly out of scope for generated validators (required/type-conformance only) or requires a separate enum surface — this must be decided, not silently folded in.
- **A family that fails differential round-trip**: must surface as a named failing message with its divergence, never silently pass.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The writer-emitter MUST generate a typed builder, a typed validator, a typed argument struct, and a typed read-back path for **every application message** defined in the FIX44 dictionary, using the same generation mechanism already used for the 33 OFFICIAL messages.
- **FR-002**: The set of covered messages MUST exclude the 7 session/admin MsgTypes (Heartbeat, TestRequest, ResendRequest, Reject, SequenceReset, Logout, Logon).
- **FR-003**: The set of covered messages MUST exclude the N-002/N-003 session-FSM application messages (UserRequest/UserResponse; ApplicationMessageRequest / …Ack / …Report).
- **FR-004**: Coverage MUST be limited to the v44 namespace. Other codegen namespaces MUST NOT gain generated builders in this feature (the all-version axis is explicitly deferred).
- **FR-005**: The generated output for the 33 previously-covered OFFICIAL messages MUST be byte-for-byte unchanged (no behavioral or output regression).
- **FR-006**: Generated validators for newly-covered messages MUST enforce required-field presence and fail closed on omission, with behavior consistent with the existing 33.
- **FR-007**: The feature MUST provide a build-time selection control that chooses between OFFICIAL-only coverage and full-family coverage. The **default MUST be full-family**; OFFICIAL-only is the opt-DOWN path a cost-sensitive build selects to bound its compile-time cost.
- **FR-008**: The project's continuous integration MUST generate and verify the full-family set in at least one build configuration, so wide coverage is continuously proven irrespective of the default selection.
- **FR-009**: Every generated application-message builder MUST be verified by a differential round-trip: its output parsed back through the independent runtime-XML path with each seeded field asserted to its exact value.
- **FR-010**: The verification MUST include an external anchor for a small exemplar-per-family subset (comparison against reference-engine-authored golden wire bytes), so the round-trip is not tautological.
- **FR-011**: The codegen MUST carry a completeness assertion pinning the exact set of emitted messages to the intended set, so neither a silent drop nor a silent addition can pass unnoticed.
- **FR-012**: The feature MUST NOT change any runtime, C-ABI, Python, or runtime-link-ABI surface (codegen and test tiers only), mirroring 067's constraint. The generated C++ header surface (`v44/Builders.hpp`) **intentionally grows** — ~50 new `build_/validate_/Args` symbols in `fixpp::v44`, which stay outside the frozen C ABI (`capi_freeze.sha256` / `c_api.h` unchanged). No-regression checks target `capi_freeze.sha256`, not the absence of new C++ builder names.
- **FR-013**: Enum value-domain validation is **out of scope** for generated validators (they enforce required-field presence + type conformance only, matching 067). The unbacked enum value-domain MUST be recorded as an explicit behavior/limitation, not left implicit.

### Key Entities *(include if feature involves data)*

- **Application message (FIX44)**: A FIX message type carrying business content (orders, quotes, trades, positions, collateral, reference data, etc.), as opposed to session/admin control messages. **85 `msgcat='app'`** messages exist in the FIX44 dictionary; 33 are already typed; **83 are in-scope** (85 minus the BE/BF session-FSM pair).
- **Typed builder**: The generated function that assembles a specific message's wire bytes from a typed argument struct.
- **Typed validator**: The generated routine that checks a message's field presence/conformance before or alongside serialization.
- **Argument struct**: The generated typed shape describing a message's (and its nested groups') settable fields.
- **Differential round-trip harness**: The verification mechanism that builds each message and re-parses it through the independent runtime path, asserting exact field-value round-trip.
- **Coverage selection control**: The build-time setting that determines whether OFFICIAL-only or full-family builders are generated.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A typed builder + validator + argument struct + typed read-back exists for **100% of in-scope FIX44 application messages** (all application messages minus the excluded session/admin and N-002/N-003 sets).
- **SC-002**: **100% of generated application-message builders** pass the differential round-trip check (every seeded field reads back exactly through the independent runtime path).
- **SC-003**: The 33 previously-covered OFFICIAL messages show **zero output change** (byte-identical generated artifacts before vs after).
- **SC-004**: With the coverage selection set to OFFICIAL-only, the generated builder count and compile cost equal today's baseline (33 builders); with it set to full-family, the count equals the full in-scope set.
- **SC-005**: A developer can construct and validate any in-scope FIX44 application message through the typed API without touching the generic runtime tag/value path.
- **SC-006**: The exemplar-per-family external-golden anchor matches reference-engine-authored bytes for the chosen exemplars (external oracle green).
- **SC-007**: The enum-domain validation disposition is recorded explicitly in the behaviors/limitations catalogue (delivered or declared out of scope).

## Assumptions

- **Default coverage selection** — **DECIDED (Clarifications 2026-07-11): default = full-family.** The stock build generates all in-scope application builders (families present in the default artifact — matches "close the legacy chapter"); OFFICIAL-only is the opt-DOWN path for cost-sensitive builds. CI verifies the full-family set regardless (FR-008).
- **Enum-domain validation** — **DECIDED (Clarifications 2026-07-11): out of scope.** Generated validators enforce required-field presence + type conformance only (matching current 067 validator behavior); enum value-domain remains unbacked and is recorded as a limitation. A defined enum surface would be a separate scope increment.
- **Exemplar-per-family external goldens** — A fixed enumerated subset (one newly-covered message per newly-covered family class + ≥1 group-heavy/nested case — see contract C4), not all 83, reusing the existing reference-engine golden-capture approach; full per-message external parity stays optional hardening.
- **Message count** — The authoritative in-scope set is **83 = 85 `msgcat='app'` minus {BE, BF}** (33 already-typed OFFICIAL + 50 new), resolved from the dictionary at codegen time via `msgcat` (not a hand-maintained list beyond the small N-002/N-003 exclusion set and the completeness pin). The spike's "~86" was its **msgtype-based** measurement (retained only as the compile-cost basis); the msgcat-based in-scope count the emitter actually produces is **83**. BW/BX/BY are absent from FIX44 (no-op excludes); XMLnonFIX `35=n` is `msgcat='admin'` and never emitted.
- **Emission is mechanical** — No per-message hand-authored builder, seed, or shape-oracle is required to emit a new message; the existing dictionary model already carries the field/rule data for all messages (verified by the spike). Hand-authoring is not part of this feature.
- **Namespace** — v44 only; the all-version axis (4.2/5.0SP2/FIXT/low-traffic) is deferred as demand-driven and is not part of "done" here.

## Normative References

Per Constitution Article VI §5, the exact catalogue/coverage-index references (sourced from `spec/coverage-index.md` + `spec/feature-catalogue.md`) that inform this spec. The write-coverage rows this feature advances are the previously-deferred FIX 4.4 application families, cited at **message-level `[FIX44]` DocAbbrev granularity** — the same `[impl]`/design-authority disposition 067 recorded (per Constitution Article VI §3), because the entire FIX44 application-message domain carries message-level refs in the coverage-index, not section-granular `[DocAbbrev §X.Y.Z]` refs. The byte-exact canonical format is fixed by the frozen 061 shape-oracle + `body_builder` + QuickFIX goldens (the design-authority block below), which serve as the authoritative format reference in lieu of section-granular spec citations.

**Newly-covered FIX44 application families (`[FIX44]`; coverage-index write column flips from `—` to `069`):**
- **A-014** `[FIX44] BusinessMessageReject (35=j)` · **A-015** `DontKnowTrade (35=Q)`.
- **A-016/017/020** and the FIX44 member of **A-022** (`AssignmentReport (35=AW)`) — the additional order-management / cross-order / list-strike variants per the coverage-index MsgType rows (e.g. `ListStrikePrice (35=m, A-020)`), message-level `[FIX44]`. (A-023/030/031/032/033 are FIX50-only — absent from FIX44, deferred; see the NOT-delivered carve-out.)
- **A-019** the List-handling family — `ListCancelRequest (35=K)` / `ListExecute (35=L)` / `ListStatusRequest (35=M)` / `ListStatus (35=N)`.
- **A-025** the SecurityList family — `SecurityListRequest (35=x)` / `SecurityList (35=y)`.
- **A-026** reference-data derivative-list family present in FIX44 — `DerivativeSecurityListRequest (35=z)` / `DerivativeSecurityList (35=AA)`. (A-027 TradingSessionList `35=BI/BJ/BS` and A-028 MarketDefinition `35=BT/BU/BV` are FIX50-only — absent from FIX44, deferred; see carve-out. A-026's `DerivativeSecurityListUpdateReport (35=BR)` is likewise FIX50-only and deferred.)
- **C-001** Collateral family — `CollateralRequest (35=AX)` / `CollateralAssignment (35=AY)` / `CollateralResponse (35=AZ)` / `CollateralReport (35=BA)` / `CollateralInquiry (35=BB)` / `CollateralInquiryAck (35=BG)`.
- **C-002** Position family — `PositionMaintenanceRequest (35=AL)` / `…Report (35=AM)` / `RequestForPositions (35=AN)` / `…Ack (35=AO)` / `PositionReport (35=AP)`. (`AdjustedPositionReport (35=BL)` is FIX50-only — absent from FIX44, deferred; see carve-out.)
- **R-001..R-005** Registration family — `RegistrationInstructions (35=o)` / `RegistrationInstructionsResponse (35=p)` and the associated registration variants per the coverage-index `R-00x` rows.
- **P-004..P-008** Post-trade family — `Confirmation (35=AK)` / `ConfirmationAck (35=AU)` / `ConfirmationRequest (35=BH)` (P-005), `TradeCaptureReportRequest (35=AD)` / `TradeCaptureReport (35=AE)` / `…RequestAck (35=AQ)` / `…Ack (35=AR)` (P-008), and the further P-004/P-006/P-007 post-trade variants per the coverage-index rows.

**Explicitly NOT delivered (per-row carve-outs the folded amendment reconciles):**
- **A-034** `XMLnonFIX (35=n)` — `msgcat='admin'`, auto-excluded by the msgcat filter, **never emitted** by 069.
- **A-024** — stays dropped as a duplicate per `[SYN §4.4]` (unchanged).
- **FIX50-only rows (absent from FIX44 — deferred to future `fixpp::v50sp2`/all-version widening, NOT closed by 069):** A-018 `ExecutionAcknowledgement (35=BN)`, A-023 `OrderMassAction (35=BZ/CA)`, A-027 `TradingSessionList (35=BI/BJ/BS)`, A-028 `MarketDefinition (35=BT/BU/BV)`, A-029 `SecurityListUpdate (35=BK/BP)`, A-030 `SettlementObligation (35=BQ)`, A-031 `AllocationInstructionAlert (35=BM)`, A-032 `UserNotification (35=CB)`, A-033 `StreamAssignment (35=CC/CD/CE)`, C-003 `AccountSummaryReport (35=CQ)` — whole rows with no FIX44 message. Mixed-row FIX50-only siblings likewise deferred: `ContraryIntentionReport (35=BO)` (A-022), `DerivativeSecurityListUpdateReport (35=BR)` (A-026), `AdjustedPositionReport (35=BL)` (C-002).
- **N-002/N-003** `BE (UserRequest)` / `BF (UserResponse)` — session-FSM-dispatch, excluded (FR-003); remain the separate v1.0-tagging gate. `BW/BX/BY` are absent from FIX44.

**Design authority (not FIX-spec sections; the frozen contracts this feature reproduces):**
- `[067] contracts/generated-builder.md` G1–G9 — the per-message `build_/validate_/Args`/registry shape 069 reproduces verbatim for the widened set.
- `[061] builder-shape-oracle.md` — `wire::body_builder`, `shape_oracle_profile()`, the QuickFIX goldens (byte-equality reference).
- `[057] dict::reify` — the already-universal typed read-back path (R4); 069 does not re-emit it.
- `[../remaining-work/v44-all-families-measure-spike.md]` — the measure-first spike (build-cost basis; msgtype-based 86 count).

**Constitutional / process authority:** `[const §VI §5]` (Normative References + 100% FIX rule), `[const §VII §8]` (test grouping + `ctest -L` selection), `[const §X]` (C-ABI freeze — FR-012), `[const §XVI.3]`/`[const §XVII]` (clarify + Gate A/B controls), `[const §XVIII §5/§7]` (roadmap discipline + the deferred A-014..A-034 set this feature reclassifies), `[const §XX §1/§2]` (folded-amendment authority — see plan.md `## Constitution Amendment Payload`).
