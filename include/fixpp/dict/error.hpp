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

#pragma once

#include <fixpp/core/error.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace fixpp::dict {

// Thrown by `XmlLoader::load(path, mr)` and `XmlLoader::load_from_string(...)`
// on AC-L2 (unreadable / nonexistent path), AC-L3 (malformed XML), AC-L5
// (missing/non-numeric `<field number>`), AC-L6 (duplicate field number),
// AC-L7 (dangling `<component>` reference), AC-L8 (unknown `<field type>`).
//
// Constructor is NOT `noexcept`: `std::runtime_error(std::string)` may allocate.
class xml_parse_error : public std::runtime_error {
public:
    explicit xml_parse_error(std::string message)
        : std::runtime_error(std::move(message)) {}

    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_xml_parse_failed;
    }
};

// Thrown by `XmlLoader::load*` when the XML's
// `<fix major="..." minor="..." [servicepack="..."]>` header does not resolve
// to one of the nine v1.0-supported FIX versions per `[2c §1.3]` (AC-L4).
class unknown_version_error : public std::runtime_error {
public:
    explicit unknown_version_error(std::string message)
        : std::runtime_error(std::move(message)) {}

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

    [[nodiscard]] char const* what() const noexcept override {
        return "fixpp::dict::xml_oom_error: PMR allocation failed during "
               "XmlLoader::load*";
    }

    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_xml_oom;
    }
};

}  // namespace fixpp::dict
