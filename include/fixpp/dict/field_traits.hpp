// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/field_traits.hpp
//
// 003-OWNED, NET-NEW (RC#1, re-/plan 2026-05-15). Literal materialisation of
// [2c §4.1.3] (.specify/2c-codegen.md v1.4 §4.1.3, lines 259-313). 002 ships
// NO field_traits.hpp (its seven dict headers do not include it —
// specs/002-dictionary-xml-loader/spec.md:189); this is the 2c-owned
// typed-decoding layer on the ≤20 ns typed-accessor hot path, consumed by the
// generated <vXX>/Messages.hpp string/int/char accessors and dict::reify
// step 3 ([2c §4.8]). Oracle:
// specs/003-dictionary-codegen/contracts/field_traits.hpp; data-model
// Entity 11; spec §4.8 AC-FT1..AC-FT3.
//
// Bridge header (arch §2.4 v0.3 dual-compile carve-out, RC#3): it #includes
// the vendored frozen wire stub; this is NOT a cyclic dictionary→wire module
// edge (check_layers.py BRIDGE_SOURCE_FILES/BRIDGE_EXEMPT_INCLUDES).
//
// from_field_view / decode_field are noexcept + allocation-free and sit on the
// ≤20 ns hot path (§6.2 string/int/char ceiling) — the bodies inline (defined
// in the matching .cpp / inline at /tasks D-5; declarations pinned here).
#pragma once
#include <fixpp/core/error.hpp>                  // expected_t, error
#include <fixpp/wire/message_view_contract.hpp>  // wire::field_view (vendored
                                                 // frozen R6 stub; RC#3 bridge)
#include <cstdint>
#include <string_view>

namespace fixpp::dict {

// Primary template; specialised below for the field-view-decodable types.
//
// `decimal_t` is DELIBERATELY NOT a field_traits specialisation
// ([2c §4.1.3:269-277] / RC#2 / AC-FT2): decimal routes through the merged-2a
// PMR-mandatory entry point decimal_t::parse(span, mr) ([2a §4.3]) directly at
// the accessor call site, because from_field_view's mr-less signature cannot
// carry the required memory_resource* through. See
// contracts/generated_message.hpp price(mr), AC-G4 / AC-G4a / data-model I-16.
template <class T>
struct field_traits;  // primary; specialised below

template <>
struct field_traits<std::string_view> {
    [[nodiscard]] static core::expected_t<std::string_view>
    from_field_view(wire::field_view const& fv) noexcept;
};

template <>
struct field_traits<char> {
    [[nodiscard]] static core::expected_t<char>
    from_field_view(wire::field_view const& fv) noexcept;
};

template <>
struct field_traits<std::int32_t> {
    [[nodiscard]] static core::expected_t<std::int32_t>
    from_field_view(wire::field_view const& fv) noexcept;
};

template <>
struct field_traits<std::int64_t> {
    [[nodiscard]] static core::expected_t<std::int64_t>
    from_field_view(wire::field_view const& fv) noexcept;
};

template <>
struct field_traits<bool> {
    [[nodiscard]] static core::expected_t<bool>
    from_field_view(wire::field_view const& fv) noexcept;
};

// The remaining field-view-decodable types ([2c §4.1.3:269-271]) — the
// timestamp / UTCTimestamp / date types and the MultiCharValue /
// MultiStringValue split — follow the SAME single-static `from_field_view`
// shape above; their specialisations are declared alongside their wire/codegen
// value types when those land (the oracle elides them deliberately; the shape
// is invariant). `decimal_t` is excluded by design (AC-FT2).

// get-then-check-then-decode helper every [2c §4.7] typed accessor and the
// [2c §4.8] dict::reify algorithm inline. Defined for every T with a
// field_traits specialisation; the decimal case does NOT use this helper (it
// needs the caller-threaded `mr` this mr-less helper cannot forward). AC-FT3.
template <class T>
[[nodiscard]] inline core::expected_t<T>
decode_field(core::expected_t<wire::field_view> fv) noexcept {
    if (!fv) return std::unexpected{fv.error()};
    return field_traits<T>::from_field_view(*fv);
}

}  // namespace fixpp::dict
