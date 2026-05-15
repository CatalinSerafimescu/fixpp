// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — 003-OWNED surface, literal extract of [2c §4.1.3]
// (.specify/2c-codegen.md v1.4 §4.1.3, lines 259-313).
//
// RC#1 RESOLUTION (re-/plan 2026-05-15). Gate A round 1 (Codex P1-1 / Opus
// N-P2-2 / RC#1) established that 002 ships NO field_traits.hpp (its seven
// dict headers do not include it — specs/002-dictionary-xml-loader/spec.md:189;
// verified on disk: include/fixpp/dict/field_traits.hpp ABSENT). It is the
// 2c-owned typed-decoding layer on the ≤20 ns typed-accessor hot path,
// consumed by contracts/generated_message.hpp:10 and every string/int/char
// accessor + dict::reify step 3 ([2c §4.8]). 002 never owned it and the bundle
// never previously scoped it. Therefore 003 OWNS it.
//
// FILES-IN-SCOPE consequence: this is a NET-NEW file —
// include/fixpp/dict/field_traits.hpp — created by 003 (Project-Structure row
// added; was previously mis-listed as "002-shipped").
//
// The shipped header is include/fixpp/dict/field_traits.hpp. Header-only:
// from_field_view / decode_field are noexcept + allocation-free and sit on the
// ≤20 ns hot path (§6.2 string/int/char ceiling) — the bodies inline.
#pragma once
#include <fixpp/core/error.hpp>                  // expected_t, error
#include <fixpp/wire/message_view_contract.hpp>  // wire::field_view (vendored
                                                 // frozen R6 stub; see RC#3
                                                 // note in contracts/reify.hpp
                                                 // — same dual-compile bridge
                                                 // carve-out, arch §2.4 v0.2→v0.3).
#include <cstdint>
#include <string_view>

namespace fixpp::dict {

// Primary template; specialised for the field-view-decodable types.
//
// The decimal_t case is DELIBERATELY NOT a field_traits specialisation
// ([2c §4.1.3:269-277] / RC#2): decimal routes through the merged-2a
// PMR-mandatory entry point decimal_t::parse(span, mr) ([2a §4.3], a shell
// over decimal_traits<FIXPP_DECIMAL_T>::from_chars(span, mr) [2a §4.2])
// directly at the accessor call site, because from_field_view's mr-less
// signature cannot carry the required memory_resource* through. See
// contracts/generated_message.hpp price(mr) and AC-G4 / AC-G4a.
template <class T>
struct field_traits;  // primary; specialised below

template <>
struct field_traits<std::string_view> {
    [[nodiscard]] static expected_t<std::string_view>
    from_field_view(wire::field_view const& fv) noexcept;
};

template <>
struct field_traits<char> {
    [[nodiscard]] static expected_t<char>
    from_field_view(wire::field_view const& fv) noexcept;
};

// Further specialisations emitted/declared for the remaining
// field-view-decodable types ([2c §4.1.3:269-271]): std::int32_t,
// std::int64_t, bool, the timestamp / UTCTimestamp / date types, and the
// MultiCharValue / MultiStringValue split. (Elided here; the shape is the
// single static from_field_view above. decimal_t is excluded by design.)

// get-then-check-then-decode helper every §4.7 typed accessor and the
// dict::reify §4.8 algorithm inline. Defined for every T with a field_traits
// specialisation; the decimal case does NOT use this helper (it needs the
// caller-threaded mr this mr-less helper cannot forward).
template <class T>
[[nodiscard]] inline expected_t<T>
decode_field(expected_t<wire::field_view> fv) noexcept {
    if (!fv) return std::unexpected{fv.error()};
    return field_traits<T>::from_field_view(*fv);
}

}  // namespace fixpp::dict

// New ACs pinned by this contract (spec §4.x, added at re-/plan):
//   AC-FT1  field_traits.hpp exists at include/fixpp/dict/ (003-NEW), declares
//           the primary template + the string_view/char (and listed) special-
//           isations + decode_field<T>; noexcept; allocation-free.
//   AC-FT2  decimal_t is NOT a field_traits specialisation; the decimal route
//           is the §4.7 decimal accessor (AC-G4) — enforced by a static check
//           that field_traits<fixpp::decimal_t> is not defined.
//   AC-FT3  decode_field<T> forwards the get<>() error unchanged on !fv and
//           returns field_traits<T>::from_field_view(*fv) otherwise.
