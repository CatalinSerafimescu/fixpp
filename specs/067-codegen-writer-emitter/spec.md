# Feature Specification: FR-015a-lite — Codegen Writer-Emitter

**Feature Branch**: `067-codegen-writer-emitter`
**Created**: 2026-07-10
**Status**: Draft
**Input**: User description: "FR-015a-lite: codegen writer-emitter (the 'Emitter-Lite' pivotal feature). Add a codegen WRITE surface to the fixpp-codegen emitter that generates a typed builder per application message, matching the frozen 061 write shape-oracle byte-for-byte."

## Context & Motivation

The library already generates a **read** surface for every application message (typed flyweights + `dict::reify()`, hardened through 057/062/063/065/066). The **write** surface is the gap: today only **2 hand-written builders** exist (A-001 NewOrderSingle, A-006 ExecutionReport, from 020) plus **5 hand-written exemplar builders** (D/8/9/E/AS from 061-slim) over the `wire::body_builder` primitive. Hand-writing does not scale to the 33 OFFICIAL v1.0 MsgTypes, let alone all-version or all-families.

061-slim deliberately shipped only the 5 exemplars as a frozen **write shape-oracle** (`contracts/builder-shape-oracle.md`, C1–C6) so this feature — the codegen writer-emitter — can generate the remaining ~28 OFFICIAL builders and be pinned to reproduce the exemplar output byte-for-byte. This is the pivotal feature that makes "every OFFICIAL message typed (read + build + round-trip)" true for v1.0.

**Anchor documents** (verified 2026-07-10):
- `research/.../remaining-work/typed-messages-100pct-v1.0-plan.md` §5 Phase 2 (scope), §7 D0–D7 decisions (DECIDED 2026-07-07: FR-015a-first + slim 061).
- `contracts/builder-shape-oracle.md` (061) — C1–C6, the byte-equality reason this feature exists.
- `include/fixpp/wire/body_builder.hpp` — the single serialization core this feature emits wrappers over.
- Emitter home: `tools/codegen/fixpp-codegen/` (`emit_messages.cpp`, `emit_validator.cpp`, `emit_fields.cpp`, `gen_util.hpp`, `ir.hpp/.cpp`).

## Clarifications

### Session 2026-07-10

- Q: Validation scope — required-presence only, or also enum-domain / conditional-required? → A: **Required-presence ONLY** (enum + conditional cut for v1.0; unbacked by existing tables, hand-validated per message where needed; no `emit_enums`).
- Q: Where does the required-presence validation run, given the headline test demands generated `commit()` be byte-identical to the pure-serializer 061 exemplars? → A: **A separate `validate()` step** — `commit()` stays a pure serializer identical to the 061 exemplars; required-presence is a distinct routine callers + the round-trip harness invoke.
- Q: How deep does the required-presence validator check — top-level message fields only, or also repeating-group entry required fields? → A: **Also group-entry required fields** — validation recurses into repeating-group entries; per-group required-field tables are emitted for this (`emit_group_rules`), in addition to the existing top-level `<Msg>_rules`.
- Q: What error disposition does a missing required field use, given FR-009 forbids new error values? → A: The **pre-existing** `error::wire_required_field_missing` (= 38, `core/error.hpp`) — no new value, no FR-009 conflict.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Generated typed builder for every OFFICIAL message (Priority: P1)

A library consumer building outbound FIX application messages wants a typed builder for any of the 33 OFFICIAL MsgTypes (not only the 7 hand-written ones), with the same fail-closed guarantees and canonical wire output as the hand-written builders.

**Why this priority**: This is the feature's core deliverable — codegen-generated write coverage of the full OFFICIAL v1.0 set. Without it the typed-message v1.0 promise ("every OFFICIAL message typed: read + build + round-trip") is unmet.

**Independent Test**: For each of the 33 OFFICIAL distinct MsgTypes, a generated builder compiles, accepts typed field setters, and emits a body that parses back (via `dict::reify()`) to the seeded field values — asserted by the parameterized round-trip witness harness.

**Acceptance Scenarios**:

1. **Given** the codegen writer-emitter is run over the v44 dictionary, **When** the OFFICIAL message set is emitted, **Then** a generated builder type exists for **exactly** the 33 OFFICIAL distinct MsgTypes (exact-set equality, not subset-presence).
2. **Given** a generated builder for a grouped message (e.g. MassQuote 35=i), **When** the caller opens a repeating group via the emitted group API, **Then** the group is serialized through `body_builder`'s single LIFO group core (no second/third group-write idiom) with `No<Group>=<N>` count-precedence and delimiter-first instances (INV-5).
3. **Given** a generated builder invoked with valid inputs, **When** `commit()` succeeds, **Then** the returned body contains only business fields — none of the framing tags {8,9,34,49,52,56,10} (INV-2) — with canonical decimals (INV-3).

---

### User Story 2 — Shape-oracle byte-equality (the headline pin) (Priority: P1)

The 5 exemplar MsgTypes (D/8/9/E/AS in v44) have BOTH a hand-written 061 builder AND a QuickFIX-authored golden. The generated builder for each MUST produce a body byte-identical to both.

**Why this priority**: This equality is the feature's reason to exist. It is the single test that proves the emitter reproduces the frozen write contract rather than drifting. If it fails, the emitter is wrong by definition.

**Independent Test**: For each of D/8/9/E/AS, drive the generated builder and the hand-written 061 exemplar builder with the identical 061 seed; assert the two emitted bodies are byte-equal, and assert both byte-match the checked-in QuickFIX golden via the 061 `shape_oracle_profile()` (excludes only framing {8,9,10,34,52}; matches every business tag verbatim, decimals by value).

**Acceptance Scenarios**:

1. **Given** the 061 seed for MsgType D (NewOrderSingle), **When** the generated v44 builder emits its body, **Then** the bytes equal the hand-written 061 exemplar body exactly.
2. **Given** the same, **When** compared against the QuickFIX golden via `shape_oracle_profile()`, **Then** they match (by-value decimals, verbatim business tags including TransactTime 60).
3. **Given** the multi-char MsgType AS (AllocationReport), **When** the generated builder emits its body, **Then** it begins `35=AS\x01` and matches both oracles (multi-char MsgType path preserved).

---

### User Story 3 — Required-presence validation, generated over emitted tables (Priority: P2)

A consumer wants a way to check that all required fields — top-level AND inside every repeating-group entry — are present before sending, without the library maintaining a second hand-rolled validation copy that can drift from the dictionary.

**Why this priority**: Validation-drift-by-construction is a core Emitter-Lite benefit — the top-level `<Msg>_rules` presence tables are already emitted and currently have **zero consumers** (verified: grep of `src/`/`include/` for `_rules` returns nothing). One generic runtime routine over those tables (plus emitted per-group `<Group>_rules`) gives required-presence validation. It is a **separate `validate()` step**, NOT part of `commit()`: `commit()` must stay a pure serializer byte-identical to the 061 exemplars (which perform no dictionary validation), so gating serialization on validation would break the headline shape-oracle equality. Lower than P1 because builders serialize without it, but it closes the fail-closed contract for callers that opt in.

**Independent Test**: A generated builder for a message with a required field (top-level or group-entry), passed to `validate()` with that field absent, returns `wire_required_field_missing`; with all required fields present at every level, `validate()` succeeds. `commit()` behavior is unchanged by validation.

**Acceptance Scenarios**:

1. **Given** a generated builder whose message has a required top-level tag absent, **When** `validate()` is called, **Then** `wire_required_field_missing` is returned.
2. **Given** a grouped message where one repeating-group entry is missing a required entry field, **When** `validate()` is called, **Then** `wire_required_field_missing` is returned (recursive depth).
3. **Given** a builder with all required fields present at top level and in every group entry, **When** `validate()` is called, **Then** it succeeds; and **When** `commit()` is called, **Then** its output is byte-identical to the equivalent hand-written exemplar (validation is off the serialize path).
4. **Given** ANY OFFICIAL message, **When** its required-presence rule set is built, **Then** both the top-level body set and every per-group set derive from the emitted level-scoped presence tables (header/framing tags excluded), not from the header-polluted flat `<Msg>_rules` and not from any hand-authored required-field list.

### Edge Cases

- **N==0 optional group**: a builder that opens an optional group with zero instances emits `No<Group>=0\x01` (present-but-empty), consistent with read-side accept (C3 / B-004-7). A required group with zero instances is rejected.
- **Group entirely absent**: a builder never asked to open an optional group omits its `No<Group>` tag entirely (distinct from N==0).
- **Undersized output span / over-cap body**: typed error, `out` untouched (inherited from `body_builder`).
- **SOH / control byte injected in a value**: rejected before any bytes reach `out` (inherited).
- **Enum out of domain / conditional-required violation**: NOT validated by this feature (explicitly cut — see Assumptions/Out of Scope). The wire is still well-formed; domain correctness is the caller's responsibility for v1.0.
- **Message with no required fields**: required-presence routine is a no-op; builder always commits on well-formed input.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The codegen writer-emitter MUST generate, for each OFFICIAL application message, a typed builder exposing one typed setter per direct field, each setter a thin wrapper over the corresponding `wire::body_builder` field/group operation.
- **FR-002**: Generated builders MUST serialize exclusively through the existing `wire::body_builder` core (its LIFO group API included). The feature MUST NOT introduce a second/third serialization or group-write idiom.
- **FR-003**: For the 5 exemplar MsgTypes (D/8/9/E/AS in v44), the generated builder body MUST be byte-identical to the hand-written 061 exemplar body AND byte-match the QuickFIX golden via the 061 `shape_oracle_profile()`, driven by the identical 061 seed.
- **FR-004**: The generated OFFICIAL builder set MUST cover **exactly** the 33 OFFICIAL distinct MsgTypes — verified by an exact-set completeness gate (set equality, not subset-presence).
- **FR-005**: Generated builders MUST preserve INV-2 (body-only framing: no {8,9,34,49,52,56,10}), INV-3 (canonical decimals), INV-4 (fail-closed atomic commit — `out` untouched on any error), and INV-5 (group grammar: count-precedence, non-empty + delimiter-first instances) — all inherited from `body_builder`.
- **FR-006**: The emitter MUST provide ONE generic runtime `validate()` routine (SEPARATE from `commit()` — off the serialize path so `commit()` stays byte-identical to the 061 exemplars) that enforces **required-field presence recursively**: top-level body fields, and every repeating-group entry's required fields. It MUST return the pre-existing `error::wire_required_field_missing` (= 38) when a required field is absent at any level — no new error value.
- **FR-006a**: The emitter MUST emit **level-scoped required-presence tables** derived from the dictionary presence already carried per-field in the IR (`FieldRef.rule`), reusing the existing `GroupPlan`/`plan_dfs` traversal — NO IR addition (verified 2026-07-10: `FieldRef.rule` is populated Required on group children). The existing `Validator.hpp` `<Msg>_rules` table MUST NOT be reused verbatim: it is header-polluted (`8/9/10/34/49/52/56` marked Required — a body-only builder never sets these) and level-flattened (a group-child tag is indistinguishable from a top-level tag). The emitter therefore emits: (a) a **top-level body** required set per message = fields with `group_no_tag==0`, `rule==Required`, tag ∉ the framing/header exclusion set `{8,9,10,34,35,49,52,56}`; and (b) a **per-group** required set (`<Group>_rules`) per repeating group = fields with `group_no_tag==<group>`, `rule==Required`. Still NO `emit_enums`/enum-domain work.
- **FR-007**: Generated builders and setters MUST reuse the existing emitter facilities (`kind_of`, `to_accessor`, `GroupPlan`, `plan_dfs`) rather than reimplementing type mapping, accessor naming, or group planning.
- **FR-008**: The round-trip witness harness MUST be non-tautological: exemplar rows anchored against QuickFIX goldens; generated bulk rows additionally asserting byte-level structure (field order matches the seed table, SOH count, absence of framing tags) — a pure build→parse loop is insufficient.
- **FR-009**: The feature MUST NOT change the read path, wire-format semantics, codegen read-layout, the C-ABI, or the Python surface, and MUST NOT add any new `fixpp_error_t` value (the C-ABI is GA-frozen at 1.5.0).
- **FR-010**: Codegen output MUST be regenerated via the forced-regeneration path and pass the codegen build-graph cleanliness gate (`git`-clean after regen); generated builder headers ship as a new generated include surface.

### Key Entities

- **Generated builder** — a per-message typed writer over `body_builder`; one typed setter per direct field, an emitted group-open method per repeating group.
- **`writer_traits<Msg>`** — the per-message binding that associates a message with its emitted top-level body required-presence table (and the per-group `<Group>_rules` tables reachable from it) so the generic `validate()` routine can locate the required-field set at every level.
- **Top-level body required-presence table** — new per-message table this feature emits: `group_no_tag==0`, `rule==Required`, header/framing tags `{8,9,10,34,35,49,52,56}` excluded. Distinct from the existing header-polluted, level-flattened `Validator.hpp` `<Msg>_rules` (which this feature does NOT reuse).
- **`<Group>_rules` presence table** — new per-repeating-group required-field table this feature emits (`group_no_tag==<group>`, `rule==Required`), giving the validator recursive group-entry required-presence.
- **Shape-oracle** — the frozen 061 reference (5 exemplar builders + `body_builder` + QuickFIX goldens) the generated exemplars must reproduce byte-for-byte.
- **Round-trip seed table** — the constexpr `{msg, version, [(tag, seed)…], group-shape}` driving the parameterized witness harness.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All **33 OFFICIAL distinct MsgTypes** have a generated builder (exact-set completeness gate passes).
- **SC-002**: For **all 5** exemplar MsgTypes, generated-vs-hand byte-equality AND generated-vs-QuickFIX-golden match hold (headline pin green).
- **SC-003**: Every generated OFFICIAL builder passes its seeded round-trip witness (each seeded field, including nested group entries, reads back at its exact input value) with the non-tautological byte-structural asserts.
- **SC-004**: Required-presence `validate()` fires for every OFFICIAL message at both levels: a missing top-level required field AND a missing group-entry required field each yield `wire_required_field_missing`; a fully-populated message validates clean; the required set is table-derived at both levels (zero hand-authored required lists). `commit()` output is unaffected by `validate()` (byte-identical to exemplars).
- **SC-005**: Read path, wire semantics, C-ABI, and Python surface are unchanged (existing suites and the C-ABI 1.5.0 freeze byte-identical); no new error enum value.
- **SC-006**: Forced codegen regeneration is `git`-clean; the codegen build-graph cleanliness gate passes.

## Assumptions

- **Validation scope is required-presence ONLY** (user decision 2026-07-10), checked **recursively** (top-level + group-entry). Enum-value-domain checks and conditional-required predicates are **cut for v1.0** — they are unbacked by the existing tables (which carry only a per-tag presence marker) and would require new `emit_enums`/predicate work. Where a specific enum must be enforced, it is hand-validated per message (020 precedent). NO `emit_enums`/enum-domain work in this feature. The one permitted table addition is per-group `<Group>_rules` (FR-006a); it reuses existing group-member presence — `/plan` confirms whether that needs a minimal IR field or is already present, and keeps it to required-presence only.
- **One representative namespace ships** (v44 for the exemplar pins; the OFFICIAL set per the plan's representative-version choice). All-version coverage (FR-015b) is a later demand-driven flag-flip once the emitter is version-agnostic — out of scope here.
- **The 061 shape-oracle is frozen and authoritative.** Its 5 exemplars + goldens + `shape_oracle_profile()` are reused as-is; this feature does not re-derive them.
- **`body_builder` is the single serialization core.** Its buffer/allocation policy (16384 B arena / 3800 B body cap, null-upstream fail-closed) is settled and inherited unchanged.
- **The `<Msg>_rules` tables are already emitted and currently unconsumed** (verified by grep); this feature is their first consumer.

## Out of Scope

- **FR-015b all-version coverage** (4.2/5.0SP2/FIXT.1.1 etc.) — post-v1.0; this feature makes the emitter version-agnostic but ships only the representative namespace.
- **Enum-domain and conditional-required validation** / any `emit_enums` / enum-domain dictionary / IR work — cut (see Assumptions). (Required-presence tables, including the new per-group `<Group>_rules`, ARE in scope per FR-006a.)
- **Typed C-ABI / Python exposure** — stays generic tag-based and GA-frozen; typed Python stubs are a v1.x follow-on with zero ABI impact.
- **N-002 / N-003** (UserRequest / ApplicationMessageRequest family) — need session-FSM dispatch, a distinct work class; deferred.
- **Deferred families** C-001..003, R-001..005, P-004..008, N-001 — v1.x; per the Orchestra/FIX-Latest direction, family breadth becomes an emitter/Orchestra output, not hand-staged work.
- **Unifying the 3 outbound serializers** (020 helpers, C-ABI `OutboundAccumulator`, typed builders) into a single core — tracked v1.x debt.

## Dependencies

- **061-slim** (PR #178) — `wire::body_builder`, 5 exemplar builders, `contracts/builder-shape-oracle.md`, `shape_oracle_profile()`, QuickFIX goldens. **DONE.**
- **062 / 063 / 065 / 066** — grouped/nested typed **read** correctness (round-trip read-back side of the witness). **DONE.**
- **057** — reify + multi-char MsgType dispatch (35=AS and any multi-char OFFICIAL type parse back). **DONE.**
