# Phase 0 Research: FR-015a-lite — Codegen Writer-Emitter

**Feature**: 067-codegen-writer-emitter · **Date**: 2026-07-10
**Method**: source-verified against the real emitter (`tools/codegen/fixpp-codegen/`), the vendored `dictionaries/FIX44.xml`, the 061 shape-oracle (`src/session/business_messages.cpp`, `tests/session/golden/*.fix`), and a generated `build/.../v44/Validator.hpp`. Every decision below cites the artifact it was checked against.

---

## R1 — Field emission order (THE byte-equality mechanism) — RESOLVED

**Decision**: Generated builders serialize with a **two-regime canonical order**, imposed by the emitter at serialize time, independent of caller value-supply order:

- **Top-level fields (`group_no_tag == 0`): sorted by tag ascending.** This regime IS correctly served by the tag-sorted per-message run `MessageIR.fields` (`m.fields`) — the loader tag-sorts that run (`xml_loader.cpp:695-702`) and QuickFIX tag-sorts the top-level body, so the two agree; goldens confirm. A repeating group's `No<Group>` count tag sorts into this sequence by its own tag value; the group's instances immediately follow it.
- **Inside a group entry: THIS group's DECLARATION member order** (the group's member enumeration in the raw dictionary XML, sourced from the new codegen-local `MessageIR.group_order`; see R9 + R7's per-message planner), NOT tag order and **NOT `m.fields` order**. A nested group's `No<sub>` sits at its member position; its instances follow.

**Rationale / evidence**: QuickFIX (which authored the goldens) tag-sorts the body at top level. Verified against the discriminator message **D (NewOrderSingle)**: its `FIX44.xml` declaration order is interleaved — `ClOrdID(11), SecondaryClOrdID(526), ClOrdLinkID(583), TradeOriginationDate(229), TradeDate(75), Account(1)…` — but golden `tests/session/golden/new_order_single.fix` is strictly ascending `11 38 40 44 54 55 60`. So "emit in raw declaration/XML order" is WRONG at top level; tag-sort is required, and the tag-sorted `m.fields` run serves it correctly. The group-entry regime is the OPPOSITE: confirmed by golden E `new_order_list.fix`, entry order `11 67 453 55 54 38` — `Symbol(55)` before `Side(54)`, i.e. **declaration** order, which tag-sort would invert to `54 55`. So the tag-sorted `m.fields` run is the WRONG source for group-entry order (RC#7); group-entry order MUST come from declaration order (`group_order`, R9). All 5 goldens (D/8/9/E/AS) reproduce under this two-source rule (checked by inspection of the checked-in bytes).

**Alternatives rejected**:
- *Emit top-level in raw declaration/XML order* — breaks D on day one (declaration order ≠ tag order); top-level must use the tag-sorted `m.fields`.
- *Emit group-entry order from the tag-sorted `m.fields` run* — breaks E (`54 55` instead of `55 54`); the loader tag-sorts the run and discards declaration order (`xml_loader.cpp:695-702`), so group-entry order must come from `group_order` (R9), NOT `m.fields`.
- *Incremental fluent setters where wire order = call order* — makes FR-008's "field order matches seed table" assert tautological (proves `body_builder` preserves order, not that the order is FIX-correct). Rejected — see R4.

**Verification gate for /implement**: a test asserts the generated builder for all 5 exemplars is byte-equal to the hand builder AND the golden under this rule. The hand exemplars are the executable spec for ordering (they are golden-matched); if generated==hand on all 5, the rule is proven.

---

## R2 — Group-child required-presence is populated in the IR — RESOLVED (viability of the depth decision)

**Decision**: The recursive required-presence **tables** (FR-006a) need **no IR addition** — they are order-independent, so the tag-sorted `m.fields` run serves them. `FieldRef.rule` (`include/fixpp/dict/field_ref.hpp:75-93`, enum `field_presence{NotDeclared=0, Optional=1, Required=2, Conditional=3}`) and `FieldRef.group_no_tag` are carried on **every** `FieldIR` in `MessageIR.fields`, including group members (a group member is a `FieldIR` with `ref.group_no_tag != 0`); `ir.cpp:95-101` copies the full run from `Dictionary::message_fields()`. Because the required SET is just `{tag : rule==Required, group_no_tag==<level>}`, the tag-sort of that run is harmless — set membership does not depend on order.

**Scope caveat (RC#7 — see R9)**: this "no IR addition" holds ONLY for the required-presence tables. The group-entry **delimiter and member ORDER** are order-dependent and therefore CANNOT come from the tag-sorted `m.fields` — they require a codegen-tool-local IR addition, `MessageIR.group_order` (R9), built by a raw-XML declaration-order walk. That addition is entirely inside the codegen tool (`ir.*` + `emit_builders.cpp`): **no runtime `Dictionary`/`GroupRef`/C-ABI/Python change** (FR-009 / C-ABI 1.5.0 freeze intact).

**Evidence the loader populates Required on group children** (the load-bearing fact the recon flagged as unverified): generated `build/linux-clang-ubsan/_codegen/include/fixpp/v44/Validator.hpp` `NewOrderList_rules` shows `{11, 2}` (ClOrdID) and `{67, 2}` (ListSeqNo) — both are `NoOrders(73)` group members, marked `rule=2 (Required)`. So group-child Required IS populated; US3 scenario 2 is not a silent no-op.

---

## R3 — The existing `Validator.hpp` `<Msg>_rules` table is NOT reusable verbatim — RESOLVED (mechanism refinement)

**Decision**: The write emitter emits **new, level-scoped, header-excluded** required-presence tables from the IR; it does **not** reuse `emit_validator.cpp`'s flat `<Msg>_rules`.

**Rationale / evidence** (same generated `NewOrderList_rules`, sized `<rule_row, 278>`):
1. **Header-polluted** — `{8,2},{9,2},{10,2},{34,2},{49,2},{52,2},{56,2}` (StandardHeader/Trailer framing fields marked Required). A body-only builder never sets these; walking this table would reject every valid `commit()`. → the write emitter's top-level table excludes the framing/header set `{8,9,10,34,35,49,52,56}`.
2. **Level-flattened** — `{11,2}` (ClOrdID) is a `NoOrders` group-child in NewOrderList, NOT a top-level field, yet the flat table cannot express that. Treating the flat table as "top-level required" would wrongly demand ClOrdID at root. → the write emitter segregates by `f.ref.group_no_tag` (0 = top-level body; else per-group).

**Consequence**: The emitter emits, per message, (a) a **top-level body** required set (`group_no_tag==0 ∧ rule==Required ∧ tag ∉ {8,9,10,34,35,49,52,56}`) and (b) a **per-occurrence group** required set (fields in THIS message's run with `group_no_tag==<group> ∧ rule==Required`). Both derive purely from IR `FieldRef.rule` off the message's own `m.fields` — Emitter-Lite (no hand-authored lists, no `emit_enums`) is intact; only the "reuse the already-emitted table" phrasing from the 100pct plan is superseded (the data is reused; the specific flat table is not). **The group required set MUST be per-occurrence, not one version-wide table per `no_tag`** (Gate A round 1, RC#1): the same `no_tag` carries a different required set in different messages — `NoMDEntries(268)` requires `MDEntryType(269)` in W (`FIX44.xml:3023`) but `MDUpdateAction(279)` in X (`FIX44.xml:3060`); a per-`no_tag` global table would over-reject one and under-reject the other, the write-side reprise of 063 Defect A. Spec FR-006a updated 2026-07-10 to match.

**What `m.fields` reliably carries vs. what it does not (RC#7)**: `FieldRef.rule` and `FieldRef.group_no_tag` ARE reliable per-field in `m.fields`, so the required SET (both the top-level body set and each per-occurrence group set) is correctly derivable from `m.fields` filtered by `group_no_tag` — order-independent. What `m.fields` does NOT carry is declaration ORDER (the loader tag-sorts + tag-dedups the run — `xml_loader.cpp:695-702`); the group-entry member order and the group **delimiter** therefore come from the new `group_order` XML-walk (R9), NOT from `m.fields`.

**Note**: `35` (MsgType) is in the exclusion set because `body_builder` emits it structurally from its ctor; required-presence for it is always satisfied and not the caller's responsibility.

---

## R4 — Generated builder API shape — RESOLVED

**Decision**: **Free function `build_<Msg>(std::span<std::byte> out, const <Msg>Args&)` returning `expected_t<std::span<std::byte>>`**, mirroring the 061 exemplars (which are exactly this shape — `build_new_order_list(out, const NewOrderListParams&)`, `business_messages.cpp:223`). `<Msg>Args` is a generated struct: `optional<T>` per scalar field (present ⇒ emitted; Bool as `optional<bool>` → `Y`/`N`, Length+Data coupled — FR-007a/RC#3), a span of nested `Args` structs per repeating group — **`std::optional<std::span>` for an OPTIONAL group** (nullopt ⇒ `No<G>` omitted; engaged-empty ⇒ `No<G>=0`) vs a plain `std::span` for a REQUIRED group (`validate` `size()>0`) — RC#2. The builder emits present fields in the R1 canonical order via `body_builder` (`bb.field(tag, args.x)` at top level in tag order; `entry->set_*` in THIS message's member order inside groups). A separate `validate_<Msg>(const <Msg>Args&)` (R3 per-occurrence tables) returns `wire_required_field_missing`.

**Rationale**: (1) Lowest divergence from the frozen shape-oracle — the exemplars already use free-function + params-struct for the grouped cases. (2) Emits in emitter-derived canonical order regardless of caller field-set order, which is what makes FR-008's byte-structural assert non-tautological. (3) `optional<>` presence == "field set", giving the driver control over WHICH fields (to reproduce a minimal exemplar seed) without controlling ORDER.

**Alternatives considered**:
- *Buffering builder class with typed setters that replays in canonical order at `commit()`* — same ordering guarantee, more codegen and a new public class shape diverging from the exemplars. Acceptable but not chosen; revisit only if the args-struct proves ergonomically poor for the widest messages.
- *Incremental fluent setters (wire order = call order)* — REJECTED (R1): tautologizes the order assert; diverges from the pure-serializer exemplar contract.

**Namespace/naming**: generated into `fixpp::v44` (mirrors the read side `fixpp::v44::<Msg>`), new header `Builders.hpp` alongside `Messages.hpp`. The 061 hand exemplars stay in `fixpp::session` untouched (they are the frozen oracle, not shipped generated code).

---

## R5 — Non-tautology insurance for the 28 non-exemplar messages — RESOLVED (scope note)

**Decision**: The ordering rule (R1) is *proven* on the 5 exemplars (external goldens) and *trusted* for the other 28. To de-risk the generalization, generate additional QuickFIX goldens for non-exemplar GROUPED messages via the existing offline harness `tests/session/golden/gen/` (5 QuickFIX-cpp programs already there; adding one follows the established pattern):

- **W and X as a PAIRED discriminator (REQUIRED — Gate A round 1, Codex #8 / RC#1)**: `MarketDataSnapshotFullRefresh (35=W)` and `MarketDataIncrementalRefresh (35=X)` byte-goldens together. They share `NoMDEntries(268)` but with **different delimiter/member order** (269-first vs 279-first) and different per-occurrence required sets — the direct discriminator that a version-wide group plan is wrong. A single grouped golden (e.g. MassQuote alone) does NOT exercise the shared-`no_tag` collision.
- **MassQuote (35=i)** (deep nested groups, tag-reused `NoQuoteEntries`), optionally **AllocationInstruction (35=J)** — additional grouped insurance.

These become byte-golden anchors in the round-trip harness for those rows; the W/X pair is the RC#1 regression pin. The remaining rows rely on the round-trip + byte-structural asserts (FR-008). Paired with an emitter unit test proving one `no_tag` → distinct per-message delimiter/member/required plans (see plan.md test structure). This is cheaper than shipping a wrong-order builder no oracle would catch. Execution deferred to /tasks (a discrete task), flagged here so it is not silently dropped.

---

## R6 — The 33 OFFICIAL distinct MsgTypes (completeness-gate exact set) — RESOLVED

All 33 verified present in `dictionaries/FIX44.xml`. Exact set for the FR-004 exact-set gate:

**A (13)**: `D E F G H 8 9 q r AF AC t u`
**M (17)**: `V W X Y c d e f g h i b S R AG Z a`
**P (3)**: `J P AS`

Reconciliation: 28 catalogue rows → 34 slots → 33 distinct (the single collision is `b`, which is `MassQuoteAcknowledgement` in v44 — `FIX44.xml:828` — shared by M-008/M-009; the catalogue's "QuoteAcknowledgement" label on M-009 has no distinct v44 message). **Multi-char MsgTypes (4)**: `AF, AC, AG, AS` — exercise the multi-char path (shipped in 057).

**/tasks note**: the completeness gate enumerates these 33 verbatim as the expected set; a generated builder must exist for each, and no more/no fewer (exact-set equality per `feedback_completeness_gate_exact_set_not_subset`).

---

## R7 — Emitter integration points (reuse map) — RESOLVED (per-occurrence group model; Gate A round 1 RC#1)

**The read emitter's version-wide `MemberMap` is NOT reused verbatim for write.** `MemberMap gmm` (`emit_messages.cpp:389-400`) is keyed by `group_no_tag` alone and deduped by **first-encounter-wins union** across the whole message list — it emits ONE `G_<no_tag>` plan version-wide. That is correct for READ (per-context liveness is decided at PARSE time by the 062/063 membership primitive), but the WRITE emitter has no runtime scoping: a version-wide plan would serialize W and X (both `NoMDEntries=268`, delimiter 269 vs 279, different member order + required sets) with the wrong delimiter/order for one → `commit()` INV-5 reject or wrong bytes. So the write emitter derives its group plan **per message** — the delimiter + member order from the new `MessageIR.group_order` declaration-order walk (R9), the required set from `m.fields` filtered by `group_no_tag`.

| Need | Disposition | Location |
|---|---|---|
| Field type → body_builder call kind | **reuse verbatim** `kind_of` → `TypeKind{String,Char,Bool,Int32,Decimal,Skip}` (honor Bool + Length/Data — see below, R7 no longer folds Bool into int) | `gen_util.hpp:144-185` |
| Field name → accessor / identifier | **reuse verbatim** `to_accessor`, `to_identifier`, `uniquify_accessor`, `strip_no_prefix` | `gen_util.hpp` |
| Per-field presence (`rule`) + parent group (`group_no_tag`) | **reuse the DATA** `FieldRef.rule`, `FieldRef.group_no_tag` (order-independent — the required SET does not depend on run order) | `field_ref.hpp`, `ir.hpp:41`, `ir.cpp:98-99`; IR carries per-message |
| Top-level field TAG order + scalar emission | **reuse** the tag-sorted `m.fields` run (top-level regime IS tag-ascending — R1) | `ir.cpp:98-99` (run is loader-tag-sorted, `xml_loader.cpp:695-702`) |
| Group plan (delimiter, member ORDER) | **NEW `MessageIR.group_order` declaration-order XML walk (R9), NOT `m.fields`, NOT the version-wide `MemberMap`** — per (message, group-occurrence); `m.fields` is tag-sorted so it CANNOT supply the delimiter/order | new codegen-local walk in `ir.cpp` + planner in `emit_builders.cpp` |
| Group required set (per occurrence) | **from `m.fields`** filtered by `group_no_tag == <group> ∧ rule==Required` (order-independent) | `field_ref.hpp`, `ir.cpp:98-99` |
| Emit invocation + output path + empty-skip | add `emit_builders(ir)` → `write_file(base/"Builders.hpp", …)` | `main.cpp:44-89` (`write_file` skips empty content — TDD-friendly) |

**Shared-vs-rebuilt decision (settles Codex #9 / folds into RC#1)**: shared = `kind_of`/`to_accessor`/`to_identifier` (type/name only); rebuilt per-message = the group planner (delimiter + member order from `group_order`, R9) + the required tables (from `m.fields`). The read-emitter determinism golden (G7) stays green as the guard that the shared helpers were not perturbed. This is a *design* decision, not a /tasks convenience, because RC#1 reopens the "reuse verbatim" premise.

**Delimiter for group_begin**: the group's delimiter tag = its **first DECLARED member** in THIS message's occurrence of the group, taken from `MessageIR.group_order` (R9), NOT from the tag-sorted `m.fields` and NOT from the global `GroupRef.first_field_tag` (per `feedback_group_delimiter_from_groupref_not_tagsorted_members`). Supplied to `group_begin(no_tag, delimiter_tag)` at codegen time — the author-supplied delimiter contract of `body_builder` (no wire→dict edge at runtime; the edge is at codegen time, which is correct). W's `NoMDEntries` delimiter is `MDEntryType(269)` (`FIX44.xml:3024`); X's is `MDUpdateAction(279)` (`FIX44.xml:3061`) — same `no_tag`, different per-message; and because X tag-sorts 269 < 279, deriving the delimiter from `m.fields` would wrongly pick 269 for X (the RC#7 defect).

**TypeKind → body_builder mapping** (honors the kinds `kind_of` already computes — RC#3): `Decimal → field(tag, decimal_t)`, `Char → field(tag, char)`, `Int32 → field(tag, int64_t)`, **`Bool → field(tag, x?'Y':'N')` via the existing char overload** (FIX Boolean is `Y`/`N`, NOT `1`/`0`; NO frozen-`body_builder` change), `String → field(tag, string_view)`, **Length+Data pair → auto-derive `Length` from the Data, emit both coupled via the string path** (clean-text only; binary/control-byte Data scoped out for v1.0 — FR-007a); top-level uses `field(...)`, group entries use `set_*` equivalents. `Skip` (DialectExtension) emits no setter.

---

## R8 — Constitution triggers & controls — RESOLVED

**Triggers (Appendix A): Wire format / parser (validator changes) + Codegen layout.** ⇒ all four mandatory controls required: `/clarify` (done), `/analyze` (step 6), Codex Gate A (after /plan), user `/plan` sign-off. (Article XVI §3, Article XVII, Appendix A.)

**NOT triggered / must be verified unchanged**:
- **Error semantics** — reuses pre-existing `error::wire_required_field_missing`(=38); NO new `fixpp_error_t`. Verify no C-ABI error-code addition (Article X §4, `tools/abi_history/error_codes_v1.txt` unchanged).
- **ABI** — FR-009: no C-ABI symbol/signature change; `nm`/abidiff/occupancy gates stay green; C-ABI 1.5.0 freeze byte-identical.
- **Session FSM / Security / Threading** — untouched.

**Article VI (100% FIX rule)**: this feature advances OFFICIAL typed-message write coverage. Before rows close: matching `/specify` artifact (this bundle), coverage-index + feature-catalogue updates for the 33 rows' write-coverage disposition, Gate A + Gate B. Normative References section required (R6 rows' `[FIX44 …]` refs).

**Article VII/IX**: TDD; the sanitizer/coverage/static-analysis matrix runs on the new emitter + generated headers (generated headers are coverage-excluded per `tools/analyze_coverage.py`, but the emitter source and the hand-written harness/validator runtime are not). Article XVII §8 verify-gate + completeness audit before Gate B.

**Performance (Article VIII)**: builders are outbound-hot-path; `body_builder`'s zero-global-heap arena is inherited. No new perf budget; confirm no regression to existing build benches.

---

## R9 — Group delimiter + member-order source: a codegen-local `MessageIR.group_order` (Gate A round 2, RC#7) — RESOLVED

**Defect corrected (RC#7)**: R1/R7 round-1 had the write emitter derive each group's **delimiter + member order** from `MessageIR.fields`. That is WRONG: the loader **tag-sorts and tag-dedups** the per-message run before it lands in the IR:
- `xml_loader.cpp:695-702` — `append_run` does `std::ranges::sort(msg_fields, by tag)` + `unique` (dedup by tag, first-seen).
- `ir.cpp:98-100` — `build_ir` copies that already-sorted `dict.message_fields()` run verbatim into `MessageIR.fields`.

So `MessageIR.fields` is **tag-sorted; declaration order is LOST**. Two concrete failures:
- **Wrong delimiter**: X (`MarketDataIncrementalRefresh`) `NoMDEntries(268)` declares `MDUpdateAction(279)` first (`FIX44.xml:3060-3061`), so 279 is the delimiter; but tag-sort orders the optional `MDEntryType(269)` before 279 → deriving "first member of the run" yields 269 (wrong). W's delimiter IS `MDEntryType(269)` (`FIX44.xml:3023-3024`) — the two DIFFER for the same `no_tag`, so no version-wide plan works either.
- **Wrong group-entry ORDER on an exemplar**: golden E `new_order_list.fix` `NoOrders` entry order is `11 67 … 55 54 38` (declaration: `Symbol(55)` before `Side(54)`); tag-sort gives `54 55` → the headline byte-equality pin (FR-003 / G2 / SC-002) FAILS.

**Why the global loader group structures are inadequate**: `Dictionary::group_first_field` / `group_fields` / `GroupRef` are all **`no_tag`-deduped first-seen** (`xml_loader.cpp:485-486`; documented L-063-3) — they record ONE global `GroupDef` per `no_tag` and CANNOT distinguish W's vs X's `NoMDEntries`. 063 also explicitly deferred the delimiter (`specs/063-nested-group-parse-correctness/tasks.md:62`). So the runtime dictionary cannot supply a per-message delimiter/order.

**The fix — a codegen-tool-local `MessageIR.group_order`**: `build_ir` already owns `xml_path` (`ir.cpp:67-69`) but NOT the raw parsed tree — pugixml is consumed ONLY inside `xml_loader.cpp`'s TU (D-15; `src/dictionary/xml_loader.cpp:9`), `XmlLoader` exposes only `load(path)→Dictionary`, and the codegen tool has no pugixml today (`grep pugi tools/codegen/` → empty), so `ir.cpp` has no handle to the parsed XML. The realizable, FR-009-safe mechanism is a **codegen-tool-local pugixml re-parse of `xml_path` inside `ir.cpp`** (a NEW pugixml dependency in the codegen tool TU), re-implementing declaration-order iteration + recursive `<component>` resolution — explicitly **NOT** a new runtime `Dictionary`/`XmlLoader` accessor (which would push pugixml-derived data through the runtime dict surface and brush FR-009 / the wire↔dict layering). The loader already proves declaration-order iteration is feasible: its group walk (`xml_loader.cpp:487-518`) iterates `child.children()` in **declaration order** to find the first field — but it finds only that first field and then DISCARDS the per-context result after recording one global `GroupDef`. The codegen re-parse re-implements that *feasibility* (declaration-order XML iteration), and in doing so duplicates the loader's `expand_field_list`/`collect_components` logic (`xml_loader.cpp:425,361`) — a second declaration-order walk whose drift risk is guarded by the RC#7 W/X (269-vs-279) + E-55-before-54 unit pins. The new codegen-local walk:
- roots at **each MESSAGE's own XML definition** (not a global group table);
- for each repeating-group OCCURRENCE (keyed by `(message, parent-path, no_tag)`, like 063's per-context keying), captures the group's **delimiter tag** = its first declared `<field>`/`<group>` member, and its **members in declaration order**;
- **resolves this message's own `<component>` references** (W → `MDFullGrp` → 269-first; X → `MDIncGrp` → 279-first — this is exactly why per-message beats the global `GroupRef`);
- **recurses** into nested `<group>`/`<component>` children so a nested group's delimiter + member order are captured at every depth;
- does **NOT** tag-sort and does **NOT** tag-dedup (side-stepping the `append_run` collapse — see the N3 census note in plan.md).

`build_<Msg>` and `validate_<Msg>` then source the group **delimiter + member order** from `MessageIR.group_order`; the required **SET** stays from `m.fields` (order-independent, R3); top-level field order stays the tag-sorted `m.fields` (R1). This is a **codegen-tool + IR-shape change** (`ir.hpp`/`ir.cpp` XML walk + a new IR field + the emitter using it) — it FALSIFIES the round-1 "no IR addition" premise for the delimiter/order, which is corrected in R2/R7/data-model §3/plan.md/spec FR-006a. It is entirely inside the codegen tool: **NO change to the runtime `Dictionary`/`GroupRef`/ABI** (FR-009 / C-ABI 1.5.0 freeze still hold).

**Verification gate for /implement**: the codegen unit test asserts `group_order` yields distinct per-message delimiter/member-order for W (269-first) vs X (279-first) from the same `no_tag`; and the E exemplar's `NoOrders` order is `55` before `54`. Both are RC#7 discriminating pins.
