// SPDX-License-Identifier: AGPL-3.0-or-later
// /plan Phase 1 contract extract — the canonical declaration lives at
// include/fixpp/dict/error.hpp once 002-dictionary-xml-loader reaches
// /implement. Inherits design from:
//   - `[2c §4.5]` (exception-API carve-out for XmlLoader)
//   - `[2c §6.1.1]` (trap_throw → dict_xml_oom translation)
//   - `[2c §6.7]` (errors-introduced table — variant names)
//   - `[arch §5.3]` (construction-time exceptions allowed)
//
// Three exception types ship in this PR; each carries a matching
// `fixpp::core::error` enum variant accessible via `.code()` so a top-level
// handler can route by code instead of by dynamic_cast (research.md D-10).
// The three enum variants are appended to `fixpp::core::error` in
// `include/fixpp/core/error.hpp` (additive; non-shape-changing per
// `[const §X.4]` forwards-compat — see research.md D-3 / D-10).

#pragma once

#include <fixpp/core/error.hpp>      // fixpp::core::error
#include <new>                        // std::bad_alloc
#include <stdexcept>                  // std::runtime_error
#include <string>

namespace fixpp::dict {

// Thrown by `XmlLoader::load(path, mr)` and `XmlLoader::load_from_string(...)`
// on:
//   - unreadable / nonexistent file path                  (AC-L2)
//   - malformed XML — unclosed tag, invalid encoding,
//     broken UTF-8                                        (AC-L3)
//   - `<field>` with missing or non-numeric `number`
//     attribute                                           (AC-L5)
//   - duplicate `<field number="N">` declarations         (AC-L6)
//   - `<message>` / `<group>` references a `<component>`
//     not defined in the same file                        (AC-L7)
//   - `<field type="UNKNOWN_TYPE">` outside the
//     `[FIX50SP2 §3.3]` field-type vocabulary             (AC-L8)
//
// Derived from `std::runtime_error` per `[2c §4.5]` so a top-level
// `catch (std::exception&)` is sufficient at the engine-init call site.
class xml_parse_error : public std::runtime_error {
public:
    explicit xml_parse_error(std::string message) noexcept
        : std::runtime_error(std::move(message)) {}

    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_xml_parse_failed;
    }
};

// Thrown by `XmlLoader::load(...)` / `load_from_string(...)` when the XML's
// `<fix major="..." minor="..." [servicepack="..."]>` header does not resolve
// to one of the nine v1.0-supported FIX versions per `[2c §1.3]` (AC-L4).
//
// Derived from `std::runtime_error` per `[2c §4.5]`.
class unknown_version_error : public std::runtime_error {
public:
    explicit unknown_version_error(std::string message) noexcept
        : std::runtime_error(std::move(message)) {}

    [[nodiscard]] fixpp::core::error code() const noexcept {
        return fixpp::core::error::dict_unknown_version;
    }
};

// Thrown by `XmlLoader::load(...)` / `load_from_string(...)` when PMR
// allocation against the caller-supplied `mr` fails inside the load path
// (AC-L9). The internal `bad_alloc` is trapped via
// `fixpp::core::detail::trap_throw` per `[2a §4.2]` / `[2c §6.1.1]` and
// re-thrown as this type.
//
// Derived from `std::bad_alloc` (NOT `std::runtime_error`) so a caller
// catching `std::bad_alloc` still gets it; `catch (std::exception&)` paths
// also still catch it. This split matches `[2c §4.5]`'s carve-out: the OOM
// case maps to `std::bad_alloc`-derived because that is what a generic OOM
// handler is conditioned on.
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
