// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/load_any.hpp
//
// `fixpp::dict::load_any` — 080-orchestra-runtime-load shared root-sniff
// dispatch helper (FR-003/FR-004, contracts/load_any.md). Sniffs the XML
// root element and dispatches to `XmlLoader::load` (`<fix>`) or
// `OrchestraLoader::load` (`<fixr:repository>`); any other root, or an
// unreadable/malformed file, throws a `dict::` parse error (fail-closed,
// guarantee G2).

#pragma once

#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
// NOLINTNEXTLINE(misc-include-cleaner)
#include <fixpp/dict/error.hpp>  // re-exported: load_any() throws dict::xml_parse_error
#include <fixpp/dict/loader_policy.hpp>  // unresolved_group_policy (fixpp#215 item 4)
#include <memory_resource>

namespace fixpp::dict {

// Precondition: mr != nullptr (asserted, guarantee G4).
//
// fixpp#215 item 4: `policy` is forwarded verbatim to whichever concrete loader
// the root sniff selects, so FR-006a's tolerant opt-in is reachable from a
// caller that does NOT already know which loader type it needs — the one thing
// this facade exists to hide. DEFAULTED trailing parameter, exactly as on
// `XmlLoader::load` / `OrchestraLoader::load`: every existing caller compiles
// unchanged and keeps the safe `fail_closed` default. (The C-ABI's
// `fixpp_dict_load_from_xml` deliberately has no such parameter — that surface
// is GA-frozen at 1.5.0; `load_any` is a plain internal C++ facade with no
// such constraint.)
[[nodiscard]] Dictionary load_any(
    std::filesystem::path const& path, std::pmr::memory_resource* mr,
    unresolved_group_policy policy = unresolved_group_policy::fail_closed);

}  // namespace fixpp::dict
