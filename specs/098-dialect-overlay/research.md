# Research: 098 dialect overlay

Every fact below was read at `8319c206` (`origin/main`). File:line citations are leads for the
implementer and will drift; re-derive with the `grep` given where one is given.

## R-1 — Merge mechanism: splice the overlay into the parsed base document

**Decision.** Each loader parses the base into a pugixml document, parses the overlay into a second
document, runs the guards in R-5's order (document shape, then the merged-graph cycle and expansion
budget walk, then the R-4 conflict checks), splices the overlay's elements into the base document
(R-3), compares the Length+Data pair relation of the spliced document with the base's (R-5 step 8a),
and then runs the loader's existing post-parse path. That path is shared with `load`; the overlay path
only arms an expansion step counter in it, which `load` leaves unarmed (R-5). For QuickFIX XML that path
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
  in `load`/`load_from_string` the document is a local, so the splice happens before the call. The
  post-parse code changes in three places only, each shared with `load`: the pair detection is factored
  into a function the step 8a check also calls (R-5), `expand_field_list` gains the step counter (R-5),
  and finalize gains the declared header store (R-7).

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

**One tag twice at one level (both directions).** A parent's *level set* is the set of tags it emits at
its own level: its direct fields, the NumInGroup tag of each direct group, and the level sets of the
components it references (inlined). A group's members are the group's own level, not the parent's. For
a QuickFIX-XML message, the level is header + body + trailer, because the loader folds all three into
one run. The overlay is refused when an addition makes a tag occur twice in the level of a parent where
the base had it at most once. Both directions are covered: a field or group redeclared as a direct child
that the parent already reaches through a component (for example `NoPartyIDs` inside component
`Parties` used by NewOrderSingle), and a new component reference that reaches a tag the parent already
declares directly or through another component. The overlay must extend the component instead. The
level sets come from R-5 step 6's walk, never from a second recursive walk (R-5).

**Match keys must be unique in the base.** The loaders accept some duplicates (no duplicate-msgtype
check in either `collect_messages`; `by_name_.emplace` drops a second `<field>` of one name; repeated
direct children). Every match index the splice uses (messages, field definitions by number and by
name, direct children, Orchestra ids and code-set names) is built by one helper. The helper records a
duplicated base key instead of refusing it, so a base that `load` accepts is still admitted with an empty
overlay. A lookup by an overlay element that hits a duplicated key is refused with
`overlay_conflict_error`, naming the key and both base occurrences: the overlay cannot say which one
it extends.

**Length+Data pairs are checked as a relation, not as a shape.** Each loader's own pair detection
(`LoaderState::detect_length_pairs`, `OrchestraLoaderState::resolve_length_pairs`) is the only
definition of which tags pair. So the merge does not model it. R-5 step 8a runs that detection on the
base document and on the spliced document and refuses with `overlay_conflict_error` when any pair that
touches a **base** tag (a field number the base declares) is gained, lost or given a second partner. The
detection is factored out of each loader into a function of a parsed document that the loader's own
pairing and the check both call, so the two cannot drift. It returns the relation in both directions,
and it reports a collision instead of throwing:
- **XML:** the forward map is `detect_length_pairs`'s Length → Data map (what
  `FieldRef::length_pair_data_tag` carries; `mark_pair` keeps a Length's first Data). The reverse
  direction is its inverse with **every** Length kept, because `mark_pair` guards only the Length half:
  two Lengths can name one Data. That is the case `table_view::set_length_pair_data_tag` displaces
  ("Re-pairing either tag drops its old partner"), so the check must see both.
- **Orchestra:** the function records a Length named by two Data fields' `lengthId` as a collision, where
  `resolve_length_pairs` throws "names a Length field already paired with another field". On the base
  document the loader still throws its own `orchestra_parse_error`, as `load` does. On the spliced
  document a collision that touches a base tag is an `overlay_conflict_error`. The other
  `resolve_length_pairs` refusals (a zero tag, a non-Data field with `lengthId`, a `lengthId` that is not a
  Length) stay the loader's `orchestra_parse_error`, as a hand edit gives.

This replaces the previous draft's adjacency rule, which missed the non-strict container walk (a
component or group between a base LENGTH and an overlay DATA does not break adjacency in header,
trailer and message containers), the reverse case (a new LENGTH before a base DATA), and Orchestra's
`lengthId` (Gate A round 2, Root cause A). Detection order matters, because `mark_pair` keeps the first
Data a Length meets: `detect_length_pairs` visits `<fields>`, then header, trailer and messages in
order, then components in document order, then every group in pre-order. A new DATA after a base
LENGTH that the base already pairs loads only when the base pair forms **earlier** in that order than
the overlay's adjacency; the relation is then unchanged. When the base pair forms later (for example a
base LENGTH last in `<fields>`, paired only by a container walk), the overlay's adjacency now takes the
LENGTH first, the base pair is lost, and the check refuses it.

**Orchestra** (`fixr:repository` overlay):

| Overlay element | Match key | On match | New |
|---|---|---|---|
| `fixr:codeSet` | `name` (the loader's key, R-2) | `id` and `type` MUST equal; listed `fixr:code`s are added by `value` (description/name MUST equal on match) | appended to `fixr:codeSets` |
| `fixr:field` | `id` | `name`, `type` and `lengthId` MUST equal | appended to `fixr:fields` |
| `fixr:component` | `id` | `name` MUST equal; children merged | appended to `fixr:components` |
| `fixr:group` | `id` | `name` MUST equal; the overlay's `fixr:numInGroup` child MUST have the base's `id` and is not spliced; other children merged | appended to `fixr:groups`; it MUST have exactly one `fixr:numInGroup` child |
| `fixr:message` | `msgType` (a duplicated base `msgType` is refused on lookup, above) | `id` and `name` MUST equal; `fixr:structure` children merged | appended to `fixr:messages` |
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
So the merge MUST detect, at the step R-5's order gives each check, and refuse with
`overlay_conflict_error` naming the element. The root checks run at step 4a and the rest at step 7, all
before the splice, with one named exception: the Length+Data relation check runs at step 8a, after the
splice and before the build, because it compares the spliced document's relation with the base's (R-3).
It follows no references (it reads `<fields>` and walks containers in document order), so it does not
break FR-010's order.

- **Overlay against base:** a matched definition whose identifying attributes differ (table above); a
  field name already used by another number; an Orchestra code-set name already used by another `id`
  (N-6); a code/enum value redescribed; `required="N"` (XML) or `presence="optional"` (Orchestra) on a
  child the base marks required; a tag that an addition makes occur twice in one level of a parent, in
  either direction (R-3); a lookup that hits a duplicated base key (R-3); an Orchestra group whose
  `fixr:numInGroup` differs from the base's, or a new one without exactly one (R-3); a Length+Data pair
  touching a base tag that is gained, lost or given a second partner (R-3, step 8a).
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
owner confirmed 2026-11-01, spec Clarifications Gate A round 1). It is disclosed in B-098-7 and witnessed (W-1,
W-5). It is not a "removal" under FR-005, which forbids deleting a listed base value, but it narrows
what the base accepted, and the disclosure says so.

## R-5 — Hardening the overlay input (FR-009, FR-010)

The guards bound two things: the overlay document's **shape**, and the **work** its references cause
when `finalize()` expands them. Both loaders' `expand_field_list` inline every componentRef and groupRef
recursively, with no memo and no budget. `finalize()` expands every message (header, body and trailer),
then every component on its own (even one no message reaches), then every first-seen group on its own.
So a small acyclic overlay can still cause exponential expansion, for example
`C_n = { component C_{n-1}; group NoG_n { field F_n; component C_{n-1} } }` over 63 levels (Gate A
round 1, A-001). Emitted entries do not bound it either: `collect_components` accepts an empty
`<component>`, so two empty-bodied chains that each reference both chains' previous level recurse
exponentially and emit nothing (Gate A round 2). Definition counts and nesting depth do not bound that.
The guard stage below does, in a unit that charges every reference followed.

The guards run in this order. Each step is non-recursive or iterative, and none reaches
`build_handle_from_doc` until every earlier step passed.

| # | Guard | Rule |
|---|---|---|
| 1 | Byte cap | **4 MiB**. Read at most cap + 1 bytes into a buffer (path form) or check `size()` (string form); refuse when more were available. Never trust a `stat` size (a FIFO or `/proc` file reports a wrong size). Before parse. |
| 2 | Parse | The overlay uses `pugi::parse_default \| pugi::parse_doctype`. A result of `xml_parse_status::status_out_of_memory` (pugixml reports allocation failure as a parse status, not a throw) becomes `xml_oom_error`, never `xml_parse_error` (A-006). Any other failed status is `xml_parse_error` (`orchestra_parse_error` for Orchestra) as today. One status-mapping function serves both parses of an overlay call, so a base-parse allocation failure inside `load_with_overlay[_from_string]` is also `xml_oom_error` (N-10). The base is read without a byte cap and parsed with the same flags as `load`; only the status mapping is shared. Steps 1, 3, 4 and 5 apply only to the overlay document. `load` and `load_from_string` keep their own mapping (FR-001). |
| 3 | DOCTYPE | refuse any `node_doctype` child of the document. pugixml 1.15 never loads external entities and expands only the five predefined entities and character references (its `parse_default` discards a DOCTYPE without a node, which is why the flag is set: to see it). Defence in depth: no DTD is ever loaded. |
| 4 | Depth | **32** element levels; an iterative (explicit stack) walk of the overlay document. |
| 4a | Root | R-4's root checks: the overlay's format matches the base's, its version matches (FR-006), its root children are in data-model §A.1's list, and no container name repeats. Steps 5 and 6 count and walk by element name, so they presume a checked format. |
| 5 | Entry and reference caps | one flat pass over the overlay document. **New** definitions only (N-7): fields **1024**; components **64**; messages **64**; Orchestra groups **64**; Orchestra code sets **256**. Enum values / codes added **4096**. References added (QuickFIX `<field>` / `<component>` / `<group>` refs, Orchestra `fixr:fieldRef` / `componentRef` / `groupRef`) **8192** in total, to new and existing definitions alike. An existing definition the overlay extends is not a "new" definition; how many are touched is bounded by the base's own count, the reference cap and step 6. |
| 6 | Merged-graph walk: cycles, expansion budget, side-table fit | One iterative walk (explicit stack) over the base's definitions plus the **unspliced** overlay, so no splice is needed first. Steps 6, 7 and 8 share one classifier that decides, per overlay node, whether the splice matches it (a promotion or merge in place), appends it, or drops it (`fixr:annotation`, `fixr:documentation`, a matched group's `fixr:numInGroup`, non-element nodes). Step 6 counts the base's children plus the children that classifier appends, and nothing else, so it walks the shape the splice will produce. Edges: XML, a definition → the components it references and the groups it holds; Orchestra, `component` / `group` → `componentRef` / `groupRef` ids. An unknown reference is a leaf here; the loader refuses it after the splice (R-4). **Cycles:** three colours. A back edge ends the walk with a structured cycle result naming the definitions on the cycle, including a cycle that closes through base definitions (spec US2 AS-4). Each loader throws its own type for it: `xml_parse_error` (XML) or `orchestra_parse_error` (Orchestra), the type `load` uses for a schema error. **Memo**, per component and group, filled in post-order and saturating: *steps*, the number of child nodes `expand_field_list`'s loop visits when it expands the definition (each child counts 1, and a component reference or a group adds the steps of its target); *entries*, the `FieldRef`s it emits (a field 1; a group 1 plus its body; a component its body); and, for step 7, *level sets* (R-3, below). **First-seen groups:** the walk visits definitions in `finalize`'s order (each message's header, body and trailer, in message order; then each component in declaration order) and records, per NumInGroup tag, the first group it meets. That is the group `finalize` expands standalone. A definition already visited is not walked again, which keeps the order of first encounters. **Budget:** the merged total is the steps `finalize` will take, from the memo: every message's expansion (a header or trailer is charged once per message), every component standalone, every first-seen group standalone. The walk returns a structured budget result, and the loader throws `overlay_limit_error` naming the budget when the merged total exceeds the base's own total by more than the budget. **Side-table fit (N-3):** a side table (components; groups) *fits* when every definition's start index and entry count is at most 65535, the most a `std::uint16_t` holds. Starts are prefix sums of *entries* in `finalize`'s order (components in declaration order; first-seen groups sorted by NumInGroup tag). When the base's table fits and the merged table does not, the loader throws `overlay_limit_error` naming the table. The budget is checked first, so an overlay that fails both is reported as over the budget. A base whose table already does not fit is out of scope (R-10). |
| 7 | Conflicts | R-4's remaining checks. Every reachability question R-4 asks is answered from step 6's level sets; there is no second recursive walk. Level sets are built only for the parents the overlay extends and the definitions they reach at their own level (for a QuickFIX-XML header or trailer addition, every message), as sorted tag vectors memoized per definition. Each is no larger than the base's declared field count, which bounds the memory: the code comment states that recipe. SC-005's fixture does not measure this cost. |
| 8 | Splice | R-3. |
| 8a | Length+Data relation | the factored pair detection (R-3) on the base document, run before step 8 changes it, and on the spliced document; refused with `overlay_conflict_error` when a pair touching a base tag differs. It reads `<fields>` and walks containers in document order (XML) or reads `lengthId` attributes (Orchestra), following no references. Errors it raises on the spliced document other than a collision (for example an unknown field type) are the loader's own, as the build would raise them. |
| 9 | Build | the loader's `build_handle_from_doc`, with the step counter armed at the smaller of step 6's merged total and the base's total plus the budget (below). |

**The step counter: the bound holds by construction (Gate A round 2, Root cause B).** Step 6 is a model
of `finalize`'s expansion. A model can miss an expansion site that a later change adds (round 2 found
one: a header-store walk). So the build does not trust it:
- Each loader's state class (`LoaderState`, `OrchestraLoaderState`) gains a step counter. `expand_field_list` increments it once at the head of
  its child loop, for every child node, which is the unit step 6 counts. When the counter is armed and
  passes its limit, `expand_field_list` throws `overlay_limit_error` with the distinct counter origin
  and message "expansion step count exceeded the pre-walk prediction (internal model drift)". Step 6's
  legitimate budget refusal retains its budget origin and message.
- The overlay path arms it with the smaller of step 6's merged total and the base's own total plus the
  budget. Wherever the build is reached, step 6's budget comparison has already admitted only a merged
  total at most the base's total plus the budget, so the armed value is the prediction and the equality
  W-1 and W-2 assert is unchanged. The second term is for a build reached without that comparison
  (mutant M-12): the counter then refuses with the counter origin once the build passes the base's
  total plus the budget, so M-12's RED is bounded and loud, not a hang or an exponential allocation
  (Gate A loop 2 round 1, G098-L2R1-003). The `load` path leaves the counter unarmed (a null limit),
  so `load`'s behaviour is unchanged by construction.
- An `expand_field_list` call step 6 does not model can therefore cause a loud refusal, never unbounded
  work. The counter does not see other recursive walks; none is added (the header store is a
  visited-set walk, R-7).
- Read seam: `src/dictionary/overlay_merge.hpp` (internal, already on the dictionary tests' include
  path) declares a test-facing entry that loads with an overlay and also returns the counter's final value
  and step 6's predicted total, and reports which of three origins a refusal came from: the pre-walk
  budget, the pre-walk side-table fit, or the build counter. W-1 and W-2 assert no counter-origin
  refusal and, on every input that loads, that the two counts are **equal**. W-11 also aborts on a
  counter-origin refusal. Equality plus the distinct refusal origin catches model drift in either
  direction. It holds only because
  step 6 counts what the loop visits before any de-duplication, counts the spliced shape (the shared
  classifier, step 6), and charges the first-seen group per NumInGroup tag, as `finalize` does.
- Mutant M-17 adds one extra `expand_field_list` call in `finalize`; the counter must refuse with the
  counter origin and W-1 must go RED.

**The expansion budget.** A fixed constant, compared against the overlay's **increase** over the base,
so any base the base loader accepts is admitted with an empty overlay. Derivation (the code comment
records this recipe, not a number): run step 6's count over every vendored base (`dictionaries/*.xml`
and `dictionaries/orchestra/OrchestraFIXLatest.xml`), take the largest total, and set the budget to the
next power of two at or above twice it. An overlay may then add at most about the expansion work of the
largest vendored base. Starting estimate: **2^20** steps, from the Gate A round-1 review's Python model
of `expand_field_list` over FIX50SP2, which counted emitted entries; the Orchestra file was not
modelled, and steps are at least entries. The implementer re-derives the value with the step 6 code
itself and records it in the verify record; where the two differ, the recipe wins. The budget is a safety bound, not a performance target: an overlay near it
can make a small base load many times slower. SC-005 measures one named fixture (W-12's at-cap overlay on
FIX50SP2), with its expansion count recorded, not every overlay the caps admit.

The values in step 5 are 2c §4.4.2's for fields, components and messages, plus the two Orchestra
containers 2c did not know about, an enum-value cap the byte cap would otherwise bound only loosely, and
the reference cap Gate A round 1 added. Fixed constants in `src/dictionary/`, no configuration knob (2c's
`FIXPP_DICT_*` names were never implemented). A cap, budget, side-table or step-counter failure throws
`overlay_limit_error`.

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
- The metadata handle gains the dictionary's **declared header tags**, computed at finalize by an
  iterative **visited-set walk** from the header (XML `header_node_`; Orchestra component 1024): it
  collects fields, NumInGroup tags and group members, and walks each component or group definition at
  most once. The store is a set, so multiplicity cannot arise, and the walk's work is bounded by the
  document's size. It is not an `expand_field_list` call, so the step counter (R-5) does not charge it;
  an implementation that used `expand_field_list` instead would trip the counter on the overlay path
  (Gate A round 2, G098-R2-003). The walk runs on the `load` path too, where it is the only header
  computation.
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
  that header from `session_pure_tests`, so a drift between the production set and the test is
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
  the same property. Disclosed in B-098-3 and the TOML contract (orchestrator default, owner confirmed 2026-11-01;
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
things below together; round 2 (Root cause C) made the compared stores data.

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

**3. What it compares: a declared store list.** The comparator declares the stores it compares as
data, one entry each, and every comparison and every diagnostic is keyed by an entry. The list has:
- one entry per `table_view` data member, and per sub-member of a member that holds several stores
  (for example `group_ctx_entry`'s `members` and `required_members`, and `enum_domain`'s `codes` and
  `multi_value`), with the two pair directions as two entries;
- one entry per `Dictionary` query field, including `FieldRef::component_index` and `group_no_tag`, and
  `ComponentRef::component_id` and `parent_component_id`;
- the census entries (`component_fields` and `group_fields` against the raw document, below).

The comparator's header comment carries the recipe that re-derives the list: read `table_view.hpp`'s
`private:` block and the `Dictionary` query and `*Ref` field declarations. The comment holds the recipe,
not the list. An entry a version-matched overlay cannot change carries a stated reason instead of a seed.
For example the `fixt_framing_*` members are a function of `which_session_version()`
(`Dictionary::as_table_view`), and FR-006 fixes the version. The entries cover:
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

**The side-table index wrap (N-5, revised at Gate A round 2, N-3).** `ComponentRef::first_field_index`
and `GroupRef::first_field_index` are `std::uint16_t` (`include/fixpp/dict/component_ref.hpp`,
`group_ref.hpp`). Both loaders' `finalize` assign them by `static_cast<std::uint16_t>` of the side table's
size, and clamp `field_count` with `std::min<std::size_t>(…, 65535U)`. The Gate A round-1 review's model
puts FIX50SP2's `component_fields_` past 65535 entries, so later components would read another
component's fields. An overlay can also cause the wrap or the clamp on a base that does not have it
today: growing an early-declared component shifts every later start, a large group with a low NumInGroup
tag sorts before base groups, and one component can expand past 65535 entries, all within the budget.
So the round-1 rationale ("pre-existing and not caused by an overlay") was false. The disposition now
(orchestrator default, owner confirmed 2026-11-01; spec Clarifications Gate A round 2):
- **Refused at load where the base fits.** R-5 step 6 refuses an overlay that makes a fitting side table
  stop fitting. On such a base an overlay load never wraps or clamps.
- **Out of scope where the base does not fit.** The base already returns wrong `component_fields` /
  `group_fields` for its later definitions, and the overlay is admitted. The result still equals the
  equivalent edited XML (FR-002), whose load has the same wrong answers. Widening the index is a layout
  change to `ComponentRef` / `GroupRef`, whose 12-byte size is pinned (2c §4.2 AC-F2,
  `tests/dictionary/ref_shape_test.cpp`); it is left to the owner.
- The census comparison covers each component and group whose start and count, computed by the test from
  the census sizes in the order `finalize` assigns them (re-derive the order from the two `finalize` loops
  that set `first_idx`), fit in 16 bits. A definition that does not fit is listed in the verify record as
  a wrap case, not compared. This is a condition, re-evaluated on every run, not a count. Where the base
  table does not fit, every definition the overlay appends after the wrap point is compared side against
  side only, and the verify record says so.
- A probe (a gtest in the dictionary binary, not a 098 witness that must go GREEN) compares
  `component_fields` of the first component past the wrap point on FIX50SP2 with its raw members. Its
  result goes in the verify record. If it shows the wrap, the defect is filed with R-15's issues and
  gets an `L-098-*` row; if it does not, the condition above admits every definition and the note says
  the model was wrong.

**4. Seeded rows (W-0), derived from the list.** Each seeded pair of valid small dictionaries declares
the set of stores the comparator must report. W-0 asserts that the reported set equals that seed's
declared expected set. The union of all expected sets equals the seedable part of the declared store
list; that union plus entries a version-matched overlay cannot change, each with its stated reason,
equals the full list. A
separable store gets a separating seed; for example, promoting an already-valid optional tag changes
`required` without changing `valid`. Every coupled or derived entry carries a reason naming the
invariant that prevents an independent difference and the query through which it is observed. This
includes `group_bits_` and `enum_bits_` derived from their maps, the reverse pair map as the inverse of
the forward map, and each `Dictionary`/`table_view` twin produced by `as_table_view`. An entry a
version-matched overlay cannot change keeps the same stated-reason form and needs no invalid seed. A context-store seed keeps the
bare and exact stores equal while its expected set names the independently observable context entry;
for example, the context `required_members` seed has the same bare `group_required_members` on both
sides (G098-R2-004). Three more seeds test the key sources, not a store:
- an **unused extra component** on one side, visible only through the census and the internal read;
- a **context record that differs only where the bare value would mask it**;
- a **census mismatch**: the census of a document that differs from the loaded one in one component
  member, which the census comparison must report.

Each is shown to produce a reported difference before the clean comparisons are trusted.

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
error types. The Orchestra half catches `orchestra_parse_error` by its derived type before the base, as
`tests/fuzz/fuzz_orchestra_loader.cpp` does, so an Orchestra schema error thrown as a bare
`xml_parse_error` falls through to the terminal rethrow (Gate A round 2, Root cause D). The harness loads
through R-5's read seam and aborts on any counter-origin refusal; when an input loads, it also aborts
if the step counter differs from step 6's prediction. A pre-walk budget or side-table refusal remains an accepted
`dict::` error. The fuzz targets do not have `src/dictionary` on their include path today, so the new
target adds it with `target_include_directories`, as `fuzz_decimal_compare` adds `tests`. Seeds: the hand-written overlays (R-10), the conflict and cap overlays, a DOCTYPE
overlay, the expansion-doubling overlay of R-5 (A-001) in both formats, the empty-component chain
(R-5), and an Orchestra cycle. Registered like `fuzz_dict_xml_loader` / `fuzz_orchestra_loader` (`tests/fuzz/CMakeLists.txt`,
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
  (`bindings/python/tests/wheel/test_import_surface.py`) and the GIL audit table in `fixpp.i`; add an
  in-tree Python test that reads an inbound Length+Data value only the merged dictionary reads intact,
  as the C-ABI live arm does (contracts W-9, W-10). The OO wrapper `fixpp_oo.Dictionary` is not extended (not asked for).

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
  issue); and, if R-10's probe shows it, the `first_field_index` side-table wrap on a base whose side
  table already does not fit, where an overlay is admitted (N-5, Gate A round 2 N-3; with its filed
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
373=2 cases; (5) only if R-10's probe shows it: the `std::uint16_t` `first_field_index` wraps, and `field_count`
clamps, past 65535 side-table entries on a base whose table does not fit (N-5, N-3).
