#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/errors.hpp
// [2b §6.7] wire_* error helper wrappers. The variants themselves live in
// <fixpp/core/error.hpp> (appended at slots 30..42, non-renumbering — T003).
// These are thin, noexcept, allocation-free constructors of the std::unexpected
// side so call sites read `return wire::err_checksum_mismatch();` instead of
// the verbose std::unexpected{core::error::wire_checksum_mismatch}.

#include <fixpp/core/error.hpp>

namespace fixpp::wire {

using core::error;

template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> fail(error e) noexcept {
    return core::expected_t<T>{std::unexpect, e};
}

// One helper per wire_* variant — names mirror the enum, `err_` prefixed.
[[nodiscard]] constexpr auto err_frame_too_large() noexcept {
    return fail(error::wire_frame_too_large);
}
[[nodiscard]] constexpr auto err_invalid_body_length() noexcept {
    return fail(error::wire_invalid_body_length);
}
[[nodiscard]] constexpr auto err_checksum_mismatch() noexcept {
    return fail(error::wire_checksum_mismatch);
}
[[nodiscard]] constexpr auto err_framing_resync() noexcept {
    return fail(error::wire_framing_resync);
}
[[nodiscard]] constexpr auto err_invalid_field_format() noexcept {
    return fail(error::wire_invalid_field_format);
}
[[nodiscard]] constexpr auto err_offset_table_full() noexcept {
    return fail(error::wire_offset_table_full);
}
[[nodiscard]] constexpr auto err_group_too_large() noexcept {
    return fail(error::wire_group_too_large);
}
[[nodiscard]] constexpr auto err_tag_out_of_range() noexcept {
    return fail(error::wire_tag_out_of_range);
}
[[nodiscard]] constexpr auto err_required_field_missing() noexcept {
    return fail(error::wire_required_field_missing);
}
[[nodiscard]] constexpr auto err_header_out_of_order() noexcept {
    return fail(error::wire_header_out_of_order);
}
[[nodiscard]] constexpr auto err_field_value_out_of_range() noexcept {
    return fail(error::wire_field_value_out_of_range);
}
[[nodiscard]] constexpr auto err_field_value_truncated() noexcept {
    return fail(error::wire_field_value_truncated);
}
[[nodiscard]] constexpr auto err_unexpected_tag() noexcept {
    return fail(error::wire_unexpected_tag);
}

}  // namespace fixpp::wire
