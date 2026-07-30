# Feature Specification: Structural Repeating-Group Detection for Legacy FIX Dictionaries

**Feature Branch**: `082-structural-group-detection`

**Created**: 2026-07-29

**Status**: Draft

**Input**: Replace the datatype-gated repeating-group detection predicate (`FieldRef::type == field_data_type::NumInGroup`) with a **structural** one derived from the `<group>` element itself, at every detection site — the runtime `Dictionary::as_table_view()` registration loops and the codegen emitters — and re-instate the `fixpp::v42` typed builder tier that feature 077 had to descope. Closes GitHub **#196**; resolves **L-063-1**, **L-061-1**, **L-066-1**, **L-077-1**.

## Context

Repeating-group detection is gated on the **XML-declared field datatype** at every site in the library:

- runtime — `Dictionary::as_table_view()`, both the legacy bare-`no_tag` registration loop (`src/dictionary/dictionary.cpp:398`) and the 063 context-scoped loop (`:441`/`:446`);
- codegen — `emit_messages.cpp:166/234/337/347/425`, `emit_builders.cpp:606`, `emit_reify.cpp:217/227`, `emit_manifest.cpp:73`.

FIX 4.0 / 4.1 / 4.2 declare **every** `<group>` count field with the legacy XML type `INT`, never `NUMINGROUP`. Those three dictionaries therefore register **zero** repeating groups: the shipped `fixpp::v42` read flyweight has zero `class G_` accessors, `as_table_view()`-driven group parsing and validation are inert, and the 077/078 typed builder tier had to exclude `v42` at the codegen driver (`tools/codegen/fixpp-codegen/main.cpp`, `ir.ns != "v42"`) — because a scalar-only `build_<Msg>` would **silently omit a `required='Y'` repeating group and emit invalid FIX 4.2**, violating Article VI (100% FIX / no silent omissions).

The fix is a predicate replacement: derive group-ness from the `<group>` element, which both loaders already track independent of the field's datatype.

### Pre-spec census (evidence for the FR shape)

Comparing, per dictionary, the **type set** `{tag : the field definition's type is NUMINGROUP}` against the **structural set** `{tag : a <group name=N> element exists, N → tag}`:

| dictionary | type | struct | verdict |
|---|---:|---:|---|
| FIX40 | 0 | 4 | structural-only (+4) |
| FIX41 | 0 | 7 | structural-only (+7) |
| FIX42 | 0 | **18** | structural-only (+18) — the #196 target |
| **FIX43** | 34 | 34 | **sets DIVERGE on two tags** (US3) |
| FIX44 | 59 | 59 | equal — no-op |
| FIX50 | 69 | 69 | equal — no-op |
| FIX50SP1 | 99 | 99 | equal — no-op |
| FIX50SP2 | 507 | 507 | equal — no-op |
| FIXT11 | 1 | 1 | equal — no-op |
| Orchestra FIX Latest | 524 | 524 | equal — no-op (after codeset resolution) |

No `<group>` element in any vendored dictionary has zero members, so a member-derived structural predicate loses nothing today.

**Orchestra / `vlatest` is unaffected.** A raw-XML count initially read 523 typed vs 524 structural because tag **552 `NoSides`** is declared `type='NoSidesCodeSet'`; that codeset itself is `type='NumInGroup'`, so the loader already resolves it. The shipped 076 golden confirms it: `G_552` is present and there are 524 distinct `class G_`. This is also why byte-identity of the unaffected tiers is written below as a **requirement pinned by a golden diff**, not as an assumption — a raw-XML census is not loader truth.

### The FIX43 divergence — both cases are upstream dictionary typos

- **tag 82 `NoRpts`** is typed `NUMINGROUP` (`FIX43.xml:2596`) but is **never** a `<group>` anywhere; it is used as a plain field inside `<message name='ListStatus'>` (`FIX43.xml:728`). FIX44 types the same tag `INT` (`FIX44.xml:4095`) and likewise uses it as a plain field. Today FIX43 registers tag 82 as a **spurious zero-member group**.
- **tag 576 `NoClearingInstructions`** is typed `INT` (`FIX43.xml:4069`) but **is** a `<group required='N'>` with member `ClearingInstruction` (`FIX43.xml:1918`). FIX44 types it `NUMINGROUP` (`FIX44.xml:5637`) and declares the same group. Today FIX43 is **group-blind on a real repeating group**.

Structural detection is correct on **both**. A union predicate (`type OR structural`) would preserve the spurious tag-82 registration and is therefore rejected.

## Clarifications

*(To be populated by `/speckit-clarify`. Exactly one question is deliberately left open — it is marked inline in **FR-006**. It is a live-traffic compatibility decision on three shipping dictionaries and is not the spec author's to make.)*

## User Scenarios & Testing *(mandatory)*

### User Story 1 - FIX 4.0/4.1/4.2 repeating groups become visible (Priority: P1)

An integrator working against a FIX 4.2 counterparty loads the vendored `FIX42.xml` and reads an inbound `MarketDataSnapshotFullRefresh`. Today the `NoMDEntries(268)` repeating group is invisible: the generated `fixpp::v42` class has no typed group accessor, and a C-ABI / typed group query on what **is** a real wire-level repeating group returns `TYPE_MISMATCH`/absent rather than a membership-bounded read (L-066-1). This story makes all 18 FIX42 groups (and FIX40's 4, FIX41's 7) materialize — typed read accessors in the regenerated `v42` tier, and real group registration in `as_table_view()` for all three dictionaries.

**Why this priority**: It is the root cause. Every other story in this feature is blocked on it, and it alone converts three shipping dictionaries from group-blind to membership-correct.

**Independent Test**: Load `FIX42.xml`, call `as_table_view()`, and assert all 18 group tags are registered with their declared members (today: 0). Separately, regenerate the `v42` read tier and assert the emitted header contains typed group accessors for those 18 tags (today: 0 `class G_`).

**Acceptance Scenarios**:

1. **Given** the vendored `FIX42.xml`, **When** `as_table_view()` is built, **Then** exactly the 18 structurally-declared group tags are registered — {33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384, 386, 398, 420, 428} — each with its declared member set, and no others.
2. **Given** the vendored `FIX40.xml` / `FIX41.xml`, **When** `as_table_view()` is built, **Then** exactly their 4 / 7 structurally-declared group tags are registered.
3. **Given** a FIX 4.2 inbound `MarketDataSnapshotFullRefresh` carrying a populated `NoMDEntries(268)` group, **When** a typed or C-ABI group read is issued, **Then** it returns a membership-bounded read of the group's entries rather than `TYPE_MISMATCH`/absent.
4. **Given** the regenerated `v42` read tier, **When** the nested `MassQuote` structure is inspected, **Then** the `NoQuoteSets(296) → NoQuoteEntries(295)` nesting is expressed as a nested typed group (5 nested group occurrences exist in FIX42 messages), not flattened.
5. **Given** the six dictionaries whose type set and structural set are equal (FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT11 / Orchestra FIX Latest), **When** `as_table_view()` is built, **Then** the registered group set is **exactly** what it was before this feature.

---

### User Story 2 - The `fixpp::v42` typed builder tier is re-instated (Priority: P1)

A developer constructing outbound FIX 4.2 messages wants the same typed `build_<Msg>` / `validate_<Msg>` / `<Msg>Args` surface that `v44`, `v50sp2`, and `vlatest` already ship (077/078). Today `v42` is excluded at the codegen driver, so the only option is the untyped runtime `wire::body_builder` path. With groups materialized (US1), `v42` can emit a builder tier whose `Args` carry its repeating groups — so a `required='Y'` group can no longer be silently omitted.

**Why this priority**: This is the deliverable issue #196 actually asks for. It is the user-visible outcome; US1 is the enabling mechanism.

**Independent Test**: Run the codegen driver without the `v42` exclusion and assert the 078 split builder/validator file set is emitted for `v42`; then build a `NewOrderList` via `build_NewOrderList` with a populated `NoOrders(73)` group and assert the emitted bytes match a QuickFIX-derived golden.

**Acceptance Scenarios**:

1. **Given** the codegen driver, **When** it runs over `FIX42.xml`, **Then** it emits the full 078 split builder/validator layout for `v42` (`messages/<Msg>.hpp`, `groups/<PlanName>.hpp` + `groups.hpp`, `validators/traits.hpp`, `all.hpp`) covering its application messages — 21 of the 39 carry at least one repeating group.
2. **Given** a `v42` message whose group is declared `required='Y'` (14 such message/group pairs across 12 messages, e.g. `NewOrderList`/`NoOrders`, `MarketDataRequest`/`NoRelatedSym`, `MassQuote`/`NoQuoteSets`), **When** `validate_<Msg>` runs on an `Args` value that omits that group, **Then** it **rejects** — the group is representable in `Args`, so its absence is detectable rather than silent.
3. **Given** a populated `v42` `Args` value, **When** `build_<Msg>` emits, **Then** the wire bytes match an independently-derived (QuickFIX) golden for that message, groups included.
4. **Given** the `v42` builder tier, **When** the per-version completeness census runs, **Then** every application message and every declared group plan is accounted for, with **no** silently-skipped message or group.

---

### User Story 3 - FIX43's two mis-typed group-count tags are corrected (Priority: P2)

An operator running a FIX 4.3 session under dictionary-backed validation is affected by two latent dictionary-typo defects that the predicate change necessarily resolves: a **real** repeating group (`NoClearingInstructions(576)`) is currently unregistered and therefore group-blind, and a **plain field** (`NoRpts(82)`) is currently registered as a spurious zero-member group.

**Why this priority**: It is unavoidable once the predicate changes — it cannot be deferred to a later feature — but it is a discovered latent-defect fix, independent of what #196 asks for. It is separated so its behavior deltas and pins are visible in their own right and are not buried under the `v42` story.

**Independent Test**: Load `FIX43.xml`, build `as_table_view()`, and assert tag 82 is **not** registered as a group while `ListStatus` still reads tag 82 as a plain required field; and assert tag 576 **is** registered as a group with member `ClearingInstruction`.

**Acceptance Scenarios**:

1. **Given** the vendored `FIX43.xml`, **When** `as_table_view()` is built, **Then** tag **82** is **not** registered as a repeating group (it is registered today).
2. **Given** the same view, **When** a `ListStatus` message carrying tag 82 is validated, **Then** tag 82 is still accepted and enforced as a **plain required field** — removing the spurious group registration must not make the field unknown or optional.
3. **Given** the same view, **When** tag **576** is queried, **Then** it **is** registered as a repeating group with member `ClearingInstruction` (it is not registered today).
4. **Given** a FIX 4.3 inbound message carrying a populated `NoClearingInstructions(576)` group, **When** a typed or C-ABI group read is issued, **Then** it returns a membership-bounded read rather than `TYPE_MISMATCH`/absent.
5. **Given** FIX43's remaining 33 group tags, **When** `as_table_view()` is built, **Then** their registration is unchanged (only the two named tags move).

---

### User Story 4 - A grouped/nested FIX 4.2 write exemplar becomes expressible (Priority: P3)

The exemplar suite (feature 061) has no FIX 4.2 grouped or nested **write** exemplar — all five exemplars are forced to `fixpp::v44` because `v42` emits zero typed groups (L-061-1). With US1 + US2 delivered, a grouped market-data write becomes expressible in `v42`.

**Why this priority**: It is the demonstration that closes L-061-1, and the concrete evidence that the builder tier is usable rather than merely emitted. It depends on US1 and US2 and delivers no value without them, so it is sequenced last.

**Independent Test**: Add a `v42` exemplar that constructs a nested-group message — `MassQuote` with `NoQuoteSets(296)` containing `NoQuoteEntries(295)` — and assert its bytes against an independently-derived golden plus a read round-trip.

**Acceptance Scenarios**:

1. **Given** the `v42` builder tier, **When** a `MassQuote` with a populated `NoQuoteSets(296) → NoQuoteEntries(295)` nesting is constructed and emitted, **Then** the wire bytes match an independently-derived golden.
2. **Given** those emitted bytes, **When** they are parsed back through the `v42` read tier, **Then** every field and both group levels round-trip field-for-field.

---

### Edge Cases

- **A group-count tag reused across contexts with different member sets.** The 063 context-scoped registration keys per `(message, parent path, no_tag)`. Newly-visible FIX40/41/42 group tags (e.g. FIX42's `LinesOfText(33)`, used by both `Email` and `News`) must register per context, not collapse to a single bare-`no_tag` entry — and the legacy bare-`no_tag` loop and the context-scoped loop must move **together** (a half-restructure would leave the two views disagreeing).
- **A `<group>` element with zero members.** None exists in any vendored dictionary today, but a structural predicate derived from a group's *members* would be blind to one. The chosen predicate must either be member-independent or fail closed rather than silently drop such a group.
- **A tag that is a group count in one dictionary and a plain field in another.** Tags 82 and 576 are exactly this across FIX43/FIX44. Detection must be resolved **per dictionary**, never globally by tag.
- **The 063 census helper is built on the predicate being changed.** `tests/dictionary/reused_tag_census.hpp:74,80` established the L-063-1 carve-out using `fr.type == NumInGroup`. If it is simply flipped to the new predicate it moves in lockstep with the code under test and witnesses nothing — it must be re-pointed to an independent source.
- **`vt11` (FIXT.1.1) has one group and zero application messages.** It must stay self-skipping in the builder tier (it is excluded by an empty registry, not by a version predicate) and its read golden must not move.
- **Nested group depth.** FIX42 has 5 nested group occurrences. The emitter's existing depth bound and its deterministic group-emission ordering must hold for the newly-visible legacy groups.

## Requirements *(mandatory)*

### Functional Requirements

**Core — structural detection**

- **FR-001**: Repeating-group detection MUST be derived from the dictionary's **structural** group declaration (the `<group>` element / the loader's group table), **not** from the group-count field's declared datatype, at **every** detection site: the `as_table_view()` legacy bare-`no_tag` registration loop, the `as_table_view()` 063 context-scoped registration loop, and the codegen emitters (`emit_messages`, `emit_builders`, `emit_reify`, `emit_manifest`). No site may retain the datatype gate.
- **FR-002**: The predicate MUST be a **replacement**, not a union with the datatype gate. A tag that is declared `NUMINGROUP` but is never a `<group>` MUST NOT be registered as a group (FIX43 tag 82); a tag that is a `<group>` but is declared `INT` MUST be registered (FIX43 tag 576, and every FIX40/41/42 group tag).
- **FR-003**: Detection MUST be resolved **per dictionary**. The same tag may be a group count in one dictionary and a plain field in another; no global tag-keyed group set may be introduced.
- **FR-004**: The two `as_table_view()` registration loops (legacy bare-`no_tag` and 063 context-scoped) MUST be changed **together** and remain mutually consistent — no configuration or code path may leave one structural and the other datatype-gated.
- **FR-005**: For FIX40 / FIX41 / FIX42, `as_table_view()` MUST register exactly the structurally-declared group set — 4 / 7 / 18 tags respectively — with each group's declared member set, in place of today's empty registration.

**Inbound-validation strictness (open)**

- **FR-006**: Registering groups on FIX40/41/42 also activates `as_table_view()`-driven group-membership validation in `wire::Validator` and the 079 per-group required-scope path (the loaders push group-scoped required pairs for `required='Y'` members inside groups, and FIX42's groups have them — 14 message/group pairs). A FIX 4.0/4.1/4.2 session with strict inbound validation enabled can therefore begin **rejecting inbound messages it previously accepted**. [NEEDS CLARIFICATION: is this strictness accepted as-is for FIX40/41/42 sessions, or must the newly-activated group-membership / per-group required-member enforcement be gated (e.g. behind an opt-in) so existing FIX40/41/42 deployments see no new rejects? — deliberately deferred to `/speckit-clarify`; it is a live-traffic compatibility decision on three shipping dictionaries.]

**`v42` typed builder tier (issue #196)**

- **FR-007**: The codegen driver's `v42` builder-tier exclusion (`ir.ns != "v42"`) MUST be removed, and `v42` MUST emit the full 078 split builder/validator layout — per-message declaration headers, the shared per-plan groups region plus its umbrella, the validator traits header, and the `all.hpp` umbrella — on the same terms as `v44` / `v50sp2` / `vlatest`.
- **FR-008**: Every `v42` `<Msg>Args` MUST represent the message's declared repeating groups. `validate_<Msg>` MUST reject an `Args` value that omits a group declared `required='Y'` — no group may be unrepresentable in `Args` and therefore silently omitted (Article VI).
- **FR-009**: `v42` builder output MUST be pinned by new goldens in the 078 golden region (the split file set plus the `--families official` pinned golden), wired into the codegen golden-matching test, on the same terms as the existing versions.
- **FR-010**: `vt11` MUST remain excluded from the builder tier by its **empty application-message registry** (self-skip), not by any version-name predicate. The removal of the `v42` predicate MUST NOT introduce a replacement version-name predicate for any version.

**FIX43 corrections**

- **FR-011**: `as_table_view()` for FIX43 MUST NOT register tag **82 `NoRpts`** as a repeating group, while `ListStatus` MUST continue to accept and enforce tag 82 as a plain **required** field.
- **FR-012**: `as_table_view()` for FIX43 MUST register tag **576 `NoClearingInstructions`** as a repeating group with member `ClearingInstruction`, making its inbound reads membership-bounded.
- **FR-013**: FIX43's other 33 group registrations MUST be unchanged. Exactly two tags move.

**Non-regression**

- **FR-014**: For the six dictionaries whose type and structural sets are equal (FIX44, FIX50, FIX50SP1, FIX50SP2, FIXT11, Orchestra FIX Latest), the registered group set from `as_table_view()` MUST be **exactly** unchanged in both directions (set equality, not containment).
- **FR-015**: The `v44`, `v50sp2`, and `vt11` **read** goldens and the `vlatest` read golden MUST stay **byte-identical**, and the existing `vlatest` / `v50sp2` / `v44` **builder** goldens MUST stay byte-identical. This MUST be demonstrated by an actual regeneration + golden diff, not asserted from a source-level census.
- **FR-016**: The `v42` read tier regenerates: its `Messages.hpp` golden goes from 0 to 18 group classes, and its `Manifest.txt` and `Reify.hpp` outputs move correspondingly. Every count that moves MUST carry a **by-construction** explanation reconciling the emitted delta to the dictionary's declared structure — "golden regenerated" alone is not sufficient evidence.
- **FR-017**: No C-ABI change. The frozen `1.5.0` surface, its symbol golden, and the abidiff baseline are untouched.
- **FR-018**: The 063 reused-tag census helper MUST be re-pointed to a source **independent** of the detection predicate under change (e.g. the raw dictionary XML), so that it can witness the FIX40/41/42 and FIX43 deltas rather than moving in lockstep with the code it checks.

**Documentation**

- **FR-019**: `spec/behaviors-and-limitations.md` MUST record the resolution of **L-063-1**, **L-061-1**, **L-066-1**, and **L-077-1**, and MUST record the FIX43 tag-82/tag-576 corrections as a named behavior with their evidence.

### Key Entities

- **Dictionary group table**: The per-dictionary structural record of declared `<group>` elements — group tag, member list, parent group — populated by both loaders directly from the `<group>` / `<fixr:group>` element, independent of the count field's declared datatype. This is the intended source of truth for detection.
- **`FieldRef::type`**: The field's declared datatype. Today it doubles as the group-ness marker; after this feature it carries datatype only.
- **`table_view`**: The runtime projection the validator and typed reads probe, holding the registered group set (both the bare-`no_tag` and the 063 context-scoped views) and the per-group required-member store.
- **Codegen IR / emitters**: The build-time representation from which the per-version read, reify, manifest, and builder/validator outputs are emitted; each currently re-derives group-ness from `FieldRef::type`.
- **Golden corpus**: The checked-in per-version emitted outputs — the 003 read goldens, the 076 `vlatest` read golden, and the 078 split builder/validator golden set — that pin byte-identity for unaffected versions and record the intended delta for `v42`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `as_table_view()` registers exactly **4 / 7 / 18** repeating groups for FIX40 / FIX41 / FIX42 (today **0 / 0 / 0**), each with its declared member set, verified against a non-circular raw-XML oracle by exact-set equality in both directions.
- **SC-002**: For FIX44, FIX50, FIX50SP1, FIX50SP2, FIXT11, and Orchestra FIX Latest, the registered group set is exact-set-equal to the pre-feature set in both directions — **0 additions, 0 removals**.
- **SC-003**: The FIX43 registered group set differs from the pre-feature set by **exactly two** tags: 82 removed, 576 added. `ListStatus` still enforces tag 82 as a plain required field, and a populated `NoClearingInstructions(576)` group reads membership-bounded.
- **SC-004**: The regenerated `v42` read tier contains typed group accessors for all **18** FIX42 group tags (today **0**), including the `296 → 295` nesting, and every emitted-count delta in its `Messages.hpp` / `Manifest.txt` / `Reify.hpp` is reconciled by construction to FIX42's declared structure.
- **SC-005**: Regeneration produces **byte-identical** `v44`, `v50sp2`, `vt11`, and `vlatest` read goldens and **byte-identical** existing `v44` / `v50sp2` / `vlatest` builder goldens — demonstrated by an actual diff over regenerated output.
- **SC-006**: `fixpp::v42` ships a typed builder tier covering its **39** application messages, with repeating groups represented in `Args` for the **21** that declare them; `validate_<Msg>` rejects omission of a `required='Y'` group at all **14** message/group pairs that declare one.
- **SC-007**: A `v42` grouped **and** nested write exemplar (`MassQuote`, `NoQuoteSets(296) → NoQuoteEntries(295)`) emits bytes matching an independently-derived golden and round-trips field-for-field through the `v42` read tier.
- **SC-008**: A FIX 4.2 inbound message carrying a real wire-level repeating group returns a membership-bounded typed / C-ABI group read where it previously returned `TYPE_MISMATCH` or absent.
- **SC-009**: The C-ABI surface (`1.5.0`), its symbol golden, and the abidiff baseline are unchanged.
- **SC-010**: `spec/behaviors-and-limitations.md` carries no remaining open L-063-1 / L-061-1 / L-066-1 / L-077-1 carve-out, and the FIX43 corrections are recorded with their evidence.

## Assumptions

- The structural group declaration is already tracked by **both** loaders (the QuickFIX-schema XML loader and the Orchestra loader) independent of the count field's datatype, so no new dictionary parsing is introduced — detection is re-pointed at data that already exists. Whether the emitters consume it via an additive read accessor or via an existing IR-local derivation is a design decision for `/speckit-plan`.
- Only **FIX42, FIX44, FIX50SP2, and FIXT11** are code-generated (`cmake/Codegen.cmake`). FIX40, FIX41, FIX43, FIX50, and FIX50SP1 are runtime dictionaries only, so this feature's codegen/golden impact is confined to **`v42`**; FIX40/41/43 impact is runtime-`as_table_view()`/validator only, with no golden to regenerate and therefore a need for **direct** regression pins.
- Byte-identity of unaffected tiers is treated as a **requirement pinned by regeneration diff**, never as an assumption — the `NoSidesCodeSet` finding above is the standing evidence that a raw-XML census does not equal loader truth.
- Verification uses the established tooling of the preceding dictionary features: non-circular raw-XML oracles compared by exact-set equality in both directions, RED→GREEN behavior pins, QuickFIX-derived wire goldens, and the codegen golden-matching + determinism tests. `tools/codegen/**` is touched, so the codegen test label is in scope for local verification.
- The strictness question in FR-006 is the only open scope decision; every other requirement here is settled by the census evidence. It is deferred to `/speckit-clarify` by explicit user instruction.

## Normative References

Per Article VI §5, the coverage-index and behaviour-record entries that inform this spec. **No new OFFICIAL catalogue rows are introduced** — this feature makes existing FIX 4.0/4.1/4.2 (and FIX 4.3) coverage correct rather than adding new spec-section coverage; traceability is via existing rows plus B&L L-row closures.

- **`[FIX50SP2 §3] Message validator — required fields, type conformance, enum values, group structure`** — `spec/coverage-index.md:189` (catalogue row W-014). The `group structure` clause is what this feature makes real for FIX40/41/42 and corrects for FIX43.
- **`[FIX42 §] MarketDataSnapshotFullRefresh`** — `spec/coverage-index.md:239` (row M-002, Application Messages — Market Data, FIX 4.2+). Currently satisfied only via `v44` codegen; US1/US4 extend it to `v42`.
- **`[FIX42 §] NewOrderList`** — `spec/coverage-index.md:307` (row A-002). Its "full-field + all-version coverage deferred" note is what US2 discharges for `v42`.
- **`spec/behaviors-and-limitations.md` L-063-1** (`behaviors-and-limitations.md:1695`) — the root-cause record: FIX40/41/42 type group-count fields `INT`, so `as_table_view()` and the codegen emitter register zero groups. Its own stated fix ("relax group detection to structural") is FR-001. **Resolved by this feature.**
- **`spec/behaviors-and-limitations.md` L-061-1** (`behaviors-and-limitations.md:1736`) — no `v42` grouped/nested write exemplar is expressible; all five exemplars forced to `v44`. **Resolved by US4.**
- **`spec/behaviors-and-limitations.md` L-066-1** (`behaviors-and-limitations.md:1749`) — FIX 4.0/4.1/4.2 sessions are strict-but-group-blind under dict-backed inbound parsing; 066's membership-correctness claims are scoped to the six group-registering dictionaries. **Resolved by US1**, which also widens that scope note.
- **`spec/behaviors-and-limitations.md` L-077-1** (`behaviors-and-limitations.md:1817`) — no typed `build_<Msg>` for `fixpp::v42`, descoped from 077 and tracked as issue **#196**, blocked on the L-063-1 fix. **Resolved by US2.**
- **`spec/behaviors-and-limitations.md` B-077-1** — the 077 structural-key safety pin: a new structural variant of an existing group tag is absorbed as a new ordinaled `Args` plan and surfaces in the builder golden diff, never a silent mis-share. `v42`'s 18 newly-visible groups enter the builder tier under this same guarantee.
