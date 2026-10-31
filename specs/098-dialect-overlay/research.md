# Research: 098 dialect overlay

Every fact below was read at `8319c206` (`origin/main`). File:line citations are leads for the
implementer and will drift; re-derive with the `grep` given where one is given.

## R-1 — Merge mechanism: splice the overlay into the parsed base document

**Decision.** Each loader parses the base into a pugixml document, parses the overlay into a second
document, runs the guards in R-5's order (document shape, then the merged-graph cycle and expansion
budget walk, then the R-4 conflict checks), splices the overlay's elements into the base document
(R-3), and then runs the loader's existing post-parse path unchanged. For QuickFIX XML that path
is `build_handle_from_doc(doc, mr, policy)` in `src/dictionary/xml_loader.cpp` (`LoaderState::parse_document`
then `finalize`); for Orchestra it is the function of the same name in `src/dictionary/orchestra_loader.cpp`.

**Rationale.**
- **Equivalence by construction.** After the splice, the document *is* the hand-edited file, so every
  downstream rule (field interning, header/trailer fold, Length+Data detection, group registration,
  delimiter capture, the 072 collision guard, the FR-023 completeness sweep, enum runs, `as_table_view`)
  runs exactly as it would on the edited file. FR-002 holds for every store at once, including the
  first-seen and declaration-order rules the loaders apply (component indices, `parent_of_`, enum
  order, the group delimiter's first emission, the Length+Data first writer), provided the splice
  places each addition where the **equivalent edited XML** places it. That file is defined as the base
  with each overlay addition appended at the end of its matched container, in overlay document order
  (spec Terminology). Any other hand edit (an alphabetical insert, say) is a different dictionary, and
  FR-002 does not claim equivalence with it.
- **No new state class.** Both loaders keep components, groups and messages as pugi node handles and
  expand at finalize; the DOM is the natural seam. `build_handle_from_doc` takes a `const` document, and
  in `load`/`load_from_string` the document is a local, so the splice happens before the call with no
  change to the post-parse code.

**Alternatives rejected.**
- *Merge into `LoaderState` / `OrchestraLoaderState` between `parse_document` and `finalize`.* Two
  state classes with different shapes (name-keyed vs id-keyed), and the order-dependent rules above
  would have to be re-implemented to match a hand edit. More code, and equivalence becomes a claim to
  test rather than a property of the construction.
- *Merge at the handle level (`Dictionary::with_overlay`, 2c §4.3).* Every handle store (`fields_`,
  per-message offsets, group pools, context delimiters, enum runs) would need its own merge, outside the
  finalize code that defines them. Rejected by the same argument, and the owner ruled the attachment
  point (spec Clarifications).
- *Text-level concatenation.* Not well-formed for nested additions (a member inside an existing group).

## R-2 — The overlay is written in its base's format

**Decision.** A QuickFIX-XML base takes a `<fix>` overlay; an Orchestra base takes a `fixr:repository`
overlay. A mismatch is refused. This supersedes the clarify-time reading (spec Clarifications).

**Measured reasons** (`src/dictionary/orchestra_loader.cpp` at `8319c206`):
- `OrchestraLoaderState` is a separate class with its own `finalize`; only `detail::` delimiter helpers
  are shared with the XML loader. Its `expand_field_list` matches only `fixr:` element names and ignores
  anything else silently, so a QuickFIX-shaped node would vanish without an error.
- Orchestra references fields, components and groups by numeric `id`; there is no name index for
  components or groups. Code sets are the exception: `collect_codesets` keys them by **name**
  (`codesets_by_name_`, a duplicate name is refused) and `collect_fields` resolves a field's `type=`
  against that name map. R-3 therefore matches a code set by name (Gate A round 1, N-6).
- Several `<fixr:group>` definitions share one NumInGroup tag (the loader's comment on NoLegs 555), so
  "extend group NoLegs" has no single target in Orchestra.
- The header is component 1024 (StandardHeader) and the trailer component 1025, referenced from each
  `<fixr:structure>`; there is no `<header>` element.
- Length+Data pairs come from the Data field's `lengthId=` attribute, with no adjacency rule, and the
  loader fails closed on a re-pair; the XML loader pairs by adjacency and keeps the first writer.
- fixpp's QuickFIX-XML version table (`kVersionTable`) has no FIX Latest row, so a `<fix>` overlay
  cannot name an Orchestra base's version.

A same-format fragment needs none of this translation, and its equivalence reference is the base file
hand-edited in its own format.

## R-3 — Splice semantics: an edit lands on the definition it names

The rule for both formats: each overlay element is matched to the base definition it names; if there is
none, it is new and is appended to its container. A matched element's children are merged by the same
rule, recursively. Appending puts each new child after the existing ones, in overlay document order.
That is the definition of the equivalent edited XML (spec Terminology), and the hand-written references
(R-10) are written to it.

**Containers.** The loaders read the **first** child of each name (`root.child("components")`,
`root.child("fixr:codeSets")` and the like), and several containers are optional (`collect_components`
and `collect_codesets` return early when theirs is absent). The splice therefore targets the first base
container of each name, and creates it, appended to the root, when the base has none. A second overlay
container of one name is a duplicate (R-4).

**What an overlay may hold.** The root's children are limited to data-model §A.1's "May contain"
column. Any other root child (for example `fixr:datatypes`, `fixr:sections`, `fixr:categories`, or a
non-QuickFIX element under `<fix>`) is refused with `overlay_conflict_error`. Inside a definition,
`fixr:annotation` and `fixr:documentation` are allowed and dropped by the splice: the Orchestra loader
already ignores them ("Ignore fixr:annotation / other unknown child elements"), and dropping them keeps
the spliced document free of overlay text no store reads. The equivalent edited references carry none.

**Presence attributes.** On an overlay element, `required` MUST be `Y`, `N` or absent (QuickFIX XML),
and `presence` MUST be `required`, `optional` or absent (Orchestra); any other value is refused with
`overlay_conflict_error` naming the element. The loaders themselves accept anything (`== "Y"` and
`== "required"` tests, everything else Optional), so the merge cannot leave this to them. On a matched
child the splice writes the attribute only for a promotion; it never copies `N` or `optional` onto a
base child.

**QuickFIX XML** (`<fix>` overlay):

| Overlay element | Match key in the base | On match | New |
|---|---|---|---|
| `<fields>/<field>` | `number` | name and type MUST equal the base's; listed `<value>`s are added (R-4) | appended to `<fields>` |
| `<field>/<value>` | `enum` within the field | description MUST equal the base's | appended to the field |
| `<header>`, `<trailer>` | the element | children merged | n/a (the element always exists; an empty one is created if the base has none) |
| `<components>/<component>` | `name` | children merged | appended to `<components>` |
| `<messages>/<message>` | `msgtype` | `name` MUST equal the base's; children merged | appended to `<messages>` |
| `<field>` ref inside a message, component, group, header or trailer | `name`, among the parent's **direct** children | `required` promotes (R-4) | appended to the parent |
| `<component>` ref inside a parent | `name`, among direct children | `required` promotes | appended |
| `<group>` inside a parent | `name`, among direct children | children merged; `required` promotes | appended |

A `<group>` or `<field>` that the parent already reaches through a component (for example `NoPartyIDs`
inside component `Parties` used by NewOrderSingle), redeclared as a direct child, is a conflict: it would
put one tag twice in the message. The overlay must extend the component instead. "Already reaches" is
answered from the R-5 graph walk's memo, never by a second recursive walk.

**Length+Data adjacency across the boundary.** The XML loader pairs a DATA field with the LENGTH field
immediately before it (`detect_length_pairs`: first in `<fields>` declaration order, then in the
containers its secondary walk visits; first writer kept). An overlay DATA field or field ref appended
right after a **base** LENGTH entry would pair a base tag, which changes a base field (FR-005). R-4
refuses it, before the splice: in any container `detect_length_pairs` walks, the first overlay entry
appended to a base container whose last entry is a LENGTH field that the base left **unpaired** may not be
a DATA field. A base LENGTH already paired by the base's own detection cannot be re-paired (the first
writer is kept), so it is no conflict.

**Orchestra** (`fixr:repository` overlay):

| Overlay element | Match key | On match | New |
|---|---|---|---|
| `fixr:codeSet` | `name` (the loader's key, R-2) | `id` and `type` MUST equal; listed `fixr:code`s are added by `value` (description/name MUST equal on match) | appended to `fixr:codeSets` |
| `fixr:field` | `id` | `name`, `type` and `lengthId` MUST equal | appended to `fixr:fields` |
| `fixr:component` | `id` | `name` MUST equal; children merged | appended to `fixr:components` |
| `fixr:group` | `id` | `name` and `numInGroup` MUST equal; children merged | appended to `fixr:groups` |
| `fixr:message` | `msgType` (unique in the vendored file, which carries no `scenario` attribute; checked 2026-10-31) | `id` and `name` MUST equal; `fixr:structure` children merged | appended to `fixr:messages` |
| `fixr:fieldRef` / `componentRef` / `groupRef` in a structure, component or group | `id`, among the parent's direct children | `presence="required"` promotes (R-4) | appended to the parent |

Header and trailer additions extend component 1024 / 1025 by `id`, like any component.

**Consequences to disclose** (spec Edge Cases; `B-098-*` rows): an Orchestra code set and an Orchestra
group are shared definitions, so an edit to either reaches every field or message that uses it. In
QuickFIX XML an enum value lands on one field, and a group edit lands on the one `<group>` element named,
which may be inline in one message or inside a component shared by several.

## R-4 — Conflicts and duplicates are refused by the merge itself

The loaders do not catch every duplicate: the XML loader silently drops a second `<field>` with an
existing name (`by_name_.emplace`) and has no duplicate-msgtype check, so two `<message>` elements with
one msgtype become two message entries; the Orchestra loader has no duplicate-message check either.
So the merge MUST detect, at the step R-5's order gives each check (the root checks at step 4a, the
rest at step 7, all before the splice), and refuse with `overlay_conflict_error` naming the element:

- **Overlay against base:** a matched definition whose identifying attributes differ (table above); a
  field name already used by another number; an Orchestra code-set name already used by another `id`
  (N-6); a code/enum value redescribed; `required="N"` (XML) or `presence="optional"` (Orchestra) on a
  child the base marks required; a child redeclared directly that the parent already reaches through a
  component (R-3); an overlay DATA entry that would pair with a base LENGTH entry (R-3).
- **Overlay against itself:** two overlay elements with one match key in one container; two new fields
  with one name; two root containers of one name.
- **Vocabulary:** a `required` / `presence` value outside R-3's list; a root child outside data-model
  §A.1's list (R-3).
- **Root:** a version mismatch (FR-006); an overlay root of the other format (R-2).
- **Required rule.** An absent `required` / `presence` attribute on a matched child means "no change".
  `required="Y"` / `presence="required"` on an Optional child promotes it. The overlay cannot relax a
  rule.

References to undefined fields or components and unknown types are left to the loader's own checks
after the splice, which already throw `xml_parse_error` for them, so their messages match those of a
hand-edited file. Presence attributes are not left to the loader: it accepts any value (R-3).

**Enum values on an open field (N-2).** `table_view::enum_valid` accepts any value for a tag with no
domain ("Floor 1", the floor that keeps FIXT11's MsgType(35), which declares no `<value>`, working) and
enforces the listed codes once a domain exists. An overlay that lists a `<value>` on a base field with no
values therefore **closes** that field's domain: only the listed values pass validation. A QuickFIX hand
edit does the same, so the merge accepts it (orchestrator default under the QuickFIX-equivalent ruling;
owner to confirm, spec Clarifications Gate A round 1). It is disclosed in B-098-7 and witnessed (W-1,
W-5). It is not a "removal" under FR-005, which forbids deleting a listed base value, but it narrows
what the base accepted, and the disclosure says so.

## R-5 — Hardening the overlay input (FR-009, FR-010)

The guards bound two things: the overlay document's **shape**, and the **work** its references cause
when `finalize()` expands them. Both loaders' `expand_field_list` inline every componentRef and groupRef
recursively, with no memo and no budget. `finalize()` expands every message (header, body and trailer),
then every component on its own (even one no message reaches), then every first-seen group on its own.
So a small acyclic overlay can still cause exponential expansion, for example
`C_n = { component C_{n-1}; group NoG_n { field F_n; component C_{n-1} } }` over 63 levels (Gate A
round 1, A-001). Definition counts and nesting depth do not bound that. The guard stage below does.

The guards run in this order. Each step is non-recursive or iterative, and none reaches
`build_handle_from_doc` until every earlier step passed.

| # | Guard | Rule |
|---|---|---|
| 1 | Byte cap | **4 MiB**. Read at most cap + 1 bytes into a buffer (path form) or check `size()` (string form); refuse when more were available. Never trust a `stat` size (a FIFO or `/proc` file reports a wrong size). Before parse. |
| 2 | Parse | `pugi::parse_default \| pugi::parse_doctype`. A result of `xml_parse_status::status_out_of_memory` (pugixml reports allocation failure as a parse status, not a throw) becomes `xml_oom_error`, never `xml_parse_error` (A-006). Any other failed status is `xml_parse_error` as today. |
| 3 | DOCTYPE | refuse any `node_doctype` child of the document. pugixml 1.15 never loads external entities and expands only the five predefined entities and character references (its `parse_default` discards a DOCTYPE without a node, which is why the flag is set: to see it). Defence in depth: no DTD is ever loaded. |
| 4 | Depth | **32** element levels; an iterative (explicit stack) walk of the overlay document. |
| 4a | Root | R-4's root checks: the overlay's format matches the base's, its version matches (FR-006), its root children are in data-model §A.1's list, and no container name repeats. Steps 5 and 6 count and walk by element name, so they presume a checked format. |
| 5 | Entry and reference caps | one flat pass over the overlay document. **New** definitions only (N-7): fields **1024**; components **64**; messages **64**; Orchestra groups **64**; Orchestra code sets **256**. Enum values / codes added **4096**. References added (QuickFIX `<field>` / `<component>` / `<group>` refs, Orchestra `fixr:fieldRef` / `componentRef` / `groupRef`) **8192** in total, to new and existing definitions alike. An existing definition the overlay extends is not a "new" definition; how many are touched is bounded by the base's own count, the reference cap and step 6. |
| 6 | Merged-graph walk: cycles and expansion budget | one iterative depth-first walk over the base's definitions plus the **unspliced** overlay (an overlay child counts as a child of the definition it matches, R-3), so no splice is needed first. XML: `component` → `component` (directly or through `<group>`); Orchestra: `component` / `group` → `componentRef` / `groupRef` ids. A visited set with three colours: a back edge refuses with `xml_parse_error` naming the cycle, including a cycle that closes through base definitions (spec US2 AS-4). A memoized, **saturating** expanded size per component and per group (a field counts 1; a group counts 1 plus its body; a component is inlined), exactly as `expand_field_list` emits. From the memo it computes the total `finalize()` will emit: every message's header + body + trailer (a header or trailer reference is charged once per message), every component standalone, every group standalone. It refuses with `overlay_limit_error` naming the budget when the merged total exceeds the base's own total by more than the budget. (Every standalone component and group and every message is in that total, so no separate per-definition check is needed.) |
| 7 | Conflicts | R-4's remaining checks. Every reachability question R-4 asks ("does the parent already reach this tag through a component?") is answered from step 6's memo. There is no second recursive walk. |
| 8 | Splice | R-3. |
| 9 | Build | the loader's unchanged `build_handle_from_doc`. |

**The expansion budget.** A fixed constant, compared against the overlay's **increase** over the base,
so any base the base loader accepts is admitted with an empty overlay. Derivation (the code comment
records this recipe, not a number): run step 6's count over every vendored base (`dictionaries/*.xml`
and `dictionaries/orchestra/OrchestraFIXLatest.xml`), take the largest total, and set the budget to the
next power of two at or above twice it. An overlay may then add at most about the expansion work of the
largest vendored base. Starting estimate: **2^20** emitted entries, from the Gate A round-1 review's
Python model of `expand_field_list` over FIX50SP2 (2026-10-31: messages 307 480 + components 106 374 +
first-seen groups ≈ 28 263, about 4.4 × 10^5; the Orchestra file was not modelled). The implementer
re-derives it with the step 6 code itself and records the value in the verify record; where the two
differ, the recipe wins. The budget is a safety bound, not a performance target: an overlay near it
can make a small base load many times slower. SC-005 measures one named fixture (W-12's at-cap overlay on
FIX50SP2), with its expansion count recorded, not every overlay the caps admit.

The values in step 5 are 2c §4.4.2's for fields, components and messages, plus the two Orchestra
containers 2c did not know about, an enum-value cap the byte cap would otherwise bound only loosely, and
the reference cap Gate A round 1 added. Fixed constants in `src/dictionary/`, no configuration knob (2c's
`FIXPP_DICT_*` names were never implemented). A cap or budget failure throws `overlay_limit_error`.

The base path is not hardened (spec FR-011): the caps apply to the overlay document; the cycle check and
the budget cover the merged graph, and so cover the base's own graph only on the overlay path.

## R-6 — Errors

New in `include/fixpp/dict/error.hpp`, both deriving from `xml_parse_error` so that every existing
`catch (xml_parse_error const&)` and `trap_throw_to_expected` site keeps working:
- `overlay_conflict_error` — R-4; message names the element and key.
- `overlay_limit_error` — R-5 caps; message names the cap and its value.

| C++ (catch order) | C ABI (new function) | TOML `reason_class` |
|---|---|---|
| 1. `overlay_limit_error` | `FIXPP_ERR_DICT_LIMIT_EXCEEDED` (201; its first producer) | `invalid_or_contradictory_selector` |
| 2. `std::bad_alloc` (covers `xml_oom_error`, which derives from it) | `FIXPP_ERR_DICT_OOM` (202) | same |
| 3. any other `std::exception` (`xml_parse_error`, `overlay_conflict_error`, `unknown_version_error`, …) | `FIXPP_ERR_DICT_CONFIG` (200) | same |
| 4. `...` | `FIXPP_ERR_DICT_CONFIG` (200) | same |
| null argument | `FIXPP_ERR_NULL_HANDLE` | n/a |

The `try` covers the whole thunk body, not only the loader call. In `fixpp_dict_load_from_xml`'s shape,
which the new function copies, `std::filesystem::path{path}`, `std::make_shared<const Dictionary>` and
`new fixpp_dict` sit outside the loader's `trap_throw_or_throw<xml_oom_error>` and throw a bare
`std::bad_alloc`; catching `std::bad_alloc` (step 2) maps those to `FIXPP_ERR_DICT_OOM` too (Gate A round
1, A-006). The mapping is a small internal function (exception → code), declared in `src/capi/capi_internal.hpp`
(which `tests/capi/CMakeLists.txt` already puts on the capi tests' include path) and defined in
`src/capi/dictionary.cpp`. The thunk's catch calls it, and a witness feeds it each exception type
directly (W-9). A real
allocation-failure seam is not added: global OOM injection is isolation-sensitive under
`[const §VII.8]`, and would need its own executable.

The existing `fixpp_dict_load_from_xml` keeps mapping everything to `FIXPP_ERR_CAPI_CONFIG_INVALID`; it is
not changed (spec FR-001). The new function uses the DICT family because those codes say what failed;
`FIXPP_ERR_DICT_LIMIT_EXCEEDED` is defined in `include/fix/c_api/error.h` and nothing returns it today
(re-derive: `git grep -n FIXPP_ERR_DICT_LIMIT_EXCEEDED -- src`).

## R-7 — Send-side header membership (FR-022, L-422-1)

**Today.** `is_send_header_tag` (`src/session/session.cpp`) is a linear find over a fixed 34-tag array
(the FIXT11.xml `<header>` plus 370). Its only caller is the header/body partition in
`Session::build_send_frame_`, which runs on every `Session::send` for each non-counted payload field.
Neither `Dictionary` nor `table_view` records header membership: the XML loader folds `<header>` and
`<trailer>` fields into every message's run, and the Orchestra loader expands component 1024 inside
each structure.

**Measured: the union changes nothing for the vendored dictionaries.** A probe (scratch script, not
checked in) expanded each vendored dictionary's header — QuickFIX `<header>` with its components and
groups; Orchestra component 1024 with its componentRefs and groupRefs, NumInGroup tags included — and
subtracted the fixed 34:

| Dictionary | Header tags | Not in the fixed set |
|---|---:|---|
| FIX40, FIX41, FIX42, FIX43, FIX44 | 18, 22, 27, 31, 30 | none |
| FIX50, FIX50SP1, FIX50SP2 | 0 (no `<header>`: the transport header is FIXT11's) | none |
| FIXT11 | 33 | none |
| OrchestraFIXLatest | 33 | none |

The probe was shown able to report non-zero: seeded copies of FIX44.xml (tag 5020 in `<header>`) and of
the Orchestra file (a `fixr:fieldRef id="5020"` in component 1024) both reported `[5020]`. So a session
on any vendored dictionary sends byte-identical frames after FR-022; only a user dictionary or overlay
declaring a header tag outside the fixed set changes anything. Re-derive with a header expansion over
`dictionaries/*.xml` and `dictionaries/orchestra/*.xml` minus `kSendHeaderTags`.

**Decision.**
- The metadata handle gains the dictionary's **declared header tags**, computed at finalize from the
  header expansion (XML `header_node_`; Orchestra component 1024), NumInGroup tags and group members
  included.
- `table_view` gains `header_bits_` in the `group_bits_` / `enum_bits_` shape (`std::vector<std::uint64_t>`
  sized to the highest set tag; a query past the end answers false), filled by `as_table_view()` through
  a `table_view_builder` forwarder, and a public `bool is_header_tag(std::uint16_t) const noexcept`.
- `build_send_frame_`'s classification becomes
  `is_send_header_tag(tag) || (inbound_tv_ && inbound_tv_->is_header_tag(tag))`, evaluated only on a
  fixed-set miss. No allocation, O(1) (a bounds check and a bit test), no suspension; the fixed set and
  its position rule are unchanged. For every vendored dictionary the union equals the fixed set (table
  above), so the result is unchanged.
- The dictionary tags take the same position rule as the fixed set's: moved into the header in the
  caller's order (the existing two-pass partition).
- **Counted Data values.** The partition classifies with
  `field->counted ? prev_header : is_send_header_tag(field->tag)`, so a counted Data value never looks
  up its own tag: it follows its Length's class, which keeps the pair adjacent. That rule is unchanged
  and FR-022 states it. A dictionary that declares the Data tag in its header but not its Length cannot
  be sent correctly either way; the pair follows the Length (A-008).
- **The fixed set becomes testable.** `kSendHeaderTags` is a function-local `static constexpr` inside
  the file-static `is_send_header_tag` in `src/session/session.cpp`, so no test can read it. It moves,
  with `is_send_header_tag`, into a new private header `src/session/send_header_tags.hpp`, as
  `scan_frame_header.hpp` was moved out of `session.cpp` for its tests. W-7's union assertion includes
  that header from the session binary, so a drift between the production set and the test is
  impossible (N-9).

**`kReplayHeaderTags` needs no change.** It is local to `build_replay_frame` (ResendRequest replay of
stored frames) and only finds where to insert PossDupFlag(43) / OrigSendingTime(122). Its comment requires
a *subset* of the real header. Stored frames were already partitioned by `build_send_frame_`, so a
dictionary header tag in a stored frame already sits in the header, after the leading framing tags the
set names; inserting 43/122 before the first tag outside the six keeps them inside the header either way.
A witness pins it (contracts W-6).

**Alternatives rejected.** A hash set (a lookup per body field on the send hot path); consulting
`Dictionary` directly (not reachable as cheaply as the session's `inbound_tv_`, and the header would
have to be re-derived per call); storing only the tags outside the fixed set (keeps the vendored stores
empty, but the dictionary module would need the session's fixed set, moving it into a header both
modules include, for a saving of one bit test).

## R-8 — C++ API

```
// dict::XmlLoader (QuickFIX-XML base + <fix> overlay)
Dictionary load_with_overlay(path const& base, path const& overlay, pmr::memory_resource*,
                             unresolved_group_policy = fail_closed);
Dictionary load_with_overlay_from_string(string_view base, string_view overlay, pmr::memory_resource*,
                                         unresolved_group_policy = fail_closed);
// dict::OrchestraLoader (Orchestra base + fixr overlay): the same two members.
// dict:: free function, next to load_any: sniffs the base root, requires the overlay root to match.
Dictionary load_any_with_overlay(path const& base, path const& overlay, pmr::memory_resource*,
                                 unresolved_group_policy = fail_closed);
```

New members on existing classes are source-compatible. `load`, `load_from_string` and `load_any` are
unchanged. The `XmlLoader` and `Dictionary` header comments that say `load_overlay` / `with_overlay` are
deferred are replaced by a pointer to the shipped calls (a comment states the procedure, not a count).

## R-9 — TOML

- `recognize_keys` (`src/config/toml_config_loader.cpp`) applies one key set at every scope. Make it
  scope-aware for one key: `dialect_overlay` leaves `kDeferred`, is accepted in `[default]` and
  `[[session]]`, and at root produces `invalid_or_contradictory_selector` with "dialect_overlay is a
  session key; set it in [default] or [[session]]". `T020_DeferredSetExactCoverage` keeps guarding
  every other deferred key (re-derive the set from `kDeferred`).
- A new resolver step runs after `resolve_engine_dictionary` in `resolve_selectors`
  (`src/config/selector_resolver.cpp`). Its guard is `bundle.engine.dictionaries.empty()`, not "the
  `[dictionary]` step failed": an absent `[dictionary]` makes `resolve_engine_dictionary` return with no
  diagnostic (the `missing_required` at `dictionary` comes earlier, from `load_toml_config`), so a skip
  keyed on that step's diagnostics would never fire, and `front()` on the empty vector is undefined
  behaviour (it aborts under libstdc++'s assertions, on by default at `-O0`). The existing fixture
  `tests/config/fixtures/neg_missing_dictionary.toml` (`[[session]]`, no `[dictionary]`), exercised by
  `LoadNegativeBattery.T020_MissingRequired_Dictionary`, reaches exactly this case (N-3).
  - **Empty:** assign nothing. For each authoring origin of `dialect_overlay` (below), add
    `invalid_or_contradictory_selector` at that key path: "dialect_overlay requires [dictionary]".
  - **Not empty:** for each session `i`, read `dialect_overlay` from the merged session table; with
    none, set `bundle.sessions[i].config.dictionary = bundle.engine.dictionaries.front()`; with one,
    `resolve_path(base_dir, …)` and load `load_any_with_overlay(base path, overlay path,
    opts.resource)`.
  - One load per distinct resolved overlay path, cached by that path (a success or a failure), and the
    merged dictionary shared by the sessions naming it; sessions do not each re-parse the base.
    `EngineConfig::dictionaries` is unchanged.
- **Value checks**, at the key path where the value was written: a non-string value is
  `malformed_value` ("dialect_overlay must be a string path"); an empty string is `empty_required`
  ("dialect_overlay must not be empty"), mirroring `dictionary.path`'s arms in
  `resolve_engine_dictionary`.
- **Diagnostics are per authoring origin, not per path (A-005).** `merge_defaults` loses origins, but
  `resolve_selectors` also receives `root_tbl`, which still holds the raw `[default]` table and the raw
  `session` array. The origin of each session's value is read there: `session[N].dialect_overlay` when
  the session's raw table sets the key, else `default.dialect_overlay`. A failing load is reported once
  per distinct origin: when `[default]` and `session[1]` both name one failing file, that is two
  diagnostics, one at each key path; when several sessions inherit one failing `[default]` value, that
  is one diagnostic at `default.dialect_overlay`.
- **No "none" value (N-11).** TOML has no null, and 098 defines no reserved value meaning "no
  overlay". A `[default]` overlay therefore cannot be cleared for one session; the operator moves the
  key out of `[default]` into each session that wants it. QuickFIX's `[DEFAULT]` `DataDictionary` has
  the same property. Disclosed in B-098-3 and the TOML contract (orchestrator default, owner to confirm;
  a reserved value would be new surface).
- `trap_throw_to_expected`'s doc (`src/config/loader_internal.hpp`) lists "ONLY these three throwing
  sites"; the overlay load is a fourth and the doc changes to the rule, not a count.
- The session dictionary fill is a behaviour change for every TOML user (B-098 row): today a host must
  copy `engine.dictionaries.front()` into each session (the one in-tree example is
  `tests/session/engine_establishment_timeout_test.cpp`); after 098 the bundle arrives filled. A host
  that still copies it gets the same pointer for a session without an overlay, and **overwrites the
  merged dictionary** for a session with one — disclosed in the B&L row and the release note.

## R-10 — The equivalence oracle (FR-017, SC-001)

No existing helper compares two dictionaries store by store (`determinism_test.cpp`'s byte serializers,
`reused_tag_census.hpp`, `required_scope_oracle.hpp` and the 096 walk each cover a slice). A new test
helper, `tests/dictionary/dictionary_equivalence.hpp`, compares two `Dictionary` values and their
`table_view`s and returns a list of differences. Gate A round 1 (A-002, A-004, N-4, N-5) fixed the four
things below together.

**1. What it is compared against.** The equivalent edited XML is the base with each overlay addition
appended at the end of its matched container, in overlay document order (spec Terminology). It is
unique, because each store's order-dependent rule (component ids and `parent_component_id`, enum order,
the Length+Data first writer, the group delimiter's first emission, the first-seen group node behind the
bare group stores) sees the same document order. The references below are written to that rule.

**2. Where the keys come from.** The public API cannot enumerate components or group contexts
(`include/fixpp/dict/dictionary.hpp` has `component(name)` and no enumeration), so keys come from the
union of three sources, over both sides:
- a **raw-document census** of both sides' XML, walked by the test: field numbers and names, message
  types, component names, group nodes, and the (MsgType, parent path, NumInGroup) contexts;
- a **test-only read of the handle's indexes** through `src/dictionary/dictionary_internal.hpp`
  (`components_by_name_`, `components_`, `groups_`, `group_ctx_delim_pool_` and the per-message runs).
  `tests/dictionary/CMakeLists.txt` already puts `src/dictionary` on the include path, and existing tests
  (`delimiter_census_test.cpp`, `loader_disposition_test.cpp`) include that header, so no public API is
  added;
- the **public API** (`messages()`, `field_by_name` and the like).

An extra message, field, component (including one no message uses), group or context on either side is
therefore reported, not hidden.

**3. What it compares.**
- **Dictionary API**: `messages()`, per message `message_fields` / `field_ref` / `required_fields`,
  `field` / `field_by_name` / `field_name` for every tag on either side, `enum_values` (in order),
  `component` (including `component_id` and `parent_component_id`) / `component_fields`, `group` /
  `group_fields` / `group_first_field`, `length_pair_data_tag`, `which_session_version`.
- **table_view**: `valid_tags_for` and `required` per message, field types, `enum_valid` for every listed
  value plus one value listed on neither side, `has_nonstandard_pair` and both pair directions, and the
  new `is_header_tag`. The `is_header_tag` comparison and its seeded row are added in plan Phase 5, when
  the query exists; Phases 2–4 build the comparator without it.
- **Group stores, each separately**: the bare stores (`group_first_field(no_tag)`,
  `group_member_tags(no_tag)`, `group_required_members(no_tag)`), the exact store
  (`group_first_field_exact`), and the context stores (the three-argument accessors) for every context
  key. The three-argument accessors fall back to the bare store, so they are never the only check: a
  missing context record whose bare value happens to match is caught by the exact and internal reads.
- **Against the census, not only side against side.** Both sides go through the same
  `build_handle_from_doc`, so a store that loader computes wrongly is equal, and wrong, on both sides.
  `component_fields` and `group_fields` are therefore also compared with the members the raw census
  expands for that definition.

**The side-table index wrap (N-5).** `ComponentRef::first_field_index` and `GroupRef::first_field_index`
are `std::uint16_t` (`include/fixpp/dict/component_ref.hpp`, `group_ref.hpp`), and both loaders' `finalize`
assign them by `static_cast<std::uint16_t>` of the side table's size. The Gate A round-1 review's model
(2026-10-31) puts FIX50SP2's `component_fields_` past 65535 entries, so later components would read
another component's fields. This is pre-existing, and 098 does not fix it (orchestrator default, owner to
confirm; spec Clarifications Gate A round 1). So:
- The census comparison covers each component and group whose start in its side table, computed by the
  test from the census sizes in the order `finalize` assigns them (re-derive the order from the two
  `finalize` loops that set `first_idx`; today components go in declaration order and groups after a
  sort by NumInGroup tag), is below 65536. A
  definition past that point is listed in the verify record as a wrap case, not compared. This is a
  condition, re-evaluated on every run, not a count.
- A probe (a gtest in the dictionary binary, not a 098 witness that must go GREEN) compares
  `component_fields` of the first component past the wrap point on FIX50SP2 with its raw members. Its
  result goes in the verify record. If it shows the wrap, the defect is filed with R-15's issues and
  gets an `L-098-*` row; if it does not, the condition above admits every definition and the note says
  the model was wrong.

**4. Seeded rows (W-0).** One seeded mismatch per store category the helper claims (a field type, a valid
tag, a required tag, an enum value, an enum order, a component member, a component id, a group member, a
group delimiter, an exact-store entry, a context-store member, a Length+Data pair, a header tag, a
message, a field name), plus:
- an **unused extra component** on one side, visible only through the census and the internal read;
- a **context record that differs only where the bare value would mask it**;
- a **bare-only `group_required_members` difference**;
- a **census mismatch**: the census of a document that differs from the loaded one in one component
  member, which the census comparison must report.

Each is shown to produce a reported difference naming its store before the clean comparisons are
trusted.

**References.** Two kinds, both written by hand from the base file and never derived from the merge
(FR-017):
- **Complete small references.** Small hand-written bases, one QuickFIX-XML (`FIX.4.4` version
  attributes) and one Orchestra (`FIX.Latest_EP303`, with components 1024/1025), each with a hand-written
  overlay and a hand-written complete edited copy. Every FR-003/FR-004 addition kind appears, including
  an enum value on a base field with no values (N-2), and every R-4 conflict has a refusing overlay.
- **Vendored-base patches.** The vendored files are 315 KB (FIX44), 1.5 MB (FIX50SP2) and 7.5 MB
  (Orchestra), too large to hand-copy. FIX44, FIX50SP2 and the Orchestra file each get an overlay and a
  hand-written **insertion patch**, applied by a small C++ test helper (no `patch` binary, so it runs on
  MSVC). A patch is a list of hunks. Each hunk has a **multi-line context block**, copied from the base,
  that MUST match exactly once, as a diff hunk's context does, and lines to insert **before the block's
  last line**. That is how an append lands at the end of a container: `</message>`, `</group>` and
  `</component>` are never unique in FIX44 or FIX50SP2, and anchoring on a unique opening line would
  insert at the start of the container, which is a different dictionary (a group's delimiter is its
  first emission, `detail::capture_first_emission`). The helper is line-based, not XML-aware: a
  structural patcher would share the splice's blind spots. A context block that matches zero or two
  places fails the cell before any comparison.
- **Kind coverage on the vendored bases.** Each vendored overlay covers at least: a header field, an enum
  value on an existing field (an Orchestra code set for the Orchestra file), a member added to an
  existing group, a new message with a required field, and a field added to existing messages. The two
  QuickFIX-XML overlays also declare a custom Length+Data pair, and the FIX44 overlay extends more than
  64 existing messages (N-7). Every other kind is covered on the small bases.

## R-11 — Fuzzing (`[const §VII.7]`)

The overlay loader is new parser-touching code over untrusted XML. Add
`tests/fuzz/fuzz_dict_overlay.cpp`: a fixed small base (one per format, chosen by the first input byte),
the rest of the input as the overlay, both C++ entry points; every throw must be one of the `dict::`
error types. Seeds: the hand-written overlays (R-10), the conflict and cap overlays, a DOCTYPE
overlay, and the expansion-doubling overlay of R-5 (A-001). Registered like `fuzz_dict_xml_loader` / `fuzz_orchestra_loader` (`tests/fuzz/CMakeLists.txt`,
built under `FIXPP_BUILD_FUZZ`). The ten-minute run is `/speckit-verify`'s fuzz step.

## R-12 — C ABI and Python

- `fixpp_dict_load_from_xml_with_overlay(const char* base_path, const char* overlay_path,
  fixpp_dict_t** out_dict)` in `include/fix/c_api/dict.h`; implementation next to
  `fixpp_dict_load_from_xml` in `src/capi/dictionary.cpp`, on `std::pmr::new_delete_resource()` as the
  existing function is (fixpp#495 D-5); errors per R-6; doc block in dict.h's style with one reentrancy
  token (`Reentrancy: single-thread.`).
- C-ABI **1.13**, additive: `version.h` block ending "Added: fixpp_dict_load_from_xml_with_overlay
  (header).", `tests/capi/version_test.cpp`'s exact-version pin, `tests/abi/golden/fixpp_capi_symbols.txt`,
  `tools/capi_freeze.sha256`. `src/capi/fixpp_capi.map` exports `fixpp_*` by glob and needs no edit.
- Python: SWIG wraps all of dict.h and the `fixpp_dict_t**` out-param by type, so the function is
  exported as `fixpp.dict_load_from_xml_with_overlay`. Update the exact-set import test
  (`bindings/python/tests/wheel/test_import_surface.py`) and the GIL audit table in `fixpp.i`; add a
  Python test. The OO wrapper `fixpp_oo.Dictionary` is not extended (not asked for).

## R-13 — Mutation runs

098 is built in `~/Work/Programming/fixpp-parallel`, so mutants cannot run there (`[const §XVI.6]`), and
the main checkout must not be switched. Mutants run in a third worktree created detached at the
implementation head, with its build tree on `/mnt/wsl/fixppbuild` (`df -h /mnt/wsl/fixppbuild` first),
removed at close-out.

## R-14 — Records

- **Retire:** L-002-2 (the overlay ships); L-422-1 (FR-022). **L-004-1** is retired as stale, with its
  evidence: it says Iter mode uses only the static pair table, but `field_iterator` takes a `dict_hooks`
  value with no default and pairs through it (`include/fixpp/wire/parser.hpp`), since fixpp#426.
- **Reword:** L-426-1 (pre-session routing and redaction use the standard table alone, for a custom
  dictionary's or an overlay's pairs); L-426-2 (applies to overlays: a pair that reuses a standard tag is
  not honoured). **L-426-3** is unchanged (QuickFIX interop of Data fields, not dictionary-specific).
- **Update:** B-066-1's extension story and its release note (the overlay is the shipped path).
- **New `B-098-*`:** the overlay's load and conflict rules; the shared-definition reach of Orchestra
  code-set and group edits; TOML sessions now arrive with `SessionConfig::dictionary` filled (and a host
  that overwrites it discards an overlay); the removed `SessionConfig::dialect_overlay` field (pre-release
  breaking, C++ API); the send-side header union; C-ABI 1.13; an enum value on a base field with no
  values closes that field's domain (N-2, R-4); a `[default]` overlay cannot be cleared for one session
  (N-11, R-9).
- **New `L-098-*`:** the FIXT registry holds base dictionaries only, so row 6 (fixpp#643) must keep the
  session's own dictionary for its own version (FR-021); the typed (codegen) tier reflects the base only;
  the base `XmlLoader::load` / `OrchestraLoader::load` paths have no caps or cycle guard (with the filed
  issue); and, if R-10's probe shows it, the `first_field_index` side-table wrap (N-5, with its filed
  issue).
- **Catalogue:** D-009 and COM-011 `done`. D-009's Spec ref `[FIX50SP2 §3.2]` is borrowed from D-010 and
  is replaced by `[impl] implementation` with a coverage-index supplemental note, as D-007 has (FIX
  defines user-defined tag ranges, not a file format or a merge). Both rows' `Applies to` cell widens
  from `4.0–5.0SP2` to `4.0–5.0SP2, FIXT.1.1, FIX Latest` (FR-001 delivers FIXT11.xml and the Orchestra
  base); no new row (A-012).
- **Design docs and brain: the inventory is a grep, not a list (A-010, A-011, Root cause #4).** The
  surfaces that describe the superseded overlay design, or that 098 makes false, are found by data-model
  §G's recipe, re-run at the records phase. Every hit is dispositioned: an in-place "Updated
  (098-dialect-overlay)" note naming what 098 shipped, or a verify-record line saying why the hit is
  history or still true. The recipe already reaches beyond 2c's overlay sections: other 2c sections that
  state the old attachment point, grammar or API, other signed-off design docs (for example
  `.specify/2d-threading.md`'s `SessionConfig` listing and `.specify/2m-pybind.md`'s Python config key),
  and the brain pages whose `refs` name a file 098 changes.
- **Constitution:** no amendment. `[const §XV.13]` is satisfied as written; no article names the
  overlay's grammar, attachment point or the dead field. fixpp#642 therefore goes to a docs PR.
- **Comments that would rot:** `FieldType::DialectExtension`'s "re-enabled when D-009 ships"
  (`include/fixpp/dict/field_ref.hpp`), the deferred notes in `dictionary.hpp` and `xml_loader.hpp`, the
  `kWitnessHeaderTags` comment in `tests/session/test_resend_answer_field_order.cpp` that calls
  `kReplayHeaderTags` the production header set (stale since #422; fixed because 098 touches that area).

## R-15 — Issues to file (spec Out of Scope)

Labelled per `gh label list` at filing time: (1) base loaders have no XML caps or component-cycle guard;
(2) the dictionary-axis engine fallback in `session_config.hpp`'s header comment is not implemented;
(3) `redact_tag554` has no production caller; (4) one `wire_unexpected_tag` for both QuickFIX 373=0 and
373=2 cases; (5) only if R-10's probe shows it: the `std::uint16_t` `first_field_index` wraps past 65535
side-table entries (N-5).
