# Contract: API changes in 102 (BREAKING: C++ types; C-ABI and Python results)

Pre-release. The C++ break is declared under Article XVII §1 (Gate A on a public C++ surface). The C-ABI result change
is declared BREAKING under `[const §X.7]` (owner ruling, 2026-11-13: "All BREAKING, bump (Recommended)"), with a
`FIXPP_C_ABI_VERSION_MINOR` bump; §6 lists it. The C ABI's types and symbols are unchanged: `fixpp_dict_t` is opaque,
`src/capi` reads `FieldRef::rule` and `type` (neither widened), and SWIG exposes none of these types.

Re-derive the type claim with one recipe over the C ABI, its implementation and the SWIG interface. It names the record
types, their widened members and every `Dictionary` member that returns or spans a record:
`command git grep -nE 'FieldRef|ComponentRef|GroupRef|field_ref\(|[.>]component\(|[.>]group\(|component_fields|group_fields|message_fields|first_field_index|component_index|component_id|name_offset|parent_component_id' -- src/capi bindings include/fix`.
Its known positive on the parent base is `src/capi/message_write.cpp`'s `field_ref(` reads, each of which reads only
`rule` or `type`. Every hit is classified as reading a member 102 does not widen, or a defect.

B&L rows: B-102-1 (layouts), B-102-2 (MultiCharValue types), B-102-3 (loads that now succeed, and the C-ABI and
Python results of §6); B-098-1 amended. The C-ABI population §6 reaches is data-model.md Appendix A.

Every C++ consumer of the `fixpp/dict` headers rebuilds against them. libfixpp's own C++ ABI changes wherever a
member returns, spans or takes a record: `Dictionary::component`, `group`, `component_fields`, `group_fields`,
`message_fields`, `field_ref`, and every other such signature.

## 1. `fixpp::dict::FieldRef`, `ComponentRef`, `GroupRef` change layout (data-model.md)

- `FieldRef`: `component_index` is `std::uint32_t`; `_reserved` is gone; `sizeof` stays 16, `alignof` goes from 2 to 4.
- `ComponentRef`: all five members are `std::uint32_t`; `_reserved` is gone; `sizeof` goes from 12 to 20, `alignof` 4.
- `GroupRef`: `first_field_index` and `field_count` are `std::uint32_t`; `sizeof` goes from 12 to 16, `alignof` 4.
  `GroupRef::_reserved` stays as the explicit tail pad: the record is 16 bytes at `alignof == 4` with it or without
  it. 2c §4.2's `_reserved` discipline (zero on emit, ignored on read) now applies to `GroupRef` alone.
- Declared member order is unchanged in all three.
- All three stay standard-layout and trivially copyable, asserted in each header and in
  `tests/dictionary/ref_shape_test.cpp` (data-model.md).
- The forward-compatibility macros `FIXPP_DICT_FIELDREF_RESERVED_USED` and `FIXPP_DICT_COMPONENTREF_RESERVED_USED`
  were never defined: outside `specs/` and `.specify/` only a header comment names one (re-derive:
  `command git grep -n RESERVED_USED -- ':!specs' ':!.specify'`). A consumer that tests them sees no change, but the
  slots they reserved are gone.

What still compiles and keeps its meaning:

- code that reads or writes members by name, into a type at least as wide;
- designated initializers that do not name `_reserved`;
- positional initializers of `FieldRef` (eight values), `ComponentRef` (five values) and `GroupRef` (six values),
  because the order is unchanged;
- a braced list whose elements are `std::uint16_t` variables, because widening is not narrowing;
- a positional initializer with fewer values than members: the trailing members are value-initialized (0), as before.

What fails to compile:

- any use of `FieldRef::_reserved` or `ComponentRef::_reserved` (for example a `_reserved == 0` check, or
  `cr._reserved = 0`);
- a positional initializer that still supplies `_reserved` to `FieldRef` (nine values) or `ComponentRef` (six values);
- a braced narrowing initialization from a widened member, for example `std::uint16_t x{cr.first_field_index}` or
  `std::uint16_t id{fr.component_index}`.

What compiles and changes silently:

- a non-braced narrowing, for example `std::uint16_t x = cr.component_id;` or `static_cast<std::uint16_t>(…)`,
  truncates on a dictionary of more than 65,535 components or a side table past 65,535 entries;
- arithmetic on a widened member: it was promoted to `int` and is now unsigned 32-bit, so a difference such as
  `a.first_field_index - b.first_field_index`, or `id - 1` on an id of 0, wraps where it used to go negative;
- code that depends on `sizeof` 12 or `alignof(FieldRef) == 2`, or stores the records in a binary format;
- code that depends on a member's offset or on the record's bytes: `offsetof`, `memcpy` into or out of a record, a
  reinterpretation of a record's storage, or a record inside a packed enclosing struct. Every `ComponentRef` member
  after the first moves, as do `GroupRef`'s members after `first_field_index` and `FieldRef`'s `enum_table_index` and
  `length_pair_data_tag` (compare data-model.md's tables with the parent base's headers);
- a type change seen through deduction: `auto x = cr.component_id;` or `decltype(…)` now names `std::uint32_t`,
  overload resolution between a `std::uint16_t` and a `std::uint32_t` overload picks the other one, and a template
  keyed on the member's type is instantiated for the other type.

Migration: widen the receiving variable to `std::uint32_t` (or `std::size_t`); drop `_reserved`; prefer designated
initializers.

## 2. Loads that now succeed (B-102-3)

- A QuickFIX XML or Orchestra base of more than 65,535 components loads, each component with its own id, parent id,
  members and name. Before, identities past that point wrapped silently. Identities, members and names are exact
  while every table stays below 2^32 entries (components, side-table entries, name-pool bytes; L-102-1 (a)–(c)).
  Past that bound a caller gets wrong answers, in bounds, with no error and no way to ask (L-102-1).
- An Orchestra document whose component or group ids pass 65535 loads; before, it was `orchestra_parse_error`. Ids above
  2147483647 (the schema's `maxInclusive`) are still refused. `orchestra_parse_error` thereby loses the cause "a
  component, group or ref id above 65535". No installed header documented it: `orchestra_loader.hpp`'s `Throws:`
  list says only "malformed".
- On the vendored FIX50SP2, `Dictionary::component_fields("RgstDistInstGrp")`, and that of every component whose
  side-table start passed 65535, now returns the component's own members; before, it returned another definition's,
  with no error (L-098-4, closed). B-102-3 records it. This is a C++ change only: no C-ABI result observes it (§6
  "Unchanged").

## 3. 098's side-table refusal is removed

`overlay_limit_error` for "the components/groups side table would stop fitting 16 bits" no longer exists. An overlay
that made a side table pass 65,535 entries now loads, with correct members. The cause is documented in
`error.hpp`'s `overlay_limit_error` comment and in `xml_loader.hpp`'s `load_with_overlay` comment, both in research
D8's amendment set. `overlay_limit_error` itself stays, for its other causes: a cap, a DOCTYPE, the expansion budget
and the step counter. Code that catches it keeps its meaning, and at the C ABI it still maps to
`FIXPP_ERR_DICT_LIMIT_EXCEEDED`.

## 4. MultiCharValue types in `fixpp::v50sp2` and `fixpp::vlatest`

Every MultiCharValue accessor (view and owning, top level and group entry) returns `expected_t<std::string_view>`; every
such builder `Args` member is `std::optional<std::string_view>`; group entries use `set_string`. A caller assigning a
`char` writes a one-character string literal instead (`args.exec_inst = "1";`). A caller comparing the result with a
`char` compares with a one-character string. The precompiled builder libraries' C++ ABI changes accordingly.

The affected symbols are every accessor, `Args` member and group-entry setter generated for a MultiCharValue field in
`fixpp::v50sp2` and `fixpp::vlatest`. Re-derive them from the MultiCharValue tag set (research Survey "Codegen"; spec
FR-004's classification); SC-003's differential lists them. `fixpp::v42`, `fixpp::v44` and `fixpp::vt11` have no
MultiCharValue field, so their typed API changes only through §5's row.

What fails to compile (re-check each with a scratch compile under GCC and Clang, `-std=c++23`, over
`std::expected<std::string_view, E>` and `std::optional<std::string_view>`):

- `char c = *acc;`, `switch (*acc)`, `*acc == '1'` and `acc == '1'`;
- `args.exec_inst = '1';`, and assigning a `char` variable or a `std::optional<char>` to the member.

What compiles and changes silently:

- `auto v = *acc;` now deduces `std::string_view`. A later use that accepts both types changes meaning: overload
  resolution between a `char` and a `std::string_view` overload picks the other one, and a template keyed on the
  type is instantiated for `std::string_view`.
- Lifetime. The accessor used to return the `char` by value; it now returns a view into the message's buffer
  (`[[clang::lifetimebound]]`, as for every MultipleStringValue accessor). A caller that keeps the value after the
  message, its buffer or an owning (reified) object is moved or destroyed compiled and was correct before. It still
  compiles, and now dangles. Migration: copy the value into a `std::string` (or a `char` where one character is
  meant) before the source goes away.
- Lifetime, builder. `args.exec_inst = std::string{"1 2"};`, or any temporary `std::string`, compiles and leaves the
  member dangling before the build call reads it. No compiler diagnostic is relied on to catch it. The caller's obligation
  is the one every MultipleStringValue `Args` member already carries: the viewed characters outlive the build call.

A consumer of a precompiled builder library rebuilds the library and its own code against the 102 headers; an
installed package ships both rebuilt. The libraries carry no version of their own (`cmake/Codegen.cmake` sets
none), so the change is recorded by B-102-2 and the PR description's BREAKING marker only.

## 5. Generated `Fields.hpp`

Every `FieldRef` row has eight initializers instead of nine. Code that includes a regenerated `Fields.hpp` sees no value
change; a hand-written copy of a row with nine values fails to compile. The package installs each version's generated
read-side headers, `Fields.hpp` among them (`fixpp_typed_tier_expected_children` in `cmake/FixppTypedTierRule.cmake`),
and, where the typed tier is installed (`FIXPP_INSTALL_TYPED_TIER`, #613), the builder libraries. So an installed
consumer receives the regenerated row from the new package. A consumer that vendored a generated header regenerates
it.

## 6. C-ABI and Python results (BREAKING, `[const §X.7]`)

Owner ruling, 2026-11-13: "All BREAKING, bump (Recommended)". No C-ABI type, symbol or error code changes; these calls
return a result other than the one their pre-102 documentation gave, in three kinds:

- a failure turned into a success: (a), (b), (c), (d);
- the same code, and a different dictionary: (f), (g), with every downstream call data-model.md Appendix A marks;
- a failure turned into a different failure (or into a success): (a′), (b′).

The classes are derived outward from what 102 corrects, not from `dict.h`'s documented causes:

| 102 corrects | C-ABI reach |
|---|---|
| the side-table refusal (retired) | (a), (a′) |
| the surrogate-id parse | (b), (b′), (c) |
| component identity (`expand_field_list`'s `components_[cit->second]` past the wrap) | (f) |
| the group side table (`finalize`'s group start) | (g) |
| the component side-table start (FIX50SP2's `RgstDistInstGrp`), the name offset, FR-008's guard, the MultiCharValue kind | none: "Unchanged" below |

(e) is unused.

| Class | Call | Input | Before (`$P`) | After |
|---|---|---|---|---|
| (a) | `fixpp_dict_load_from_xml_with_overlay`, either format | an overlay whose side-table fault (a component or group side table that would stop fitting 16 bits) was the only refusal it reached | `FIXPP_ERR_DICT_LIMIT_EXCEEDED` | `FIXPP_ERR_OK` |
| (a′) | `fixpp_dict_load_from_xml_with_overlay`, either format | an overlay with that side-table fault and a fault the guard detects after its side-table arms: a step-7 conflict (QuickFIX XML `required="X"`; Orchestra `presence="X"`), a loader or `finalize` failure after the splice, an allocation failure | `FIXPP_ERR_DICT_LIMIT_EXCEEDED` | that fault's code: `FIXPP_ERR_DICT_CONFIG` for a conflict or a loader failure, `FIXPP_ERR_DICT_OOM` for an allocation failure |
| (b) | `fixpp_dict_load_from_xml_with_overlay` | an Orchestra base or overlay whose only refusal at `$P` was the loader's parse of a component, group, componentRef or groupRef id in 65536..2147483647 | `FIXPP_ERR_DICT_CONFIG`, every subtype: the guard's own Orchestra keys (`orch_id_key_of`, `orch_child_key`, the graph build's `component_by_id` and `group_by_id` fills, `records_of`'s `has_id`) skip such an id silently, so the load ends in the loader's strict `parse_orchestra_id` after the splice (`orchestra_parse_error`); recorded at `H0` by tasks T021a | `FIXPP_ERR_OK` |
| (b′) | `fixpp_dict_load_from_xml_with_overlay` | an Orchestra input with such an id whose refusal at `$P` was a guard the skipped id moved: the expansion budget, which at `$P` did not count the skipped definitions; or a new-definition cap (`kMaxNewComponents`, `kMaxNewGroups`), which at `$P` counted an overlay's restatement of a base definition past 65535 as new, because its key did not parse | budget: `FIXPP_ERR_DICT_CONFIG` (the load reached the loader's parse); cap: `FIXPP_ERR_DICT_LIMIT_EXCEEDED` | budget: `FIXPP_ERR_DICT_LIMIT_EXCEEDED`; cap: the restatements match, so the load goes on to the later guards and the loader (`FIXPP_ERR_OK` when nothing else refuses) |
| (c) | `fixpp_dict_load_from_xml` | an Orchestra document whose only refusal at `$P` was such an id | `FIXPP_ERR_CAPI_CONFIG_INVALID` | `FIXPP_ERR_OK` |
| (d) | Python `fixpp.dict_load_from_xml` and `fixpp_oo.Dictionary.load_xml` / `fixpp.dict_load_from_xml_with_overlay` | the inputs of (c) / of (a), (b) and the cap half of (b′); of (a′) and the budget half of (b′) | raised `CapiError` / `ValidatorError` with the `.code` of the C call | returns a dictionary; for (a′) and the budget half of (b′), raises `ValidatorError` with the `.code` the C call now returns |
| (f) | either call | a base, or a base with an overlay the 098 arm admits, of more than 65,535 components (an Orchestra base with more than 65,536 has an id past 65535, so at `$P` it is class (b) or (c)) | `FIXPP_ERR_OK`; component identities past the wrap wrong, and in a QuickFIX XML base a componentRef past the wrap expanded another component's node into the referencing message, group or component | `FIXPP_ERR_OK`; identities correct, and every expansion its own: `message_fields` changes, and with it every `table_view` store built from it (the valid-tag and required sets, the group contexts, the Length+Data pairs), the header store and the section records. The observers are data-model.md Appendix A's B rows |
| (g) | either call | a base, or a base with an overlay the 098 arm admits, whose group side table passes 65,535 entries | `FIXPP_ERR_OK`; `group_fields` of a group whose start passed the wrap returned another group's members, and so did the bare member store built from it | `FIXPP_ERR_OK`, and its own members. Observers: a validating session's Step 3 on a context miss (the receive callback, or a Reject), the wire read on a miss in every record store `table_view::wire_group_resolve` probes first, and the other Appendix A B rows that read groups |

- (a) was documented by cause: `dict.h`'s `FIXPP_ERR_DICT_LIMIT_EXCEEDED` line points at `xml_loader.hpp`'s cause
  list, which named the side-table fit; B-098-1, B-098-6 and L-098-4 said the same. (b) and (c) were covered by the
  calls' catch-all failure results. (a′) and (b′) change a code `dict.h` documents by cause; 1.9 counted a failure
  turned into a different failure as BREAKING (`specs/091-data-field-bytes/data-model.md` B-091-4).
- (f) and (g) keep their code and change the dictionary the call yields, on the precedent of C-ABI 1.9 (091): "The
  return code is the same, but the dictionary this call yields carries the pair". Their downstream effect is not one
  clause: data-model.md Appendix A derives every export it reaches.
- The population is derived, in 1.9's form: data-model.md Appendix A classifies every export of
  `tests/abi/golden/fixpp_capi_symbols.txt` by one predicate, and research D8's C-ABI population recipe re-derives it
  at the head (tasks T067).
- Unchanged:
  - an id above 2147483647, a negative or malformed id (still refused, with the same code);
  - the component side-table start, so contract §2's third bullet (FIX50SP2's `RgstDistInstGrp`) is a C++ change
    only: it is read only through `Dictionary::component_fields`, and nothing in `src/` calls it (re-derive:
    `command git grep -n -E 'component_fields(_impl)?\(' -- src`; unchanged while it reports no caller besides the
    definition and its `_impl`);
  - the component name offset: nothing in `src/` reads `ComponentRef::name_offset` (re-derive:
    `command git grep -n name_offset -- src`; unchanged while it reports the loaders' writes only);
  - FR-008's run-reader guard: no loader writes an overrunning record below 2^32 entries;
  - the MultiCharValue kind: no `src/capi` file includes a generated typed-tier header, and the runtime validator
    does not read the kind (spec FR-004);
  - every export data-model.md Appendix A classifies U.
- TOML (`dictionary.path`, `dialect_overlay`) is outside the C ABI; it now loads these inputs, and B-102-3 records it.

Declared by: a `FIXPP_C_ABI_VERSION_MINOR` bump to one past the value `main` holds after the post-squash rebase, with
a `version.h` comment (which also carries the effects no declaration carries); a BREAKING note on both `dict.h` load
declarations naming every class above, the same-code ones in 1.9's form; one BREAKING family paragraph in each header
whose calls data-model.md Appendix A marks (`message.h`, `session.h`); B-102-3 marked BREAKING; the PR description;
every in-repository consumer the version touches updated in the same PR (tasks T059a). Migration: a consumer that
relied on these inputs being refused checks the dictionary's content itself; no code that handles the refusal needs to
change, because the refusal no longer happens for these inputs. A consumer over a dictionary in region (f) or (g)
sees each marked call answer from the corrected dictionary.
