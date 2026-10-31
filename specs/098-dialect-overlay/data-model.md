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

Never loaded on its own; bounded by the R-5 caps.

### A.2 Merged dictionary (output)

An ordinary `dict::Dictionary` produced by the base format's existing post-parse path over the spliced
document (R-1). Its `which_session_version()` is the base's. It is placed by the user in
`SessionConfig::dictionary` (and, where wanted, `EngineConfig::dictionaries`).

### A.3 Declared header tags (new store, FR-022)

- **Metadata handle:** a sorted, de-duplicated `std::pmr::vector<std::uint16_t>` of the header tags the
  dictionary declares, computed at finalize: QuickFIX XML from the `<header>` expansion (fields,
  component members, NumInGroup tags and group members); Orchestra from component 1024's expansion by
  the same rule. Empty for FIX50/FIX50SP1/FIX50SP2, whose `<header>` is empty (research R-7 table).
- **`table_view`:** `header_bits_`, a `std::vector<std::uint64_t>` bit store in the `group_bits_` shape,
  filled by `as_table_view()` through a `table_view_builder` forwarder; public query
  `[[nodiscard]] bool is_header_tag(std::uint16_t tag) const noexcept`. No allocation at query time.
- **The session's fixed set is unchanged** and stays in `src/session/session.cpp`; the session asks
  `is_header_tag` only after a fixed-set miss.

### A.4 TOML session dictionary (FR-015, FR-016)

| Scope | `dialect_overlay` | Session dictionary |
|---|---|---|
| root | refused, `invalid_or_contradictory_selector`, "dialect_overlay is a session key; set it in [default] or [[session]]" | n/a |
| `[default]` | path, inherited by every session that does not set its own | merged dictionary for that path |
| `[[session]]` | path, overrides `[default]` | merged dictionary for that path |
| neither | — | `engine.dictionaries.front()` (the `[dictionary]` dictionary) |

One merged dictionary per distinct resolved overlay path, shared (`std::shared_ptr<const Dictionary>`)
by every session naming it.

## §B — C++ surface

| Change | Where |
|---|---|
| `XmlLoader::load_with_overlay`, `XmlLoader::load_with_overlay_from_string` | `include/fixpp/dict/xml_loader.hpp` |
| `OrchestraLoader::load_with_overlay`, `OrchestraLoader::load_with_overlay_from_string` | `include/fixpp/dict/orchestra_loader.hpp` |
| `dict::load_any_with_overlay` | `include/fixpp/dict/load_any.hpp` |
| `dict::overlay_conflict_error`, `dict::overlay_limit_error` (derive from `xml_parse_error`) | `include/fixpp/dict/error.hpp` |
| `table_view::is_header_tag` | `include/fixpp/dict/table_view.hpp` |
| **Removed:** `SessionConfig::dialect_overlay` and the forward declaration of `dict::DialectOverlay` | `include/fixpp/session/session_config.hpp` |
| Comment fixes: `FieldType::DialectExtension`; the deferred notes in `dictionary.hpp` and `xml_loader.hpp` | as named |

Signatures: research R-8. `DialectExtension` stays a reserved enumerator.

## §C — C ABI and Python

| Item | Value |
|---|---|
| New function | `fixpp_error_t fixpp_dict_load_from_xml_with_overlay(const char* base_path, const char* overlay_path, fixpp_dict_t** out_dict)` |
| Ownership | as `fixpp_dict_load_from_xml`: the caller destroys the handle with `fixpp_dict_destroy` |
| Errors | research R-6 |
| Version | C-ABI 1.12 → **1.13** (additive) |
| Pins to update | `include/fix/c_api/version.h`, `tests/capi/version_test.cpp`, `tests/abi/golden/fixpp_capi_symbols.txt`, `tools/capi_freeze.sha256` |
| Python | `fixpp.dict_load_from_xml_with_overlay` (SWIG, automatic); `bindings/python/tests/wheel/test_import_surface.py`; the GIL audit table in `bindings/python/fixpp.i` |

## §D — B&L delta (`spec/behaviors-and-limitations.md`)

| Row | Action | Content |
|---|---|---|
| L-002-2 | retire → closed file | the overlay ships |
| L-422-1 | retire → closed file | FR-022 |
| L-004-1 | retire → closed file, as stale | Iter mode pairs through `dict_hooks` since fixpp#426 (research R-14) |
| L-426-1 | reword | names an overlay's pairs alongside a custom dictionary's |
| L-426-2 | reword | applies to overlays |
| L-426-3 | unchanged | — |
| B-066-1 + its release note | update | the extension story points at the shipped overlay |
| B-098-1 | new | load with overlay: same-format fragment, splice semantics, conflicts refused, caps |
| B-098-2 | new | Orchestra code sets and groups are shared definitions: an edit reaches every user |
| B-098-3 | new | TOML sessions arrive with `SessionConfig::dictionary` filled; a host that overwrites it discards the overlay |
| B-098-4 | new | `SessionConfig::dialect_overlay` removed (pre-release breaking, public C++ API) |
| B-098-5 | new | send-side header set = fixed set ∪ the dictionary's declared header tags; byte-identical for every vendored dictionary |
| B-098-6 | new | C-ABI 1.13: `fixpp_dict_load_from_xml_with_overlay`; first producer of `FIXPP_ERR_DICT_LIMIT_EXCEEDED` |
| L-098-1 | new | the engine registry holds base dictionaries; row 6 (fixpp#643) must keep the session's own dictionary for its own version |
| L-098-2 | new | the typed (codegen) tier reflects the base dictionary only; overlay fields are read and written by tag |
| L-098-3 | new | the base load paths have no caps or cycle guard (issue filed, R-15) |

## §E — Catalogue and coverage index

- `spec/feature-catalogue.md`: D-009 and COM-011 → `done`, feature `098-dialect-overlay`, the PR, the
  witnesses (contracts). D-009's Spec ref becomes `[impl] implementation`.
- `spec/coverage-index.md`: a D-009 supplemental note in the D-007 form (a design choice, not a FIX spec
  section; cites `[FIX44 Vol1] User Defined Fields` for the tag ranges), and COM-011's row.

## §F — Design-doc supersession

`.specify/2c-codegen.md`: an "Updated (098-dialect-overlay)" note in place at §1.2 (merge-cost line),
§4.3 (`with_overlay` / `was_dialect_promoted`), §4.4 (the `DialectOverlay` value type), §4.4.1 (the
restricted grammar), §4.4.2 (the caps: the values kept, the `FIXPP_DICT_*` knobs not), §6.4 (merge
contract, conflict policy). Each note names what 098 shipped instead. `brain/components/dictionary.md`
gains a flagged entry for each.
