# Phase 1 Data Model: Structural Repeating-Group Detection

**Feature**: `082-structural-group-detection` | **Date**: 2026-07-29

This feature changes **which existing data is consulted**, not what data exists. Only one new
element is introduced, and it is codegen-tool-local. Entities below are described by their role
in group detection; fields not relevant to that role are omitted.

---

## Entity 1 — `GroupRef` / the `groups_` table (runtime, EXISTING, unchanged)

The structural record of a declared repeating group. One entry per `<group>` element.

| Field | Role in detection |
|---|---|
| `no_tag` | the group's count tag — **the identity this feature keys on** |
| `first_field_tag` | the delimiter; `group_first_field(no_tag)` returns it, or **0 if `no_tag` is not a declared group** |
| `first_field_index`, `field_count` | span into the per-group side table (`group_fields(no_tag)`) |
| `parent_group_no_tag` | enclosing group, 0 at top level |

**Population**: `xml_loader.cpp:580,608-649` (`<fix>` schema) and `orchestra_loader.cpp:626-637`
(`fixr:` schema), both keyed on the **element**, never the datatype. Sorted by `no_tag`; looked up by
binary search (`dictionary.cpp:92-99`). **FR-023 adds one validation on this population path** — a
`<group>` with no resolvable member is rejected at load instead of recorded with
`first_field_tag == 0` (I-1a).

**Invariants used by this feature**
- **I-1 (not field-run-derived)** — the table is built from the `<group>` element, never from
  `{fr.group_no_tag}` or a `members.empty()` test, so it cannot conflate "this dictionary declares a
  group with count tag T" with "T has members in this message". *(This — not member-independence —
  is why D-1 keys on `groups_`.)* Scope: this is an invariant of the **table** and of the detection
  predicate reading it; `dictionary.cpp:463`'s post-detection `members.empty()` **registration**
  guard is downstream of both, unchanged by 082, and not in tension with it (contract P1 / P1-NON).
- **I-1a (sentinel ambiguity — a NON-invariant of the accessor, whose INPUT FR-023 removes)** — an
  entry *is* pushed for every `<group>` element (`:644`/`:649` are unconditional), but a
  **member-less** one carries `first_field_tag == 0`, which `group_first_field_impl` (`:92-99`)
  cannot distinguish from "not a group". So a zero-member group would **not** be visible through
  this accessor, and the earlier claim that it is stays withdrawn. The accessor is unchanged by 082;
  what changes is that **FR-023 rejects a member-less `<group>` at load in both loaders**, so no
  admitted `Dictionary` can carry the ambiguous state. The case is unrepresentable downstream
  (`table_view::set_group_first(t, 0)` would insert member tag 0), which is why it is rejected rather
  than tolerated — contract C1.1 / P1-NON / K11, research D-1a. The ambiguity itself is still real
  *in isolation*; only its reachability changed.
- **I-2 (per-dictionary)** — the table belongs to one loaded `Dictionary`; the same tag may be a
  group in one dictionary and a plain field in another (FIX43/FIX44 tags 82 and 576 are exactly
  this). Detection must never be keyed globally by tag (FR-003).
- **I-3 (12-byte POD, ABI-frozen)** — `sizeof(GroupRef) == 12`, standard-layout, trivially copyable
  (`group_ref.hpp`). **Unchanged** — this feature adds no field.

**Not modified as a type.** No field, layout, or accessor changes; FR-023 changes only which
documents the loaders admit into the table.

## Entity 2 — `FieldRef::type` (runtime, EXISTING, unchanged)

The field's declared datatype. Today it is **overloaded**: datatype *and* group marker.

**After this feature it carries datatype only.** FIX42's count fields keep `INT`; no dictionary
re-types any field (D-4). This is load-bearing for FR-016a: `Fields.hpp` emits the verbatim
`FieldRef` array, so it is predicted **byte-identical** — a falsifiable claim the regeneration
diff can refute.

`FieldRef::group_no_tag` (the *immediate enclosing* group) is unchanged and still supplies
per-message membership in the context-scoped store.

## Entity 3 — `table_view` group stores (runtime, EXISTING, contents change)

Two stores, both populated by `Dictionary::as_table_view()`, both of which must move together
(FR-004 — a half-restructure would leave them disagreeing):

| Store | Keyed by | Populated at | Detection site |
|---|---|---|---|
| legacy bare | `no_tag` | `dictionary.cpp:398` loop | `:398` |
| 063 context-scoped | `(msg_type, parent path, no_tag)` | `dictionary.cpp:441-463` loop | `:441`, `:446` |

**State change (the whole feature, at the runtime tier):**

Counts are **reachability-restricted and measured** by `contracts/predicate_census.py` — a group
registers only if transitively reachable from a `<message>`, including via `<header>`/`<trailer>`
(expanded into every message's run at `xml_loader.cpp:926-931`). See contract C2.

| Dictionary | groups registered before | after |
|---|---:|---:|
| FIX40 | 0 | **4** |
| FIX41 | 0 | **7** |
| FIX42 | 0 | **18** |
| FIX43 | 33 | **34** (`+1 tag (576)`) |
| FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT11 / FIX Latest | 59 / 67 / 97 / 505 / 1 / 524 | **unchanged** |

FIX50/SP1/SP2 register 2 fewer than they *declare* (`NoHops(627)`, `NoMsgTypes(384)` — unreachable
because those dictionaries ship an empty `<header/>` and `Logon` lives in FIXT11). Unreachable both
before and after, so I-4 is unaffected.

**I-4 (exact-set, both directions)** — for the six unchanged dictionaries the registered set must
be *equal*, not merely a superset (FR-014). A subset check would pass while silently dropping a
group. **I-4 binds the BARE store's tag set.** The context store is keyed
`(msg_type, parent path, no_tag)` and is pinned separately by **I-4a**.

**I-4a (per-context member-set equality)** — for each `(msg_type, parent path, no_tag)` context, the
context store's direct-member set must equal the FR-018 oracle's set for that same key. A tag-set
projection across the two stores is **not** sufficient: it passes while every per-context member set
is wrong, which is the 063 Defect-A shape. The discriminating subject is a tag whose member lists
genuinely diverge — `NoRelatedSym(146)`, 4 distinct lists across 6 FIX42 occurrences, or
`NoOrders(73)`, 3 across 3. `LinesOfText(33)` cannot witness it (both occurrences carry identical
members `{58, 354, 355}`).

**I-5 (reachability preserved)** — a group registers under the messages whose field run contains
its count tag, exactly as today. Enumerating `groups_` globally instead would register
component-only groups and break I-4.

## Entity 4 — `VersionIR::group_tags` (codegen, **NEW**, tool-local)

```
std::vector<std::uint16_t> group_tags;   // sorted, unique
```

The version-wide set of structurally declared group count tags — the single codegen-side source
of "is tag T a group".

- **Derived from**: the union of `{e.no_tag : e ∈ m.group_order}` over all `ir.messages`.
- **Populated in**: `build_ir()`, after `populate_group_order` / the Orchestra projection.
- **Consumed by**: `emit_messages` (`:166`, `:234`, `:337`, `:347`, `:425`), `emit_reify`
  (`:217`, `:227`), `emit_builders` (`:606`).
- **Not consumed by**: `emit_fields`, `emit_validator`, `emit_manifest`, `emit_normative_refs` —
  which is precisely why their `v42` output is predicted byte-identical (D-10). (`emit_manifest`
  emits nothing at all for `<fix>`-schema versions: `MessageIR::occurrences` is Orchestra-only, so
  `v42` has no `Manifest.txt` — spec FR-016.)

**I-6 (not field-run-derived, mirrors I-1)** — `walk_level` keys on the element name and appends a
`GroupOrderEntry` unconditionally (`ir.cpp:80-100`), never consulting `{f.ref.group_no_tag}`. A
derivation from `{f.ref.group_no_tag : != 0}` would conflate declaration with per-message membership
and is rejected for that reason. *As a side effect `group_tags` would see a member-less `<group>` —
a stronger property than the runtime tier can offer (I-1a) — but the case is **moot post-FR-023**,
since no dictionary carrying one loads at all. It MUST NOT be restated as a feature-level guarantee;
see contract P1-NON.*

**I-6a (per-tag, not per-plan)** — `group_tags` is a set of group **count tags**. The read tier
consumes it per tag (`emit_messages.cpp:139`, `group_cls(no_tag) = "G_" + no_tag`, over **all**
messages including admin ⇒ 18 `class G_` for `v42`). The **builder** tier re-keys onto
`(no_tag, recursive signature)` plans over the `is_application`-gated message set
(`emit_builders.cpp:236-267`, `:1300-1302`) ⇒ **28** plans over **17** tags for `v42` under
`--families all` (`384` excluded: its only host is `Logon`, `msgcat='admin'`). **18 ≠ 28 ≠ 17 is
by construction, not a discrepancy** — research D-9a. A tag count must never be used as a
plan/file-count proxy.

**I-7 (schema-agnostic)** — `group_order` is populated on both paths (`populate_group_order` for
`<fix>`, `walk_orchestra_level` for Orchestra), so `vlatest` derives identically and its golden
must not move.

**I-8 (ABI-inert)** — codegen-tool-local; no runtime type, no `GroupRef`/`FieldRef` change, no
C-ABI implication (FR-017).

## Entity 5 — `MessageIR::group_order` (codegen, EXISTING, unchanged)

Declaration-order `GroupOrderEntry` list per message, recursive to every nesting depth.
**Already correctly populated for FIX42 today** — the single most consequential research finding,
since it means the codegen side is a re-point rather than new plumbing.

## Entity 6 — Golden corpus (EXISTING, contents change)

| Golden | Change |
|---|---|
| `specs/003-.../golden/v42_Messages.golden.hpp` | 46 message classes unchanged; **0 → 18** `class G_`; group accessors on 22 messages |
| `specs/003-.../golden/v44_`, `v50sp2_`, `vt11_Messages.golden.hpp` | **byte-identical** |
| `specs/076-.../golden/vlatest_Messages.golden.hpp` | **byte-identical** |
| `specs/078-.../golden/` (v44 / v50sp2 / vlatest split sets) | **byte-identical** |
| `specs/078-.../golden/` **v42 split set** | **NEW**, `--families all` only — **226** files: 39 × (`messages/<Msg>.hpp` + `.builder.inl` + `.builder.cpp` + `.validator.inl` + `.validator.cpp`) + **28** `groups/<PlanName>.hpp` + `groups.hpp` + `validators/traits.hpp` + `all.hpp` |
| `--families official` (any version) | **no golden exists** — 078 retired the official-mode byte gate (`determinism_test.cpp:898-909`); the pin is the structural witness `OfficialModeBuildersStructuralShape` (`:920-948`). `v42`'s instantiation: 147 files / 19 plan headers / registry 25. |

**I-9** — byte-identity is established by an actual regeneration diff, never inferred from the
census (spec § Context: a raw-XML census is not loader truth — the `NoSidesCodeSet` case proves it).

**I-10 (golden coverage is one-of-five, not five-of-five)** — the 003 corpus holds only
`*_Messages.golden.hpp`. There is **no** checked-in golden for `v42`'s `Fields.hpp`, `Validator.hpp`,
`Reify.hpp` or `NormativeReferences.md`, so FR-016's classification of those four rests on the
regeneration diff recorded in the verify record plus FR-021's by-construction gate for the group
axis — not on a durable byte pin. Stating which artifacts *are* pinned is part of FR-016.

---

## FR → pin map

Every requirement has a named witness. No FR relies on inspection alone. **28 FRs**
(FR-001..FR-023 plus 006a/006b/006c/016a/016b), all 28 covered by the 26 rows below (two rows cover
two FRs each: FR-006+006b share a witness, FR-016+016a share one). Rows carrying a *Location* note name
the directory `/speckit-tasks` must place the witness in — added where § Project Structure had none.

| FR | Pin |
|---|---|
| FR-001 | **Behavioral, not a token grep.** Runtime path: FIX43 tag 576 registering proves no datatype gate survives in `as_table_view()` (576 is `INT`-typed — it *cannot* register while a datatype gate exists). Codegen path: `v42` emitting 18 `class G_` proves the same for the emitters. A token census over `NumInGroup` would false-fail on the **six** legitimate remaining occurrences (`emit_manifest.cpp:73`, `gen_util.hpp:162`, the two loader type-name tables, `field_type.hpp:70`, `src/capi/message_write.cpp:134-137`) or need an allowlist that drifts. |
| FR-002 | FIX43: 576 registered **and** 82 not registered, from one predicate |
| FR-003 | Cross-dictionary: tag 576 is a group in FIX43+FIX44; tag 82 in neither — asserted per dictionary |
| FR-004 | **Per-context member-set** equality (I-4a/K4) for a **divergent-signature** tag — `NoRelatedSym(146)`, 4 distinct member lists across 6 occurrences: the context store holds the distinct set per `(msg_type, path)` matching the oracle, the bare store holds the loader's first-seen set. Not a tag-set projection; not tag 33 (identical members, unobservable collapse). *Location: `tests/dictionary/`* |
| FR-005 | Exact-set equality vs the raw-XML oracle for FIX40 (4) / FIX41 (7) / FIX42 (18), **bare** store, both directions |
| FR-006, 006b | Strict-validation ON: the new group-required rejections equal the dictionary-derived enumeration. **P4's named-source leg additionally pinned by K6b** — on FIX42, `fixpp_msg_group_begin(t)` succeeds for exactly the bare store's registered tag set, both directions, so a divergent second structural realization inside the runtime tier cannot pass. *Location: `tests/session/` (the strict-ON enforcement pin — it rides `validate_inbound_messages`, so it is a `Session`-level parse/dispatch test and sits beside FR-006a's strict-OFF sibling) + `tests/capi/` (cross-path K6b). Corrected at `/speckit-analyze` (finding F2): this row previously read `tests/dictionary/` for the registration leg, which is where the *set* pins live (FR-005/FR-014) but not where the enforcement witness belongs.* |
| FR-006a | Parse-correction pin with `validate_inbound_messages` **OFF**. *Location: `tests/session/`* |
| FR-006c | B&L behavior-change row + release note present |
| FR-007 | `v42` 078 split file set emitted (inverts `V42EmitsNoBuilders`) |
| FR-008 | `validate_<Msg>` rejects an `Args` omitting a `required='Y'` group, at all 14 pairs — 13 built as top-level omissions and **one, `MassQuote`/295-inside-296, built as a 296 *entry* with an empty 295 span** (checked per-entry via `gc.validate_entry`, not by a top-level `group_checks` row). *Location: `tests/codegen/`* |
| FR-009 | `v42` builder golden-SET match, `--families all` (226 files / 28 plan headers / registry 39) **+** a `--families official` **structural** witness mirroring `OfficialModeBuildersStructuralShape` (147 files / 19 plan headers / registry 25) — *not* a golden. *Location: `tests/codegen/`* |
| FR-010 | No version-name predicate remains in `main.cpp`; `vt11` still self-skips via empty registry |
| FR-011 | FIX43 tag 576 registered with member `ClearingInstruction` |
| FR-012 | FIX43 tag 82 unregistered as a group **and** enforced as a plain required field in `ListStatus` |
| FR-013 | FIX43 registered set differs from baseline by exactly `+1 tag (576)` |
| FR-014 | Exact-set equality both directions for the six unchanged dictionaries, against the **registered-after** (reachability-restricted) column: 59 / 67 / 97 / 505 / 1 / 524 |
| FR-015 | Regeneration diff: 4 read goldens + 3 builder golden sets byte-identical |
| FR-016, 016a | All **five** `v42` artifacts classified (`Manifest.txt` is not one — Orchestra-only emitter); `Fields.hpp` + `Validator.hpp` byte-identical; one of the five golden-pinned (I-10) |
| FR-016b | Both 077 descope tests inverted (not deleted); `vt11` companions unchanged; expected `v42` plan set **derived from the interning rule** (28 / 17 tags under `all`), never transcribed |
| FR-017 | No `capi/` diff; symbol golden untouched — verified by the `nm`-based `abi-golden.yml` gate, this repo's current abidiff-equivalent check (abidiff itself retired 2026-06-22, superseded, not regenerated) |
| FR-018 | `reused_tag_census.hpp` reads an **extension of `tests/dictionary/required_scope_oracle.hpp`** (no third walker), which reproduces the reachability restriction and equals C2's registered-after column on all ten |
| FR-019 | L-063-1 / L-061-1 / L-066-1 / L-077-1 closed; FIX43 correction recorded; L-066-1's stale `dictionary.cpp:335` cite refreshed to `:398` |
| FR-020 | `.specify/constitution.md` Article XVIII §7 no longer says v42 builders are deferred; Status banner carries a v0.11 line; Article I §1 unchanged; user ratification recorded in-branch (SC-011) |
| FR-021 | `v42` class-side (`Messages.hpp` text) ⟷ raw-XML-oracle consistency gate — the by-construction reconciliation behind SC-004's 0 → 18 delta. *Location: `tests/codegen/`* |
| FR-022 | **Three** Article VIII benchmark obligations in the same PR — **§2 re-baselining for (a)/(b), §3 run-and-record for (c)** (leg (c) produces no baseline, so it discharges §3): **(a)** `as_table_view()` build-time profile re-measured (`BM_TableView_BuildFix{44,50SP2}` + a new FIX 4.2 row, `BM_TableView_Sizeof` re-reported) with a **new** checked-in baseline — the leg that measures the changed function; **(b)** FIX 4.2 group-bearing parse bench + fresh baseline; **(c)** existing `compile_time_bench` run and its `v42` figure recorded under the load-bearing ≤3 s ceiling (gate, not baseline). Plus SC-012's 8-file pre-existing set within ±5%. *Location: (a) `bench/dictionary/table_view_footprint_bench.cpp` + `bench/baselines/dictionary/table_view_footprint_bench.json`; (b) `bench/wire/` + `bench/baselines/wire/`; (c) `bench/codegen/compile_time_bench/` (no new file — `ctest -L bench`)* |
| FR-023 | **A rejection test per loader** (K11): a synthetic member-less-`<group>` fixture → the `<fix>` loader throws `xml_parse_error`, the Orchestra loader throws `orchestra_parse_error`, each diagnostic naming the group's `name` and `no_tag`. The fixture MUST place the member-less `<group>` at a **non-first-seen** occurrence of its `no_tag`, or the pin cannot fail on a check wrongly placed inside the first-seen-wins dedup guard (`xml_loader.cpp:609`, `orchestra_loader.cpp:626`). **Plus the no-regression leg**: all **ten** vendored dictionaries still load clean. B&L behavior row + release note present (FR-023, shares the FR-019 closure pass). *Location: `tests/dictionary/`* |

## Anti-patterns this model is shaped to avoid

- **Circular corpus** — I-6/FR-018: the census must not read the predicate it checks.
- **Forked oracle** — FR-018: extend `required_scope_oracle.hpp`; a third `tests/dictionary/` XML
  walker would be the same single-source failure this feature fixes, one tier up.
- **Subset instead of exact-set** — I-4: containment would hide a silent drop.
- **Proxy instead of the named post-condition** — I-4a: "the two stores agree" projected to a tag
  set passes while every per-context member set is wrong.
- **A pin whose subject cannot fail** — I-4a: `LinesOfText(33)`'s two occurrences have identical
  members, so it cannot witness a context collapse. Use a divergent tag.
- **Half-restructure** — FR-004: the two `as_table_view()` stores move as one unit; and FR-006/P4:
  `as_table_view()` must not adopt a *different* structural accessor from the capi write path.
- **Golden "regenerated" without explanation** — D-10 budgets each artifact in advance, so an
  unexpected delta is a finding rather than something absorbed into a diff.
- **Field-run-derived structural predicate** — I-1/I-6: conflates declaration with per-message
  membership.
- **Tag count used as a file/plan count** — I-6a: 18 tags ⇒ 28 builder plan headers over 17 tags.
- **Expected set transcribed from the first run** — FR-016b: an exact-set completeness gate whose
  expected value came from the emitter enshrines whatever the emitter did.
- **"No hot-path change" asserted from the diff** — D-12: the diff is setup-time; the FIX40/41/42
  parse *behaviour* moves from a `group_bits_` clear-bit short-circuit to real group resolution.
- **Documentation closure that stops at B&L** — FR-020: it must reach `.specify/constitution.md`.
- **A fail-closed rule placed inside a first-seen-wins dedup guard** — FR-023/K11: the rejection
  would then depend on declaration order, admitting a member-less second occurrence of a tag whose
  first occurrence had members. Validate per occurrence, and pin it on a non-first-seen fixture.
