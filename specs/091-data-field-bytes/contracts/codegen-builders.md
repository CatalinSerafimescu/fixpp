# Contract C-2 — Generated builders

Emitter: `tools/codegen/fixpp-codegen/emit_builders.cpp` (and `gen_util.hpp` comments). Loader:
`src/dictionary/xml_loader.cpp` (C-2.5). The design is in research.md R-5 to R-8 and R-11.

## C-2.1 Coupled Length+Data member (FR-010, FR-011)

For each coupled `LevelItem` (Length tag `L`, Data tag `D`, accessor `m`), in both arms:

```cpp
// top level (owner "bb")
if (args.m) {
    static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data(D) == L);
    auto r_pair = bb.field_data(D, ::std::as_bytes(::std::span{*args.m}));
    if (!r_pair) return ::std::unexpected(r_pair.error());
}
// nested (owner = the current entry_handle local)
if (item.m) {
    static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data(D) == L);
    auto r_pair = ehN.set_data(D, ::std::as_bytes(::std::span{*item.m}));
    if (!r_pair) return ::std::unexpected(r_pair.error());
}
```

- "Coupled" covers every **standard** pair whose two halves appear at one level of a message (FR-010);
  after C-2.5 that equals each shipped dictionary's own coupling.
- The Args member keeps the type `std::optional<std::string_view>`, except the FR-011 carve-out: for
  the five FIX 5.0 SP2 pairs C-2.5 newly couples, the caller-set `std::optional<std::int64_t>` Length
  member is deleted (no alias) and the Data member becomes the coupled member. At ruling time
  (2026-09-24, dated) the owning structs were `PayManagementRequest`, `PayManagementReport` and
  groups `G_555_2`, `G_555_5`, `G_42485`, `G_42683`, `G_42981`; re-derive by grepping the pre-change
  v50sp2 goldens for Args members emitted as `field`/`set_int` on the Length tags 2494, 2815, 43109,
  43110, 43111.
- No `field(L, …)` / `set_int(L, …)` is emitted for a coupled item.
- The token `r_data` no longer appears in generated output.

## C-2.2 Census (FR-010, FR-012, SC-003), and proof that it can fire

1. **Compile-time pair identity.** The `static_assert` above is emitted at every coupled call site.
   It guards pair **identity** at the sites that exist; it cannot see an omitted site.
   - **Positive control:** an emitter mutant passing `item.tag` for `D` must fail to compile the v44
     builders. Run it in a scratch copy, never in the PR worktree.
2. **Exact completeness census** (a test under `tests/codegen/`, labelled `091`).
   - **Expected** multiset, derived from the IR per shipped dictionary version: every
     (message, structural path, Length tag, Data tag, arm ∈ {top, nested}) where the Length and Data
     tags are a row of `core::detail::standard_length_data_pairs` and both appear at that level.
     Pair-ness comes from the standard table, **never** from `FieldRef::length_pair_data_tag`.
   - **Actual** multiset, parsed from the regenerated builder sources: each `field_data(D, …)` /
     `set_data(D, …)` call with its owning message, its structural path (the enclosing group chain)
     and the Length tag of the `static_assert` preceding it. **Unit:** every message has both a
     `.builder.cpp` and a `.builder.inl` carrying the same call sites, so the census parses
     `.builder.cpp` only, and separately asserts that each message's `.builder.cpp` and
     `.builder.inl` call-site multisets are identical (otherwise every site counts twice).
   - **Pass condition:** actual == expected, and no `field_data`/`set_data` names a `D` outside the
     expected set (FR-012). No pass value is pinned; the expected set is recomputed each run.
   - **Orphan-half check (FR-008 proviso):** in every regenerated version, 0 calls `field(T, …)` or
     `set_<kind>(T, …)` **other than `field_data`/`set_data`**, where T is a tag of
     `core::detail::standard_length_data_pairs`. Keyed on the call name and the tag, not on the
     result local's name, so an emitter renaming its locals cannot make it report clean. A standard
     pair half emitted alone would be refused at commit (FR-008), and the coupled-site census cannot
     see it.
   - **Positive controls (each shown RED before relying on the census):**
     (a) run it over the output of the "loader walk removed" mutant (quickstart §3: the new emitter
     over the unfixed loader): the reported missing set MUST equal exactly the tuples of the five
     R-11 pairs, with nothing else missing; and the orphan-half check MUST report exactly the ten
     tags of those five pairs on v50sp2;
     (b) delete one coupled emission from a regenerated golden: reported missing;
     (c) move one call site from the nested arm to the top arm: reported as a wrong arm.
     (The pre-change goldens are **not** a control: they hold no `field_data`/`set_data` call at
     all, so every tuple is missing and control (a) could not fail.)

## C-2.3 `message_encoding` (FR-011a)

- **Which messages.** Those where any member at any depth is the Data half of a pair whose field name
  **contains** `Encoded`. The set is derived from the IR, not hand-listed.
  - **Witness:** a v50sp2 message whose only encoded field is one of `DerivativeEncodedIssuer(1278)`,
    `DerivativeEncodedSecurityDesc(1281)` or `InstrumentScopeEncodedSecurityDesc(1621)` has the
    member. The implementer names the message by census.
  - **The mutant:** a "begins with" rule must turn that witness RED.
- **The member.** Top-level Args gain `std::optional<std::string_view> message_encoding;`, appended
  **last**.
- **The emit.** When set, the builder emits `bb.field(347, *args.message_encoding)` as the **first**
  body field.
- **Messages without an `Encoded*` field** get no member. A test asserts one such message per version
  (e.g. v44 `Heartbeat` or whichever app message the census names) has no `message_encoding` member,
  via a `requires` expression.
- **Accessor collision** is resolved by the existing `uniquify_accessor`.

## C-2.4 Goldens (FR-013)

- **078 builder goldens** are regenerated for v42/v44/v50sp2/vlatest. The diff is restricted to
  FR-013's transformations: (a) coupled-call rerouting, (b) `message_encoding` member and emit, (c) on
  v50sp2 only, the five newly coupled pairs (Length member deleted, Data member coupled, two emits →
  one coupled call at the Length's position).
- **Validation is structural, against an expected manifest**, not a token filter (a `grep -vE`
  allowlist accepts any changed line containing an allowed identifier, so a stray `field(347, …)`
  inside a group or a deleted `r_len` with no replacement would pass):
  - (a) and (c): the C-2.2 census over the regenerated output, plus a check that every removed
    two-call site in the old golden has exactly one corresponding coupled call in the new one;
  - (b): the set of messages whose Args carry `message_encoding` equals the IR-selected set (C-2.3),
    the member is the last Args member, and its emit is the first body statement;
  - after normalising (a)–(c) out of both sides (rewrite each old two-call site to its coupled form,
    delete the `message_encoding` member/emit, delete the (c) Length members), old and new must be
    byte-identical. That residual diff is the check; it must be empty.
  - **Positive control:** inject a stray `bb.field(347, …)` inside a group body of one regenerated
    golden; the residual diff must be non-empty.

## C-2.5 Loader pairing and read-tier pins (FR-017, FR-018)

- **Loader.** `LoaderState::detect_length_pairs`'s secondary walk additionally visits every
  `<component>` definition and every `<group>` at any depth (every `<group>` under `<fix>`,
  whatever its parent: `<header>`, `<trailer>`, `<message>`, `<component>` or another `<group>`); in those containers adjacency is broken
  by any non-`<field>` child. The existing direct-`<field>` walk of header, trailer and messages is unchanged; the groups inside them belong to the new group walk (R-11). Visit
  order (R-11): `<fields>`, then header, trailer, messages (as today), then `<component>` definitions
  in document order, then `<group>` elements in document order; a conflict is settled by
  `mark_pair`'s existing first-writer rule. The false "global-fields path already captures all
  standard pairs" comment is deleted, and the header comment names the superseding decision.
- **Per-dictionary drift arm** in `tests/wire/length_data_pairs_drift_test.cpp` (`wire_dict_tests`):
  for each shipped dictionary, every standard pair whose two tags both occur in some message
  expansion (the `message_fields()` probe `add_pairs` already uses) is paired by that dictionary's
  loader. **TDD order:** written first, shown RED on the unfixed loader (FIX50SP2, exactly the five
  pairs), then GREEN with the loader change. Every arm, Orchestra FIX Latest included, asserts that
  its probed standard-pair set is non-empty and prints it; a quickstart §3 mutant empties one
  non-FIX50SP2 leg's probe and must turn that leg RED, so the arm is shown able to fail beyond
  FIX50SP2. `HeaderEqualsShippedDictionaryUnion` stays and still catches an over-pairing.
- **Read-tier pins.** `tests/codegen/read_tier_byte_diff_test.cmake` rebaselines exactly
  `_expected_v50sp2_Fields.hpp` and `_expected_v50sp2_Validator.hpp`, with a banner paragraph in the
  fixpp#427 style giving the re-derivation recipe relative to the **current** pins:
  - `Validator.hpp`: take the regenerated file, delete the `length_data_pairs` rows for the five R-11
    pairs, restore the array extent, and sha256 it — the result is the pre-091 pin.
  - `Fields.hpp`: take the regenerated file, zero the `length_pair_data_tag` column of the `FieldRef`
    rows for Length tags 2494, 2815, 43109, 43110, 43111, and sha256 it — the result is the pre-091
    pin.
  - **Consequential edits in the same file** (NEW-2, Gate A r2):
    - the fixpp#427 banner's recipe is re-stated as **chained** — apply 091's Validator recipe
      first, then #427's — because after 091 the regenerated `Validator.hpp` also holds pairs with a
      Length tag at or below 2500 that T001 lacked, so #427's recipe alone no longer reproduces its
      hash;
    - the 082 banner's and the header's result statements ("byte-identical" / "every other artifact
      remains gated against the pre-077 T001 baseline") are **deleted**, and point at the 091 banner
      rather than restating a list of which pins moved;
    - `_baseline_desc` names 091's baseline for `v50sp2/Fields.hpp` and, instead of #427's, for
      `v50sp2/Validator.hpp`; the FATAL_ERROR and PASSED summary texts, which name the baselines,
      are updated to match.
  - Every other pin stays unchanged. `v50sp2/Messages.hpp`'s pin also corroborates a checked-in
    golden (`specs/003-dictionary-codegen/contracts/golden/v50sp2_Messages.golden.hpp`); if it, or
    any other of the remaining pins, moves, stop and report to the owner.

## C-2.5a Loader pairing over a user-loaded dictionary (FR-017), synthetic XML

`XmlLoader` is a public runtime API, and on the shipped dictionaries "break on a non-field child"
and "skip it" yield the same pair set, so only a synthetic dictionary can observe the rule. A loader
unit test in `tests/dictionary/` (label `091`) loads a synthetic `<fix>` XML string with a custom
LENGTH/DATA pair (for example 5001/5002). In every arm the two fields are **not** adjacent in
`<fields>` order and **not** consecutive among the `<field>` children of any message, header or
trailer, non-field children (groups, component references) skipped, which is how the unfixed
loader's message walk reads a container; so the unfixed loader does not pair them:
- **(i)** adjacent only inside a `<component>` definition → paired (`length_pair_data_tag(5001) ==
  5002`), and `table_view::has_nonstandard_pair()` is true;
- **(ii)** adjacent only inside a `<group>` nested in a component → paired;
- **(iii)** the same two fields in a component with a `<component>` reference between them → **not**
  paired;
- **(iv)** a Length adjacent to Data A in one component and to Data B in a later one (document order)
  → paired with A (`mark_pair`'s first-writer rule, R-11 visit order);
- **(v)** adjacent only inside a `<group>` that is a **direct child of a `<message>`**; both tags
  live only inside that group, never as direct `<field>` children of the message → paired;
- **(vi)** adjacent only inside a `<group>` nested **directly in another `<group>`**, the outer one
  inside a component; both tags live only inside the inner group → paired;
- **(vii)** (visit-order discriminator, R-11) a Length adjacent to Data A inside a `<group>` and to
  Data B as direct fields of a component that follows the group in document order → paired with B,
  because every component definition is visited before any group. Two placements of the group, each
  with its own tags so the first-writer rule cannot leak between arms: (a) the group is a direct
  child of a `<message>`; (b) the group sits in an earlier component C1 and Data B in a later
  component C2; (c) the group is a direct child of `<header>` and Data B sits as direct fields of a
  component → paired with B;
- **(viii)** a pair adjacent only inside a `<group>` that is a direct child of (a) `<header>` and
  (b) `<trailer>`. Each placement has its own tags, and both tags live only inside that group,
  under the global non-adjacency preconditions above → paired.

TDD: (i), (ii), (iv), (v), (vi), (vii) (all placements) and (viii) are RED on the unfixed loader. (iii) is GREEN there by
construction; its liveness comes from the quickstart §3 mutant "the new walk skips non-field
children instead of breaking", which must turn it RED. (v) and (vi) are made depth-discriminating by
the mutant "the new walk visits only components and their direct `<group>` children", and (vii)
order-discriminating by the mutant "groups are walked depth-first, right after their container";
(viii) is made parent-discriminating by the mutant "the group walk is entered only from messages and
component definitions";
each must turn its arms RED.

## C-2.6 Generated-builder exhaustive witnesses (SC-001)

Two parameterized tests over every octet `0x00–0xFF`, each asserting raw frame boundaries (Length
value = 1, Data octet verbatim) and a re-parse through the inbound parser recovering the octet:
- **top level:** v44 `NewOrderSingle` with `encoded_text = {b}`;
- **nested:** a coupled member inside a v44 repeating-group entry (for example
  `EncodedLegIssuer(618/619)` in a leg group); the implementer names the message and group by the
  C-2.2 census.
The "emitter changes only the top-level arm" mutant must turn the **nested** test RED.
