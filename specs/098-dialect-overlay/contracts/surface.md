# Contract: 098 public surface

Behaviour is defined by the spec FRs and `research.md` R-3..R-9; this file fixes the shapes.

## C++ (`fixpp::dict`)

```cpp
// include/fixpp/dict/xml_loader.hpp — QuickFIX-XML base, <fix> overlay
class XmlLoader {
public:
    // ... load, load_from_string unchanged ...

    // Parses `base` and `overlay`, refuses an overlay that breaks research R-4/R-5, splices it into
    // the base document (R-3) and builds the Dictionary from the spliced document exactly as `load`
    // builds one from a file. Throws: xml_parse_error (base or overlay malformed, unknown reference,
    // cycle), unknown_version_error (base), overlay_conflict_error, overlay_limit_error, xml_oom_error.
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
// overlay schema errors throw orchestra_parse_error (a subclass of xml_parse_error), as `load` does.
//
// xml_oom_error derives from std::bad_alloc, not from xml_parse_error, so the C ABI maps it by its own
// catch branch (FIXPP_ERR_DICT_OOM) ahead of the xml_parse_error branches.

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
The returned `Dictionary` is independent of the base `Dictionary` any other call returned (FR-008).

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
 *  @return FIXPP_ERR_DICT_LIMIT_EXCEEDED  the overlay exceeds a cap; *out_dict = NULL
 *  @return FIXPP_ERR_DICT_OOM             allocation failed; *out_dict = NULL
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
returned bundle has `config.dictionary` set (data-model §A.4).
