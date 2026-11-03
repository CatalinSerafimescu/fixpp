// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/xml_loader.hpp
//
// `fixpp::dict::XmlLoader` — stateless loader for QuickFIX-XML-format FIX
// data-dictionary files. Canonical declaration; mirrors the `[2c §4.5]`
// extract recorded at
// `specs/002-dictionary-xml-loader/contracts/xml_loader.hpp` and the
// data-model.md Entity 5 entry.
//
// Scope of this PR (per spec.md /clarify Session 2026-05-14):
//   - Q1 → B: ships XML data for FIX 4.2 / 4.4 / 5.0 SP2 / FIXT.1.1; the
//     loader code path accepts all nine v1.0-supported versions per AC-L4.
//   - Q2 → A: overlays. The calls are `load_with_overlay` /
//     `load_with_overlay_from_string` (below), `OrchestraLoader`'s pair and
//     `dict::load_any_with_overlay` (098-dialect-overlay).
//   - Q3 → A: third-party XML parser selection is **pugixml** per
//     research.md D-1; the parser is consumed only from
//     `src/dictionary/xml_loader.cpp` and is **not** transitively visible
//     from this public header (research.md D-15).

#pragma once

#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
// NOLINTNEXTLINE(misc-include-cleaner)
#include <fixpp/dict/error.hpp>  // re-exported: load*() throws dict::xml_*_error
#include <fixpp/dict/loader_policy.hpp>
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

class XmlLoader {
public:
    // Construction-time exception allowed per `[arch §5.3]`. The hot path
    // (parse / serialize / validate) is exception-free; `XmlLoader` runs
    // only at engine init / session open. PMR allocations within `load*`
    // are wrapped in `fixpp::core::detail::trap_throw_or_throw<xml_oom_error>`
    // (research.md D-3) and translated to `dict::xml_oom_error` on PMR
    // failure (`[2c §6.1.1]`).
    XmlLoader() noexcept = default;

    // Load a QuickFIX-XML-format dictionary from a file path. Allocates from
    // `mr`; the returned `Dictionary`'s metadata handle is heap-pinned
    // (`[2c §4.3]`) and PMR-allocates copies of the loaded tables on `mr`.
    //
    // Throws `dict::xml_parse_error` (derives from `std::runtime_error`) on
    // malformed XML; throws `dict::unknown_version_error` on a version
    // string outside the v1.0 supported set; PMR allocation failure is
    // translated to a thrown `dict::xml_oom_error`. Otherwise returns the
    // loaded `Dictionary` by value.
    //
    // Preconditions:
    //   - `mr != nullptr` (caller precondition violation, NOT a runtime
    //     error; debug-asserts on null, undefined in release per
    //     research.md D-5).
    //
    // ACs: AC-L1 / AC-L2..L8 / AC-L9 / AC-P1 / AC-P2.
    // 083 FR-006a / C-6.6: `policy` is a DEFAULTED trailing parameter, never a
    // new required argument — every existing source caller compiles unchanged.
    // Defaults to fail-closed; see `unresolved_group_policy`.
    [[nodiscard]] Dictionary load(
        std::filesystem::path const& xml_path, std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed);

    // In-process equivalent of `load(path, mr)`. Same exception discipline.
    // Drives the AC-L3..L8 negative-path test suite without on-disk
    // fixtures (spec.md §3.2 user story 2).
    //
    // ACs: AC-L10 (positive path equivalence) + AC-L3..L8 negative paths.
    [[nodiscard]] Dictionary load_from_string(
        std::string_view xml_text, std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed);

    // 098-dialect-overlay (contracts/surface.md, research R-1..R-6).
    // Reads and parses `base` exactly as `load` does: no overlay byte cap and `load`'s parse flags.
    // Only the parse-status mapping is shared with the overlay parse. Parses `overlay` with
    // parse_default | parse_doctype, then runs research R-5's guards in order (shape caps, then one
    // iterative walk of the merged reference graph for cycles, the expansion budget and side-table
    // fit, then the R-4 conflict checks), splices the overlay into the base document (R-3), checks
    // the spliced document's Length+Data relation against the base's (R-5 step 8a) and builds the
    // Dictionary from the spliced document as `load` builds one from a file, with the expansion
    // step counter armed. Throws: xml_parse_error (base or overlay malformed, unknown reference,
    // cycle), unknown_version_error (base), overlay_conflict_error, overlay_limit_error (a cap, a
    // DOCTYPE, the expansion budget, the step counter, or a side table that would stop fitting 16
    // bits). A step-counter refusal keeps this public type but has a distinct internal origin and
    // model-drift message exposed by R-5's test read seam; it is not a budget refusal. Also throws
    // xml_oom_error (pugixml's status_out_of_memory on the base or the overlay parse). A base or
    // overlay path that cannot be opened or read (missing, unreadable, a directory) throws
    // xml_parse_error, as `load` does for its path (research R-6). `policy` is applied to the build
    // of the spliced document as `load` applies it to a file; step 6's counts do not depend on it
    // (research R-5). The string form keeps no view into `base_xml` or `overlay_xml` after it
    // returns, as `load_from_string` keeps none. Preconditions as `load`: `mr != nullptr`. Thread
    // safety as `load`.
    [[nodiscard]] Dictionary load_with_overlay(
        std::filesystem::path const& base, std::filesystem::path const& overlay,
        std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed);
    [[nodiscard]] Dictionary load_with_overlay_from_string(
        std::string_view base_xml, std::string_view overlay_xml, std::pmr::memory_resource* mr,
        unresolved_group_policy policy = unresolved_group_policy::fail_closed);

    // The overlay calls are the two `load_with_overlay` members above
    // (098-dialect-overlay); the `load_overlay` names /clarify Q2 → A used are
    // not part of the surface.
};

}  // namespace fixpp::dict
