// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/orchestra_loader.hpp
//
// `fixpp::dict::OrchestraLoader` — native reader for the official FIX
// Orchestra machine-readable standard (`fixr:repository` schema, FIX Latest
// EP303). Sibling to `XmlLoader` (QuickFIX-XML); source-compatible surface
// per `specs/074-orchestra-native-reader/contracts/orchestra_loader.md` so
// callers/tests consume it identically. Stateless.
//
// Per contracts/orchestra_loader.md / research.md D-1 / D-15: pugixml is
// consumed only from `src/dictionary/orchestra_loader.cpp` and is NOT
// transitively visible from this public header.

#pragma once

#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
// NOLINTNEXTLINE(misc-include-cleaner)
#include <fixpp/dict/error.hpp>
#include <fixpp/dict/loader_policy.hpp>  // re-exported: load*() throws dict::orchestra_parse_error etc.
#include <memory_resource>
#include <string_view>

namespace fixpp::dict {

// Native reader for the official FIX Orchestra machine-readable standard
// (fixr:repository schema). Sibling to XmlLoader (QuickFIX-XML). Stateless.
class OrchestraLoader {
public:
    OrchestraLoader() noexcept = default;

    // Parse an OrchestraFIXLatest.xml file into an internal Dictionary.
    // Precondition: mr != nullptr (asserted).
    // Throws: orchestra_parse_error (malformed/unknown-datatype/wrong-root/
    //         dangling-ref), unknown_version_error (root version= not FIX
    //         Latest), group_delimiter_collision_error (nested delimiter ==
    //         parent), xml_oom_error (PMR bad_alloc, via
    //         trap_throw_or_throw), and -- 083 T038/FR-006c -- a further
    //         orchestra_parse_error when a group's delimiter cannot be
    //         resolved from its own declaration under the fail-closed
    //         default (FR-006). The type is the DERIVED one, deliberately:
    //         tests/fuzz/fuzz_orchestra_loader.cpp catches
    //         orchestra_parse_error, so a base xml_parse_error thrown here
    //         would escape to its terminal rethrow and crash the fuzzer on
    //         every input carrying an unresolvable group.
    //
    // 083 FR-006a / C-6.4 / C-6.6: `policy` is the SAME option with the SAME
    // semantics as XmlLoader's, defaulted to fail-closed, so existing callers
    // compile unchanged.
    [[nodiscard]] Dictionary load(std::filesystem::path const& path, std::pmr::memory_resource* mr,
                                  unresolved_group_policy policy =
                                      unresolved_group_policy::fail_closed);

    [[nodiscard]] Dictionary load_from_string(std::string_view xml, std::pmr::memory_resource* mr,
                                              unresolved_group_policy policy =
                                                  unresolved_group_policy::fail_closed);
};

}  // namespace fixpp::dict
