# Phase 0 Research: Runtime validator required-presence scoping (fixpp#201)

## R1 — Root cause (confirmed by code map)

`FieldRef::rule` (`include/fixpp/dict/field_ref.hpp:83-91`) carries a field's **own local** `required=`/`presence=` from where it is declared, plus a `component_index` *pointer* (which component it lexically lives in) — but **not** the `required=`/`presence=` of the enclosing container reference. Every required-set projection is built by walking `FieldRef.rule`:

- **XML loader** `expand_field_list` (`src/dictionary/xml_loader.cpp:492-602`): the `<field>` branch (`:517-520`) pushes the tag into the message-level `required_out`; the `<group>` recursion (`:595-598`) already forces `in_group=true` (candidate fix) so group members no longer leak.
- **Orchestra loader** `expand_field_list` (`src/dictionary/orchestra_loader.cpp:476-575`): structurally identical; `fieldRef` gate at `:503`, `groupRef` forces `in_group=true` at `:571`.
- **Codegen IR**: for `<fix>`-schema dicts, `ir.cpp:603-613` passes `dict.message_fields()` straight through (inherits the loader's `FieldRef`); the required *check* is emitted by `emit_builders.cpp` (`resolve_level` `:594-702` sets `item.required = ref.rule == Required`; `emit_writer_traits_for_level` `:704-826` emits the per-message required checks). Top-level items come from `collect_top_fields` (`:557-573`), which **filters `group_no_tag==0`** — so group members never enter the top-level required check.

**Decision**: the message-level required set is the correct locus for the fix (runtime validator flat-probes it). The candidate's `in_group` flag is the right mechanism.

## R2 — The group-scope fix (candidate `177a0535`, adopted as the design)

- **Loaders**: `expand_field_list` gains an `in_group` flag; once inside any `<group>`/`<groupRef>`, member `required='Y'` fields do not enter `required_out`. Identical change in `xml_loader.cpp` and `orchestra_loader.cpp`. QuickFIX AND-composition parity — no new message-level requirements.
- **Additive store** (`include/fixpp/dict/table_view.hpp`): a per-group required-member store (bare `group_required_members(no_tag)` + context `group_required_members(msg_type, parent_path, no_tag)`), populated by `dictionary.cpp::as_table_view()` from `rule==Required && group_no_tag==no_tag`. Mirrors the existing `group_members_` store (063 context-scoping precedent).
- **Validator** (`include/fixpp/wire/validator.hpp` `consume_group`): a per-instance required-member check (bitmask, delimiter pre-marked, guarded ≤64) → `wire_required_field_missing` with the offending tag. Mirrors the existing fail-closed disposition.

**Decision — no per-component store**: unlike groups (which need a per-instance runtime check), an optional component's required-ness would only ever affect the *message-level* set, not a runtime per-occurrence check. Since R3 shows there are no such sites, no component-scope store or threading is added. Had sites existed, the fix would have been an `enclosing_required` AND threaded through the componentRef branches (`xml_loader.cpp:521-530`, `orchestra_loader.cpp:506-516`) — which today never read the component-usage `required`/`presence` attribute.

## R3 — Component/codegen scope: enumerated, found VACUOUS (scope narrowing)

The issue posited an optional-component over-require leg ("fix item 3", ~6 codegen sites). This was **issue-asserted, not reproduced** — the fable 6.1 repro hit only `<group>` cases (NoUnderlyings, NoSides, NoAllocs). The directly analogous component case, **L-067-1, proved vacuous** ("not recoverable from any vendored dictionary"). So it was enumerated statically before committing any codegen apparatus.

**Method** (static, no build): parse each raw dictionary XML; for every message, recursively expand components/groups tracking `(in_group, enclosing_all_components_required)`; a field is an optional-component over-require site iff `required='Y'` AND `not in_group` AND `not enclosing_all_components_required` (reached through ≥1 optional component). The DIFF against the current shipped required set is the site set.

**Result**:

| Dict set | Over-require sites | Evidence |
|----------|-------------------|----------|
| 9 QuickFIX-schema (FIX40..FIX50SP2, FIXT11) | **0** | FIX44: 327 optional-component usages, but 0 component-defs carry a directly-required field. FIX50SP2: the only 2 such comp-defs (`MDStatisticParameters`, `PostTradePayment`) are never used with optional presence at message level. |
| vlatest (Orchestra) | **0 genuine** | 11 apparent hits are ALL StandardHeader/StandardTrailer fields (8/9/34/35/49/52/56/10), flagged only because messages like `PayManagementReportAck` (StandardHeader `presence` absent→optional) and `AccountSummaryReport` (StandardTrailer `presence` absent→optional) reference the header/trailer componentRef with default presence. These are structurally always-required and MUST NOT be dropped. |

**Decision**: component/codegen leg is **out of scope** (vacuous). The codegen tier is not modified — it never over-required groups (`group_no_tag==0` filter) and has 0 component sites. Scope = runtime-only + group-only.

**Guard against being wrong**: the census (R4) and two-tier agreement (R7) are scope-agnostic — they compare full required *sets* / verdicts — so a component or codegen over-require, if one existed and this enumeration missed it, surfaces as RED. The narrowing shrinks the *fix*, not the *verification*.

**Orthogonal observation (NOT #201)**: the Orchestra dict references StandardHeader/Trailer with default (optional) presence in some messages. Whether fixpp's vlatest path keeps header/trailer fields required through that modeling is a separate concern; noted for a later glance, out of scope here.

## R4 — Non-circular census design

**Reused patterns**: the 076 exact-set-equality skeleton (`tests/codegen/vlatest_completeness_census_test.cpp` — independent pugixml `build_dict` + `walk_structure`, `std::set_difference` both directions) and the 077/078 per-version harness (`tests/codegen/builder_completeness_common.hpp` — independent raw-XML `expected_msgtypes` walks that explicitly do NOT call `build_ir()`).

**Design**:
- **Expected side** — an independent raw-XML walker (shares no code with the loaders/IR) computes the expected message-level required set per message: field `required='Y'`, excluding all group members, honoring existing component-usage handling. This is the AND-rule oracle. NOTE the 076 walker computes *per-occurrence* rule, not an AND-composed message-level set — the #201 census must add the message-level composition (group exclusion) to the walk.
- **Actual side** — the *shipped* required set the validator probes: `Dictionary::required_fields(msg_type)` / `table_view::required_fields()`, plus the codegen IR-derived top-level required set (the safety-net leg).
- **Assertion** — exact **set** equality (0 extra, 0 missing) per message, across all 10 dicts, both directions ([[feedback_completeness_gate_exact_set_not_subset]]). Non-circular: the expected walker is independent of the projection it checks ([[feedback_noncircular_census_projection_source_stops_pinning_shipped_classes]]).
- **RED proof** — deliberately reintroduce a group-scoped required (revert the `in_group` gate) → census MUST fail. Prove RED before GREEN.

## R5 — QuickFIX parity (set-level oracle, 9 dicts)

**Reused pattern**: the 075 golden capture/consume (`tools/quickfix_enum_golden/main.cpp` guarded by `FIXPP_BUILD_QUICKFIX_GOLDEN`, off in CI; consumed via checked-in golden by `tests/wire/enum_golden_manifest_test.cpp` with no QuickFIX link at CI test time).

**Decision — capture the required-field SET, not frame verdicts**: frame accept/reject verdicts under-constrain (a frame that omits the differing tag passes even if the sets differ). Capture QuickFIX `DataDictionary`'s per-message required-field *set* and assert set-equality against fixpp's — this is the tiebreaker that makes the census non-circular (otherwise the independent walker and the loader implement the *same* AND-rule reading, and a shared misreading greens falsely).

**Scope — 9 QuickFIX-schema dicts only**: quickfix-cpp 1.16.0 does not parse Orchestra, so there is **no QuickFIX oracle for vlatest** ([[feedback_parity_corpus_row_needs_a_surface_the_reference_engine_has]]). vlatest's oracle is the independent raw-XML walker (R4), which also cross-checks the 9. quickfix-cpp is vendored at the parent git root `reference-engines/` (gitignored).

## R6 — Per-instance group required check + real frames

The candidate's `consume_group` bitmask check is the mechanism. Replace the candidate's synthetic per-instance pin with **real frames**: a conforming AP-without-underlyings → ACCEPT; a group whose second instance omits an intra-group required member → REJECT (offending tag surfaced). Named + one-per-version corpus (Clarifications).

## R7 — Two-tier verdict agreement

A test running the same frames (R6) through BOTH the runtime validator and the generated typed `validate_<Msg>` validator, asserting identical accept/reject. Guards the R3 conclusion that the codegen tier needs no change — if it unexpectedly fails, it localizes a missed codegen leg.

## R8 — Performance

The validator is a hot path (Article VIII §3; validate() 489–986 ns per 075). The required-set derivation and the per-group required-member store are built at dictionary-load / `as_table_view()` time — not per message. The only per-message addition is `consume_group`'s per-instance bitmask check, which runs only when a group is present. If a bench shows a measurable delta, add it in the PR ([[feedback_gateb_perf_change_needs_bench_not_a_metpartial_note]]); the design intent is near-zero per-message overhead for messages without groups.

## Open items for /tasks

- The census's independent expected-set walker must add message-level AND composition (group exclusion) on top of the 076 per-occurrence walk shape.
- Confirm `Dictionary::required_fields()` (or the exact shipped accessor the validator probes) is the right "actual" surface for the census; the IR-projection leg needs the codegen's top-level required list exposed to the test.
- The QuickFIX golden generator needs a per-message required-set extraction path in `DataDictionary` (distinct from the 075 enum-validate corpus).
