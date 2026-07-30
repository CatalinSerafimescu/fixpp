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

**Population**: `xml_loader.cpp:580,645` (`<fix>` schema) and `orchestra_loader.cpp` (`fixr:` schema),
both keyed on the **element**, never the datatype. Sorted by `no_tag`; looked up by binary search
(`dictionary.cpp:92`).

**Invariants used by this feature**
- **I-1 (member-independence)** — an entry exists for every `<group>` element regardless of member
  count. A zero-member group is therefore visible. *(This is why D-1 keys on `groups_` rather than
  on `{fr.group_no_tag}`.)*
- **I-2 (per-dictionary)** — the table belongs to one loaded `Dictionary`; the same tag may be a
  group in one dictionary and a plain field in another (FIX43/FIX44 tags 82 and 576 are exactly
  this). Detection must never be keyed globally by tag (FR-003).
- **I-3 (12-byte POD, ABI-frozen)** — `sizeof(GroupRef) == 12`, standard-layout, trivially copyable
  (`group_ref.hpp`). **Unchanged** — this feature adds no field.

**Not modified.**

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

| Dictionary | groups registered before | after |
|---|---:|---:|
| FIX40 | 0 | **4** |
| FIX41 | 0 | **7** |
| FIX42 | 0 | **18** |
| FIX43 | 33 | **34** (`+576`) |
| FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT11 / FIX Latest | 59 / 69 / 99 / 507 / 1 / 524 | **unchanged** |

**I-4 (exact-set, both directions)** — for the six unchanged dictionaries the registered set must
be *equal*, not merely a superset (FR-014). A subset check would pass while silently dropping a
group.

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
  which is precisely why their `v42` output is predicted byte-identical (D-10).

**I-6 (member-independence, mirrors I-1)** — `walk_level` appends a `GroupOrderEntry`
unconditionally (`ir.cpp`), skipping only the `delimiter_tag` assignment when members are empty.
So `group_tags` sees a zero-member group. A derivation from `{f.ref.group_no_tag : != 0}` would
not, and is rejected for that reason.

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
| `specs/003-.../golden/v42_Messages.golden.hpp` | **0 → 18** `class G_`; group accessors on 22 messages |
| `specs/003-.../golden/v44_`, `v50sp2_`, `vt11_Messages.golden.hpp` | **byte-identical** |
| `specs/076-.../golden/vlatest_Messages.golden.hpp` | **byte-identical** |
| `specs/078-.../golden/` (v44 / v50sp2 / vlatest split sets) | **byte-identical** |
| `specs/078-.../golden/` **v42 split set** | **NEW** — `messages/<Msg>.*`, `groups/<PlanName>.hpp`, `groups.hpp`, `validators/traits.hpp`, `all.hpp` + the `--families official` pin |

**I-9** — byte-identity is established by an actual regeneration diff, never inferred from the
census (spec § Context: a raw-XML census is not loader truth — the `NoSidesCodeSet` case proves it).

---

## FR → pin map

Every requirement has a named witness. No FR relies on inspection alone.

| FR | Pin |
|---|---|
| FR-001 | Site census test: zero `field_data_type::NumInGroup` detection gates remain in the nine production sites (D-7 table) |
| FR-002 | FIX43: 576 registered **and** 82 not registered, from one predicate |
| FR-003 | Cross-dictionary: tag 576 is a group in FIX43+FIX44; tag 82 in neither — asserted per dictionary |
| FR-004 | Both stores queried for the same newly-visible FIX42 group; must agree |
| FR-005 | Exact-set equality vs the raw-XML oracle for FIX40 (4) / FIX41 (7) / FIX42 (18) |
| FR-006, 006b | Strict-validation ON: the new group-required rejections equal the dictionary-derived enumeration |
| FR-006a | Parse-correction pin with `validate_inbound_messages` **OFF** |
| FR-006c | B&L behavior-change row + release note present |
| FR-007 | `v42` 078 split file set emitted (inverts `V42EmitsNoBuilders`) |
| FR-008 | `validate_<Msg>` rejects an `Args` omitting a `required='Y'` group, at all 14 pairs |
| FR-009 | `v42` builder golden match + `--families official` pin |
| FR-010 | No version-name predicate remains in `main.cpp`; `vt11` still self-skips via empty registry |
| FR-011 | FIX43 tag 576 registered with member `ClearingInstruction` |
| FR-012 | FIX43 tag 82 unregistered as a group **and** enforced as a plain required field in `ListStatus` |
| FR-013 | FIX43 registered set differs from baseline by exactly `{+576}` |
| FR-014 | Exact-set equality both directions for the six unchanged dictionaries |
| FR-015 | Regeneration diff: 4 read goldens + 3 builder golden sets byte-identical |
| FR-016, 016a | All six `v42` artifacts classified; `Fields.hpp` + `Validator.hpp` byte-identical |
| FR-016b | Both 077 descope tests inverted (not deleted); `vt11` companions unchanged |
| FR-017 | No `capi/` diff; symbol golden + abidiff baseline untouched |
| FR-018 | `reused_tag_census.hpp` derives from raw XML; mutating the predicate cannot silence it |
| FR-019 | L-063-1 / L-061-1 / L-066-1 / L-077-1 closed; FIX43 correction recorded |

## Anti-patterns this model is shaped to avoid

- **Circular corpus** — I-6/FR-018: the census must not read the predicate it checks.
- **Subset instead of exact-set** — I-4: containment would hide a silent drop.
- **Half-restructure** — FR-004: the two `as_table_view()` stores move as one unit.
- **Golden "regenerated" without explanation** — D-10 budgets each artifact in advance, so an
  unexpected delta is a finding rather than something absorbed into a diff.
- **Member-derived structural predicate** — I-1/I-6: invisible to zero-member groups.
