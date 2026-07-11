# Feature Specification: v44 all-families typed codegen coverage

**Feature Branch**: `069-v44-all-families`
**Created**: 2026-07-11
**Status**: Draft
**Input**: User description: "v44 all-families typed codegen coverage — extend the existing 067 codegen writer-emitter from the 33 OFFICIAL FIX44 MsgTypes to EVERY application message in the FIX44 dictionary (measured 33 → 86), generating the same build_/validate_/Args/reify artifacts per message, v44 namespace only, application messages only, verified by differential round-trip at breadth."

## Context & Motivation

fixpp already parses, validates, and serializes **every** FIX message across all nine legacy versions via its runtime-XML layer (dictionary-driven, `view.get(tag)` + `wire::body_builder`). What is partial is the **typed compile-time convenience layer**: feature `067-codegen-writer-emitter` generates typed `build_<Msg>()` / `validate_<Msg>()` builders for only the **33 OFFICIAL** FIX44 application MsgTypes. The remaining ~53 application messages the FIX44 dictionary defines (Collateral, Position, TradeCapture, Confirmation, SecurityList, TradingSessionList, MarketDefinition, List-handling, Registration, and more) have no typed builder — a developer must drop to the generic runtime path for them.

This feature closes that gap on the **one representative v44 namespace**: it widens the existing writer-emitter to generate the same typed artifacts for **all** FIX44 application messages, so the legacy typed-ergonomics chapter is complete before the successor Orchestra / FIX-Latest direction. A measure-first spike (`../remaining-work/v44-all-families-measure-spike.md`) confirmed emission is mechanical from the existing dictionary model, with a bounded build-time cost and no architectural blocker.

## Clarifications

### Session 2026-07-11

- Q: Default build coverage for v44 typed builders (CMake selection default)? → A: **Full-family by default** — the stock build generates all in-scope application builders; a cost-sensitive build can opt DOWN to OFFICIAL-only. Best serves "close the legacy chapter" (families present in the default artifact).
- Q: Scope of enum value-domain validation in generated validators? → A: **Out of scope** — generated validators enforce required-field presence + type conformance only (matching 067); enum value-domain stays unbacked and is recorded as an explicit limitation.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Typed build/validate for any FIX44 application message (Priority: P1)

A developer integrating fixpp wants to construct and validate any FIX44 application message — not only the 33 OFFICIAL ones — using the typed builder API instead of hand-assembling tag/value pairs on the generic runtime path. Today, if they need TradeCaptureReport, PositionReport, CollateralInquiry, SecurityList, or any of the other ~53 application messages, no typed builder exists.

**Why this priority**: This is the feature's core value — completing typed coverage of the FIX44 application message set. Without it, the legacy typed layer stays partial and the "close the chapter" goal is unmet.

**Independent Test**: Pick any previously-uncovered family (e.g. TradeCaptureReport `35=AE`), populate its typed argument struct, invoke its generated builder, and assert the produced wire bytes are well-formed and carry the seeded fields; invoke its generated validator and confirm required-field enforcement.

**Acceptance Scenarios**:

1. **Given** the FIX44 dictionary defines an application message M (any of the ~86, excluding the 7 session/admin types), **When** the codegen runs with full-family coverage enabled, **Then** a typed builder, validator, argument struct, and typed read-back are generated for M.
2. **Given** a generated builder for a previously-uncovered family, **When** a developer supplies typed field values and calls it, **Then** it emits conformant FIX44 wire bytes for that MsgType.
3. **Given** a message with a required field omitted, **When** the generated validator runs, **Then** it fails closed and reports the missing required field — identical in behavior to the 33 OFFICIAL messages.
4. **Given** the 33 previously-covered OFFICIAL messages, **When** full-family coverage is generated, **Then** their generated output is byte-for-byte unchanged (no regression to existing builders).

---

### User Story 2 - Every generated builder is verified, not just emitted (Priority: P1)

A maintainer must trust that ~86 mechanically-generated builders are correct without hand-writing 86 golden fixtures — and without a mass positive-fixture push that would silently enshrine any emitter bug.

**Why this priority**: Breadth without verification is worse than no breadth — it ships latent bugs behind a "typed" label. The verification methodology is what makes wide coverage trustworthy, so it is co-equal P1 with the coverage itself.

**Independent Test**: Run the differential harness — for every generated application message, seed its argument struct, build wire bytes, parse them back through the independent runtime-XML path, and assert each seeded field reads back with its exact value. The harness passes for all covered messages or names the divergent ones.

**Acceptance Scenarios**:

1. **Given** every generated application-message builder, **When** the differential round-trip harness runs, **Then** each builder's output parses back through the runtime path with all seeded fields matching exactly.
2. **Given** a small exemplar-per-family subset, **When** their output is compared to externally-authored (reference-engine) golden wire bytes, **Then** the bytes match — anchoring the round-trip against an external oracle so builder and parser cannot be co-wrong.
3. **Given** the emitter's completeness pin, **When** codegen runs, **Then** the pin asserts the exact set of emitted messages equals the intended set (no silent drop, no silent extra).

---

### User Story 3 - Build cost of wide coverage is bounded and selectable (Priority: P2)

An operator building fixpp — especially in the resource-constrained CI matrix (multiple sanitizer presets on limited hardware) — must not be forced to pay the full compile-time cost of ~86 builders when they only need the OFFICIAL set, while CI must still generate and verify the full set.

**Why this priority**: The spike measured a +19 s/TU compile cost (2.57×) for the full header. The default is full-family (families present by default — see Clarifications), so a cost-sensitive build needs an opt-DOWN path to OFFICIAL-only to bound its compile cost. A selection control provides that without foreclosing full coverage. It is P2 because it constrains *how* the P1 value is delivered, not *whether*.

**Independent Test**: Configure the build with each selection value; confirm the generated coverage matches the selection (full-family default vs OFFICIAL-only opt-down), and that a build opting down to OFFICIAL-only regenerates exactly today's 33-builder output.

**Acceptance Scenarios**:

1. **Given** the default build (no override), **When** codegen runs, **Then** all in-scope (~86) application-message builders are generated.
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
- **FR-012**: The feature MUST NOT change any runtime, C-ABI, or Python surface (codegen and test tiers only), mirroring 067's no-public-runtime-surface constraint.
- **FR-013**: Enum value-domain validation is **out of scope** for generated validators (they enforce required-field presence + type conformance only, matching 067). The unbacked enum value-domain MUST be recorded as an explicit behavior/limitation, not left implicit.

### Key Entities *(include if feature involves data)*

- **Application message (FIX44)**: A FIX message type carrying business content (orders, quotes, trades, positions, collateral, reference data, etc.), as opposed to session/admin control messages. ~86 exist in the FIX44 dictionary; 33 are already typed.
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
- **Exemplar-per-family external goldens** — Assumed a small representative subset (one exemplar per major family group), not all ~86, reusing the existing reference-engine golden-capture approach; full per-message external parity stays optional hardening.
- **Message count** — The "~86" figure is the spike's measured count from the current vendored FIX44 dictionary; the authoritative in-scope set is "all FIX44 application messages minus the excluded sets," resolved from the dictionary at codegen time (not a hand-maintained list beyond the exclusion sets and the completeness pin).
- **Emission is mechanical** — No per-message hand-authored builder, seed, or shape-oracle is required to emit a new message; the existing dictionary model already carries the field/rule data for all messages (verified by the spike). Hand-authoring is not part of this feature.
- **Namespace** — v44 only; the all-version axis (4.2/5.0SP2/FIXT/low-traffic) is deferred as demand-driven and is not part of "done" here.
