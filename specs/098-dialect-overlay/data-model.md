# Data model: 098 dialect overlay

The normative grammar, conflict, cap and error tables are in `research.md` R-3 (splice rules per
format), R-4 (conflicts), R-5 (caps and guards) and R-6 (error mapping). This file names the entities
and stores that change, and the records delta.

## §A — Entities

### A.1 Overlay document (input)

| Base format | Root | Version identity (FR-006) | May contain |
|---|---|---|---|
| QuickFIX XML | `<fix>` | `type`, `major`, `minor`, `servicepack` equal to the base's | `<header>`, `<trailer>`, `<messages>`, `<components>`, `<fields>` (any subset, any order) |
| FIX Orchestra | `fixr:repository` | `version` equal to the base's (`FIX.Latest_EP303` for the vendored base) | `fixr:codeSets`, `fixr:fields`, `fixr:components`, `fixr:groups`, `fixr:messages` |

Never loaded on its own; bounded by the R-5 caps and expansion budget. A root child outside the "May
contain" column is refused (`overlay_conflict_error`, research R-3), as is a second container of one
name; `fixr:annotation` / `fixr:documentation` inside a definition are accepted and dropped by the
splice. The splice targets the base's first container of each name and creates it when absent.

### A.2 Merged dictionary (output)

An ordinary `dict::Dictionary` produced by the base format's existing post-parse path over the spliced
document (R-1). Its `which_session_version()` is the base's. It is placed by the user in
`SessionConfig::dictionary` (and, where wanted, `EngineConfig::dictionaries`).

### A.3 Declared header tags (new store, FR-022)

- **Metadata handle:** a sorted, de-duplicated `std::pmr::vector<std::uint16_t>` of the header tags the
  dictionary declares, computed at finalize by an iterative visited-set walk from the header (fields,
  component members, NumInGroup tags and group members; each definition walked once): QuickFIX XML from
  `<header>`, Orchestra from component 1024. It is a set walk, not an `expand_field_list` call, so the
  overlay path's step counter does not charge it (research R-5, R-7). It runs after the message loop and
  skips a reference the loader cannot resolve, adding no tag and throwing nothing, so a base whose
  `<messages>` is empty and whose header names an unknown definition still loads, as `load` does today
  (implement, T056, 2026-11-01). Empty for
  FIX50/FIX50SP1/FIX50SP2, whose `<header>` is empty (research R-7 table).
- **`table_view`:** `header_bits_`, a `std::vector<std::uint64_t>` bit store in the `group_bits_` shape,
  filled by `as_table_view()` through a `table_view_builder` forwarder; public query
  `[[nodiscard]] bool is_header_tag(std::uint16_t tag) const noexcept`. No allocation at query time.
- **The session's fixed set is unchanged in content.** `kSendHeaderTags` and `is_send_header_tag` move
  from `src/session/session.cpp` into the private header `src/session/send_header_tags.hpp`, so W-7 can
  read the production set (research R-7). The session asks `is_header_tag` only after a fixed-set miss.
  A counted Data value keeps following its Length's class.

### A.4 TOML session dictionary (FR-015, FR-016)

| Scope | `dialect_overlay` | Session dictionary |
|---|---|---|
| root | refused, `invalid_or_contradictory_selector`, "dialect_overlay is a session key; set it in [default] or [[session]]" | n/a |
| `[default]` | path, inherited by every session that does not set its own | merged dictionary for that path |
| `[[session]]` | path, overrides `[default]` | merged dictionary for that path |
| neither | — | `engine.dictionaries.front()` (the `[dictionary]` dictionary) |
| any, with `engine.dictionaries` empty (`[dictionary]` absent or failed) | `invalid_or_contradictory_selector` at each origin's key path, "dialect_overlay requires [dictionary]" | none assigned |
| `[default]` or `[[session]]`, non-string value | `malformed_value` at that key path | none assigned |
| `[default]` or `[[session]]`, empty string | `empty_required` at that key path | none assigned |

One load per distinct resolved overlay path, cached by path; the merged dictionary is shared
(`std::shared_ptr<const Dictionary>`) by every session naming it. A failure is reported once per
authoring origin (`default.dialect_overlay`, or `session[N].dialect_overlay` when that session's raw
table sets the key), read from `root_tbl`'s raw tables (research R-9). There is no value meaning "no
overlay", so a `[default]` value cannot be cleared for one session (B-098-3).

Research R-9 gives the remaining rules once: path resolution, the cache key, the per-origin cases, each
diagnostic's location and message, the key inside any other table (not read), a configuration with no
`[[session]]` (not read), and the reach (C++ hosts only). A load that reports any diagnostic returns no
bundle (FR-015).

## §B — C++ surface

| Change | Where |
|---|---|
| `XmlLoader::load_with_overlay`, `XmlLoader::load_with_overlay_from_string` | `include/fixpp/dict/xml_loader.hpp` |
| `OrchestraLoader::load_with_overlay`, `OrchestraLoader::load_with_overlay_from_string` | `include/fixpp/dict/orchestra_loader.hpp` |
| `dict::load_any_with_overlay` | `include/fixpp/dict/load_any.hpp` |
| `dict::overlay_conflict_error`, `dict::overlay_limit_error` (derive from `xml_parse_error`) | `include/fixpp/dict/error.hpp` |
| `table_view::is_header_tag` | `include/fixpp/dict/table_view.hpp` |
| **Removed:** `SessionConfig::dialect_overlay` and the forward declaration of `dict::DialectOverlay` | `include/fixpp/session/session_config.hpp` |
| Comment fixes: `field_data_type::DialectExtension` (`field_ref.hpp`); the deferred notes in `dictionary.hpp` and `xml_loader.hpp` | as named |

Signatures: research R-8. `DialectExtension` stays a reserved enumerator (FR-012 gives its check).
`Dictionary` gains no public member. FR-022's "the dictionary MUST expose that header membership" is met
by `table_view::is_header_tag` on the view `Dictionary::as_table_view()` returns. The header store
itself lives in the metadata handle (`src/dictionary/dictionary_internal.hpp`).

## §C — C ABI and Python

| Item | Value |
|---|---|
| New function | `fixpp_error_t fixpp_dict_load_from_xml_with_overlay(const char* base_path, const char* overlay_path, fixpp_dict_t** out_dict)` |
| Ownership | as `fixpp_dict_load_from_xml`: the caller destroys the handle with `fixpp_dict_destroy`; `fixpp_session_config_set_dictionary` copies it; on every failure `*out_dict` is NULL, including a failure of the handle's own allocation (202); a handle the caller left in `*out_dict` is overwritten, not released (research R-12) |
| Errors | research R-6 |
| Version | C-ABI 1.12 → **1.13** (additive) |
| Pins to update | `include/fix/c_api/version.h` (the 1.13 block and `FIXPP_C_ABI_VERSION_MINOR`), `tests/capi/version_test.cpp`, `tests/abi/golden/fixpp_capi_symbols.txt`, `tools/capi_freeze.sha256` (refreshed by the recipe in `tools/check_capi_freeze.sh`'s header), and through SWIG the import-surface exact set and the GIL audit row (Python row). Derived, not listed: every hit of `git grep -l -e 'include/fix/c_api' -- tools .github/workflows ci tests/abi` is read at the head and either updated or recorded unchanged with its reason. Unchanged: `src/capi/fixpp_capi.map` (it exports `fixpp_*` by glob), `include/fix/c_api/error.h`, `tools/abi_history/error_codes_v1.txt` and `tools/check_capi_occupancy.sh` (no code is minted, research R-6) |
| Python | `fixpp.dict_load_from_xml_with_overlay` (SWIG, automatic); `bindings/python/tests/wheel/test_import_surface.py`; the GIL audit table in `bindings/python/fixpp.i`. The new Python test is in-tree only, with no wheel twin, so the wheel README's allowlist is unchanged (contracts W-10) |

## §D — B&L delta (`spec/behaviors-and-limitations.md`)

| Row | Action | Content |
|---|---|---|
| L-002-2 | retire → closed file | the overlay ships |
| L-422-1 | retire → closed file | FR-022 |
| L-004-1 | retire → closed file, as stale | Iter mode pairs through `dict_hooks` since fixpp#426 (research R-14) |
| L-426-1 | reword | names an overlay's pairs alongside a custom dictionary's |
| L-426-2 | reword | applies to overlays |
| L-426-3 | unchanged | — |
| L-097-1 | unchanged | read: its "The TOML resolver loads one `[dictionary]`" is about `EngineConfig::dictionaries`, which 098 leaves holding the base alone; the per-session merged dictionaries reach `SessionConfig::dictionary` only. §G's recipe names no pattern that reaches it, so this row records the reason |
| B-066-1 + its release note | update | the extension story points at the shipped overlay |
| Release note (098) | new | the filled TOML bundle (a host that overwrites `config.dictionary` discards a session's overlay, B-098-3); the removed `SessionConfig::dialect_overlay` (B-098-4). Migration: stop copying `engine.dictionaries.front()` into `config.dictionary`, because the bundle arrives filled, or copy it only when `config.dictionary` is null; build a `dict_snapshot` from `config.dictionary`, never from the engine's list; replace a `.dialect_overlay` initializer by loading with an overlay (`load_any_with_overlay`) |
| B-098-1 | new | load with overlay: same-format fragment, splice semantics, conflicts refused, caps. Neither `overlay_conflict_error` nor `overlay_limit_error` derives from `orchestra_parse_error`, so a `catch (orchestra_parse_error const&)` around an Orchestra overlay load misses both: catch `xml_parse_error` for all three. The step counter's model-drift refusal is an `overlay_limit_error` and is reported as a limit (201, `ValidatorError` 201, TOML `invalid_or_contradictory_selector`); only its `what()` and the C++ read seam tell it apart (research R-6) |
| B-098-2 | new | Orchestra code sets and groups are shared definitions: an edit reaches every user |
| B-098-3 | new | TOML sessions arrive with `SessionConfig::dictionary` filled. The ways a host's existing code meets the filled bundle: overwriting `config.dictionary` with `engine.dictionaries.front()` silently discards that session's overlay; building `SessionConfig::dict_snapshot` from the engine's base dictionary makes `Session::open()` fail with `invalid_session_config` on every overlay session, because `open()` refuses a snapshot whose `source()` is not `cfg_.dictionary`. A `[default]` `dialect_overlay` cannot be cleared for one session (no "none" value; QuickFIX's `[DEFAULT]` behaves the same). The key reaches C++ hosts only: C and Python hosts use `fixpp_dict_load_from_xml_with_overlay`. Each distinct overlay re-reads the base file (N overlays, N + 1 base parses), and two spellings `resolve_path` keeps apart give two loads (research R-9) |
| B-098-4 | new | `SessionConfig::dialect_overlay` removed (pre-release breaking, public C++ API; no C-ABI effect, since the C ABI never exposed it). What a consumer sees: a designated initializer naming `.dialect_overlay` stops compiling; `SessionConfig` is an aggregate, so a positional initializer that runs past `dictionary` binds each later value one member earlier (a `{}` or `nullptr` there compiles silently); code naming `fixpp::dict::DialectOverlay` stops compiling |
| B-098-5 | new | send-side header set = fixed set ∪ the dictionary's declared header tags; byte-identical for every vendored dictionary. `table_view` and the metadata handle each gain the header store, a C++ layout change that needs a consumer rebuild (no C-ABI effect: `fixpp_dict_t` is opaque). No size or trait pin exists: re-derive with `git grep -n -e 'sizeof(.*table_view)' -e 'is_trivially[a-z_]*<.*table_view' -- include src tests`, run at the head (`bench/dictionary/table_view_footprint_bench.cpp` measures `sizeof`; it is not a pin) |
| B-098-6 | new | C-ABI 1.13: `fixpp_dict_load_from_xml_with_overlay`; first producer of `FIXPP_ERR_DICT_LIMIT_EXCEEDED`. Differs from its sibling: `fixpp_dict_load_from_xml` maps every failure to `FIXPP_ERR_CAPI_CONFIG_INVALID` (10, `CapiError` in Python); the new function returns 200, 201 or 202 (`ValidatorError` in Python, told apart by `.code`; 202 is not a `MemoryError`). It returns a code only: `fixpp_strerror` gives the code's fixed text, and the element or cap FR-007 and FR-009 name is in the C++ `what()` and the TOML message, not at the C ABI or in Python. Not offered: `unresolved_group_policy`, the string form, a memory resource (research R-12). Python: the module function only; `fixpp_oo.Dictionary` is not extended |
| B-098-7 | new | an overlay enum value on a base field with no values closes that field's domain to the listed values, as a hand edit does; example: a code added to MsgType(35) on a FIXT11 base makes validation refuse every standard MsgType |
| L-098-1 | new | the engine registry holds base dictionaries; row 6 (fixpp#643) must keep the session's own dictionary for its own version, and must not replace a session's `SessionConfig::dictionary` with a registry dictionary for that version, or the overlay is lost. Through TOML the one `[dictionary]` file is the engine's and every session's base, so a FIXT.1.1 TOML session can overlay only that file (FIXT11.xml or the Orchestra FIX Latest file, whichever it names) |
| L-098-2 | new | the typed (codegen) tier reflects the base dictionary only; overlay fields are read and written by tag |
| L-098-3 | new | the base load paths have no caps or cycle guard (issue filed, R-15). That includes a base named through the C ABI's `base_path` or TOML's `[dictionary] path`, which the host or operator chooses; on the overlay path the cycle check and the budget cover the base's graph (research R-5), but the byte, depth and entry caps do not apply to the base |
| L-098-4 | new, only if research R-10's probe shows the wrap | `ComponentRef` / `GroupRef::first_field_index` is `std::uint16_t` and `field_count` clamps at 65535, so on a base whose side table already passes that, `component_fields` / `group_fields` return another definition's fields, with or without an overlay; an overlay that would make a fitting table stop fitting is refused (issue filed, R-15) |

## §E — Catalogue and coverage index

- `spec/feature-catalogue.md`: D-009 and COM-011 → `done`, feature `098-dialect-overlay`, the PR, the
  witnesses (contracts). D-009's Spec ref becomes `[impl] implementation`. Both rows' `Applies to` cell
  widens from `4.0–5.0SP2` to `4.0–5.0SP2, FIXT.1.1, FIX Latest`; no new row.
- `spec/coverage-index.md`: a D-009 supplemental note in the D-007 form (a design choice, not a FIX spec
  section; cites `[FIX44 Vol1] User Defined Fields` for the tag ranges; notes that FIX Latest goes
  through the Orchestra path), and COM-011's row.

## §F — Design-doc supersession

Every hit of §G's recipe gets an "Updated (098-dialect-overlay)" note in place, naming what 098 shipped
instead, or a verify-record line saying why the hit is history or still true. The 2c sections known at
plan time are a floor, not the set: §1.2 (merge-cost line), §2 (the Non-goals bullet "No mid-session
overlay grammar extension"), §4.1 (the `FieldRef` listing comment naming
`dict_overlay_unsupported_length_pair`), §4.3 (`with_overlay` /
`was_dialect_promoted`), §4.4 (the `DialectOverlay` value type), §4.4.1 (the restricted grammar), §4.4.2
(the caps: the values kept, the `FIXPP_DICT_*` knobs not), §6.4 (merge contract, conflict policy).
`brain/components/dictionary.md` gains a flagged entry for each 2c note.

## §G — Records inventory recipe (§D–§F)

The inventory is derived, not listed, so that it cannot go stale between plan and records (Gate A round
1, Root cause #4). Re-run both parts at the records phase, from the library root, and disposition every
hit (§F). The verify record keeps the commands and the disposition of each hit, not a count.

1. **Text that names the superseded design or a record 098 changes:**

   ```
   git grep -n -i -e 'DialectOverlay' -e 'dialect.overlay' -e 'with_overlay' -e 'load_overlay' \
       -e 'was_dialect_promoted' -e 'dict_overlay_' -e 'L-422-1' -e 'L-002-2' -e 'D-009' -e 'COM-011' \
       -- '*.md' ':!specs/' ':!CLAUDE-history.md'
   git grep -n -i -w 'overlay' -- .specify/2c-codegen.md
   ```

   `dialect.overlay` matches the prose spellings ("dialect overlay", "dialect-overlay") as well as the
   key; `dict_overlay_` matches 2c's error names. The second command lists every `overlay` line of 2c,
   because 2c also states the old grammar in lines none of the first patterns reach; its hits are
   dispositioned by the heading they sit under. Hits in a document's own history (a changelog row, a superseded draft such as
   `.specify/2c-codegen.draft-r1.md`) are dispositioned as history with that reason.
2. **Brain pages for the code 098 changes:** every `brain/components/*.md` whose front-matter `refs` list
   names a file in `git diff --name-only origin/main...HEAD`. Read each for a statement 098 makes false
   (for example a "not taken" or "deferred" claim, or a C-ABI history that ends before 1.13), and fix it
   in place.

**Proving the recipe can report.** Before trusting a clean disposition, check that part 1 reports each
of the hits Gate A rounds 1 and 2 named, as known positives: 2c §1's goal 4, §2's Non-goals bullet,
§4.1's `FieldRef` comment, §4.3, §7.1, the D-009 rows, `.specify/2d-threading.md`'s `SessionConfig`
listing and `.specify/2m-pybind.md`'s Python key. A known positive the recipe does not report means a
pattern is missing; widen the recipe, never the list. Run part 2 against a diff that names a file in a
brain page's `refs`, and see it report that page. W-13's presence check for each new note has its own seeded
negative (contracts).
