# Contract: 098 public surface

Behaviour is defined by the spec FRs and `research.md` R-3..R-9; this file fixes the shapes.

## C++ (`fixpp::dict`)

```cpp
// include/fixpp/dict/xml_loader.hpp — QuickFIX-XML base, <fix> overlay
class XmlLoader {
public:
    // ... load, load_from_string unchanged ...

    // Reads and parses `base` exactly as `load` does: no overlay byte cap and `load`'s parse flags.
    // Only the parse-status mapping is shared with the overlay parse. Parses `overlay` with
    // parse_default | parse_doctype, then runs research R-5's guards in order (shape caps, then one
    // iterative walk of the merged reference graph for cycles, the expansion budget and side-table
    // fit, then the R-4 conflict checks), splices the overlay into the base document (R-3), checks
    // the spliced document's Length+Data relation against the base's (R-5 step 8a) and builds the
    // Dictionary from the spliced document as `load` builds one from a file, with the expansion
    // step counter armed. Throws: xml_parse_error (base or overlay malformed, unknown reference,
    // cycle), unknown_version_error (base), overlay_conflict_error, overlay_limit_error (a cap, the
    // expansion budget, the step counter, or a side table that would stop fitting 16 bits). A step-
    // counter refusal keeps this public type but has a distinct internal origin and model-drift
    // message exposed by R-5's test read seam; it is not a budget refusal. Also throws xml_oom_error
    // (pugixml's status_out_of_memory on the base or the overlay parse).
    [[nodiscard]] Dictionary load_with_overlay(
        std::filesystem::path const& base, std::filesystem::path const& overlay,
        std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed);
    [[nodiscard]] Dictionary load_with_overlay_from_string(
        std::string_view base_xml, std::string_view overlay_xml, std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed);
};

// include/fixpp/dict/orchestra_loader.hpp — Orchestra base, fixr:repository overlay
// OrchestraLoader::load_with_overlay / load_with_overlay_from_string: the same two members; base and
// overlay schema errors, a cycle included, throw orchestra_parse_error (a subclass of
// xml_parse_error), as `load` does. A Length a new Data's lengthId would give a second partner is
// overlay_conflict_error, not the loader's "already paired" orchestra_parse_error (research R-3).
//
// xml_oom_error derives from std::bad_alloc, not from xml_parse_error. The C ABI catches, in order:
// overlay_limit_error, then std::bad_alloc (which covers xml_oom_error and every bare bad_alloc the
// thunk itself can raise), then std::exception, then `...` (research R-6).

// include/fixpp/dict/load_any.hpp
// Sniffs the base root as load_any does; the overlay root MUST be the same format, else
// overlay_conflict_error. Dispatches to the matching loader's load_with_overlay.
[[nodiscard]] Dictionary load_any_with_overlay(
    std::filesystem::path const& base, std::filesystem::path const& overlay,
    std::pmr::memory_resource* mr,
    unresolved_group_policy policy = unresolved_group_policy::fail_closed);

// include/fixpp/dict/error.hpp
class overlay_conflict_error : public xml_parse_error { using xml_parse_error::xml_parse_error; };
class overlay_limit_error    : public xml_parse_error { using xml_parse_error::xml_parse_error; };

// include/fixpp/dict/table_view.hpp
// True when the dictionary declares `tag` in its header (QuickFIX <header> expansion, Orchestra
// component 1024 expansion). No allocation.
[[nodiscard]] bool table_view::is_header_tag(std::uint16_t tag) const noexcept;
```

Removed from `include/fixpp/session/session_config.hpp`: `class DialectOverlay;` (forward declaration)
and `std::shared_ptr<const fixpp::dict::DialectOverlay> dialect_overlay;`.

Preconditions as `load`: `mr != nullptr`. Thread safety as `load`: stateless loaders, no shared state.
No `Dictionary` is an input to these calls (they take paths or strings), so a load with an overlay
cannot change any other `Dictionary` (FR-008, structural).

Overlay attribute vocabulary: `required` is `Y`, `N` or absent (QuickFIX XML); `presence` is `required`,
`optional` or absent (Orchestra); anything else is `overlay_conflict_error` (research R-3).

## C ABI (`include/fix/c_api/dict.h`, C-ABI 1.13)

```c
/** fixpp_dict_load_from_xml_with_overlay — load a base dictionary with one dialect overlay.
 *
 *  Wraps `fixpp::dict::load_any_with_overlay(base_path, overlay_path,
 *  std::pmr::new_delete_resource())`. The overlay is in the base's format (QuickFIX XML or FIX
 *  Orchestra).
 *
 *  THUNK: construction-time; never on a hot path.
 *  Reentrancy: single-thread.
 *
 *  @return FIXPP_ERR_OK                   *out_dict owns a new handle (destroy with fixpp_dict_destroy)
 *  @return FIXPP_ERR_NULL_HANDLE          a NULL argument; *out_dict = NULL when out_dict is not NULL
 *  @return FIXPP_ERR_DICT_LIMIT_EXCEEDED  the overlay exceeds a cap or the expansion budget, or
 *                                         would push a side table past 16 bits; *out_dict = NULL
 *  @return FIXPP_ERR_DICT_OOM             allocation failed anywhere in the call, including the
 *                                         handle's own allocation; *out_dict = NULL
 *  @return FIXPP_ERR_DICT_CONFIG          any other load or merge failure; *out_dict = NULL
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_dict_load_from_xml_with_overlay(
    const char* base_path, const char* overlay_path, fixpp_dict_t** out_dict);
```

`version.h`: a `1.13` block ("Added: fixpp_dict_load_from_xml_with_overlay (header).") and
`FIXPP_C_ABI_VERSION_MINOR 13`. Python: `fixpp.dict_load_from_xml_with_overlay(base_path, overlay_path)`
returning a dictionary proxy, raising the typed error for a non-OK code (the existing
`%FIXPP_HANDLE_OUT(fixpp_dict_t)` typemap).

## TOML

```toml
[dictionary]
kind = "path"
path = "dictionaries/FIX44.xml"

[default]
dialect_overlay = "venue-common.xml"      # inherited by every session that sets none

[[session]]
sender_comp_id = "A"                      # uses venue-common.xml

[[session]]
sender_comp_id = "B"
dialect_overlay = "venue-b.xml"           # overrides [default]
```

Relative paths resolve against the configuration file's directory (`resolve_path`, as
`dictionary.path`). Root-scope `dialect_overlay` is refused (data-model §A.4). Every session in the
returned bundle has `config.dictionary` set when the load succeeds (data-model §A.4).

- A non-string value is `malformed_value`; an empty string is `empty_required`; a `dialect_overlay`
  with no usable `[dictionary]` is `invalid_or_contradictory_selector`, "dialect_overlay requires
  [dictionary]". Each is reported at the key path where the value was written.
- A failing overlay is reported once per authoring origin: `[default]` and `session[1]` naming one
  failing file give two diagnostics.
- There is no value meaning "no overlay": a `[default]` `dialect_overlay` cannot be cleared for one
  session (B-098-3). Move the key into the sessions that want it.
