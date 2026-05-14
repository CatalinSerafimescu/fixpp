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
//   - Q2 → A: `load_overlay` / `load_overlay_from_string` are **absent**
//     from the public surface in this PR. F2 (spec.md §10) tracks the add.
//   - Q3 → A: third-party XML parser selection is **pugixml** per
//     research.md D-1; the parser is consumed only from
//     `src/dictionary/xml_loader.cpp` and is **not** transitively visible
//     from this public header (research.md D-15).

#pragma once

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/error.hpp>

#include <filesystem>
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
    [[nodiscard]] Dictionary
    load(std::filesystem::path const& xml_path,
         std::pmr::memory_resource* mr);

    // In-process equivalent of `load(path, mr)`. Same exception discipline.
    // Drives the AC-L3..L8 negative-path test suite without on-disk
    // fixtures (spec.md §3.2 user story 2).
    //
    // ACs: AC-L10 (positive path equivalence) + AC-L3..L8 negative paths.
    [[nodiscard]] Dictionary
    load_from_string(std::string_view xml_text,
                     std::pmr::memory_resource* mr);

    // load_overlay / load_overlay_from_string — DEFERRED to F2 per /clarify
    // Q2 → A (spec.md §10). Adding them later is source-compatible by C++
    // language rule and stays within `[arch §9.3]` "Stable from v1.0".
};

}  // namespace fixpp::dict
