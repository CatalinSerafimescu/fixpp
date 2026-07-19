# Feature Specification: Strict-Validation-Path Residual Closeout

**Feature Branch**: `081-strict-validation-residuals`

**Created**: 2026-07-19

**Status**: Draft

**Input**: Resolve two tracked residuals of feature 079 (fixpp#201) on the opt-in runtime dictionary-driven strict inbound-validation path — GitHub #203 (FIXT two-dictionary header/trailer resolution, L-041-2) and GitHub #205 (adopt QuickFIX group-gating for per-group required members; supersedes waiver W-204-1).

## Context

Both concerns live **only** on the opt-in strict inbound-validation path (`SessionConfig::validate_inbound_messages`). When that flag is **off (the default)**, no full-frame validation runs and behavior is a byte-identical no-op — so both concerns affect only deployments that have deliberately enabled strict inbound validation. Both are confined to the runtime validator / dictionary-loader / `table_view` path; neither touches the C-ABI (frozen `1.5.0`) or the wire encode/parse path.

- **Concern A (#203 / L-041-2)** — The FIX50/FIX50SP1/FIX50SP2 application dictionaries ship an **empty `<header/>`**: the FIXT.1.1 session/transport layer owns the standard header (tags 8/9/34/49/52/56) and trailer (tag 10). Because those tags are never registered valid for any FIX50SPx message, full-frame `validate()` rejects **every** inbound application frame at its first step with an unexpected-tag error on tag 8, before the required-field / enum / group-structure checks ever run. The dictionary-*derivation* tier (required-field set, enum domain, group membership) is already correct for FIX50SPx — only full-frame header/trailer tag **acceptance** is blocked.
- **Concern B (#205)** — Feature 079 introduced the first per-instance group-required-member check using **"required-once-present"** semantics: a member declared `required='Y'` is enforced once its group *instance* is present, regardless of whether the enclosing group *usage* is optional. QuickFIX instead gates a direct member's required-ness on the enclosing group's own `required=` attribute. The two agree on 29,223 / 29,247 (99.92%) group contexts; the 24 disagreements are all fixpp strict-supersets (fixpp over-requires, never under-requires). User decision (2026-07-19): adopt QuickFIX group-gating (issue #205 Option 1) for exact parity.

## Clarifications

### Session 2026-07-19

- Q: Concern A (#203) — how should a FIX50SPx application frame obtain the FIXT.1.1-owned header/trailer tags at strict-validation time? → A: Merge at load — merge the vendored FIXT.1.1 standard header/trailer into each empty-`<header/>` application dictionary's validation view at load time; `validate()` runs a single pass against the merged view (pure dictionary/`table_view` layer, no session/DefaultApplVerID plumbing, no config key).
- Q: Concern A (#203) — how deep should header/trailer validation go once the tags are accepted? → A: Accept-only — stop rejecting the FIXT header/trailer tags as unexpected and let the type/enum checks that already run generically apply to the now-known header fields; do NOT add new message-level "header field required" enforcement (34/49/52/56 presence) in the dictionary validator (session-layer FSM governs SeqNum/CompID).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - FIX50SPx application frames validate under strict inbound validation (Priority: P1)

An operator runs a FIXT session negotiated to a FIX 5.0 / 5.0SP1 / 5.0SP2 application version and enables strict inbound validation. Inbound application messages (NewOrderSingle, ExecutionReport, TradeCaptureReport, market-data snapshots, …) carry the standard header (8/9/34/49/52/56) and trailer (10) owned by the FIXT.1.1 session layer. Today every such frame is rejected on the first header tag; this story makes conforming frames validate and pass through to the required-field / enum / group-structure checks.

**Why this priority**: This is a complete functional break — with strict validation on, *no* FIX50SPx application traffic is accepted at all. It is the highest-severity residual and blocks any real FIX50SPx strict-validation deployment. A per-release QuickFIX interop gate would trip on it.

**Independent Test**: Load the vendored `FIX50SP2.xml`, enable strict validation, feed a well-formed FIX50SP2 application frame (e.g. TradeCaptureReport), and assert it is **accepted** where it is currently rejected with an unexpected-tag error on tag 8.

**Acceptance Scenarios**:

1. **Given** strict inbound validation is enabled for a FIX50SP2 application session, **When** a well-formed application frame carrying the FIXT-owned standard header/trailer arrives, **Then** the standard header tags (8/9/34/49/52/56) and trailer tag (10) are accepted and the frame proceeds to the required-field / enum / group-structure checks (no unexpected-tag rejection).
2. **Given** the same session, **When** an application frame legitimately omits a genuinely-required application field, **Then** it is still rejected on that field (the header/trailer acceptance does not weaken the existing required-field checks).
3. **Given** a FIX50 or FIX50SP1 application session with strict validation enabled, **When** a well-formed application frame arrives, **Then** its standard header/trailer tags are accepted (parity with FIX50SP2).

---

### User Story 2 - Per-group required-member enforcement matches QuickFIX exactly (Priority: P2)

An operator with strict inbound validation enabled receives an application message that contains an **optional** repeating group whose present instance omits a member declared `required='Y'`. QuickFIX accepts this (it gates a direct member's required-ness on the enclosing group's own `required=`); fixpp today rejects it (over-strict at 24 group contexts). This story relaxes fixpp to match QuickFIX exactly, while keeping the enforcement for members of **required** groups.

**Why this priority**: Safe, bounded parity fix — fixpp only over-requires at these 24 sites (never under-requires), so today's behavior is a false-reject of a present-but-incomplete optional-group instance, consistent with the false-rejects feature 079 set out to eliminate. It affects only opt-in strict validation and cannot cause a false-accept regardless of direction.

**Independent Test**: At a representative site among the 24 divergent contexts, feed a present-but-incomplete instance of an **optional** group (a member `required='Y'` omitted) and assert it is now **accepted** (matching QuickFIX); feed a present-but-incomplete instance of a **required** group's member and assert it is still **rejected**.

**Acceptance Scenarios**:

1. **Given** strict validation is enabled, **When** an inbound message carries a present instance of an **optional** repeating group that omits a `required='Y'` direct member, **Then** the message is accepted (matching QuickFIX).
2. **Given** strict validation is enabled, **When** an inbound message carries a present instance of a **required** repeating group that omits a `required='Y'` direct member, **Then** the message is still rejected.
3. **Given** the loaded dictionaries, **When** the per-group required-member sets are compared against a QuickFIX-derived oracle across every dictionary/message/group context, **Then** they are exact-set-equal in both directions (0 divergences at the 24 previously-divergent contexts).

---

### Edge Cases

- **Non-FIXT dictionaries keep their own header.** FIX40/41/42/43/44 carry a populated `<header>`; their full-frame validation behavior MUST be unchanged (the header/trailer resolution fires only for empty-`<header/>` app dictionaries).
- **FIXT.1.1 admin/session messages.** Full-frame validation of FIXT.1.1 session messages (Logon, Heartbeat, ResendRequest, …) MUST be unchanged by the resolution mechanism.
- **`vlatest` (FIX Latest / Orchestra) is not affected by Concern A.** The Orchestra dictionary models the StandardHeader (component 1024) and StandardTrailer (1025) inline in every message, which the Orchestra loader expands into each message's field set — so the standard header/trailer tags are already valid for vlatest and it does not reproduce the tag-8 reject. Any distinct full-frame vlatest strict-validation concern is entangled with the `v50sp2` registry-slot sharing (L-074-1) and the ApplExtID(1156)=303 re-keying, which are **out of scope** here.
- **Dictionary availability for resolution.** The header/trailer source is the FIXT.1.1 session dictionary, which is the session-layer dictionary already in play for a FIXT session. The resolution MUST NOT trigger the `version_registry` collision abort (L-074-1) for the normal FIXT.1.1 + FIX50SPx pairing.
- **Required group vs optional group at the same message.** A message may contain both a required and an optional group; group-gating (Concern B) must decide required-ness per enclosing group independently.

## Requirements *(mandatory)*

### Functional Requirements

**Concern A — FIXT header/trailer resolution (#203)**

- **FR-001**: On the strict inbound-validation path, full-frame validation of a FIX50 / FIX50SP1 / FIX50SP2 application message MUST accept the FIXT.1.1-owned standard header tags (8, 9, 34, 49, 52, 56) and trailer tag (10) instead of rejecting the frame with an unexpected-tag error on the first header tag. The header/trailer definitions MUST be sourced by **merging the vendored FIXT.1.1 standard header/trailer into the application dictionary's validation view at load time** (single-pass validation against the merged view; no session/DefaultApplVerID resolution, no new configuration key).
- **FR-002**: The header/trailer resolution MUST apply **only** to application dictionaries that defer their standard header/trailer to the session layer (the empty-`<header/>` dictionaries: FIX50, FIX50SP1, FIX50SP2). Dictionaries that carry their own populated header/trailer (FIX40/41/42/43/44 and FIXT.1.1) MUST have full-frame validation behavior unchanged.
- **FR-003**: The existing FIX50SPx required-field, enum-domain, and group-structure checks MUST remain unchanged — header/trailer resolution only widens the set of tags accepted at the unexpected-tag step; it MUST NOT weaken any check that already runs.
- **FR-003a**: Resolution is **accept-only**: the merged header/trailer tags are accepted at the validator's Step-1 unexpected-tag gate (so the type/enum checks that already run generically then apply to them), but full-frame validation MUST NOT add new message-level "header field required" enforcement (e.g. presence of 34/49/52/56) — required-presence of session-owned header fields stays governed by the session-layer FSM (SeqNum/CompID), not the dictionary validator. Acceptance MUST be implemented on a validator-private surface that does NOT alter the shared valid-tag store the inbound parser reads (see FR-009). Where a merged header field has a defined structural datatype, the type check MUST apply it (a malformed numeric header field such as `34=abc` / `1156=abc` MUST be rejected, not defaulted to an unconstrained type) — no false-accept (FR-011).
- **FR-004**: `vlatest` is out of scope for Concern A (see Edge Cases) and MUST NOT be modified by the resolution mechanism.

**Concern B — QuickFIX group-gating (#205)**

- **FR-005**: A field declared `required='Y'` that is a direct member of an **optional** repeating group MUST NOT be enforced per group instance — its per-instance required-ness is gated on the enclosing group's own `required=` attribute (matching QuickFIX `addXMLGroup` group-gating) at all 24 previously-divergent contexts.
- **FR-006**: A field declared `required='Y'` that is a direct member of a **required** repeating group MUST still be enforced per group instance (a present-but-incomplete required-group instance still rejects). Concern B relaxes only the optional-group case.
- **FR-007**: The generated typed validators' per-group required checks (v44, v50sp2, vlatest — every version whose messages contain an optional group with `required='Y'` members, e.g. FIX44 PositionReport/NoUnderlyings) MUST agree with the runtime validator's group-gating after this change (no residual over-require in the codegen emitter). Any change to generated *validator* output is confined to the affected group-gating sites.

**Cross-cutting invariants**

- **FR-008**: No C-ABI change — the frozen `1.5.0` surface, its symbol golden, and abidiff baseline are untouched (no `error.h` / `version.h` / capi header edits, no symbol-golden or abidiff regeneration).
- **FR-009**: All **read/reify** goldens (v42 / v44 / v50sp2 / vt11 / vlatest) MUST stay byte-identical. Concern B changes the generated **typed validator** goldens (v44 / v50sp2 / vlatest) only at the affected group-gating sites (consistent with the runtime change); Concern A changes no golden at all (its merge is confined to a validator-private framing surface, which no golden consumes). **Parser invariant**: Concern A MUST NOT change the inbound parser's known/unknown-field classification (`unknown_fields()`) — it must be byte-identical whether strict validation is on or off — because the framing acceptance lives on a validator-private surface, not the shared valid-tag store the parser reads.
- **FR-010**: When `validate_inbound_messages` is off (the default), behavior MUST be a byte-identical no-op for both concerns.
- **FR-011**: The Article VI "100% FIX / no silent omissions" and the standing per-release QuickFIX interop parity obligations apply; neither concern may introduce a false-accept.

### Key Entities

- **Application dictionary**: The loaded `Dictionary` for a FIX version; for FIX50SPx it defers the standard header/trailer to the session layer (empty `<header/>`).
- **Session dictionary (FIXT.1.1)**: Owns the standard header/trailer tags for FIXT sessions; the source for Concern A's resolution.
- **`table_view`**: The runtime projection the validator probes — the valid-tag set, message-level required set, and per-group required-member store. Concern A adds a **validator-private FIXT framing surface** (a framing tag→datatype table read only by the validator) that leaves the parser-shared valid-tag store byte-identical.
- **Per-group required-member store**: The 079-introduced structure that records which members are required within a group context; Concern B changes how optional-group members populate it.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A well-formed FIX50SP2 application message validated standalone against the vendored `FIX50SP2.xml` with strict validation enabled is **accepted** (today it is rejected with an unexpected-tag error on tag 8). Same result for FIX50 and FIX50SP1.
- **SC-002**: Across every dictionary / message / group context, fixpp's per-group required-member set is exact-set-equal to the non-circular raw-XML group-gating oracle in both directions — **0 divergences** on the optional-group axis this feature targets (down from the 24 strict-superset contexts; the census pins oracle ≡ loaded store, both directions). **Amended at /implement (2026-07-19):** the quickfix-cpp 1.16.0 per-group parity golden (T020) additionally surfaced **3 residual stricter-superset contexts** — FIX44 / FIX50 / FIX50SP1 `MassQuote('i')` / `NoQuoteSets(296)`, where fixpp requires the nested `NoQuoteEntries(295)` count-tag but real QuickFIX does not (QuickFIX `addXMLGroup` hardcodes `componentRequired=false` for a `<component>` nested directly in a `<group>`, `DataDictionary.cpp:563` — an axis the D-3 immediate-enclosing rule never modeled). These 3 are a **named, bounded stricter-superset** (fixpp ⊇ QuickFIX = QuickFIX ∪ {295}; safe direction, **no false-accept**), of the same W-204-1 lineage; **WAIVED** (user-approved 2026-07-19) and recorded in `spec/behaviors-and-limitations.md` (T024). The parity test pins them as an exact named carve-out (goes RED if the residual grows, shrinks, or shifts).
- **SC-003**: No conforming FIX50SPx inbound application message is false-rejected for a standard header/trailer tag under strict validation.
- **SC-004**: Full-frame validation behavior for FIX40/41/42/43/44 and FIXT.1.1 is unchanged (regression pins pass), and no read/reify golden for any version changes.
- **SC-005**: With `validate_inbound_messages` off (default), the build produces byte-identical wire and validator results versus before this feature.
- **SC-006**: The C-ABI surface (`1.5.0`), its symbol golden, and abidiff baseline are unchanged.

## Assumptions

- Concern A sources the standard header/trailer by merging the **vendored FIXT.1.1 dictionary** (`dictionaries/FIXT11.xml`) into each empty-`<header/>` application dictionary at load time — a build/library-vendored source, not a runtime session-resolution or a new user-facing configuration key.
- The `v50sp2` registry-slot sharing (L-074-1) and ApplExtID(1156)=303 re-keying are **out of scope** and unchanged; the resolution mechanism must not trip the `version_registry` collision abort for the normal FIXT.1.1 + FIX50SPx pairing.
- FIX42's `INT`-typed `NumInGroup` group-count carve-out (#196 / L-066-1) is a separate feature and out of scope; Concern B operates on the group structures the loaders already materialize.
- Verification uses the established 079 tooling: RED→GREEN behavior pins, a non-circular raw-XML census (QuickFIX immediate-enclosing group-gating required sets per dictionary/message/scope, exact-set-equal against loaded tables), and a quickfix-cpp 1.16.0 parity golden.

## Normative References

Per Article VI §5, the exact coverage-index and behaviour-record entries that inform this spec. **No new OFFICIAL catalogue rows are introduced** — both concerns are correctness of existing versions on the opt-in strict-validation path; traceability is via existing rows + B&L L-rows (B&L updates only, see research.md D-6).

- **`[FIX50SP2 §3] Message validator — required fields, type conformance, enum values, group structure`** — `spec/coverage-index.md:193` (catalogue row W-014). The `required fields` / `group structure` clause is what Concern B corrects (per-instance group required-member gating brought to QuickFIX-exact); the `type conformance` clause governs Concern A's accept-only type check on the now-accepted framing tags.
- **`spec/behaviors-and-limitations.md` L-041-2** (`behaviors-and-limitations.md:1480`) — FIXT application-message validation / empty-`<header/>` tag-8 reject; the deferred residual (issue **#203**) Concern A resolves. This feature records the accept-only-vs-QuickFIX header-required divergence as named intent (research.md D-6), and updates L-041-2's status to resolved.
- **Waiver W-204-1 / issue #205** (079 / PR #204 Gate B waiver; recorded in `phases/phase-4/wire/079-required-presence-scope.md` and the CLAUDE.md last-merged pointer) — 079's per-group *required-once-present* enforcement is stricter than QuickFIX at 24/29,247 group contexts (all fixpp superset, no false-accept). Concern B **supersedes** W-204-1 by adopting QuickFIX immediate-enclosing group-gating (exact parity at all 24 contexts); a B&L L-row records the flip (research.md D-6).
- **`spec/behaviors-and-limitations.md` L-063-1 / L-066-1** (`behaviors-and-limitations.md:1695` / `:1749`) — the FIX40/41/42 `INT`-typed `NumInGroup` group-count carve-out (#196). Retained and untouched: FIX42 stays group-blind and out of scope for Concern B (Assumptions, above).
