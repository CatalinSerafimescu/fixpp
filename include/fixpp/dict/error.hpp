// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/error.hpp
//
// `fixpp::dict::xml_parse_error` / `unknown_version_error` / `xml_oom_error`.
// Canonical declaration; mirrors the extract recorded at
// `specs/002-dictionary-xml-loader/contracts/error.hpp` and the data-model.md
// Entity 6 entry. Inherits design from:
//   - `[2c §4.5]` (exception-API carve-out for XmlLoader)
//   - `[2c §6.1.1]` (trap_throw → dict_xml_oom translation)
//   - `[2c §6.7]` (errors-introduced table — variant names)
//   - `[arch §5.3]` (construction-time exceptions allowed)
//
// Each exception type carries a matching `fixpp::core::error` enum variant
// accessible via `.code()`. The variants are appended to `fixpp::core::error`
// in `include/fixpp/core/error.hpp` (additive; non-shape-changing per
// `[const §X.4]` forwards-compat — see research.md D-3 / D-10).
//
// Alternative pattern (no enum append): a new error may instead DERIVE from an
// existing exception here and REUSE that base's inherited `.code()`, being
// discriminated by CATCH TYPE rather than by a distinct `code()` value. `code()`
// is non-virtual, so a base-typed catch cannot observe a derived `code()` anyway
// — the catch type is the only discriminator that works. This keeps a new,
// narrowly-scoped load error out of the `core::error` enum entirely (avoiding the
// no-`default` `error_message()` `-Wswitch`/`-Werror` co-edit and the
// `test_020_error_completeness.cpp` slot pin). See `group_delimiter_collision_error`
// below and 072-nested-group-hardening research D-A1.

#pragma once

#include <cstdint>
#include <fixpp/core/error.hpp>
#include <new>
#include <stdexcept>
#include <string>

namespace fixpp::dict {

// Thrown by `XmlLoader::load(path, mr)` and `XmlLoader::load_from_string(...)`
// on AC-L2 (unreadable / nonexistent path), AC-L3 (malformed XML), AC-L5
// (missing/non-numeric `<field number>`), AC-L6 (duplicate field number),
// AC-L7 (dangling `<component>` reference), AC-L8 (unknown `<field type>`).
//
// Constructor is NOT `noexcept`: `std::runtime_error(std::string)` may allocate.
class xml_parse_error : public std::runtime_error {
public:
    explicit xml_parse_error(const std::string& message) : std::runtime_error(message) {}

    // cppcheck-suppress unusedFunction  // read by tests; cppcheck doesn't see cross-file callers
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_xml_parse_failed;
    }
};

// Thrown by `XmlLoader::load*` when a nested repeating group's delimiter
// (`first_field_tag`, the first declared field) equals its immediate parent
// group's delimiter — a structural convention the nested-group read machinery
// relies on (a flat instance splitter would mis-split the outer group's
// boundaries). Rejecting such a dialect at load closes a public-API path to a
// silent wire-corruption mis-split (072-nested-group-hardening FR-003 / L-063-4).
//
// DERIVES from `xml_parse_error` (so existing `catch (xml_parse_error&)`
// bad-dictionary handlers still catch it) and REUSES the inherited `code()`
// (`dict_xml_parse_failed`) — it does NOT append a `fixpp::core::error` variant
// (see the header note above and research D-A1). Callers that need to
// distinguish it discriminate by catch type: `catch (group_delimiter_collision_error&)`.
class group_delimiter_collision_error : public xml_parse_error {
public:
    explicit group_delimiter_collision_error(const std::string& message)
        : xml_parse_error(message) {}

    // Build a message naming the offending group's no_tag, the shared delimiter,
    // and the parent group's no_tag — the three facts an operator needs to fix
    // the offending dialect.
    [[nodiscard]] static group_delimiter_collision_error make(std::uint16_t no_tag,
                                                              std::uint16_t delimiter,
                                                              std::uint16_t parent_no_tag) {
        return group_delimiter_collision_error(
            std::string("fixpp::dict::group_delimiter_collision_error: nested group ") +
            std::to_string(no_tag) + " has delimiter (first_field_tag) " +
            std::to_string(delimiter) + " equal to its parent group " +
            std::to_string(parent_no_tag) +
            "'s delimiter; a nested group's delimiter must differ from its parent's");
    }
};

// Thrown by `OrchestraLoader::load*` (074-orchestra-native-reader) on any
// malformed / wrong-grammar Orchestra input: non-`fixr:repository` root,
// unknown `<fixr:datatype>` token used by a field, dangling component ref,
// truncated/malformed XML, or a QuickFIX-XML file fed to the Orchestra reader.
//
// DERIVES from `xml_parse_error` (so existing `catch (xml_parse_error&)` /
// `catch (std::exception&)` bad-dictionary handlers still catch it) and REUSES
// the inherited `code()` (`dict_xml_parse_failed`) — it does NOT append a
// `fixpp::core::error` variant, NO C-ABI change (the
// `group_delimiter_collision_error` precedent, 072). Callers that need to
// distinguish it discriminate by catch type: `catch (orchestra_parse_error&)`.
class orchestra_parse_error : public xml_parse_error {
public:
    using xml_parse_error::xml_parse_error;
};

// Thrown by `XmlLoader::load*` when the XML's
// `<fix major="..." minor="..." [servicepack="..."]>` header does not resolve
// to one of the nine v1.0-supported FIX versions per `[2c §1.3]` (AC-L4).
class unknown_version_error : public std::runtime_error {
public:
    explicit unknown_version_error(const std::string& message) : std::runtime_error(message) {}

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static) — exception API.
    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_unknown_version;
    }
};

// Thrown by `XmlLoader::load*` when PMR allocation against the caller-supplied
// `mr` fails inside the load path (AC-L9). The internal `std::bad_alloc` is
// trapped via `fixpp::core::detail::trap_throw_or_throw<dict::xml_oom_error>`.
//
// Derived from `std::bad_alloc` (NOT `std::runtime_error`) so a caller catching
// `std::bad_alloc` still gets it; `catch (std::exception&)` paths also still
// catch it.
class xml_oom_error : public std::bad_alloc {
public:
    xml_oom_error() noexcept = default;

    // cppcheck-suppress unusedFunction  // read by oom_injection_test
    [[nodiscard]] char const* what() const noexcept override {
        return "fixpp::dict::xml_oom_error: PMR allocation failed during "
               "XmlLoader::load*";
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static) — exception API.
    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_xml_oom;
    }
};

}  // namespace fixpp::dict
