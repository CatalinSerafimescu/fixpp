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
| **FIX43** | 34 | 34 | **sets DIVERGE on two tags**; effective delta **+576 only** (US3) |
| FIX44 | 59 | 59 | equal — no-op |
| FIX50 | 69 | 69 | equal — no-op |
| FIX50SP1 | 99 | 99 | equal — no-op |
| FIX50SP2 | 507 | 507 | equal — no-op |
| FIXT11 | 1 | 1 | equal — no-op |
| Orchestra FIX Latest | 524 | 524 | equal — no-op (after codeset resolution) |

No `<group>` element in any vendored dictionary has zero members, so a member-derived structural predicate loses nothing today.

This table is reproducible: `contracts/predicate_census.py` is the checked-in, non-circular raw-XML oracle that produced it (it loads neither `Dictionary`/`table_view` nor the codegen IR, so it is independent of the predicate under change). FR-018 requires it to become the source the 063 census helper reads.

**Orchestra / `vlatest` is unaffected.** A raw-XML count initially read 523 typed vs 524 structural because tag **552 `NoSides`** is declared `type='NoSidesCodeSet'`; that codeset itself is `type='NumInGroup'`, so the loader already resolves it. The shipped 076 golden confirms it: `G_552` is present and there are 524 distinct `class G_`. This is also why byte-identity of the unaffected tiers is written below as a **requirement pinned by a golden diff**, not as an assumption — a raw-XML census is not loader truth.

### The FIX43 divergence — both cases are upstream dictionary typos

- **tag 576 `NoClearingInstructions`** is typed `INT` (`FIX43.xml:4069`) but **is** a `<group required='N'>` with member `ClearingInstruction` (`FIX43.xml:1918`). FIX44 types it `NUMINGROUP` (`FIX44.xml:5637`) and declares the same group. Today FIX43 is **group-blind on a real repeating group** — this is a live defect and the one effective FIX43 delta.
- **tag 82 `NoRpts`** is typed `NUMINGROUP` (`FIX43.xml:2596`) but is **never** a `<group>` anywhere; it is used as a plain field inside `<message name='ListStatus'>` (`FIX43.xml:728`). FIX44 types the same tag `INT` (`FIX44.xml:4095`) and likewise uses it as a plain field. The datatype gate *nominates* tag 82 as a group — but **both registration stores already reject it downstream**: the legacy bare store via `group_first_field(82) == 0` (`dictionary.cpp:402`; `group_first_field_impl` binary-searches the structural `groups_` table and returns 0 for a non-`<group>` tag, `dictionary.cpp:92`), and the context-scoped store via its `members.empty()` guard (`dictionary.cpp:463`). **Tag 82 is therefore already unregistered today, and this feature does not change that.** What changes is *why*: the rejection becomes principled (the dictionary does not declare a `<group>`) instead of incidental (a downstream guard happens to catch it).

**Effective FIX43 delta: exactly one tag — `+576`.** Tag 82 is a **no-regression pin**, not a behavior change.

**Why pure structural rather than a union (`type OR structural`).** The honest case is *not* that a union misbehaves today — the same downstream guards that already reject tag 82 would reject it under a union too, so union and structural produce identical results on all ten dictionaries as they stand. The case is design integrity: a union keeps two sources of truth for one property and leaves a latent trap (a future dictionary declaring a `NUMINGROUP`-typed non-group tag that *does* acquire members would register spuriously, with no guard left to catch it), keeps `FieldRef::type` semantically overloaded as both datatype and group marker, and costs more code for zero present benefit. One predicate, one source.

### Structural truth is already available on both sides — this is a re-point, not new plumbing

Both group-declaration walks already key on the **element name**, never on the datatype:

- runtime — `src/dictionary/xml_loader.cpp:580` (`else if (tag_name == "group")`) builds the `Dictionary` group table, and `Dictionary::group(no_tag)` / `group_fields(no_tag)` expose it. The raw structural accessors are correct for FIX40/41/42 **today** (L-063-1 notes `group_fields(382)` on FIX42 returns its 4 members); only `as_table_view()`'s registration loops re-derive group-ness from the datatype and therefore register nothing.
- codegen — `tools/codegen/fixpp-codegen/ir.cpp:80` (`walk_level`, `else if (tag_name == "group")`) builds each message's declaration-order `group_order` (the 067/R9 `GroupOrderEntry` structure). So `MessageIR::group_order` is **already correctly populated for FIX42 today**. The emitters nonetheless re-derive group-ness from `FieldRef::type` over the tag-deduped `MessageIR::fields` run — `emit_builders.cpp:606` (`top_level_synthetic_members`) and `emit_messages.cpp:425` (`group_tags` collection) are the two discovery sites.

The work is therefore to re-point each consumer at a structural source that already exists, not to add dictionary parsing. `FieldRef::type` is **not** changed by this feature — it keeps carrying the declared datatype (`INT` for FIX42's count fields), which is what makes the byte-identity claims in FR-015/FR-016 checkable.

### Two existing tests encode the descope and must invert

`tests/codegen/test_077_builder_no_emit.cpp` (`V42EmitsNoBuilders` — asserts `v42/all.hpp` and `v42/messages/` are **absent**) and `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp` (defines the v42 builder-completeness expected set as **∅ by policy**, while recording that FIX42 really has 39 application messages) are the 077 descope pins. They are the natural RED→GREEN witnesses for US2.

## Clarifications

### Session 2026-07-29

- Q: Registering groups on FIX40/41/42 changes (1) inbound parse/field-addressing **unconditionally** — `session.cpp:992` builds `inbound_tv_ = dictionary->as_table_view()` in `open()` regardless of `validate_inbound_messages`, and 066 flipped the parser onto it — and (2) group-membership + 079 per-group required-member enforcement, reached only when strict validation is already opted into. What compat posture should the spec take? → A: **Accept both, no new gate.** The parse/addressing correction ships unconditionally (today's behavior is documented-wrong — absent or positionally-wrong reads); the newly-activated strictness rides the **existing** `validate_inbound_messages` opt-in rather than a second knob. Both are recorded as a named behavior change plus a release note in `spec/behaviors-and-limitations.md`.
- Q: US4 — the `v42` grouped/nested write exemplar closing L-061-1 needs a QuickFIX-derived wire golden plus exemplar-suite wiring on top of US1+US2. Keep it in this feature or split it out? → A: **Keep in 082 at P3.** L-061-1 is one of the four limitations #196 names for closure, and the exemplar is the concrete proof that the `v42` builder tier is *usable* rather than merely emitted. Cost is bounded — one message, one golden capture, reusing the existing 061 exemplar harness.

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

An operator running a FIX 4.3 session under dictionary-backed parsing is affected by a latent dictionary-typo defect that the predicate change resolves: a **real** repeating group, `NoClearingInstructions(576)`, is typed `INT` and is therefore currently unregistered and group-blind. The companion typo — plain field `NoRpts(82)` typed `NUMINGROUP` — is already rejected by both stores' downstream guards, so it carries a **no-regression pin** rather than a behavior change.

**Why this priority**: It is unavoidable once the predicate changes — it cannot be deferred to a later feature — but it is a discovered latent-defect fix, independent of what #196 asks for. It is separated so its delta and pins are visible in their own right and are not buried under the `v42` story.

**Independent Test**: Load `FIX43.xml`, build `as_table_view()`, and assert tag 576 **is** registered as a group with member `ClearingInstruction` (it is not today); separately assert tag 82 remains **un**registered as a group and `ListStatus` still reads it as a plain required field.

**Acceptance Scenarios**:

1. **Given** the vendored `FIX43.xml`, **When** `as_table_view()` is built, **Then** tag **576** **is** registered as a repeating group with member `ClearingInstruction` (it is not registered today).
2. **Given** a FIX 4.3 inbound message carrying a populated `NoClearingInstructions(576)` group, **When** a typed or C-ABI group read is issued, **Then** it returns a membership-bounded read rather than `TYPE_MISMATCH`/absent.
3. **Given** the same view, **When** tag **82** is queried, **Then** it is **not** registered as a repeating group — unchanged from today, now because the dictionary declares no `<group>` for it rather than because a downstream guard rejects it.
4. **Given** the same view, **When** a `ListStatus` message carrying tag 82 is validated, **Then** tag 82 is still accepted and enforced as a **plain required field** — the predicate change must not make the field unknown or optional.
5. **Given** FIX43's other 33 group tags, **When** `as_table_view()` is built, **Then** their registration is unchanged. Exactly one tag moves.

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
- **A `<group>` element with zero members.** None exists in any vendored dictionary today, and both candidate structural sources are already **member-independent** — `ir.cpp`'s `walk_level` appends the `GroupOrderEntry` unconditionally (it only skips setting `delimiter_tag` when the member list is empty), and `xml_loader.cpp:645` records a group definition per `<group>` element. The chosen source MUST preserve that property; a predicate derived from a group's *members* would silently drop such a group and is therefore excluded.
- **A FIX40/41/42 consumer reading a tag that lives inside a repeating group.** Because the parse-side change is ungated (FR-006a), such a read changes shape for **every** FIX40/41/42 session, not only strict-validation ones — today it resolves flat/positionally, afterwards it is group-scoped. This is the intended correction, and it needs a before/after pin taken with `validate_inbound_messages` **off** so the two axes are not conflated.
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

**Legacy-session behavior change (accepted, ungated)**

- **FR-006**: The FIX40/41/42 behavior change MUST ship **ungated** — no new configuration key, and no per-dictionary opt-in for structural detection. There MUST remain exactly one detection path in the code.
- **FR-006a**: The **inbound parse / field-addressing** change is unconditional and MUST be treated as a correctness fix. Verified end-to-end, both ends of the path: `Session::open()` builds `inbound_tv_` from `as_table_view()` regardless of `validate_inbound_messages` (`src/session/session.cpp:992`), and `parse_and_dispatch_` **consumes** it unconditionally — `Parser<access_mode::Index> pd_parser{*inbound_tv_}` (`src/session/session.cpp:328`, guarded only by an `assert` on the invariant, with no strict-flag condition anywhere on the path), which wires `group_member_fn` into the group context (`include/fixpp/wire/parser.hpp:306`). So once groups register, a FIX40/41/42 session's reads of tags inside a repeating group become **membership-bounded** where they were previously absent or positionally-wrong. This MUST be pinned by a before/after behavior test with strict validation **off**, demonstrating the corrected read.
- **FR-006b**: The newly-activated group-membership and 079 per-group required-member **enforcement** MUST ride the **existing** `validate_inbound_messages` opt-in — no additional gate. A FIX 4.0/4.1/4.2 session that has opted into strict inbound validation MAY consequently reject messages it previously accepted (FIX42 declares `required='Y'` members inside groups at 14 message/group pairs); with the flag off (the default), no new rejection path is reachable.
- **FR-006c**: Both changes MUST be recorded in `spec/behaviors-and-limitations.md` as a **named behavior change with an operator-facing release note**, stating explicitly that FIX40/41/42 inbound group reads change shape and that strict-validation deployments on those versions may see new rejects.

**`v42` typed builder tier (issue #196)**

- **FR-007**: The codegen driver's `v42` builder-tier exclusion (`ir.ns != "v42"`) MUST be removed, and `v42` MUST emit the full 078 split builder/validator layout — per-message declaration headers, the shared per-plan groups region plus its umbrella, the validator traits header, and the `all.hpp` umbrella — on the same terms as `v44` / `v50sp2` / `vlatest`.
- **FR-008**: Every `v42` `<Msg>Args` MUST represent the message's declared repeating groups. `validate_<Msg>` MUST reject an `Args` value that omits a group declared `required='Y'` — no group may be unrepresentable in `Args` and therefore silently omitted (Article VI).
- **FR-009**: `v42` builder output MUST be pinned by new goldens in the 078 golden region (the split file set plus the `--families official` pinned golden), wired into the codegen golden-matching test, on the same terms as the existing versions.
- **FR-010**: `vt11` MUST remain excluded from the builder tier by its **empty application-message registry** (self-skip), not by any version-name predicate. The removal of the `v42` predicate MUST NOT introduce a replacement version-name predicate for any version.

**FIX43 corrections**

- **FR-011**: `as_table_view()` for FIX43 MUST register tag **576 `NoClearingInstructions`** as a repeating group with member `ClearingInstruction`, making its inbound reads membership-bounded. This is the one effective FIX43 behavior change.
- **FR-012**: `as_table_view()` for FIX43 MUST continue to leave tag **82 `NoRpts`** unregistered as a group, and `ListStatus` MUST continue to accept and enforce tag 82 as a plain **required** field. This is a **no-regression pin**: tag 82 is already unregistered today (rejected by `group_first_field(82) == 0` in the legacy store and by `members.empty()` in the context-scoped store), so the requirement is that the predicate change preserves that outcome while making it principled rather than incidental.
- **FR-013**: FIX43's other 33 group registrations MUST be unchanged. **Exactly one tag moves** (`+576`).

**Non-regression**

- **FR-014**: For the six dictionaries whose type and structural sets are equal (FIX44, FIX50, FIX50SP1, FIX50SP2, FIXT11, Orchestra FIX Latest), the registered group set from `as_table_view()` MUST be **exactly** unchanged in both directions (set equality, not containment).
- **FR-015**: The `v44`, `v50sp2`, and `vt11` **read** goldens and the `vlatest` read golden MUST stay **byte-identical**, and the existing `vlatest` / `v50sp2` / `v44` **builder** goldens MUST stay byte-identical. This MUST be demonstrated by an actual regeneration + golden diff, not asserted from a source-level census.
- **FR-016**: The `v42` read tier regenerates. **All six** emitted `v42` artifacts — `Fields.hpp`, `Messages.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md`, `Manifest.txt` — MUST be classified explicitly as either byte-identical or changed-with-explanation; none may be left unexamined. `Messages.hpp` goes from 0 to 18 group classes. Every count that moves MUST carry a **by-construction** explanation reconciling the emitted delta to FIX42's declared structure — "golden regenerated" alone is not sufficient evidence.
- **FR-016a**: Because `FieldRef::type` is unchanged (FR-001 re-points detection, it does not re-type any field), the `v42` artifacts whose content is a function of field datatype rather than group structure are expected to be byte-identical — specifically `Fields.hpp` (the constexpr `FieldRef` arrays) and `Validator.hpp` (per-message rule tables plus the Length+Data pair table; the validator emitter has no group axis at all). This expectation MUST be **verified by regeneration diff**, and any artifact that moves against it is a finding to be explained, not absorbed.
- **FR-016b**: The two existing `v42` builder-descope pins — `tests/codegen/test_077_builder_no_emit.cpp` (`V42EmitsNoBuilders`) and `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp` (v42 expected set defined as ∅ by policy) — MUST be inverted to assert the delivered `v42` builder tier, not deleted. `vt11`'s companion assertions in the same files MUST be unchanged.
- **FR-017**: No C-ABI change. The frozen `1.5.0` surface, its symbol golden, and the abidiff baseline are untouched.
- **FR-018**: The 063 reused-tag census helper MUST be re-pointed to a source **independent** of the detection predicate under change — a raw-dictionary-XML walk — so that it can witness the FIX40/41/42 and FIX43 deltas rather than moving in lockstep with the code it checks. The pre-spec census logic that produced this feature's evidence base (§ Context) MUST be **checked in** as that oracle, so SC-001 / SC-002 / SC-003 are self-verifying in CI rather than resting on a one-off transcript.

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
- **SC-003**: The FIX43 registered group set differs from the pre-feature set by **exactly one** tag: **576 added**. A populated `NoClearingInstructions(576)` group reads membership-bounded where it previously did not. Tag 82 stays unregistered and `ListStatus` still enforces it as a plain required field (no-regression pin, not a delta).
- **SC-004**: The regenerated `v42` read tier contains typed group accessors for all **18** FIX42 group tags (today **0**), including the `296 → 295` nesting, and every emitted-count delta in its `Messages.hpp` / `Manifest.txt` / `Reify.hpp` is reconciled by construction to FIX42's declared structure.
- **SC-005**: Regeneration produces **byte-identical** `v44`, `v50sp2`, `vt11`, and `vlatest` read goldens and **byte-identical** existing `v44` / `v50sp2` / `vlatest` builder goldens — demonstrated by an actual diff over regenerated output.
- **SC-006**: `fixpp::v42` ships a typed builder tier covering its **39** application messages, with repeating groups represented in `Args` for the **21** that declare them; `validate_<Msg>` rejects omission of a `required='Y'` group at all **14** message/group pairs that declare one.
- **SC-007**: A `v42` grouped **and** nested write exemplar (`MassQuote`, `NoQuoteSets(296) → NoQuoteEntries(295)`) emits bytes matching an independently-derived golden and round-trips field-for-field through the `v42` read tier.
- **SC-008**: A FIX 4.2 inbound message carrying a real wire-level repeating group returns a membership-bounded typed / C-ABI group read where it previously returned `TYPE_MISMATCH` or absent — demonstrated with `validate_inbound_messages` **off**, confirming the parse/addressing correction is independent of the strict-validation opt-in.
- **SC-008a**: With `validate_inbound_messages` **off** (the default), no FIX40/41/42 inbound message is newly **rejected** — the ungated change alters read shape only, never acceptance. With the flag **on**, the new group-required rejections are exactly those derivable from the dictionary's `required='Y'` group members, enumerated and pinned rather than merely observed.
- **SC-009**: The C-ABI surface (`1.5.0`), its symbol golden, and the abidiff baseline are unchanged.
- **SC-010**: `spec/behaviors-and-limitations.md` carries no remaining open L-063-1 / L-061-1 / L-066-1 / L-077-1 carve-out, and the FIX43 corrections are recorded with their evidence.

## Assumptions

- The structural group declaration is already tracked on both sides independent of the count field's datatype — the loaders' `<group>`-element walk (`xml_loader.cpp:580`) feeding the `Dictionary` group table, and the codegen IR's `walk_level` (`ir.cpp:80`) feeding `MessageIR::group_order`. **No new dictionary parsing is introduced**; detection is re-pointed at data that already exists (see § Context). The expected default is that **neither side needs a new public accessor**: `as_table_view()` is a `Dictionary` member with access to `dictionary_internal.hpp`, so it can read the private group table directly, and the codegen emitters already have `MessageIR::group_order`. An additive public accessor should be introduced only if a site genuinely cannot reach either source — confirming that is a `/speckit-plan` task, and avoiding it keeps FR-017 trivially satisfied.
- `FieldRef::type` is **not** modified. FIX42's count fields keep their declared `INT` datatype; only what the detection sites *consult* changes. This is what makes FR-016a's byte-identity expectation for the datatype-derived artifacts a meaningful, falsifiable check rather than a tautology.
- Only **FIX42, FIX44, FIX50SP2, and FIXT11** are code-generated (`cmake/Codegen.cmake`). FIX40, FIX41, FIX43, FIX50, and FIX50SP1 are runtime dictionaries only, so this feature's codegen/golden impact is confined to **`v42`**; FIX40/41/43 impact is runtime-`as_table_view()`/validator only, with no golden to regenerate and therefore a need for **direct** regression pins.
- Byte-identity of unaffected tiers is treated as a **requirement pinned by regeneration diff**, never as an assumption — the `NoSidesCodeSet` finding above is the standing evidence that a raw-XML census does not equal loader truth.
- **Adding FIX40 / FIX41 (or FIX43 / FIX50 / FIX50SP1) to the code-generated version set is explicitly out of scope.** Structural detection makes typed `v40`/`v41` tiers newly *possible*, but this feature changes which dictionaries register groups, not which are code-generated. The codegen version set stays FIX42 / FIX44 / FIX50SP2 / FIXT11.
- The `v42` builder tier is emitted in the default `--families all` coverage mode (all **39** application messages); the `--families official` frozen-subset mode gains its own pinned `v42` golden on the same terms as the existing versions.
- Verification uses the established tooling of the preceding dictionary features: non-circular raw-XML oracles compared by exact-set equality in both directions, RED→GREEN behavior pins, QuickFIX-derived wire goldens, and the codegen golden-matching + determinism tests. `tools/codegen/**` is touched, so the codegen test label is in scope for local verification.
- The FIX40/41/42 compat posture is **settled** (Clarifications, 2026-07-29): ungated, one detection path, strictness riding the existing `validate_inbound_messages` opt-in, recorded as a named behavior change with a release note. The library is pre-v1.0 GA, so correcting documented-wrong legacy reads now is cheaper than preserving them behind a permanent knob; this follows 081's precedent of shipping a validation-behavior flip as a straight correction with pins.

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
