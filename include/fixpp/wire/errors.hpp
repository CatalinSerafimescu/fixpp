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
// Templated on the expected success type (defaults to void) so a call site
// returning core::expected_t<U> can write `return err_frame_too_large<U>();`
// and a void-returning one just `return err_frame_too_large();`.
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_frame_too_large() noexcept {
    return fail<T>(error::wire_frame_too_large);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_invalid_body_length() noexcept {
    return fail<T>(error::wire_invalid_body_length);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_checksum_mismatch() noexcept {
    return fail<T>(error::wire_checksum_mismatch);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_framing_resync() noexcept {
    return fail<T>(error::wire_framing_resync);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_invalid_field_format() noexcept {
    return fail<T>(error::wire_invalid_field_format);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_offset_table_full() noexcept {
    return fail<T>(error::wire_offset_table_full);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_group_too_large() noexcept {
    return fail<T>(error::wire_group_too_large);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_tag_out_of_range() noexcept {
    return fail<T>(error::wire_tag_out_of_range);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_required_field_missing() noexcept {
    return fail<T>(error::wire_required_field_missing);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_header_out_of_order() noexcept {
    return fail<T>(error::wire_header_out_of_order);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_field_value_out_of_range() noexcept {
    return fail<T>(error::wire_field_value_out_of_range);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_field_value_truncated() noexcept {
    return fail<T>(error::wire_field_value_truncated);
}
template <class T = void>
[[nodiscard]] constexpr core::expected_t<T> err_unexpected_tag() noexcept {
    return fail<T>(error::wire_unexpected_tag);
}

}  // namespace fixpp::wire
