# Research: 098 dialect overlay

Every fact below was read at `8319c206` (`origin/main`). File:line citations are leads for the
implementer and will drift; re-derive with the `grep` given where one is given.

## R-1 — Merge mechanism: splice the overlay into the parsed base document

**Decision.** Each loader parses the base into a pugixml document, parses the overlay into a second
document, checks the overlay against the base (R-4, R-5), splices the overlay's elements into the base
document (R-3), and then runs the loader's existing post-parse path unchanged. For QuickFIX XML that path
is `build_handle_from_doc(doc, mr, policy)` in `src/dictionary/xml_loader.cpp` (`LoaderState::parse_document`
then `finalize`); for Orchestra it is the function of the same name in `src/dictionary/orchestra_loader.cpp`.

**Rationale.**
- **Equivalence by construction.** After the splice, the document *is* the hand-edited file, so every
  downstream rule (field interning, header/trailer fold, Length+Data detection, group registration,
  delimiter capture, the 072 collision guard, the FR-023 completeness sweep, enum runs, `as_table_view`)
  runs exactly as it would on the edited file. FR-002 holds for every store at once, including the
  first-seen and declaration-order rules the loaders apply (component indices, `parent_of_`, enum
  order), provided the splice places each addition where a hand edit would (appended to its
  container, R-3).
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
  components, groups or code sets.
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
rule, recursively. Appending puts each new child after the existing ones, where a hand edit adding a line
at the end of the element would put it. The hand-edited fixtures (R-10) place additions at the same
position.

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
put one tag twice in the message. The overlay must extend the component instead.

**Orchestra** (`fixr:repository` overlay):

| Overlay element | Match key | On match | New |
|---|---|---|---|
| `fixr:codeSet` | `id` | `name` and `type` MUST equal; listed `fixr:code`s are added by `value` (description/name MUST equal on match) | appended to `fixr:codeSets` |
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
So the merge MUST detect, before the splice, and refuse with `overlay_conflict_error` naming the element:

- **Overlay against base:** a matched definition whose identifying attributes differ (table above); a
  field name already used by another number; a code/enum value redescribed; `required="N"` (XML) or
  `presence` other than `required` (Orchestra) on a child the base marks required; a child redeclared
  directly that the parent already reaches through a component (R-3).
- **Overlay against itself:** two overlay elements with one match key in one container; two new fields
  with one name.
- **Root:** a version mismatch (FR-006); an overlay root of the other format (R-2).
- **Required rule.** An absent `required` / `presence` attribute on a matched child means "no change".
  `required="Y"` / `presence="required"` on an Optional child promotes it. The overlay cannot relax a
  rule.

References to undefined fields or components, unknown types and malformed attributes are left to the
loader's own checks after the splice, which already throw `xml_parse_error` for them, so their messages
match those of a hand-edited file.

## R-5 — Hardening the overlay input (FR-009, FR-010)

| Guard | Rule | Where |
|---|---|---|
| Byte cap | **4 MiB**. Read at most cap + 1 bytes into a buffer (path form) or check `size()` (string form); refuse when more were available. Never trust a `stat` size (a FIFO or `/proc` file reports a wrong size) | before parse |
| DOCTYPE | parse with `pugi::parse_default \| pugi::parse_doctype` and refuse any `node_doctype` child of the document. pugixml 1.15 never loads external entities and expands only the five predefined entities and character references (its `parse_default` discards a DOCTYPE without a node, which is why the flag is set: to see it) | after parse |
| Depth | **32** element levels; checked by an iterative (explicit stack) walk of the overlay document | after parse |
| Entry caps | new field definitions **1024**; enum values / codes added **4096**; components **64**; messages **64**; Orchestra groups **64**; Orchestra code sets **256** | during the merge walk |
| Cycles | after the splice, an iterative depth-first walk of the **merged** reference graph: XML `component` → `component` (directly or through `<group>`); Orchestra `component`/`group` → `componentRef`/`groupRef` ids. A back edge refuses with `xml_parse_error` naming the cycle. The walk runs over the whole merged graph, so a cycle that closes through base definitions is caught (spec US2 AS-4) | after splice, before `build_handle_from_doc` |

The values are 2c §4.4.2's, plus the two Orchestra containers 2c did not know about and an enum-value cap
the byte cap would otherwise bound only loosely. Fixed constants in `src/dictionary/`, no configuration
knob (2c's `FIXPP_DICT_*` names were never implemented). A cap failure throws `overlay_limit_error`.

The base path is not hardened (spec FR-011): the caps apply to the overlay document, the cycle check to
the merged graph (which covers the base's own graph only on the overlay path).

## R-6 — Errors

New in `include/fixpp/dict/error.hpp`, both deriving from `xml_parse_error` so that every existing
`catch (xml_parse_error const&)` and `trap_throw_to_expected` site keeps working:
- `overlay_conflict_error` — R-4; message names the element and key.
- `overlay_limit_error` — R-5 caps; message names the cap and its value.

| C++ | C ABI (new function) | TOML `reason_class` |
|---|---|---|
| `overlay_limit_error` | `FIXPP_ERR_DICT_LIMIT_EXCEEDED` (201; its first producer) | `invalid_or_contradictory_selector` |
| `xml_oom_error` | `FIXPP_ERR_DICT_OOM` (202) | same |
| any other `xml_parse_error` / `unknown_version_error` / `std::exception` | `FIXPP_ERR_DICT_CONFIG` (200) | same |
| null argument | `FIXPP_ERR_NULL_HANDLE` | n/a |

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
  session key; set it in [default] or [[session]]". `T020_DeferredSetExactCoverage` keeps guarding the
  other twelve deferred keys.
- A new resolver step runs after `resolve_engine_dictionary` in `resolve_selectors`
  (`src/config/selector_resolver.cpp`): for each session `i`, read `dialect_overlay` from the merged
  session table; with none, set `bundle.sessions[i].config.dictionary = bundle.engine.dictionaries.front()`;
  with one, `resolve_path(base_dir, …)` and load `load_any_with_overlay(base path, overlay path,
  opts.resource)`. One merged dictionary per distinct resolved overlay path, shared by the sessions
  naming it (a lookup by path; sessions do not each re-parse the base). Skip the step when the
  `[dictionary]` step failed. `EngineConfig::dictionaries` is unchanged.
- Diagnostics: the key path where the value was written. `merge_defaults` loses origins, so the step
  compares the session's raw table: a session that sets the key reports `session[N].dialect_overlay`; a
  value inherited from `[default]` reports `default.dialect_overlay`, once per distinct failing path.
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
`table_view`s and returns a list of differences:

- **Keys from the union of both sides**, so an extra message, field, component or group on either side is
  reported, not hidden.
- **Dictionary API**: `messages()`, per message `message_fields` / `field_ref` / `required_fields`,
  `field` / `field_by_name` / `field_name` for every tag on either side, `enum_values`, `component` /
  `component_fields`, `group` / `group_fields` / `group_first_field`, `length_pair_data_tag`,
  `which_session_version`.
- **table_view**: `valid_tags_for` and `required` per message, field types, `enum_valid` for every listed
  value plus one value listed on neither side, `has_nonstandard_pair` and both pair directions, the new
  `is_header_tag`, and the context group stores (`group_first_field`, `group_member_tags`,
  `group_required_members`) for every (MsgType, parent path, NumInGroup) key — keys derived by walking
  both dictionaries' group structure, since `group_ctx_` cannot be enumerated.
- **One seeded mismatch per store category** the helper claims (a field type, a valid tag, a required
  tag, an enum value, a component member, a group member, a group delimiter, a context-store member, a
  Length+Data pair, a header tag, a message), each shown to produce a reported difference before
  the clean comparisons are trusted.

**References.** Hand-written complete files are practical only for small bases; the vendored files are
315 KB (FIX44), 1.5 MB (FIX50SP2) and 7.5 MB (Orchestra). So:
- **Kind coverage:** small hand-written bases, one QuickFIX-XML (`FIX.4.4` version attributes) and one
  Orchestra (`FIX.Latest_EP303`, with components 1024/1025), each with a hand-written overlay and a
  hand-written complete edited copy. Every FR-003/FR-004 addition kind appears, and every R-4 conflict
  has a refusing overlay.
- **Real bases:** FIX44, FIX50SP2 and the Orchestra file, each with an overlay and a hand-written
  **insertion patch**: a list of (anchor line, lines to insert after it) applied by a small C++ test
  helper with line-exact matching, no `patch` binary (runs on MSVC). Every anchor MUST match exactly
  once or the test fails; a deliberately non-applying patch must turn the cell RED. The patch is written
  by hand from the base file, never derived from the merge.

## R-11 — Fuzzing (`[const §VII.7]`)

The overlay loader is new parser-touching code over untrusted XML. Add
`tests/fuzz/fuzz_dict_overlay.cpp`: a fixed small base (one per format, chosen by the first input byte),
the rest of the input as the overlay, both C++ entry points; every throw must be one of the `dict::`
error types. Seeds: the hand-written overlays (R-10), the conflict and cap overlays, and a DOCTYPE
overlay. Registered like `fuzz_dict_xml_loader` / `fuzz_orchestra_loader` (`tests/fuzz/CMakeLists.txt`,
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
  breaking, C++ API); the send-side header union; C-ABI 1.13.
- **New `L-098-*`:** the FIXT registry holds base dictionaries only, so row 6 (fixpp#643) must keep the
  session's own dictionary for its own version (FR-021); the typed (codegen) tier reflects the base only;
  the base `XmlLoader::load` / `OrchestraLoader::load` paths have no caps or cycle guard (with the filed
  issue).
- **Catalogue:** D-009 and COM-011 `done`. D-009's Spec ref `[FIX50SP2 §3.2]` is borrowed from D-010 and
  is replaced by `[impl] implementation` with a coverage-index supplemental note, as D-007 has (FIX
  defines user-defined tag ranges, not a file format or a merge).
- **Design doc:** `.specify/2c-codegen.md` §1.2, §4.3, §4.4, §4.4.1, §4.4.2, §6.4 get in-place "Updated
  (098-dialect-overlay)" notes; `brain/components/dictionary.md` flags them.
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
373=2 cases.
