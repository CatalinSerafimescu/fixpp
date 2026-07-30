# Phase 0 Research: Structural Repeating-Group Detection

**Feature**: `082-structural-group-detection` | **Date**: 2026-07-29 | **Spec**: [spec.md](./spec.md)

All Technical Context unknowns are resolved here. Every decision below is grounded in a
read of the current source, cited by file and line.

---

## D-1 — The runtime structural predicate: `group_first_field(tag) != 0`

**Decision.** In both `as_table_view()` registration loops, replace
`fr.type == field_data_type::NumInGroup` with **`group_first_field(fr.tag) != 0`** — "the
dictionary declares a `<group>` whose count tag is `fr.tag`".

**Rationale.** `dict_metadata_handle::group_first_field_impl` (`src/dictionary/dictionary.cpp:92`)
binary-searches the `groups_` table — built exclusively from the `<group>` element
(`src/dictionary/xml_loader.cpp:580,645`, and the Orchestra sibling at
`orchestra_loader.cpp:626-637`) — and returns `0` when `no_tag` is not a declared group.
That is precisely the structural property required, and it is:

- **already public** — `Dictionary::group_first_field` (`include/fixpp/dict/dictionary.hpp:111`).
  **No new accessor, public or internal.** FR-017 is satisfied trivially.
- **member-independent** — `groups_` records a `GroupDef` per `<group>` element regardless of
  member count, so a zero-member group is visible (spec § Edge Cases).
- **per-dictionary** — it is an instance method on the loaded `Dictionary` (FR-003).
- **reachability-preserving** — it is applied as a *filter over `all_fields`* (this message's
  own field run), exactly where the datatype test sits today, so the set of messages a group
  registers under is unchanged. Enumerating `groups_` globally instead would register groups
  declared in unused components and would break FR-014's exact-set equality.

**Alternatives rejected.**

| Alternative | Rejected because |
|---|---|
| Derive `{fr.group_no_tag : != 0}` from `all_fields` | **Member-dependent.** A group's own count field carries its *parent's* `group_no_tag`, never its own, so a zero-member group has no field pointing at it and would vanish. Violates the Edge-Cases constraint. |
| New additive `Dictionary::groups()` enumeration accessor | Unnecessary — `group_first_field` already answers the question. A gratuitous public accessor enlarges the frozen surface for no gain. |
| Read `handle_->groups_` directly via `dictionary_internal.hpp` | Also unnecessary, and strictly more coupling than the existing public call. |
| Union predicate (`type OR structural`) | See D-2. |

## D-2 — Pure replacement, not union — and an honest statement of why

**Decision.** Replace the datatype gate outright (FR-002). Do **not** union it with the
structural test.

**Rationale — corrected during research.** The spec originally argued that a union would
preserve a *spurious* FIX43 tag-82 registration. **That premise is wrong and was corrected.**
Tag 82 is already unregistered today, because both stores reject it downstream:

- legacy bare store — `if (legacy_first == 0) continue;` where
  `legacy_first = group_first_field(82) = 0` (`dictionary.cpp:402`);
- context-scoped store — `if (members.empty()) continue;` (`dictionary.cpp:463`), since no
  FIX43 field carries `group_no_tag == 82`.

So a union and a pure replacement produce **identical results on all ten dictionaries as they
stand today**. The real case for replacement is design integrity:

1. **One source of truth.** A union leaves two independent notions of "is a group" that can
   disagree, with nothing arbitrating.
2. **It closes a latent trap.** The guards that currently absorb the FIX43 tag-82 mis-typing
   are incidental. Under a union, a future dictionary that mis-types a *member-bearing* tag as
   `NUMINGROUP` would register spuriously, with no guard left to catch it.
3. **It un-overloads `FieldRef::type`,** which today doubles as datatype *and* group marker.
4. Less code, no behavioral cost.

**Note this also makes the FIX43 story smaller than the issue implied:** the effective delta
is `+576` only. Tag 82 becomes a no-regression pin (FR-012).

## D-3 — The codegen structural predicate: a `VersionIR`-level group-tag set from `group_order`

**Decision.** Add one codegen-tool-local field to `VersionIR`:

```
std::vector<std::uint16_t> group_tags;   // sorted, unique — structurally declared group count tags
```

populated in `build_ir()` as the union of `{e.no_tag : e ∈ m.group_order}` over all messages.
Every emitter discovery/branch site consults **that set** instead of `FieldRef::type`.

**Rationale.** `walk_level` (`tools/codegen/fixpp-codegen/ir.cpp:80`) already detects groups by
**element name** (`else if (tag_name == "group")`) and appends the `GroupOrderEntry`
**unconditionally** — it only skips setting `delimiter_tag` when the member list is empty. So
`MessageIR::group_order` is:

- **already correctly populated for FIX42 today** (this is the single most important research
  finding — the fix is a re-point, not new plumbing);
- **member-independent**, satisfying the same Edge-Cases constraint as D-1;
- populated on **both** schema paths — `populate_group_order` for `<fix>` and
  `walk_orchestra_level` for Orchestra (`ir.cpp:316+`), so `vlatest` derives identically.

`emit_messages`' member map `gmm` is *already* keyed on `f.ref.group_no_tag`
(`emit_messages.cpp:412-416`) — structural. Only the **discovery** list `group_tags`
(`emit_messages.cpp:421-430`) is datatype-gated. Centralising discovery on `VersionIR` means
each emitter's remaining `type == NumInGroup` tests become set-membership tests against one
value, computed once.

**Alternatives rejected.**

| Alternative | Rejected because |
|---|---|
| Per-emitter local derivation from `group_order` | Four emitters would each re-derive the same set — divergence risk, and precisely the "census all handrolled scanners" anti-pattern. |
| Stamp a `bool is_group` onto `FieldIR` | `FieldIR::ref` is the verbatim 16-byte `FieldRef` POD emitted into `Fields.hpp`; adding a parallel flag risks the emitted array and re-overloads the per-field record. `Fields.hpp` must stay byte-identical (FR-016a). |
| Derive from `{f.ref.group_no_tag : != 0}` | Member-dependent, same defect as in D-1. |

## D-4 — `FieldRef::type` is not modified

**Decision.** No dictionary re-types any field. FIX42's count fields keep `INT`.

**Rationale.** This is what makes FR-016a falsifiable: `Fields.hpp` (constexpr `FieldRef`
arrays) and `Validator.hpp` are functions of datatype, not group structure, so both are
**expected byte-identical for v42** — a real prediction the regeneration diff can refute.
Re-typing would additionally mutate the wire type-conformance checks for those tags, which is
out of scope and would silently change FIX42 validation semantics.

`emit_manifest.cpp:73` is a pure datatype→token mapping (`NumInGroup → "num_in_group"`); since
no field is re-typed, it needs **no change**, and v42's manifest datatype axis is unchanged.

## D-5 — The change is ungated on the parse path (verified end-to-end)

**Decision.** Ship ungated (FR-006), and pin the parse-side correction with strict validation
**off** (SC-008/SC-008a).

**Rationale — both ends of the path verified.** The build site
`inbound_tv_ = cfg_.dictionary->as_table_view()` (`src/session/session.cpp:992`) sits in
`open()` with no `validate_inbound_messages` condition. Its comment claims the parser does not
yet consume it — **that comment is stale** (written mid-066 at T002). T006 landed: the consume
site is `Parser<access_mode::Index> pd_parser{*inbound_tv_}` in `parse_and_dispatch_`
(`src/session/session.cpp:328`), guarded only by an `assert` on the invariant, and
`parser.hpp:306` wires `group_member_fn` into the group context. Only the *strict validator*'s
second `as_table_view()` call (`session.cpp:1234`) is behind the flag.

Citing the build site alone would have been the "parity claim cited the line below the one that
decides" trap; both ends are now cited in FR-006a.

## D-6 — The non-circular oracle

**Decision.** `contracts/predicate_census.py` is checked in and becomes the source
`tests/dictionary/reused_tag_census.hpp` reads (FR-018).

**Rationale.** That helper (`:74,80`) established the L-063-1 carve-out using
`fr.type == NumInGroup` — the very predicate under change. Flipping it to the new predicate
would move it in lockstep with the code under test and witness nothing
(*corpus-built-from-the-read-it-checks-is-blind*). The Python oracle loads neither
`Dictionary`/`table_view` nor the codegen IR; it walks raw XML, resolving Orchestra codeset
indirection (`NoSidesCodeSet → NumInGroup`, which is why FIX Latest reports 524 ≡ 524). Its
output reproduces every number in spec.md's census table.

**Port target.** The C++ helper re-derives the same two sets from raw XML via the existing
codegen-tool-local pugixml pattern, so `SC-001`/`SC-002`/`SC-003` assert **exact-set equality in
both directions** against a source that cannot move with the fix.

## D-7 — Site inventory (the full census — no site may be missed)

Every `field_data_type::NumInGroup` occurrence in the tree, with its disposition:

| Site | Role | Disposition |
|---|---|---|
| `dictionary.cpp:398` | legacy bare-store discovery | **CHANGE** → D-1 predicate |
| `dictionary.cpp:441` | context-store `immediate_parent` build | **CHANGE** → D-1 predicate |
| `dictionary.cpp:446` | context-store outer loop | **CHANGE** → D-1 predicate (must move **with** :398 and :441 — FR-004) |
| `emit_messages.cpp:425` | version-wide group discovery | **CHANGE** → D-3 set |
| `emit_messages.cpp:166` | `plan_dfs` child discovery | **CHANGE** → D-3 set |
| `emit_messages.cpp:234` | group-member scalar skip | **CHANGE** → D-3 set |
| `emit_messages.cpp:337,347` | message top-level scalar vs group accessor | **CHANGE** → D-3 set |
| `emit_reify.cpp:217,227` | owning-class scalar vs group accessor | **CHANGE** → D-3 set |
| `emit_builders.cpp:606` | `top_level_synthetic_members` `is_grp` | **CHANGE** → D-3 set |
| `emit_manifest.cpp:73` | datatype→token name | **NO CHANGE** (D-4) |
| `gen_util.hpp:162` | datatype→`TypeKind` mapping | **NO CHANGE** (D-4) |
| `xml_loader.cpp:70`, `orchestra_loader.cpp:113` | XML/Orchestra type-name tables | **NO CHANGE** (D-4) |
| `field_type.hpp:70` | enum→string | **NO CHANGE** |

Test-side sites carrying the predicate or the carve-out, all requiring revisit:
`tests/dictionary/reused_tag_census.hpp:74,80` (→ D-6), `reused_tag_census_test.cpp:158`,
`required_scope_test.cpp:107`, `required_scope_census_test.cpp:341`,
`tests/wire/validator_type_check_test.cpp:966`, `tests/codegen/test_067_emit_builders_unit.cpp:662`.
`tests/dictionary/fixt_header_merge_test.cpp:88` is a local XML type-name parser — no change.

## D-8 — v42 builder-tier re-instatement is a one-line driver change plus goldens

**Decision.** Delete the `if (ir.ns != "v42")` guard at `tools/codegen/fixpp-codegen/main.cpp:132`
and introduce **no** replacement version predicate (FR-010).

**Rationale.** `vt11` self-skips through a genuinely empty application-message registry inside
`emit_builders`, not through a name test, so removing the `v42` name test leaves exactly one
policy in the code. `--families` defaults to `CoverageMode::All` (`main.cpp:69`), so the default
build emits all **39** v42 application messages; `--families official` gets its own pinned
golden on the same terms as the existing versions.

**Descope pins to invert (not delete):** `tests/codegen/test_077_builder_no_emit.cpp`
(`V42EmitsNoBuilders`) and `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp`
(v42 expected set defined as ∅ by policy). Their `vt11` companions stay untouched (FR-016b).

## D-9 — FIX42 shape facts driving the expected deltas

Derived from `dictionaries/FIX42.xml` by the D-6 oracle plus a declaration walk:

- **18** distinct group tags: 33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384, 386, 398, 420, 428.
- **46** messages (39 `app`, 7 `admin`); **22** contain ≥1 group, **21** of those are `app`.
- **14** message/group pairs declare the group `required='Y'`, across 12 messages — including
  `NewOrderList`/`NoOrders`, `MarketDataRequest`/`NoRelatedSym`+`NoMDEntryTypes`,
  `MarketDataSnapshotFullRefresh`/`NoMDEntries`, `MassQuote`/`NoQuoteSets`+`NoQuoteEntries`.
- **5** nested (depth ≥ 1) group occurrences — including `MassQuote`'s `296 → 295`, which is
  US4's exemplar target.
- v42's current read golden has **92** emitted classes and **0** `class G_`.

## D-10 — Deltas expected per artifact (the by-construction budget for FR-016)

| Artifact | Expectation |
|---|---|
| `v42/Fields.hpp` | **byte-identical** (datatype-derived; D-4) |
| `v42/Validator.hpp` | **byte-identical** (`emit_validator.cpp` has *zero* group handling — shape/exhaustiveness rule tables only) |
| `v42/NormativeReferences.md` | **byte-identical** (message-level) |
| `v42/Manifest.txt` | datatype axis unchanged (D-4); any movement must be explained, not absorbed |
| `v42/Messages.hpp` | **0 → 18** `class G_`, plus per-message group accessors on the 22 group-bearing messages |
| `v42/Reify.hpp` | owning-class group accessors appear on the same 22 messages |
| `v44` / `v50sp2` / `vt11` / `vlatest` read goldens | **byte-identical** — pinned by regeneration diff (FR-015), *not* asserted from the census |
| `v44` / `v50sp2` / `vlatest` builder goldens | **byte-identical** (FR-015) |
| `v42` builder goldens | **new** — 078 split set + `--families official` pin (FR-009) |

## D-11 — Risks

| Risk | Mitigation |
|---|---|
| A changed emitter perturbs an *unaffected* version's golden | FR-015 requires a real regeneration diff over all versions; D-3's set is empty-equivalent for versions where type set ≡ struct set, so `v44`/`v50sp2`/`vlatest`/`vt11` must diff clean or the change is wrong. |
| Half-restructure across the two `as_table_view()` stores | FR-004; D-7 lists all three sites as one change unit. |
| Stale build objects false-greening the golden tests | `tools/codegen/**` changed ⇒ run `ctest -L codegen`; force a clean codegen rebuild before trusting a golden diff (known emitter-staleness trap: non-debug dirs compile stale `Reify.hpp`). |
| v42 builder tier inflating CI compile cost | v42 is small (39 messages / 18 groups) next to v50sp2 (558) and vlatest; 078 already made builder tests link the prebuilt library. Measure, don't assume. |
| The 079 required-scope path activating for FIX40/41/42 | Intended and accepted (FR-006b); the enumerated new rejections are pinned (SC-008a), and `required_scope_census_test.cpp:341`'s carve-out text must be rewritten rather than left stale. |
