# Phase 0 Research: FR-015a-lite — Codegen Writer-Emitter

**Feature**: 067-codegen-writer-emitter · **Date**: 2026-07-10
**Method**: source-verified against the real emitter (`tools/codegen/fixpp-codegen/`), the vendored `dictionaries/FIX44.xml`, the 061 shape-oracle (`src/session/business_messages.cpp`, `tests/session/golden/*.fix`), and a generated `build/.../v44/Validator.hpp`. Every decision below cites the artifact it was checked against.

---

## R1 — Field emission order (THE byte-equality mechanism) — RESOLVED

**Decision**: Generated builders serialize with a **two-regime canonical order**, imposed by the emitter at serialize time, independent of caller value-supply order:

- **Top-level fields (`group_no_tag == 0`): sorted by tag ascending.** A repeating group's `No<Group>` count tag sorts into this sequence by its own tag value; the group's instances immediately follow it.
- **Inside a group entry: dictionary member order** (the `MemberMap` / `plan_dfs` enumeration order), NOT tag order. A nested group's `No<sub>` sits at its member position; its instances follow.

**Rationale / evidence**: QuickFIX (which authored the goldens) tag-sorts the body at top level. Verified against the discriminator message **D (NewOrderSingle)**: its `FIX44.xml` declaration order is interleaved — `ClOrdID(11), SecondaryClOrdID(526), ClOrdLinkID(583), TradeOriginationDate(229), TradeDate(75), Account(1)…` — but golden `tests/session/golden/new_order_single.fix` is strictly ascending `11 38 40 44 54 55 60`. So "emit in IR/XML order" is WRONG at top level; tag-sort is required. The group-entry regime is confirmed by golden E `new_order_list.fix`: entry order `11 67 453 55 54 38` (55 before 54 → member order, not tag order). All 5 goldens (D/8/9/E/AS) reproduce under this rule (checked by inspection of the checked-in bytes).

**Alternatives rejected**:
- *Emit in IR `m.fields` order* — breaks D on day one (XML order ≠ tag order).
- *Incremental fluent setters where wire order = call order* — makes FR-008's "field order matches seed table" assert tautological (proves `body_builder` preserves order, not that the order is FIX-correct). Rejected — see R4.

**Verification gate for /implement**: a test asserts the generated builder for all 5 exemplars is byte-equal to the hand builder AND the golden under this rule. The hand exemplars are the executable spec for ordering (they are golden-matched); if generated==hand on all 5, the rule is proven.

---

## R2 — Group-child required-presence is populated in the IR — RESOLVED (viability of the depth decision)

**Decision**: FR-006a (recursive group-entry required-presence) is viable with **no IR addition**. `FieldRef.rule` (`include/fixpp/dict/field_ref.hpp:75-93`, enum `field_presence{NotDeclared=0, Optional=1, Required=2, Conditional=3}`) is carried on **every** `FieldIR` in `MessageIR.fields`, including group members (a group member is a `FieldIR` with `ref.group_no_tag != 0`). `ir.cpp:95-101` copies the full flat run from `Dictionary::message_fields()`.

**Evidence the loader populates Required on group children** (the load-bearing fact the recon flagged as unverified): generated `build/linux-clang-ubsan/_codegen/include/fixpp/v44/Validator.hpp` `NewOrderList_rules` shows `{11, 2}` (ClOrdID) and `{67, 2}` (ListSeqNo) — both are `NoOrders(73)` group members, marked `rule=2 (Required)`. So group-child Required IS populated; US3 scenario 2 is not a silent no-op.

---

## R3 — The existing `Validator.hpp` `<Msg>_rules` table is NOT reusable verbatim — RESOLVED (mechanism refinement)

**Decision**: The write emitter emits **new, level-scoped, header-excluded** required-presence tables from the IR; it does **not** reuse `emit_validator.cpp`'s flat `<Msg>_rules`.

**Rationale / evidence** (same generated `NewOrderList_rules`, sized `<rule_row, 278>`):
1. **Header-polluted** — `{8,2},{9,2},{10,2},{34,2},{49,2},{52,2},{56,2}` (StandardHeader/Trailer framing fields marked Required). A body-only builder never sets these; walking this table would reject every valid `commit()`. → the write emitter's top-level table excludes the framing/header set `{8,9,10,34,35,49,52,56}`.
2. **Level-flattened** — `{11,2}` (ClOrdID) is a `NoOrders` group-child in NewOrderList, NOT a top-level field, yet the flat table cannot express that. Treating the flat table as "top-level required" would wrongly demand ClOrdID at root. → the write emitter segregates by `f.ref.group_no_tag` (0 = top-level body; else per-group).

**Consequence**: The emitter emits (a) a per-message **top-level body** required set (`group_no_tag==0 ∧ rule==Required ∧ tag ∉ {8,9,10,34,35,49,52,56}`) and (b) a per-group `<Group>_rules` set (`group_no_tag==<group> ∧ rule==Required`). Both derive purely from IR `FieldRef.rule` — Emitter-Lite (no hand-authored lists, no `emit_enums`) is intact; only the "reuse the already-emitted table" phrasing from the 100pct plan is superseded (the data is reused; the specific flat table is not). Spec FR-006a updated 2026-07-10 to match.

**Note**: `35` (MsgType) is in the exclusion set because `body_builder` emits it structurally from its ctor; required-presence for it is always satisfied and not the caller's responsibility.

---

## R4 — Generated builder API shape — RESOLVED

**Decision**: **Free function `build_<Msg>(std::span<std::byte> out, const <Msg>Args&)` returning `expected_t<std::span<std::byte>>`**, mirroring the 061 exemplars (which are exactly this shape — `build_new_order_list(out, const NewOrderListParams&)`, `business_messages.cpp:223`). `<Msg>Args` is a generated struct: `optional<T>` per scalar field (present ⇒ emitted), spans of nested `Args` structs per repeating group. The builder emits present fields in the R1 canonical order via `body_builder` (`bb.field(tag, args.x)` at top level in tag order; `entry->set_*` in member order inside groups). A separate `validate_<Msg>(const <Msg>Args&)` (R3 tables) returns `wire_required_field_missing`.

**Rationale**: (1) Lowest divergence from the frozen shape-oracle — the exemplars already use free-function + params-struct for the grouped cases. (2) Emits in emitter-derived canonical order regardless of caller field-set order, which is what makes FR-008's byte-structural assert non-tautological. (3) `optional<>` presence == "field set", giving the driver control over WHICH fields (to reproduce a minimal exemplar seed) without controlling ORDER.

**Alternatives considered**:
- *Buffering builder class with typed setters that replays in canonical order at `commit()`* — same ordering guarantee, more codegen and a new public class shape diverging from the exemplars. Acceptable but not chosen; revisit only if the args-struct proves ergonomically poor for the widest messages.
- *Incremental fluent setters (wire order = call order)* — REJECTED (R1): tautologizes the order assert; diverges from the pure-serializer exemplar contract.

**Namespace/naming**: generated into `fixpp::v44` (mirrors the read side `fixpp::v44::<Msg>`), new header `Builders.hpp` alongside `Messages.hpp`. The 061 hand exemplars stay in `fixpp::session` untouched (they are the frozen oracle, not shipped generated code).

---

## R5 — Non-tautology insurance for the 28 non-exemplar messages — RESOLVED (scope note)

**Decision**: The ordering rule (R1) is *proven* on the 5 exemplars (external goldens) and *trusted* for the other 28. To de-risk the generalization, generate **1–2 additional QuickFIX goldens for non-exemplar GROUPED messages** — primary candidate **MassQuote (35=i)** (deep nested groups, tag-reused `NoQuoteEntries`), optionally **AllocationInstruction (35=J)** — via the existing offline harness `tests/session/golden/gen/` (5 QuickFIX-cpp programs already there; adding one follows the established pattern). These become byte-golden anchors in the round-trip harness for those rows. The remaining rows rely on the round-trip + byte-structural asserts (FR-008). This is cheaper than shipping a wrong-order builder no oracle would catch. Execution deferred to /tasks (a discrete task), flagged here so it is not silently dropped.

---

## R6 — The 33 OFFICIAL distinct MsgTypes (completeness-gate exact set) — RESOLVED

All 33 verified present in `dictionaries/FIX44.xml`. Exact set for the FR-004 exact-set gate:

**A (13)**: `D E F G H 8 9 q r AF AC t u`
**M (17)**: `V W X Y c d e f g h i b S R AG Z a`
**P (3)**: `J P AS`

Reconciliation: 28 catalogue rows → 34 slots → 33 distinct (the single collision is `b`, which is `MassQuoteAcknowledgement` in v44 — `FIX44.xml:828` — shared by M-008/M-009; the catalogue's "QuoteAcknowledgement" label on M-009 has no distinct v44 message). **Multi-char MsgTypes (4)**: `AF, AC, AG, AS` — exercise the multi-char path (shipped in 057).

**/tasks note**: the completeness gate enumerates these 33 verbatim as the expected set; a generated builder must exist for each, and no more/no fewer (exact-set equality per `feedback_completeness_gate_exact_set_not_subset`).

---

## R7 — Emitter integration points (reuse map) — RESOLVED

| Need | Reuse (verbatim) | Location |
|---|---|---|
| Per-version member map (group_no_tag → members, tag-deduped) | `MemberMap gmm` build loop | `emit_messages.cpp:384-412` |
| Group topological plan (deps-first, nested) | `GroupPlan` + `plan_dfs` | `emit_messages.cpp:147-181` |
| Group class naming / delimiter identification | `group_cls(no_tag)`, `NumInGroup` detection | `emit_messages.cpp:139` |
| Field type → body_builder call kind | `kind_of` → `TypeKind{String,Char,Bool,Int32,Decimal,Skip}` | `gen_util.hpp:144-185` |
| Field name → accessor / identifier | `to_accessor`, `to_identifier`, `uniquify_accessor`, `strip_no_prefix` | `gen_util.hpp` |
| Per-field presence (`rule`) + parent group (`group_no_tag`) | `FieldRef.rule`, `FieldRef.group_no_tag` | `field_ref.hpp`; IR carries verbatim |
| Emit invocation + output path + empty-skip | add `emit_builders(ir)` → `write_file(base/"Builders.hpp", …)` | `main.cpp:44-89` (`write_file` skips empty content — TDD-friendly) |

**Delimiter for group_begin**: the group's delimiter tag = its first member in dictionary member order (per `feedback_group_delimiter_from_groupref_not_tagsorted_members`). The emitter supplies it to `group_begin(no_tag, delimiter_tag)` from the `plan_dfs`/member enumeration — the author-supplied delimiter contract of `body_builder` (no wire→dict edge at runtime; the edge is at codegen time, which is correct).

**TypeKind → body_builder mapping**: `Decimal → field(tag, decimal_t)`, `Char → field(tag, char)`, `Int32/Bool → field(tag, int64_t)`, `String → field(tag, string_view)` (top-level) / `set_*` equivalents (group entries). `Skip` (DialectExtension) emits no setter.

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
