// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/field_traits.cpp
//
// 003-dictionary-codegen (RC#1) — out-of-line bodies of the 003-owned
// dict::field_traits<T>::from_field_view specialisations declared in
// include/fixpp/dict/field_traits.hpp ([2c §4.1.3]). noexcept + allocation-
// free (≤20 ns hot path; AC-FT1 / data-model I-15). Bridge file (arch §2.4
// v0.3, RC#3 — #includes the vendored frozen wire stub; NOT a cyclic
// dictionary→wire link edge; check_layers.py BRIDGE_SOURCE_FILES).
//
// NOTE: the decode-failure error taxonomy ([2c §4.1.3] does not pin a
// dedicated slot — it is NOT one of the six 003 reify slots, and the 2b/wire
// malformed-value error is 2b-owned, spec A6) is refined with the US1
// emitters; AC-FT1/FT2/FT3 (the Foundational gate) constrain the SHAPE and
// the !fv forwarding (decode_field, header-inline), not the malformed-value
// variant. Placeholder maps to dict_xml_parse_failed pending the [2c §4.1.3]
// wire decode-error binding (T024/US1).
#include <charconv>
#include <cstdint>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/field_traits.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <string_view>
#include <system_error>

namespace fixpp::dict {

namespace {
constexpr core::error kDecodeFailed = core::error::dict_xml_parse_failed;  // see banner
}

core::expected_t<std::string_view> field_traits<std::string_view>::from_field_view(
    wire::field_view const& fv) noexcept {
    return fv.as_string();
}

core::expected_t<char> field_traits<char>::from_field_view(wire::field_view const& fv) noexcept {
    std::string_view const s = fv.as_string();
    if (s.size() != 1) {
        return std::unexpected{kDecodeFailed};
    }
    return s.front();
}

core::expected_t<std::int32_t> field_traits<std::int32_t>::from_field_view(
    wire::field_view const& fv) noexcept {
    std::string_view const s = fv.as_string();
    std::int32_t out{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc{} || p != s.data() + s.size()) {
        return std::unexpected{kDecodeFailed};
    }
    return out;
}

core::expected_t<std::int64_t> field_traits<std::int64_t>::from_field_view(
    wire::field_view const& fv) noexcept {
    std::string_view const s = fv.as_string();
    std::int64_t out{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc{} || p != s.data() + s.size()) {
        return std::unexpected{kDecodeFailed};
    }
    return out;
}

core::expected_t<bool> field_traits<bool>::from_field_view(wire::field_view const& fv) noexcept {
    std::string_view const s = fv.as_string();
    if (s == "Y") {
        return true;
    }
    if (s == "N") {
        return false;
    }
    return std::unexpected{kDecodeFailed};
}

}  // namespace fixpp::dict
