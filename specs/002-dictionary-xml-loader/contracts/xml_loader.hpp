// SPDX-License-Identifier: AGPL-3.0-or-later
// extract from .specify/2c-codegen.md v1.3 §4.5 (signed off 2026-05-10)
// This file is a /plan Phase 1 contract extract — the canonical declaration
// lives at include/fixpp/dict/xml_loader.hpp once 002-dictionary-xml-loader
// reaches /implement.
//
// Scope of this PR (per spec.md /clarify Session 2026-05-14):
//   - Q1 → B: ships XML data for FIX 4.2 / 4.4 / 5.0 SP2 / FIXT.1.1; the
//     loader code path accepts all nine v1.0-supported versions per AC-L4.
//   - Q2 → A: `load_overlay` / `load_overlay_from_string` are **absent**
//     from the public surface in this PR; the corresponding declarations
//     from `[2c §4.5]` are commented out below with a follow-up tag. The
//     `DialectOverlay` value type defers to D-009 (spec.md §10 F2).
//   - Q3 → A: third-party XML parser selection is **pugixml** per
//     research.md D-1; the parser is consumed only from
//     `src/dictionary/xml_loader.cpp` and is **not** transitively visible
//     from this public header (research.md D-15).

#pragma once

#include <fixpp/dict/dictionary.hpp>     // Dictionary
#include <fixpp/dict/error.hpp>           // xml_parse_error,
                                          // unknown_version_error,
                                          // xml_oom_error (declared types
                                          // thrown by `load*`)
#include <filesystem>
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

class XmlLoader {
public:
    // Construction-time exception allowed per `[arch §5.3]`. The hot path
    // (parse / serialize / validate) is exception-free; `XmlLoader` runs only
    // at engine init / session open, where the alternative is
    // `expected_t<Dictionary>` at a call site that reads "throws on bad XML"
    // ergonomically. PMR allocations within `load*` are wrapped in
    // `fixpp::core::detail::trap_throw_or_throw<dict::xml_oom_error>` (the
    // new exception-API sibling of `[2a §4.2]`'s `trap_throw` — added in
    // this PR per research.md D-3 / Gate A round 1 root cause #1) and
    // translated to `dict::xml_oom_error` on PMR failure (`[2c §6.1.1]`).
    XmlLoader() noexcept = default;

    // Load a per-version standard or QuickFIX-XML-format dictionary from a
    // file path. Allocates from `mr`; the returned `Dictionary`'s metadata
    // handle is heap-pinned (`[2c §4.3]`) and PMR-allocates copies of the
    // loaded tables on `mr` (since this PR uses the runtime-XML code path
    // for all four shipped versions — the codegen versions' `constexpr`
    // tables ship under D-008, separate feature).
    //
    // Throws `dict::xml_parse_error` (which derives from `std::runtime_error`)
    // on malformed XML; throws `dict::unknown_version_error` on a version
    // string outside the v1.0 supported set; otherwise returns the loaded
    // `Dictionary` by value. PMR allocation failure is translated to a thrown
    // `dict::xml_oom_error`.
    //
    // Preconditions:
    //   - `mr != nullptr` (caller precondition violation, NOT a runtime
    //     error; debug-asserts on null, undefined in release per
    //     research.md D-5).
    //
    // ACs satisfied: AC-L1 (positive path) + AC-L2..L8 (negative paths) +
    // AC-L9 (PMR OOM) + AC-P1, AC-P2 (PMR allocation discipline).
    [[nodiscard]] Dictionary
    load(std::filesystem::path const& xml_path,
         std::pmr::memory_resource* mr);

    // Bypass for in-process construction (testing, programmatic overlay
    // building once F2 ships, embedded scenarios where filesystem isn't
    // available). Same exception discipline as `load`. The primary use in
    // this PR is the AC-L3..L8 negative-path test suite — every malformed
    // XML literal is fed through `load_from_string` so the negative-path
    // tests need no on-disk fixtures (spec.md §3.2 user story 2).
    //
    // ACs satisfied: AC-L10 (positive path equivalence with `load`) +
    // every AC-L3..L8 negative-path test via the inline-XML pattern.
    [[nodiscard]] Dictionary
    load_from_string(std::string_view xml_text,
                     std::pmr::memory_resource* mr);

    // -------------------------------------------------------------------
    // load_overlay / load_overlay_from_string — DEFERRED.
    //
    // Per /clarify Q2 → A and spec.md §10 F2, the overlay surface is absent
    // in this PR. The corresponding declarations from `[2c §4.5]` are
    // intentionally OMITTED here so a future reviewer of `XmlLoader`'s
    // public surface sees the unfinished extension. Adding the two methods
    // later is source-compatible by C++ language rule (added member
    // functions can never invalidate existing call sites) and stays within
    // the `[arch §9.3]` "Stable from v1.0" tier; no call site in v1.0
    // depends on the absence of these methods.
    //
    // For reference, the deferred declarations are (verbatim from
    // `[2c §4.5]`):
    //
    //   [[nodiscard]] DialectOverlay
    //   load_overlay(std::filesystem::path const& xml_path,
    //                std::pmr::memory_resource* mr);
    //
    //   [[nodiscard]] DialectOverlay
    //   load_overlay_from_string(std::string_view xml_text,
    //                            std::pmr::memory_resource* mr);
    //
    // Adding these later is source-compatible by C++ language rule (added
    // member functions can never invalidate existing call sites) and stays
    // within the `[arch §9.3]` "Stable from v1.0" tier. (The earlier
    // draft's `[arch §9.2]` cite for this claim was repaired in Gate A
    // round 1 per Opus root cause #3.)
    // -------------------------------------------------------------------
};

}  // namespace fixpp::dict
