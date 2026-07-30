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
(`src/dictionary/xml_loader.cpp:580,608-649`, and the Orchestra sibling at
`orchestra_loader.cpp:626-637`) — and returns `0` when `no_tag` is not a declared group.
That is precisely the structural property required, and it is:

- **already public** — `Dictionary::group_first_field` (`include/fixpp/dict/dictionary.hpp:111`).
  **No new accessor, public or internal.** FR-017 is satisfied trivially.
- **already the runtime tier's predicate in production** — the C-ABI **outbound write** path
  decides group-ness structurally today, at four call sites, with exactly this accessor:
  `is_group_collision()` (`src/capi/message_write.cpp:157`,
  `h->dict_->group_first_field(tag) != 0`), the delimiter resolve at `:719`, and the
  `fixpp_msg_group_begin` / nested-group entry gates at `:812` and `:923`
  (`group_first_field(group_tag) == 0 ⇒ FIXPP_ERR_TYPE_MISMATCH`). The pre-082 library is not
  uniformly group-blind on FIX40/41/42 — it is **asymmetric**: structural on the C-ABI write
  side, datatype-gated on the read/validate side. Adopting `group_first_field` in
  `as_table_view()` therefore *converges* the runtime tier onto its existing single predicate
  (C1.3 **P4**) rather than adding a second realization. Any other accessor — including
  `Dictionary::group(no_tag).has_value()` — would fork it, which is the half-restructure
  FR-004 exists to prevent.
- **not derived from a message's own field-run membership** — this is the property that
  actually discriminates D-1 from its rejected alternative. `groups_` is built from the
  `<group>` element; it never consults `{fr.group_no_tag}` or a `members.empty()` test, so it
  cannot conflate "**D** declares a group with count tag T" with "T has members **in this
  message**". See D-1a below for what this does *not* claim on its own, and for the loader
  rejection (FR-023) that removes the one input on which the delimiter sentinel is ambiguous.
- **per-dictionary** — it is an instance method on the loaded `Dictionary` (FR-003).
- **reachability-preserving** — it is applied as a *filter over `all_fields`* (this message's
  own field run), exactly where the datatype test sits today, so the set of messages a group
  registers under is unchanged. Enumerating `groups_` globally instead would register groups
  declared in unused components and would break FR-014's exact-set equality. This is not
  hypothetical: **FIX50/SP1/SP2 each declare 2 groups that are unreachable** — `NoHops(627)`
  (their `<header/>` is empty; FIXT owns the header, per 081/L-041-2) and `NoMsgTypes(384)`
  (belongs to `Logon`, which lives in FIXT11) — so global enumeration would add 2 spurious
  registrations to each and fail C3. Note `<header>`/`<trailer>` **are** expanded into every
  message's run (`xml_loader.cpp:926-931`), which is why `NoHops` *is* reachable in
  FIX43/44/FIXT11 but not in FIX50SPx.

### D-1a — Zero-member `<group>`: REJECTED at the loader (OD-1 resolved 2026-07-30 — FR-023)

Earlier drafts of this research claimed D-1 is *member-independent* in the strong sense that "a
zero-member group is visible". **That claim is false at the source and is withdrawn.** The
correction, verified line-by-line:

- `xml_loader.cpp:608-649` — `std::uint16_t first_field_tag = 0;` (`:610`) is only assigned if a
  `field`/`group` child resolves in `by_name_`, or a `component` child's first direct `<field>`
  resolves. `GroupDef gd{}` is then recorded **unconditionally** with
  `gd.first_field_tag = first_field_tag;` (`:644`) and `groups_.push_back(gd)` (`:649`). A
  member-less `<group>` therefore yields a `GroupDef` whose `first_field_tag` is **0**.
- `dictionary.cpp:92-99` — `group_first_field_impl()` returns 0 both when `no_tag` is absent from
  `groups_` **and** when the found entry's `first_field_tag` is 0. The sentinel is ambiguous, so
  `group_first_field(t) != 0` collapses a declared zero-member group into "not a group".
- `orchestra_loader.cpp:626-637` — the Orchestra sibling has the same shape, so this is not
  schema-specific.

**The reason this state has no tolerable form is representational, and it binds every candidate
predicate, not just D-1.** The three grounds below were originally written to justify *accepting* the
case as a bounded limitation; the user's OD-1 decision (below) turns them into the grounds on which
the case is **rejected at load** instead:

1. **`table_view` cannot express a delimiter-less group.** `table_view::group_first_field(no_tag)`
   returns 0 for "not a group" (`include/fixpp/dict/table_view.hpp:290`) — the same ambiguous
   sentinel one tier up. And `table_view::set_group_first(no_tag, first)` (`:570-575`) calls
   `set_group_bit(no_tag)` **and** `add_group_member(no_tag, first)`, so registering a zero-member
   group would set the group bit and insert **member tag 0** — a malformed registration the parser
   and validator would then consume. A group's delimiter *is* its operative identity on the wire;
   a delimiter-less group has no representation any consumer can use.
2. **The 063 context-scoped store cannot deliver the property at all**, under any predicate:
   `dictionary.cpp:463` is `if (members.empty()) continue;` — a member-dependent guard that
   survives a predicate swap. Under FR-004 (both loops move together and stay mutually
   consistent), a "zero-member groups are visible" property is unachievable in one of the two
   stores by construction.
3. **No vendored dictionary declares one.** `contracts/predicate_census.py` emits no
   zero-member-`<group>` warning for any of the ten (S0), and an independent walk of
   FIX40/41/42/43 finds zero `<group>` elements with no children. `spec.md` § Pre-spec census is
   therefore right that a member-derived predicate "loses nothing today" — but that is a statement
   about today's *data*, not about the predicate's algebra. **This ground is now FR-023's
   no-regression leg**: zero vendored dictionaries are affected by the rejection, so there is no
   regression against the shipped set — and it is *not* the ground on which the case is dispositioned,
   because reachability says nothing about third-party XML a consumer loads at runtime.

**DECISION — RESOLVED BY THE USER 2026-07-30: take the fail-closed loader rejection (FR-023).**
The revision reviewed at Gate A recorded the case as a documented non-property and a bounded
limitation (silent skip), and flagged the fail-closed alternative as spec § Open decisions **OD-1**.
The user chose the **alternative**. A member-less `<group>` is now a **load error** in both loaders —
`src/dictionary/xml_loader.cpp` (member scan `:610-641`, recorded `:644`, pushed `:649`) throwing
`xml_parse_error`, and `src/dictionary/orchestra_loader.cpp` (`first_member_tag(group_node)` returning
0 at `:629`, helper `:467`, record block `:626-635`) throwing `orchestra_parse_error`. Neither needs
a new exception subclass; see FR-023 for the full disposition, and note the rejection must be
evaluated **per `<group>` occurrence**, outside the first-seen-wins dedup guards
(`xml_loader.cpp:609`, `orchestra_loader.cpp:626`) — so OD-1's original "roughly a one-line addition
per loader" cost estimate does not survive.

**The deciding evidence — the loader's own existing fail-closed dispositions, which no review pass
censused.** It is not that a *new* validation rule was accepted; it is that the member-less `<group>`
was the last structurally-broken group form still passing silently:

- `src/dictionary/xml_loader.cpp` is already aggressively fail-closed on malformed dictionary
  structure — **27** `throw` sites — and **two are group-specific**.
- `xml_loader.cpp:584` already throws `xml_parse_error` for a `<group name="X">` with **no matching
  `<field>` declaration (NoXxx tag)". A structurally-broken *group declaration* is therefore already
  a load error, and a member-less `<group>` is that case's **sibling**.
- `xml_loader.cpp:1017` throws `group_delimiter_collision_error::make(...)` when a nested group's
  delimiter collides with its parent's, under the guard
  `if (g.first_field_tag != 0 && g.first_field_tag == parent.first_field_tag)`. **That `!= 0` means
  the zero-delimiter case is already special-cased into silence at the one site whose entire purpose
  is fail-closed delimiter validation** — the member-less group is that guard's **excluded** case.
- `include/fixpp/dict/error.hpp:67-85` records the house standard the new diagnostic follows:
  `group_delimiter_collision_error::make` names "the three facts an operator needs to fix the
  offending dialect" (`:73`).

So rejection **mirrors two existing dispositions** rather than inventing a rule. And the objection
recorded against it —
that it widens the load-failure surface for third-party XML — is largely **already true** via `:584`.
The default's justification was a *reachability* claim ("no vendored dictionary declares one" — true,
measured across all ten, §3 above), and reachability does not cover XML a consumer loads at runtime.

**What this does and does not change about the predicate.** `group_first_field`'s sentinel is
**still ambiguous when read in isolation** — `dictionary.cpp:92-99` is untouched by 082. What FR-023
removes is the ambiguous *input*: no `Dictionary` the loaders admit can carry a member-less group, so
over the admitted set `group_first_field(t) != 0` is exactly C1's predicate with **no caveat**. This
is why contract C1.3 **P1-NON** is rewritten from a tolerated non-property into a retired one, rather
than the ambiguity being claimed away.

**Alternatives rejected.**

| Alternative | Rejected because |
|---|---|
| Derive `{fr.group_no_tag : != 0}` from `all_fields` | **Derived from a message's own field-run membership**, which is exactly what the predicate must not be: a group's own count field carries its *parent's* `group_no_tag`, never its own, so this set conflates "declared a group" with "has members in this message" and mis-answers for any tag reused as a plain scalar. |
| `Dictionary::group(no_tag).has_value()` (Codex's counter-proposal) | It *does* exist and is public (`include/fixpp/dict/dictionary.hpp:133`, impl `dictionary.cpp:141-148`) and *does* test presence in `groups_` without the sentinel. Rejected anyway, on two grounds: (a) it delivers the zero-member property at the **producer** and breaks it at the **consumer** — `table_view::set_group_first(t, 0)` would register member tag 0 (D-1a §1), and `dictionary.cpp:463`'s `members.empty()` guard defeats it in the context store regardless (D-1a §2); (b) it would **fork the runtime tier's structural predicate**, since `src/capi/message_write.cpp:157/719/812/923` already use `group_first_field(t) != 0` — breaking C1.3 **P4** in the exact place FR-004 is written to prevent it. |
| New additive `Dictionary::groups()` enumeration accessor | Unnecessary — `group_first_field` already answers the question. A gratuitous public accessor enlarges the frozen surface for no gain. |
| Read `handle_->groups_` directly via `dictionary_internal.hpp` | Also unnecessary, and strictly more coupling than the existing public call. |
| Union predicate (`type OR structural`) | See D-2. |

## D-2 — Pure replacement, not union — and an honest statement of why

**Decision.** Replace the datatype gate outright (FR-002). Do **not** union it with the
structural test.

**Rationale — corrected during research.** The spec originally argued that a union would
preserve a *spurious* FIX43 tag-82 registration. **That premise is wrong and was corrected.**
Tag 82 is already unregistered today, because both stores reject it downstream:

- legacy bare store — `if (legacy_first == 0) continue;` (`dictionary.cpp:403-405`) where
  `legacy_first = group_first_field(82) = 0` (the lookup is at `:402`);
- context-scoped store — `if (members.empty()) continue;` (`dictionary.cpp:463`), since no
  FIX43 field carries `group_no_tag == 82`.

So a union and a pure replacement produce **identical results on all ten dictionaries as they
stand today**. The real case for replacement is design integrity:

1. **One source of truth — and the runtime tier already *has* it in production.** A union leaves
   two independent notions of "is a group" that can disagree, with nothing arbitrating. This is a
   **convergence** argument, not an aspiration: `src/capi/message_write.cpp:157/719/812/923`
   already decide group-ness with `group_first_field(t) != 0` on the C-ABI outbound write path
   (D-1). Structural replacement in `as_table_view()` makes the runtime tier single-sourced; a
   union would leave the read/validate side holding a *second*, datatype-flavoured notion that the
   write side does not share.
2. **It closes a latent trap.** The guards that currently absorb the FIX43 tag-82 mis-typing
   are incidental. Under a union, a future dictionary that mis-types a *member-bearing* tag as
   `NUMINGROUP` would register spuriously, with no guard left to catch it.
3. **It un-overloads `FieldRef::type`,** which today doubles as datatype *and* group marker.
4. Less code, no behavioral cost.

**Note this also makes the FIX43 story smaller than the issue implied:** the effective delta
is `+1 tag (576)`. Tag 82 becomes a no-regression pin (FR-012).

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
- **not derived from a message's own field-run membership** — the same discriminating property as
  D-1: `walk_level` keys on the element name and appends the `GroupOrderEntry` unconditionally,
  never consulting `{f.ref.group_no_tag}`. It skips only the `delimiter_tag` assignment when the
  member list is empty, so `group_tags` *would* see a zero-member `<group>` on the codegen side —
  but the runtime side cannot represent one (D-1a), so this is a stronger property on one tier,
  not a feature-level guarantee. Do not restate it as one. **Moot post-FR-023**: no dictionary
  declaring a member-less `<group>` reaches the IR at all, since it no longer loads;
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
| Derive from `{f.ref.group_no_tag : != 0}` | Derived from a message's own field-run membership, same defect as in D-1. |

## D-4 — `FieldRef::type` is not modified

**Decision.** No dictionary re-types any field. FIX42's count fields keep `INT`.

**Rationale.** This is what makes FR-016a falsifiable: `Fields.hpp` (constexpr `FieldRef`
arrays) and `Validator.hpp` are functions of datatype, not group structure, so both are
**expected byte-identical for v42** — a real prediction the regeneration diff can refute.
Re-typing would additionally mutate the wire type-conformance checks for those tags, which is
out of scope and would silently change FIX42 validation semantics.

`emit_manifest.cpp:73` is a pure datatype→token mapping (`NumInGroup → "num_in_group"`) inside a
single exhaustive `field_data_type` switch (`:65-130`) with **no group branching**; since no field is
re-typed, it needs **no change**. It is **not a detection site** — the manifest's group axis is
`OccurrenceIR::group_path` (`ir.hpp:79`, populated at `ir.cpp:354`/`:396` from the structural walk),
already structural. FR-001's absolute must therefore be scoped to *detection* sites, or an
implementer following it literally would rip out `:73` and move a manifest — against three separate
statements in D-4/D-7/D-10 that it must not change.

**And `v42` has no manifest at all.** `MessageIR::occurrences` is populated **only** by
`populate_orchestra_projection` (`ir.cpp:476`); the `<fix>`-schema `populate_group_order` does not
produce occurrences. So `emit_manifest` short-circuits on `if (!any_occurrences) return {};`
(`emit_manifest.cpp:154-166`) for every `<fix>`-schema version, and `main.cpp:29`'s
`if (content.empty()) return;` writes no file. Verified empirically against a generated tree: the
`v42` directory holds exactly `Fields.hpp`, `Messages.hpp`, `NormativeReferences.md`, `Reify.hpp`,
`Validator.hpp` — and only `vlatest` carries a `Manifest.txt`. FR-016 is corrected from "all six
emitted `v42` artifacts" to **five**.

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

**Port target — EXTEND 079's shared oracle; do NOT fork a third XML walker.** The reviewed
revision planned "a fresh raw-XML derivation inside `reused_tag_census.hpp`". That would make
**three** independent XML walkers in `tests/dictionary/` and repeat, one tier up, the exact
single-source failure this feature is about. `tests/dictionary/required_scope_oracle.hpp` already
**is** this instrument, and 079's own task text names the rule being broken: its banner (`:3-13`)
records that it was "the SHARED, non-circular independent oracle **extracted out of**
`required_scope_census_test.cpp` … so `required_scope_parity_test.cpp` can reuse the SAME
QuickFIX-XML walker rather than forking it", quoting "**do NOT duplicate/fork the walker logic,
that would break the single-oracle guarantee**".

What it already has, verified at the source:

- a from-scratch `pugixml` walk of the vendored XML with an explicit non-circularity banner
  (`:19-30`) — it must not call `XmlLoader`/`OrchestraLoader`/`build_ir()`;
- `<component>` ref resolution (`:169-179`) and `<header>`/`<trailer>` inclusion as an explicit
  parameter, `include_header_trailer` (`:196-197`, walked at `:223-232`);
- `GroupContextKey{msg_type, path, no_tag}` (`:64-72`), explicitly mirroring `table_view.hpp`'s
  `group_ctx_key` convention (path EXCLUDES `no_tag`);
- `DictOracle::group_members` (`:81`) — **every real per-context group → its direct member tags**,
  which is precisely the census 082 needs. `{k.no_tag : k ∈ group_members}` **is** C2's
  "registered after" column;
- an Orchestra sibling (`orch_walk`/`build_orchestra_oracle`), so `vlatest` is covered by the same
  extension.

**Decision.** FR-018's oracle is an **extension of `required_scope_oracle.hpp`** (add the group-tag
census projection over its existing `group_members` walk), consumed by
`tests/dictionary/reused_tag_census.hpp`. Overriding 079's single-oracle rule would require a
stated reason; there is none.

**The reachability obligation — the port's hardest half, previously unstated.** C2's "registered"
columns are **reachability-restricted**: a group registers only if transitively reachable from a
`<message>`, including via `<header>`/`<trailer>`, which `xml_loader.cpp:926-931` expands into
**every** message's run. FIX50 / FIX50SP1 / FIX50SP2 **declare** 69 / 99 / 507 group tags but
**register** 67 / 97 / 505 — `NoHops(627)` is unreachable there (those dictionaries ship an empty
`<header/>`; FIXT owns the header, 081 / L-041-2) and `NoMsgTypes(384)` belongs to `Logon`, which
lives in FIXT11. A ported oracle that compares *declared* sets would fail SC-002's
both-directions equality on three dictionaries — and the usual failure mode is that it then gets
weakened until it stops pinning anything. So: the oracle MUST reproduce component expansion **and**
the `<header>`/`<trailer>` merge (`include_header_trailer = true`), and its ten-dictionary output
MUST equal C2's "registered after" column **exactly**. That is what makes "both directions"
meaningful.

With that, `SC-001`/`SC-002`/`SC-003` assert **exact-set equality in both directions** against a
source that cannot move with the fix.

**Exercised, not merely named.** `contracts/predicate_census.py` was run at Gate A round 1 and
reproduces C2 exactly, including the FIX50x reachability subtlety and **no** zero-member-`<group>`
warning on any of the ten (S0's expected output).

## D-7 — Site inventory (the full census — no site may be missed)

**Counting unit: one row = one *line*-site.** (Earlier drafts mixed units — see `plan.md`
§ Scale/Scope. Rows that previously grouped `emit_messages.cpp:337,347` and
`emit_reify.cpp:217,227` are now split, so the production total is **11 detection line-sites**
— 3 runtime + 8 emitter — plus **1 driver site** (`main.cpp:132`, D-8) = **12 lines changed**,
across **9 disposition groups** if counted by role.)

Every `field_data_type::NumInGroup` occurrence in the tree, with its disposition:

| Site | Role | Disposition |
|---|---|---|
| `dictionary.cpp:398` | legacy bare-store discovery | **CHANGE** → D-1 predicate |
| `dictionary.cpp:441` | context-store `immediate_parent` build | **CHANGE** → D-1 predicate |
| `dictionary.cpp:446` | context-store outer loop | **CHANGE** → D-1 predicate (must move **with** :398 and :441 — FR-004) |
| `emit_messages.cpp:425` | version-wide group discovery | **CHANGE** → D-3 set |
| `emit_messages.cpp:166` | `plan_dfs` child discovery | **CHANGE** → D-3 set |
| `emit_messages.cpp:234` | group-member scalar skip | **CHANGE** → D-3 set |
| `emit_messages.cpp:337` | message top-level scalar vs group accessor | **CHANGE** → D-3 set |
| `emit_messages.cpp:347` | message top-level scalar vs group accessor | **CHANGE** → D-3 set |
| `emit_reify.cpp:217` | owning-class scalar vs group accessor | **CHANGE** → D-3 set |
| `emit_reify.cpp:227` | owning-class scalar vs group accessor | **CHANGE** → D-3 set |
| `emit_builders.cpp:606` | `top_level_synthetic_members` `is_grp` | **CHANGE** → D-3 set |
| `main.cpp:132` | driver `ir.ns != "v42"` builder-tier exclusion | **DELETE** (D-8) — *driver* site, not a detection site; counted separately |
| `emit_manifest.cpp:73` | datatype→token name | **NO CHANGE** (D-4) |
| `gen_util.hpp:162` | datatype→`TypeKind` mapping | **NO CHANGE** (D-4) |
| `xml_loader.cpp:70`, `orchestra_loader.cpp:113` | XML/Orchestra type-name tables | **NO CHANGE** (D-4) |
| `field_type.hpp:70` | enum→string | **NO CHANGE** |
| `src/capi/message_write.cpp:134-137` | `is_int_category()` — `t == T::NumInGroup` in the integer-category test | **NO CHANGE** — a datatype question, correctly answered by the datatype. *(Was absent from this census in the reviewed revision; a census that claims "every occurrence" and misses one is not a census.)* |

**The six group-ness decisions that carry no `NumInGroup` token.** Four are the already-structural
C-ABI write sites — the reason D-1 must stay `group_first_field`-based. The other two sit **inside
`as_table_view()` itself**, adjacent to the changed lines, and an implementer needs their
disposition stated here because this census is where they will look. **Counting note:** none of
these six is a *detection* site, so none is counted in the banner's 11 detection line-sites + 1
driver site; the two new rows cite line **spans** (lookup + guard) rather than single line-sites for
that reason.

| Site | Role | Disposition |
|---|---|---|
| `src/capi/message_write.cpp:157` | `is_group_collision()` — `group_first_field(tag) != 0` | **ALREADY STRUCTURAL — no change.** Establishes the runtime tier's single predicate (C1.3 P4). |
| `src/capi/message_write.cpp:719` | delimiter resolve — `dict->group_first_field(e.tag)` | **ALREADY STRUCTURAL — no change.** |
| `src/capi/message_write.cpp:812` | `fixpp_msg_group_begin` gate — `== 0 ⇒ TYPE_MISMATCH` | **ALREADY STRUCTURAL — no change.** Consequence: `fixpp_msg_group_begin(268)` on a FIX42 dictionary **already succeeds today** (see C4.4's write leg). |
| `src/capi/message_write.cpp:923` | nested-group entry gate, same test | **ALREADY STRUCTURAL — no change.** |
| `dictionary.cpp:402-405` — `legacy_first = group_first_field(legacy_no_tag)` (`:402`) then `if (legacy_first == 0) { continue; }` (`:403-405`) | bare-store guard: skip a `NumInGroup`-typed tag that is not a declared group | **FOLD / REDUNDANT.** Once the filter at `:398` *becomes* `group_first_field(fr.tag) != 0`, this guard is **tautologically true — dead code**. The natural form is a single lookup: hoist `group_first_field(fr.tag)` into the filter and reuse its value as the delimiter. Folding is preferred over leaving it (a dead branch would also cost Article IX branch coverage); either way the implementer must decide deliberately, not inherit it. |
| `dictionary.cpp:463` — `if (members.empty()) continue;` | context-store guard: skip a declared group with no members **in this message** | **NO CHANGE — post-detection registration guard, retained.** It runs *after* the detection filter at `:446`, so it is outside the predicate's scope, and it is why a *declared* member-less group could never have been visible in the context store under **any** predicate (the original ground for contract **P1-NON**). Post-FR-023 a member-less `<group>` can no longer reach it — such a dictionary does not load — but the guard is unchanged and still fires for its own, different case: a group whose count tag is in this message's field run while its members are not. Not to be conflated with P1's ban on a `members.empty()` **detection** test — see C1.3 P1's scoping clause. |

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
build emits all **39** v42 application messages; `--families official` gets its own **structural
witness** on the same terms as the existing versions (**D-10** — 078 retired the official-mode pinned
golden at `tests/codegen/determinism_test.cpp:898-909`, replacing it with the
`OfficialModeBuildersStructuralShape` witness at `:920-948`; there is no `--families official` golden
for any version).

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
- **7** of the 18 group tags carry **divergent direct-member lists across their contexts** — the
  063 Defect-A shape, and the input to D-9a's plan arithmetic: `146` → 4 distinct member lists
  across 6 occurrences, `73` → 3 across 3, `295` → 3 across 3, and `78` / `268` / `296` / `420`
  → 2 each. The remaining 11 are single-signature. **`33 LinesOfText` is NOT one of them**: its
  two occurrences (`News`, `Email`) have **identical** member lists `{58, 354, 355}` — see
  spec § Edge Cases, where it was replaced as the context-scoping example for exactly that reason.
- v42's current read golden (`specs/003-dictionary-codegen/contracts/golden/v42_Messages.golden.hpp`)
  emits **46** message classes (`^class <Msg> {$`) and **0** `class G_`. *(An earlier draft said
  "92 emitted classes" — that was a raw `grep -c 'class '` artifact: 46 `class <Msg> {`
  definitions + 46 `class owning_<Msg>;` forward declarations. Corrected; the operative figures
  are 46 and 0.)* For comparison, `v44`'s golden is 93 message classes + **59** `class G_`.

## D-9a — The `v42` **builder-tier plan** arithmetic (derived, not approximated)

077's builder tier is keyed on the **plan**, not the tag: `emit_builders.cpp:236-267` interns on
`(no_tag, recursive_signature)`, and `assign_plan_names()` (`:327-340`) emits `G_<no_tag>Args` for a
single-signature tag but `G_<no_tag>_<k>Args` for every tag with ≥ 2 signatures. The signature
(`compute_signature`, `:280-319`) is `D<delimiter>;` followed, per member, by
`G<no_tag>:<group_required>:<group_check_required>:{<child signature>};` for a group and
`S<tag>:<required>:<kind>:<coupled>[:<data_tag>];` for a scalar — so `kind`, `coupled`, `data_tag`,
the **RAW** `group_required` and the 081-gated `group_check_required` (`:283-300`) all fork a plan
independently. **`v42` is the first version where ordinaling fires on newly-visible groups**, so
these counts are new information, not inherited.

Derived from that rule (see the derivation note below), the `groups/<PlanName>.hpp` file count is
`intern.plans.size()` exactly — `emit_builders.cpp:1377-1381` emits one header per interned plan:

| `--families` | in-scope messages | group tags reaching the builder tier | **distinct `(no_tag, signature)` plans** = `groups/*.hpp` | full emitted file set |
|---|---:|---:|---:|---:|
| `all` (default, `main.cpp:69` `CoverageMode::All`) | **39** | **17** | **28** | **226** = 39×5 + 28 + 3 |
| `official` (`is_official`, the frozen 33-MsgType set) | **25** | **11** | **19** | **147** = 25×5 + 19 + 3 |

- **Ordinaled tags under `all`**: `146` → 4, `73` → 3, `295` → 3, and `78` / `268` / `296` / `420`
  → 2 each — **7 tags contributing 18 plans**; the other 10 tags name bare, contributing 10
  (18 + 10 = 28). Under `official` the reachable tag set narrows to 11 and the fork pattern
  changes: `146` → 3, `295` → 3, `73` / `78` / `268` / `296` → 2 each (6 tags / 14 plans) plus 5
  bare (14 + 5 = 19). **Plan names are mode-dependent** — a tag that is bare under one mode can be
  ordinaled under the other, because `assign_plan_names()` keys on the *final* per-`no_tag`
  distinct-signature count over the in-scope set only. The two modes' goldens/witnesses must
  therefore never be cross-compared by plan name.
- **18 declared group tags, but only 17 reach the builder tier.** `384 NoMsgTypes`'s only host is
  `Logon`, which is `msgcat='admin'`; `emit_builders`' `in_scope` predicate (`:1300-1302`) is
  `is_application`-gated, so 384 produces **no** plan. This is the one count in the feature where
  the read tier and the builder tier legitimately disagree, and it is why "18 group tags" must not
  be used as a file-count proxy — see D-10.
- **The read tier is keyed per TAG, not per plan.** `emit_messages.cpp:139`'s
  `group_cls(no_tag) = "G_" + no_tag` emits one flyweight per group tag, and its discovery loop
  (`:420-431`) walks **all** `ir.messages` including admin — so `Messages.hpp` gains exactly
  **18** `class G_` (384 included). **18 read classes vs 28 builder plan headers over 17 tags** is
  the by-construction reconciliation SC-004 asks for; the two numbers are not in tension.
- **Derivation note (checked in, self-validating, reproducible).** These figures come from applying
  `emit_builders`' own signature/intern/naming rule to `dictionaries/FIX42.xml`, *not* from a tool
  run (082 is not implemented, so `v42` emits zero builders today). The derivation is checked in as
  **`contracts/builder_plan_census.py`**, alongside `predicate_census.py`, and **self-validates on
  every run** against the three **shipped** builder tiers:

  ```
  $ python3 specs/082-structural-group-detection/contracts/builder_plan_census.py
  OK   v44 --families all: 83 msgs / 88 plans, name set == specs/078-.../golden/v44/groups exactly
  OK   v50sp2 --families all: 156 msgs / 558 plans, name set == specs/078-.../golden/v50sp2/groups exactly
  OK   v44 --families official: 33 msgs / 54 plans   (== determinism_test.cpp's kExpectedOfficialGroupPlanCount)
  ```

  It exits non-zero if any of the three diverges — including a **name-for-name** set comparison
  against the 078 goldens' `groups/` directories, all 88 and all 558 including every ordinal. A rule
  that reproduces three shipped artifacts exactly is the basis for the `v42` prediction; the reviewer
  can re-run it rather than take the number on trust. (It is a **design-time derivation aid**, not a
  shipped test and not the FR-018 oracle. `vlatest` is out of its scope — Orchestra IR comes from
  `walk_orchestra_level`, a different walker.)
- **Implementation obligation (FR-016b).** The completeness census's expected `v42` set MUST be
  derived **by construction from this same rule** — never transcribed from the first emitter run.
  A transcribed set is a corpus built from the read it checks.

## D-10 — Deltas expected per artifact (the by-construction budget for FR-016)

| Artifact | Expectation |
|---|---|
| `v42/Fields.hpp` | **byte-identical** (datatype-derived; D-4) |
| `v42/Validator.hpp` | **byte-identical** (`emit_validator.cpp` has *zero* group handling — shape/exhaustiveness rule tables only) |
| `v42/NormativeReferences.md` | **byte-identical** (message-level) |
| `v42/Manifest.txt` | **not emitted, before or after** — `MessageIR::occurrences` is Orchestra-only (`ir.cpp:476`), so `emit_manifest` returns empty for `<fix>`-schema versions (`emit_manifest.cpp:154-166`) and `main.cpp:29` writes no file. Verified against a generated tree. `v42` has **five** artifacts, not six (FR-016). Adding `<fix>`-schema `occurrences` population is out of scope. |
| `v42/Messages.hpp` | **46 message classes unchanged; 0 → 18** `class G_` (per TAG, incl. 384 from `Logon`), plus per-message group accessors on the 22 group-bearing messages |
| `v42/Reify.hpp` | owning-class group accessors appear on the same 22 messages |
| `v44` / `v50sp2` / `vt11` / `vlatest` read goldens | **byte-identical** — pinned by regeneration diff (FR-015), *not* asserted from the census |
| `v44` / `v50sp2` / `vlatest` builder goldens | **byte-identical** (FR-015) |
| `v42` builder golden set, `--families all` | **new** — **226** files: 39 messages × 5 + **28** `groups/<PlanName>.hpp` + `groups.hpp` + `validators/traits.hpp` + `all.hpp` (D-9a). `builder_registry` cardinality **39**. |
| `v42` builder tier, `--families official` | **no checked-in golden** — a **structural** witness, per 078's convention (below): **147** files = 25 × 5 + **19** plan headers + 3, `builder_registry` cardinality **25**. |

**Which of the five `v42` read artifacts actually has a durable pin.** The 003 golden corpus
contains only `*_Messages.golden.hpp` — there is **no** checked-in golden for `Fields.hpp`,
`Validator.hpp`, `Reify.hpp` or `NormativeReferences.md`. So exactly **one of five** is
golden-pinned. FR-016's strongest sentence ("'golden regenerated' alone is not sufficient
evidence") needs an instrument behind the other four.

**FR-021 — and why it is the class-side leg, not the 076 V-1/V-1b pair.**
`tests/codegen/vlatest_manifest_class_consistency_test.cpp` (076 T014, contracts V-1/V-1b) is
exactly the by-construction reconciliation FR-016 asks for, and its design is the right one: V-1
pins `manifest == raw-XML`, V-1b pins `manifest == shipped read class` (parsed from the *text* of
the generated `Messages.hpp`, including `group_view<...G_N>` return types), and composing the two
pins `class == raw-XML` **non-circularly**, through two different derivations. But it exists for
`vlatest` **only** — hardcoded via `FIXPP_CODEGEN_VLATEST_MANIFEST` /
`FIXPP_CODEGEN_VLATEST_MESSAGES_HPP` (`tests/codegen/CMakeLists.txt:206-209`) with a hardcoded
181-message expectation — **and V-1b keys on a `Manifest.txt` that `v42` does not have.** So the
pair cannot be instantiated for `v42` as-is; doing so for the `<fix>`-schema versions is blocked on
`occurrences` population, which is out of scope.

What FR-021 requires instead is the **class-side ⟷ raw-XML** leg directly: the class side parsed
purely from `v42/Messages.hpp`'s own C++ text per the version-agnostic extraction rule that test
documents at `:33-63` (`^class <Name> {$` message classes; `^    class G_<N> {$` group flyweights;
`view_.template get<N>()` scalar accessors; `group_view<...G_<N>>` return types marking a group
reference, with the transitive nested closure), compared against FR-018's oracle. That delivers the
property FR-016/SC-004 actually need — a by-construction reconciliation of the 0 → 18 `class G_`
delta, durable rather than a transcript — without new IR plumbing. Ideally it is
version-parameterised, which would close the same hole for `v44`/`v50sp2`/`vt11`; `v42` is the
requirement. The remaining (non-group) axes stay pinned by the regeneration diff recorded in the
`/speckit-verify` record.

**The `--families official` pin is STRUCTURAL, not a golden — correcting an inherited assumption.**
The reviewed revision said `v42` "gains its own pinned `--families official` golden … on the same
terms as the existing versions". That is not the convention: 078 **deliberately retired** the
pre-078 official-mode byte-identity gate. `tests/codegen/determinism_test.cpp:898-909` records the
rationale in full — the official subset's per-message bytes are already pinned by the default-mode
golden-SET diff, so "no `v44-official/` golden set is checked in". What *is* pinned, by
`OfficialModeBuildersStructuralShape` (`:920-948`), is the per-**mode** emitted file-SET shape and
the `builder_registry` array cardinality, against a fresh isolated `--families official` run —
because a regression that silently emitted the wrong subset under `official` would pass the
default-mode golden untouched. `v42`'s official-mode obligation is therefore an instantiation of
**that** witness with the D-9a figures (147 files / 19 plan headers / registry 25), not a new
golden directory. FR-009 is restated accordingly.

**Why "18 group tags" is not a file count.** § Scale/Scope and quickstart S6 previously carried
"18 group tags" next to the emitted-artifact discussion. The 078 split layout emits **28**
`groups/*.hpp` for `v42`, over **17** tags — materially more than 18, which is also the input to
D-11's "measure, don't assume" CI-compile-cost risk. State plan counts where plans are meant and
tag counts where tags are meant; never one as a proxy for the other.

## D-12 — Article VIII: **three** benchmark obligations — two EXISTING profiles move (`as_table_view()` build time, and the `v42` read-tier compile ceiling), plus an UNBENCHED FIX40/41/42 parse path

The reviewed revision's Article VIII verdict rested on "no hot-path change". Half of that is
verified and stands; the other half was never analysed, and the *reasoning* is load-bearing because
Article VIII §3 is absolute ("no perf change merged without a benchmark in the same PR") and
`plan.md` pre-emptively records the omission so Gate B does not read it as an oversight.

**What is verified.** `as_table_view()` has exactly two production call sites,
`src/session/session.cpp:992` and `:1234`, **both inside `open()`**. The added `group_first_field`
binary search is genuinely setup-time; the internally-tense phrase "an O(log G) binary search per
field per message, at setup only" describes the *setup loop's* own iteration over `all_fields`, not
a per-message cost. No hot-path line is edited.

**Setup-time is not the same as unmeasured, and Article VIII does not scope §2/§3 to the hot path.**
Read verbatim (`.specify/constitution.md:184-186`): §1 "Every perf-sensitive module has a benchmark
in `bench/`"; §2 "Regression budget: **±5%** vs `bench/baselines/` per profile. Intentional perf
changes update the baseline **in the same PR** with rationale in the PR body."; §3 "No perf change
merged without a benchmark in the same PR." §5 is the only hot-path-specific clause (allocator
policy) and is not what is at issue. §1 already classifies this module as perf-sensitive —
075 T011 created `bench/dictionary/table_view_footprint_bench.cpp` for *exactly* this measurement
(`bench/dictionary/CMakeLists.txt:23-26`). So the setup loop's own cost is inside §2/§3's reach,
and §1 to §3 is what the census in Decision §1 below must discharge.

**What was missing — the downstream effect of registration.** `table_view::group_bits_`
(`include/fixpp/dict/table_view.hpp:734`, accessor `group_bit()` `:736-742`) is an **exact
pre-filter** over both group stores, and its own comment (`:717-733`) states: "A clear bit ⟹ both stores miss ⟹ the
accessors' current behaviour is already 'return 0 / empty span' — the filter only skips the
(string+path) hash probes, never changes an answer." Today **every** group bit is clear for
FIX40/41/42, so every group probe short-circuits on a bounds-check. After 082 the bits are set, so
`group_ctx_` / `group_members_` hash probes run, group slices are built, and `group_member_fn`
(`include/fixpp/wire/parser.hpp:306`) does real work — **per message, for every FIX40/41/42
session**. `group_bits_` also grows from empty to `(max no_tag >> 6) + 1` words per `table_view`
copy (a few hundred bytes, per the FOOTPRINT-VARIANT note at `:729-733`).

**Decision — restate the verdict on correct grounds, and add the bench (option (a)).**

1. **Bench census — basis: *which bench profiles measure something 082 changes*.** The reviewed
   revision censused *which dictionaries the benches load*; set-equality of the registered group set
   then licensed "cannot move". That basis is wrong twice over: a bench can time a changed function
   on a dictionary whose registered *set* is unchanged, and a bench profile need not run a
   dictionary at all to move (a compile-time profile moves when the *emitted headers* grow).

   **Enumeration basis — closed, not a file glob.** A `bench/**/*.cpp` glob is itself incomplete:
   two profiles under `bench/` have **no `.cpp` and no `add_executable`**, because the measurement
   *is* a compiler invocation. The closed basis is *every bench profile registered under `bench/`* =
   the **32** `add_executable` binary targets (`find bench -name '*.cpp'` cross-checked against
   every `add_executable`) **∪** the **2** script-registered harnesses reached via `add_test`
   (`grep -rn add_test bench/` → 4 hits; two register existing binaries in `bench/tls` and
   `bench/threading`, two register the compile-time harnesses below) = **34** profiles:

   | Bench | Timed region | Disposition |
   |---|---|---|
   | `bench/dictionary/table_view_footprint_bench.cpp` `:113-116` (`BM_TableView_BuildFix50SP2`), `:129-132` (`BM_TableView_BuildFix44`) | `dictionary.as_table_view()` — **the changed function itself**; the `Dictionary` is loaded once *outside* the loop (`:112`, `:128`), and the file banner (`:5`, `:40`) confirms "only `as_table_view()` itself is timed" | **DOES MOVE — on every dictionary, magnitude unmeasured.** The per-field test in the bare loop goes from `fr.type != field_data_type::NumInGroup` (`dictionary.cpp:398`, one enum compare) to `group_first_field(fr.tag) != 0` — an **O(log G) binary search over `groups_` for every field of every message's run** (`dictionary.cpp:92-99`); same substitution at the context loop's two sites (`:441`, `:446`). On FIX50SP2 `G = 507` (≈9 comparisons). Set-equality is **irrelevant** here: FIX44/FIX50SP2 are C2 EQUAL, which is exactly why this cost is invisible to every set-based pin and visible only to this bench. No `bench/baselines/dictionary/table_view_footprint_bench.json` exists — the 075 baselines live as an in-file comment block (`:16-41` pre-change T011, `:42-62` post-change T032), so §2 has **no `bench/baselines/` profile to compare against**. Note the profile has a **known-tight history**: 075's own T032 re-measurement recorded `BM_TableView_BuildFix44` at **+5.06%**, already at/over §2's ±5% budget, accepted then. FR-022. |
   | `bench/dictionary/table_view_footprint_bench.cpp` `:91-97` (`BM_TableView_Sizeof`) | `sizeof(fixpp::dict::table_view)` | **NO MOVE — and it cannot see the axis that does.** 082 adds no member, so this compile-time constant is unchanged. The footprint that *does* grow is heap-side: `group_bits_` from empty to `(max no_tag >> 6) + 1` words per `table_view` **copy** on FIX40/41/42. `sizeof` does not capture it — so FR-022 requires this counter re-reported (to pin "unchanged") with the heap growth stated alongside, not inferred from it. |
   | `bench/wire/validator_bench.cpp` | `validator.validate(...)` only, `:302-310` | **NO MOVE — source-verified.** A *real* FIX44 `table_view` is used, but it is built at `:250` (`load_fix44_table_view`, `:77-79`) under the comment "dictionary + table_view (built once, outside the loop)" (`:247`), i.e. outside the measured window; and FIX44 is a C2 EQUAL row, so the registered set `validate()` walks is unchanged. |
   | `bench/dictionary/xml_loader_bench.cpp` (`FIX44`/`FIX42`/`FIX50SP2`, `:32`/`:48`/`:64`) | `XmlLoader::load` only | **NO MOVE from the detection re-point** — it never calls `as_table_view()` and never parses a message. **But FR-023 (OD-1, resolved post-convergence) does add work inside the timed region**: a per-`<group>`-occurrence member-resolution check in `XmlLoader::load` itself, which is exactly what `BM_XmlLoader_LoadFix{42,44,50SP2}` time. The original "never calls `as_table_view()`" ground no longer covers this profile, so it is re-dispositioned rather than left standing: **expected negligible, and stated as the actual delta rather than as a no-op**: the member scan (`xml_loader.cpp:610-641`) today runs **once per distinct `no_tag`**, because it sits inside the first-seen-wins dedup guard at `:609`; FR-023 makes it run **once per `<group>` occurrence**, so on a dictionary with heavily reused group tags the scan count multiplies by the average occurrences-per-tag (FIX50SP2 declares 507 group tags). The scan is a bounded walk of one element's direct children, so the absolute cost stays small — but it is a multiplier, not a re-run of work already done. **Already covered** — `bench/baselines/dictionary/xml_loader.json` exists and is in SC-012's 8-file ±5% re-check set, so the added load-path cost is measured there. **Deliberately NOT a fourth FR-022 leg**: the baseline and the re-check already exist, so promoting it would manufacture an obligation rather than close a gap (the same discipline that kept the `v42` builder-tier compile cost on D-11's risk row instead of in FR-022). |
   | `bench/wire/parser_bench.cpp`, `offset_table_bench.cpp`, `offset_table_footprint_bench.cpp` | parse / offset-table paths over a **test-double** `table_view` (`support/mock_dict_table.hpp`, `parser_bench.cpp:32`, `offset_table_bench.cpp:27`, `offset_table_footprint_bench.cpp:43`) | **NO MOVE — stronger than set-equality.** These load no dictionary at all; no `as_table_view()` output reaches them. |
   | `bench/wire/framer_bench.cpp`, `writer_bench.cpp`, `check_alive_bench.cpp` | framing / write / liveness | **NO MOVE.** No dictionary, no `table_view`. |
   | `bench/codegen/typed_accessor_bench.cpp` | generated typed accessors over wire bytes | **NO MOVE.** Loads no dictionary; and the codegen output it compiles against (`v44`/`v50sp2`/`vlatest`) is byte-identical (FR-015 / SC-005). Baseline `bench/baselines/codegen/typed_accessor_bench.json`. |
   | `bench/dictionary/reify_bench.cpp` | `owning_<Msg>::from_view()` dispatch over the frozen wire stub | **NO MOVE.** **Loads no dictionary** — verified: zero matches for `XmlLoader` / `dict_path` / `load(` in the file (R6-deferred stub, `:16-30`). Baseline `bench/baselines/dictionary/reify_bench.json`. |
   | The remaining **21** binary targets — `core/decimal_bench`, `log/{log_enqueue,log_spike}`, `session/*` (8), `sync/*` (2), `threading/bench_threading`, `tls/*` (3), `transport/*` (3), `src/placeholder_bench` | decimal arithmetic, logging, session FSM / seqnum / store / time, async mutex, threading, TLS, transport, placeholder | **NO MOVE.** No dictionary, no `table_view`, no codegen-emitted artifact on any timed path. |
   | `bench/codegen/compile_time_bench/` — script harness, no `.cpp`, registered by `add_test` (`CMakeLists.txt:23-33`, `LABELS bench`) | `clang++ -std=c++23 -fsyntax-only` wall time for **one TU per version**: `#include <fixpp/<ver>/Messages.hpp>` + `Reify.hpp`, `VERSIONS=(v42 v44 v50sp2 vt11)` (`compile_time_bench.sh:56`, `:69-83`) | **DOES MOVE — on the `v42` TU, against a HARD ceiling.** 082 adds 18 `class G_` group-accessor classes to `v42/Messages.hpp` and group accessors to `v42/Reify.hpp` (SC-004), so the v42 TU's syntax-only time grows. `SINGLE_CEILING=3` s is **load-bearing** (`:35`, banner `:10-11`) and **only `v50sp2` is exempted** as a KNOWN_OVERAGE (`:93-99`) — for `v42` an overage sets `STATUS="FAIL"`, `PASS=false`, and `:139-143` (`if ${PASS}; then … else … exit 1; fi`) **really does exit non-zero** (verified at the source, not inferred from `set -e`, which would not catch a `false` *variable*). By contrast the all-versions ceiling is **WARN-only** — `:132-135` sets `ALL_STATUS="WARN (soft ceiling exceeded)"` and never exits — so it reports, it does not gate. **But nothing runs this automatically:** `tier1.yml`'s `bench` job (`:1099-1106`) is a declared **soft** gate that runs only `./bin/placeholder_bench` against `bench/baselines/placeholder.json` under `continue-on-error: true` (`:1221-1227`); no workflow invokes `ctest -L bench`. So a `v42` overage would exit non-zero **locally and be invisible in CI**. That is precisely why FR-022 (c) makes running it an explicit obligation rather than relying on the gate: **run it and record the `v42` number**. No new bench and no `bench/baselines/` entry — it is a ceiling check, not a baseline comparison. |
   | `bench/codegen/vlatest_builders_compile_bench/` — script harness, no `.cpp`, registered by `add_test` (`CMakeLists.txt:31-41`, `LABELS "bench;077;compile-budget"`) | peak RSS + wall time of `-fsyntax-only` on `#include <fixpp/vlatest/messages/NewOrderSingle.hpp>` (`compile_bench.sh:100`) | **NO MOVE.** `vlatest`-only, and FR-015 / SC-005 keep every `vlatest` emitted artifact **byte-identical**. Its shape is nonetheless the relevant precedent for the gap noted below. |

   So **two** existing profiles move: `table_view_footprint_bench` (times the changed function) and
   `compile_time_bench`'s `v42` TU (compiles the grown headers). Every currently-*baselined* profile
   is still provably unmoved — that sentence was true and stays true; what it omitted is that
   *neither* mover has a `bench/baselines/` entry, which is why the old census read as a clean bill
   of health.

   **One stated gap, not a new obligation.** No profile measures the `v42` **builder** tier's compile
   cost — 082 emits **226** new files there (D-9a) and D-11 carries that as a risk row ("Measure,
   don't assume"). `compile_time_bench` covers only the *read* tier (`Messages.hpp` + `Reify.hpp`);
   `vlatest_builders_compile_bench` is the instrument shaped for the builder tier but is
   `vlatest`-scoped. Instantiating it for `v42` is the natural discharge of D-11's row and is
   recorded there as such; FR-022 does **not** require it, because 078 already made builder tests
   link the prebuilt per-version library rather than re-including the headers. Stated so the gap is
   a decision, not an omission.
2. **No existing baseline covers FIX4x group *parsing*** either — so on that leg there is likewise
   nothing to regress against, which is exactly why an N/A verdict on §3 grounds would be vacuous
   rather than true.
3. Therefore this is an Article VIII **§2 intentional perf change** with **three** obligations in the
   same PR, all required by FR-022: (a) re-measure the `as_table_view()` build-time profile —
   `BM_TableView_BuildFix44` / `BM_TableView_BuildFix50SP2`, plus a new FIX 4.2 row the bench lacks —
   and check in `bench/baselines/dictionary/table_view_footprint_bench.json`, the profile §2's ±5%
   budget currently has nothing to compare against; and (b) a **FIX42 group-bearing parse bench plus
   a fresh baseline**, reusing the 061/067 harnesses; and (c) **run `ctest -L bench`'s existing
   `compile_time_bench` and record the `v42` TU number** — no new bench, but a load-bearing ≤3 s
   ceiling that only `v50sp2` is exempt from, so 082's 18 new `class G_` must be shown to fit under
   it rather than assumed to. (a) measures the diff directly; (b) measures its downstream parse
   consequence; (c) measures the emitted-header growth. All three are cheap, all three convert an
   unmeasured change into a recorded one, and all three pre-empt the Gate B finding. The alternative
   — waive with the reasoning above recorded — is strictly weaker and is not taken.
4. **SC-012's ±5% re-check set.** Selection rule: every checked-in `bench/baselines/` profile whose
   bench executes dictionary, wire-parse, or codegen-emitted code — the only three subsystems 082
   touches. **The "dictionary" leg spans the whole of `src/dictionary/`, both loaders included** —
   made explicit here because FR-023 (OD-1) edits `XmlLoader`/`OrchestraLoader`, and this rule was
   first written when the load path was dispositioned NO MOVE; without the clarification the rule
   would not license `dictionary/xml_loader.json`'s membership on the ground the row above now cites
   it for. That is **8** of the 20 checked-in baseline files:
   `wire/{framer_bench,offset_table_bench,parser_bench,validator_bench,writer_bench}.json`,
   `codegen/typed_accessor_bench.json`, `dictionary/{reify_bench,xml_loader}.json`. The other **12**
   — `decimal_baseline.json`, `log/log_enqueue.json`, `session/*` (7), `sync/async_mutex_baselines.json`,
   `threading/threading_baselines.json`, `placeholder.json` — are excluded by the rule: no dictionary,
   no `table_view`, no codegen artifact on their timed paths. `dictionary/reify_bench.json` is *in*
   the set by subtree but its bench loads no dictionary (row above), so it is a no-move member kept
   in the enumeration for auditability rather than silently dropped. This same 8-file list is used in
   `spec.md` SC-012 and `quickstart.md` S9 — the reviewed revision enumerated it three inconsistent
   ways.

## D-11 — Risks

| Risk | Mitigation |
|---|---|
| A changed emitter perturbs an *unaffected* version's golden | FR-015 requires a real regeneration diff over all versions; D-3's set is empty-equivalent for versions where type set ≡ struct set, so `v44`/`v50sp2`/`vlatest`/`vt11` must diff clean or the change is wrong. |
| Half-restructure across the two `as_table_view()` stores | FR-004; D-7 lists all three sites as one change unit. |
| Stale build objects false-greening the golden tests | `tools/codegen/**` changed ⇒ run `ctest -L codegen`; force a clean codegen rebuild before trusting a golden diff (known emitter-staleness trap: non-debug dirs compile stale `Reify.hpp`). |
| v42 builder tier inflating CI compile cost | v42 is small — **39 messages / 28 group plans / 226 emitted files** (D-9a, the derived figures, *not* "18 groups") — next to v50sp2 (156 / 558) and vlatest (173 / 577); 078 already made builder tests link the prebuilt library. Measure, don't assume. **No bench profile covers this** (D-12 §1): `compile_time_bench` measures only the *read* tier, and `vlatest_builders_compile_bench` — the instrument shaped for a builder-tier probe — is `vlatest`-scoped. Instantiating it for `v42` is the natural discharge of this row if the risk is to be measured rather than argued; deliberately **not** made an FR-022 obligation, on the 078 prebuilt-library ground above. |
| **FIX40/41/42 parse path moves from a clear-bit short-circuit to real group resolution** | The diff is setup-time, but the *behaviour* is not: see D-12. Mitigated by adding a FIX42 group-bearing parse bench + a fresh baseline (FR-022) rather than by asserting "no hot-path change". |
| **`as_table_view()` build time moves on *every* dictionary — the one bench that times the changed function has no checked-in baseline** | The per-field test becomes an O(log G) `groups_` binary search, so `table_view_footprint_bench` moves even where the registered set is set-equal (D-12 §1). Mitigated by FR-022's **leg (a)**: re-measure `BM_TableView_BuildFix{44,50SP2}` + a new FIX 4.2 row and check in `bench/baselines/dictionary/table_view_footprint_bench.json`. Measure, don't assume — 075's own T032 re-measurement already put this profile at +5.06%. |
| The 079 required-scope path activating for FIX40/41/42 | Intended and accepted (FR-006b); the enumerated new rejections are pinned (SC-008a), and `required_scope_census_test.cpp:341`'s carve-out text must be rewritten rather than left stale. |

## D-13 — Governing-document closure reaches the **constitution**, not just B&L

**The gap.** The reviewed revision's documentation closure (FR-019, quickstart S8) stopped at
`spec/behaviors-and-limitations.md`. But `.specify/constitution.md` **Article XVIII §7** (`:386`)
states, in bold: "**`fixpp::v42` builders remain DEFERRED** — FIX 4.2 types its `NumInGroup` count
fields as legacy XML `INT`, so the emitter materializes zero typed repeating groups (L-063-1), and a
scalar-only v42 builder would silently omit required repeating groups (invalid FIX 4.2).
Re-instating v42 builders is blocked on the L-063-1 structural-group-detection fix … tracked as
issue #196." The Status banner (`:85`) records the same at v0.9. **Feature 082 delivers exactly the
thing both loci say is deferred.** Article XVIII §4 (`:383`) — "Roadmap changes are constitution
amendments"; Article XX §1 (`:402`) — "The constitution is amendable but **not silently
violatable**. Any conflict between this document and a feature spec must be resolved by amending the
article first (with rationale committed in the same PR), then proceeding"; Article XX §2 (`:403-407`)
requires Codex Gate A review on the amendment **and** user sign-off.

**Why it is cheap.** The change is *permissive*: Article I §1's codegen scope (`:94`) already reads
"FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1" per `[2c §1.3]`, so 082 moves the library **toward**
Article I §1, not away — no scope widening (the FIX40/41 out-of-scope assumption in spec §
Assumptions stays true). This is an **annotation-only** amendment, not a design change.

**Decision (FR-020).** Fold an annotation-only amendment into **this feature's own branch**, per the
unbroken precedent — v0.5 (069), v0.6 (074), v0.7 (075), v0.8 (076), v0.9 (077), v0.10 (078) each
folded an annotation into the delivering feature's branch with a recorded user ratification — **not**
a standalone `Constitution: amend …` PR. Scope: Article XVIII §7's "v42 builders remain DEFERRED"
sentence is replaced by a delivered-by-082 record; the Status banner gains a v0.11 line; Article I §1
is confirmed unchanged. Note the v0.9 amendment-log entry at `:18` carries the same sentence as
*historical record* of that amendment — it must be left intact; only the live article text and the
banner move.

**And the fourth mandatory control.** `.specify/constitution.md` **Appendix A** (`:414-424`) is "the
canonical mandatory-trigger reference" and requires **all four** controls — `/clarify`, `/analyze`,
Codex Gate A, **user `/plan` sign-off** — for its listed trigger categories. 082 hits at least two
rows: "**Codegen layout** — dictionary loader, multi-version coexistence" (`:424`; the driver change
plus the new `v42` split layout) and "**Wire format / parser** — offset-table semantics, framing
rules, **validator changes**" (`:423`; FIX40/41/42 group-membership and 079 per-group enforcement
become reachable, and FIX43 tag 576 registers). `plan.md`'s Constitution Check tracked three of the
four; the fourth was untracked, so nothing in the pipeline would surface it. `plan.md` now carries an
Appendix-A row enumerating all four; the user `/plan` sign-off it surfaced was **GIVEN 2026-07-30**.
Cite Appendix A, not an article-level clause: Appendix A is authoritative on conflicts per its own
preamble. Current status of the four: `/clarify` DONE, `/analyze` PENDING (pipeline step 6, the only
outstanding one), Codex Gate A CONVERGED at round 3 + user-signed-off 2026-07-30, user `/plan`
sign-off GIVEN 2026-07-30.

**RATIFIED 2026-07-30 (OD-2).** The user ratifies the annotation-only Article XVIII §7 +
Status-banner **v0.11** amendment, folded into the 082 branch rather than a standalone
`Constitution: amend …` PR — so Article XX §2's user-sign-off precondition is satisfied. **Editing
`.specify/constitution.md` remains an implementation-time task and is NOT done in this design
bundle**; what changed is that its precondition is now met, not its scope.
