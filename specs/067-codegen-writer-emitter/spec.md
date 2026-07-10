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

**Independent Test**: For each of the 33 OFFICIAL distinct MsgTypes, a generated builder compiles, accepts a populated `<Msg>Args` aggregate (per FR-001 — no per-field setter surface), and emits a body that parses back (via `dict::reify()`) to the seeded field values — asserted by the parameterized round-trip witness harness.

**Acceptance Scenarios**:

1. **Given** the codegen writer-emitter is run over the v44 dictionary, **When** the OFFICIAL message set is emitted, **Then** a generated builder type exists for **exactly** the 33 OFFICIAL distinct MsgTypes (exact-set equality, not subset-presence).
2. **Given** a generated builder for a grouped message (e.g. MassQuote 35=i), **When** the caller supplies a repeating-group span in `<Msg>Args`, **Then** the group is serialized through `body_builder`'s single LIFO group core (no second/third group-write idiom) with `No<Group>=<N>` count-precedence and delimiter-first instances (INV-5), the delimiter and member order taken from THIS message's own occurrence of the group (not a version-wide plan).
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

**Why this priority**: Validation-drift-by-construction is a core Emitter-Lite benefit — the top-level `<Msg>_rules` presence tables are already emitted and currently have **zero consumers** (verified: grep of `src/`/`include/` for `_rules` returns nothing). One generic runtime routine over those tables (plus emitted per-occurrence group required tables) gives required-presence validation. It is a **separate `validate()` step**, NOT part of `commit()`: `commit()` must stay a pure serializer byte-identical to the 061 exemplars (which perform no dictionary validation), so gating serialization on validation would break the headline shape-oracle equality. Lower than P1 because builders serialize without it, but it closes the fail-closed contract for callers that opt in.

**Independent Test**: A generated builder for a message with a required field (top-level or group-entry), passed to `validate()` with that field absent, returns `wire_required_field_missing`; with all required fields present at every level, `validate()` succeeds. `commit()` behavior is unchanged by validation.

**Acceptance Scenarios**:

1. **Given** a generated builder whose message has a required top-level tag absent, **When** `validate()` is called, **Then** `wire_required_field_missing` is returned.
2. **Given** a grouped message where one repeating-group entry is missing a required entry field, **When** `validate()` is called, **Then** `wire_required_field_missing` is returned (recursive depth).
3. **Given** a builder with all required fields present at top level and in every group entry, **When** `validate()` is called, **Then** it succeeds; and **When** `commit()` is called, **Then** its output is byte-identical to the equivalent hand-written exemplar (validation is off the serialize path).
4. **Given** ANY OFFICIAL message, **When** its required-presence rule set is built, **Then** both the top-level body set and every per-group set derive from the emitted level-scoped presence tables (header/framing tags excluded), not from the header-polluted flat `<Msg>_rules` and not from any hand-authored required-field list.

### Edge Cases

- **N==0 optional group**: an optional group supplied as an *engaged* empty span (`std::optional<std::span>` present, `size()==0`) emits `No<Group>=0\x01` (present-but-empty), consistent with read-side accept (C3 / B-004-7). A required group (non-optional span) with zero instances is rejected by `validate_*` (`size() > 0` rule).
- **Group entirely absent**: an optional group supplied as `std::nullopt` (the `std::optional<std::span>` disengaged) omits its `No<Group>` tag entirely — distinct from the engaged-empty N==0 case above. This is why optional groups are `std::optional<std::span<const <G>Args>>`, not a plain span: a plain span cannot represent "absent" (see data-model §1.1).
- **Undersized output span / over-cap body**: typed error, `out` untouched (inherited from `body_builder`).
- **SOH / control byte injected in a value**: rejected before any bytes reach `out` (inherited).
- **Enum out of domain / conditional-required violation**: NOT validated by this feature (explicitly cut — see Assumptions/Out of Scope). The wire is still well-formed; domain correctness is the caller's responsibility for v1.0.
- **Message with no required fields**: required-presence routine is a no-op; builder always commits on well-formed input.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The codegen writer-emitter MUST generate, for each OFFICIAL application message, a free function `build_<Msg>(std::span<std::byte> out, const <Msg>Args&)` over a typed value aggregate `<Msg>Args` (one `std::optional<T>` member per direct scalar field; a group span per repeating group). Field emission is a thin pass over the corresponding `wire::body_builder` field/group operations; there is no per-field setter surface and no caller-driven "open group" API (the caller supplies group entries as a span in `<Msg>Args`).
- **FR-002**: Generated builders MUST serialize exclusively through the existing `wire::body_builder` core (its LIFO group API included). The feature MUST NOT introduce a second/third serialization or group-write idiom.
- **FR-003**: For the 5 exemplar MsgTypes (D/8/9/E/AS in v44), the generated builder body MUST be byte-identical to the hand-written 061 exemplar body AND byte-match the QuickFIX golden via the 061 `shape_oracle_profile()`, driven by the identical 061 seed.
- **FR-004**: The generated OFFICIAL builder set MUST cover **exactly** the 33 OFFICIAL distinct MsgTypes — verified by an exact-set completeness gate (set equality, not subset-presence). The gate MUST compare over a generated **MsgType→builder registry** keyed by the MsgType wire string (including the multi-char types `AF/AC/AG/AS`), NOT by comparing `build_<Identifier>` symbol names (`build_NewOrderSingle`) to MsgType strings (`D`) — those are different namespaces (identifiers via `to_identifier`; the MsgType string reaches only the `body_builder{"D"}` ctor).
- **FR-005**: Generated builders MUST preserve INV-2 (body-only framing: no {8,9,34,49,52,56,10}), INV-3 (canonical decimals), INV-4 (fail-closed atomic commit — `out` untouched on any error), and INV-5 (group grammar: count-precedence, non-empty + delimiter-first instances) — all inherited from `body_builder`.
- **FR-006**: The emitter MUST provide ONE generic runtime `validate()` routine (SEPARATE from `commit()` — off the serialize path so `commit()` stays byte-identical to the 061 exemplars) that enforces **required-field presence recursively**: top-level body fields, and every repeating-group entry's required fields. It MUST return the pre-existing `error::wire_required_field_missing` (= 38) when a required field is absent at any level — no new error value.
- **FR-006a**: The emitter MUST emit **level-scoped, per-message required-presence tables** derived from the dictionary presence carried per-field in the IR (`FieldRef.rule`), walking each message's own field run (`MessageIR.fields`) — the required-presence tables need **NO IR addition** because required-set membership is order-independent (verified 2026-07-10: `FieldRef.rule` is populated Required on group children; `MessageIR.fields` carries every field of THAT message with its correct `group_no_tag` and `rule`). NOTE (RC#7): `MessageIR.fields` is loader-**tag-sorted** (`xml_loader.cpp:695-702`), so it does NOT carry declaration order; the group **delimiter and member order** (FR-007) therefore require a codegen-tool-local `MessageIR.group_order` addition — the required-presence tables here are unaffected. The tables MUST be derived per message/per group-occurrence, NOT from a version-wide union: the same group `no_tag` can carry a different required set in different messages (confirmed: `NoMDEntries(268)` requires `MDEntryType(269)` in W but `MDUpdateAction(279)` in X — `dictionaries/FIX44.xml:3023` vs `:3060`), so a single version-wide `<Group>_rules` keyed by `no_tag` would over-reject one message and under-reject the other. The existing `Validator.hpp` `<Msg>_rules` table MUST NOT be reused verbatim: it is header-polluted (`8/9/10/34/49/52/56` marked Required — a body-only builder never sets these) and level-flattened (a group-child tag is indistinguishable from a top-level tag). The emitter therefore emits, per message: (a) a **top-level body** required set = fields with `group_no_tag==0`, `rule==Required`, tag ∉ the framing/header exclusion set `{8,9,10,34,35,49,52,56}`; and (b) a **per-occurrence group** required set = fields in THIS message's run with `group_no_tag==<group>`, `rule==Required`. Still NO `emit_enums`/enum-domain work.
- **FR-007**: Generated builders MUST reuse the existing type-and-name facilities (`kind_of`, `to_accessor`, `to_identifier`) rather than reimplementing type mapping or accessor naming. The group plan MUST be derived **per message**, NOT reused verbatim from the read emitter's version-wide `MemberMap`/`GroupPlan`, which dedups members into ONE plan per `no_tag` (correct for read, where membership is decided per-context at parse time by 062/063; unsound for write, which has no such runtime scoping). Within the per-message plan the two axes have DIFFERENT sources: the group **delimiter and member ORDER** come from `MessageIR.group_order` — a codegen-tool-local **declaration-order** walk of the message's raw XML (RC#7) — because `MessageIR.fields` is loader-tag-sorted (`xml_loader.cpp:695-702`) and cannot carry declaration order; the group **required set** comes from `m.fields` filtered by `group_no_tag ∧ rule==Required` (order-independent). Which type/name helpers are shared vs the per-message group planner is settled here (shared: `kind_of`/`to_accessor`/`to_identifier`; per-message: the group planner + `group_order`), not deferred; the read emitter's determinism golden stays green as the guard.
- **FR-007a**: The generated builder MUST honor the field kinds `kind_of` already computes, not fold them into an Int/String projection. (a) **Boolean** (`TypeKind::Bool`, FIX `Y`/`N`): the `<Msg>Args` member is `std::optional<bool>` and the emitter routes it through the EXISTING `body_builder` char overload as `args.x ? 'Y' : 'N'` — NOT through the int64 path (which would serialize `1`/`0`, invalid FIX; e.g. `LocateReqd(114)` on NewOrderSingle). No change to the frozen `body_builder`. (b) **Length+Data pairs** (e.g. `EncodedTextLen(354)`/`EncodedText(355)`): modeled as ONE coupled `<Msg>Args` member (the Data value), with the emitter auto-deriving the Length field from the Data at emit time — not two independent optionals a caller can desync. Binary `Data` containing control bytes/SOH is out of scope for v1.0 (see Out of Scope + B&L): `body_builder`'s string path fail-closes on control bytes, and it exposes no arbitrary-bytes API; clean-text Data (no `0x01`) is supported via the string path.
- **FR-008**: The round-trip witness harness MUST be non-tautological: exemplar rows anchored against QuickFIX goldens; generated bulk rows additionally asserting byte-level structure (field order matches the seed table, SOH count, absence of framing tags) — a pure build→parse loop is insufficient.
- **FR-009**: The feature MUST NOT change the read path, wire-format semantics, codegen read-layout, the C-ABI, or the Python surface, and MUST NOT add any new `fixpp_error_t` value (the C-ABI is GA-frozen at 1.5.0).
- **FR-010**: Codegen output MUST be regenerated via the forced-regeneration path and pass the codegen build-graph cleanliness gate (`git`-clean after regen); generated builder headers ship as a new generated include surface.

### Key Entities

- **Generated builder** — a per-message free function `build_<Msg>(out, const <Msg>Args&)` over `body_builder`; inputs are a value aggregate `<Msg>Args` (one `std::optional<T>` per direct scalar field; a group span per repeating group), NOT a setter/open-group object.
- **`writer_traits<Msg>`** — the per-message binding that associates a message with its emitted top-level body required-presence table (and the per-occurrence group required tables reachable from it) so the generic `validate()` routine can locate the required-field set at every level.
- **Top-level body required-presence table** — new per-message table this feature emits: `group_no_tag==0`, `rule==Required`, header/framing tags `{8,9,10,34,35,49,52,56}` excluded. Distinct from the existing header-polluted, level-flattened `Validator.hpp` `<Msg>_rules` (which this feature does NOT reuse).
- **Per-occurrence group required table** — new per-message, per-group-occurrence required-field table this feature emits (fields in THAT message's run with `group_no_tag==<group>`, `rule==Required`), giving the validator recursive group-entry required-presence. Derived per message (not one version-wide table per `no_tag`): the same `no_tag` (e.g. `NoMDEntries(268)`) carries a different required set in W vs X.
- **Shape-oracle** — the frozen 061 reference (5 exemplar builders + `body_builder` + QuickFIX goldens) the generated exemplars must reproduce byte-for-byte.
- **Round-trip seed table** — the constexpr `{msg, version, [(tag, seed)…], group-shape}` driving the parameterized witness harness.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All **33 OFFICIAL distinct MsgTypes** have a generated builder (exact-set completeness gate over the generated MsgType→builder registry passes — keyed by MsgType wire string, multi-char `AF/AC/AG/AS` included; not by `build_*` identifier symbol names).
- **SC-002**: For **all 5** exemplar MsgTypes, generated-vs-hand byte-equality AND generated-vs-QuickFIX-golden match hold (headline pin green).
- **SC-003**: Every generated OFFICIAL builder passes its seeded round-trip witness (each seeded field, including nested group entries, reads back at its exact input value) with the non-tautological byte-structural asserts.
- **SC-004**: Required-presence `validate()` fires at the levels each message actually has (not universally — many of the 33 are group-free): (a) **every message with a required top-level field** rejects that field's absence with `wire_required_field_missing` (representative: D NewOrderSingle, missing `Symbol(55)`); (b) **every message with a repeating group carrying a required entry field** rejects that field's absence at group depth (representatives: E NewOrderList missing `ClOrdID(11)` in a `NoOrders` entry; W vs X on the per-occurrence `NoMDEntries` required set). A fully-populated message validates clean; the required set is per-message/per-occurrence table-derived at both levels (zero hand-authored required lists). `commit()` output is unaffected by `validate()` (byte-identical to exemplars).
- **SC-005**: Read path, wire semantics, C-ABI, and Python surface are unchanged (existing suites and the C-ABI 1.5.0 freeze byte-identical); no new error enum value.
- **SC-006**: Forced codegen regeneration is `git`-clean; the codegen build-graph cleanliness gate passes.

## Assumptions

- **Validation scope is required-presence ONLY** (user decision 2026-07-10), checked **recursively** (top-level + group-entry). Enum-value-domain checks and conditional-required predicates are **cut for v1.0** — they are unbacked by the existing tables (which carry only a per-tag presence marker) and would require new `emit_enums`/predicate work. Where a specific enum must be enforced, it is hand-validated per message (020 precedent). NO `emit_enums`/enum-domain work in this feature. The one permitted table addition is the per-message, per-occurrence group required table (FR-006a); it reuses existing group-member presence from the message's own IR field run (no IR addition for the required tables — they are order-independent), and keeps it to required-presence only. (The group **delimiter/member order** — FR-007 — separately DOES add a codegen-tool-local `MessageIR.group_order`, RC#7/R9; that is host-tool-only and leaves the runtime/C-ABI unchanged.)
- **One representative namespace ships** (v44 for the exemplar pins; the OFFICIAL set per the plan's representative-version choice). All-version coverage (FR-015b) is a later demand-driven flag-flip once the emitter is version-agnostic — out of scope here.
- **The 061 shape-oracle is frozen and authoritative.** Its 5 exemplars + goldens + `shape_oracle_profile()` are reused as-is; this feature does not re-derive them.
- **`body_builder` is the single serialization core.** Its buffer/allocation policy (16384 B arena / 3800 B body cap, null-upstream fail-closed) is settled and inherited unchanged.
- **The `<Msg>_rules` tables are already emitted and currently unconsumed** (verified by grep); this feature is their first consumer.

## Out of Scope

- **FR-015b all-version coverage** (4.2/5.0SP2/FIXT.1.1 etc.) — post-v1.0; this feature makes the emitter version-agnostic but ships only the representative namespace.
- **Enum-domain and conditional-required validation** / any `emit_enums` / enum-domain dictionary / IR work — cut (see Assumptions). (Required-presence tables, including the new per-occurrence group required tables, ARE in scope per FR-006a.)
- **Binary Data-field content containing control bytes / SOH** (e.g. `RawData(96)`, `SecureData`, binary `EncodedText(355)`) — cut for v1.0 (FR-007a). `body_builder` exposes no arbitrary-bytes API and its string path fail-closes on `0x01`. Clean-text Length+Data (no control bytes) IS supported (length auto-derived). Recorded as a Behaviors & Limitations row; binary-payload support is a v1.x follow-on if demand-driven. (Decision, not an open question.)
- **Typed C-ABI / Python exposure** — stays generic tag-based and GA-frozen; typed Python stubs are a v1.x follow-on with zero ABI impact.
- **N-002 / N-003** (UserRequest / ApplicationMessageRequest family) — need session-FSM dispatch, a distinct work class; deferred.
- **Deferred families** C-001..003, R-001..005, P-004..008, N-001 — v1.x; per the Orchestra/FIX-Latest direction, family breadth becomes an emitter/Orchestra output, not hand-staged work.
- **Unifying the 3 outbound serializers** (020 helpers, C-ABI `OutboundAccumulator`, typed builders) into a single core — tracked v1.x debt.

## Dependencies

- **061-slim** (PR #178) — `wire::body_builder`, 5 exemplar builders, `contracts/builder-shape-oracle.md`, `shape_oracle_profile()`, QuickFIX goldens. **DONE.**
- **062 / 063 / 065 / 066** — grouped/nested typed **read** correctness (round-trip read-back side of the witness). **DONE.**
- **057** — reify + multi-char MsgType dispatch (35=AS and any multi-char OFFICIAL type parse back). **DONE.**

## Normative References

Per Constitution Article VI §5, the exact catalogue/spec references (sourced from `spec/coverage-index.md`) that inform this spec. The write-coverage rows this feature advances (33 OFFICIAL distinct MsgTypes over the FIX 4.4 application dictionary, `[FIX44]` per the coverage-index DocAbbrev registry):

**Application — Trade (`[FIX44]`, catalogue A-001..A-013):**
- A-001 `[FIX44] NewOrderSingle (35=D)` · A-002 `NewOrderList (35=E)` · A-003 `OrderCancelRequest (35=F)` · A-004 `OrderCancelReplaceRequest (35=G)` · A-005 `OrderStatusRequest (35=H)` · A-006 `ExecutionReport (35=8)` · A-007 `OrderCancelReject (35=9)` · A-008 `OrderMassCancelRequest (35=q)` · A-009 `OrderMassCancelReport (35=r)` · A-010 `OrderMassStatusRequest (35=AF)` · A-011 `MultilegOrderCancelReplace (35=AC)` · A-012 `CrossOrderCancelReplaceRequest (35=t)` · A-013 `CrossOrderCancelRequest (35=u)`.

**Application — Market Data & Quote (`[FIX44]`, catalogue M-001..M-012):**
- M-001 `[FIX44] MarketDataRequest (35=V)` · M-002 `MarketDataSnapshotFullRefresh (35=W)` · M-003 `MarketDataIncrementalRefresh (35=X)` · M-004 `MarketDataRequestReject (35=Y)` · M-005 `SecurityDefinitionRequest (35=c)` / `SecurityDefinition (35=d)` · M-006 `SecurityStatusRequest (35=e)` / `SecurityStatus (35=f)` · M-007 `TradingSessionStatusRequest (35=g)` / `TradingSessionStatus (35=h)` · M-008 `MassQuote (35=i)` / `MassQuoteAcknowledgement (35=b)` · M-009 `Quote (35=S)` · M-010 `QuoteRequest (35=R)` / `QuoteRequestReject (35=AG)` · M-011 `QuoteCancel (35=Z)` · M-012 `QuoteStatusRequest (35=a)`.

**Application — Post-Trade (`[FIX44]`, catalogue P-001..P-003):**
- P-001 `[FIX44] AllocationInstruction (35=J)` · P-002 `AllocationInstructionAck (35=P)` · P-003 `AllocationReport (35=AS)`.

**Design authority (not FIX-spec sections; the frozen contracts this feature reproduces):**
- `[061] builder-shape-oracle.md` C1–C6 — `wire::body_builder`, the 5 exemplar builders (D/8/9/E/AS), `shape_oracle_profile()`, QuickFIX goldens (the byte-equality reference).
- `[063] nested-group-parse-correctness` — the per-context group-membership pattern; this feature is its codegen-time write analogue (per-message group plans; see FR-006a/FR-007).
- `[FIX44] MDFullGrp/MDIncGrp` — the `NoMDEntries(268)` W-vs-X delimiter/required divergence (`dictionaries/FIX44.xml:3023`, `:3060`) that mandates the per-occurrence model.

**Constitutional / process authority:** `[const §VI]` (100% FIX rule + Normative References), `[const §X]` (ABI freeze — FR-009), `[const §XVI.3]`/`[const §XVII]` (clarify + Gate A/B controls).
