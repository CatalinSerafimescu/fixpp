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
#include <memory_resource>

namespace fixpp::dict {

// Precondition: mr != nullptr (asserted, guarantee G4).
[[nodiscard]] Dictionary load_any(std::filesystem::path const& path, std::pmr::memory_resource* mr);

}  // namespace fixpp::dict
