#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/table_view_test_access.hpp — TEST-ONLY read access to table_view's
// stores (098-dialect-overlay, research R-10 §2 Correction).
//
// `table_view` declares `friend struct table_view_test_access;` unconditionally.
// Define this struct only in this header; a differing definition of this struct
// elsewhere in the program is an ODR violation (ill-formed, no diagnostic required).
// Add new accessors here, never as members of `table_view` gated behind a test macro.
// Read accessors only: population goes through `table_view_builder`. Never installed;
// production targets must not include this header. Needs no test macro.

#include <fixpp/dict/table_view.hpp>

namespace fixpp::dict {

struct table_view_test_access {
    using valid_store = decltype(table_view::valid_);
    using required_store = decltype(table_view::required_);
    using group_first_store = decltype(table_view::group_first_);
    using group_members_store = decltype(table_view::group_members_);
    using group_required_store = decltype(table_view::group_required_members_);
    using bit_store = decltype(table_view::group_bits_);
    using group_ctx_store = decltype(table_view::group_ctx_);
    using types_store = decltype(table_view::types_);
    using enums_store = decltype(table_view::enums_);
    using framing_tags_store = decltype(table_view::fixt_framing_tags_);
    using framing_types_store = decltype(table_view::fixt_framing_types_);
    using pair_store = decltype(table_view::length_pair_data_tag_);

    [[nodiscard]] static valid_store const& valid(table_view const& tv) noexcept {
        return tv.valid_;
    }
    [[nodiscard]] static required_store const& required(table_view const& tv) noexcept {
        return tv.required_;
    }
    [[nodiscard]] static group_first_store const& group_first(table_view const& tv) noexcept {
        return tv.group_first_;
    }
    [[nodiscard]] static group_members_store const& group_members(table_view const& tv) noexcept {
        return tv.group_members_;
    }
    [[nodiscard]] static group_required_store const& group_required_members(
        table_view const& tv) noexcept {
        return tv.group_required_members_;
    }
    [[nodiscard]] static bit_store const& group_bits(table_view const& tv) noexcept {
        return tv.group_bits_;
    }
    [[nodiscard]] static group_ctx_store const& group_ctx(table_view const& tv) noexcept {
        return tv.group_ctx_;
    }
    [[nodiscard]] static types_store const& types(table_view const& tv) noexcept {
        return tv.types_;
    }
    [[nodiscard]] static bit_store const& enum_bits(table_view const& tv) noexcept {
        return tv.enum_bits_;
    }
    [[nodiscard]] static enums_store const& enums(table_view const& tv) noexcept {
        return tv.enums_;
    }
    [[nodiscard]] static framing_tags_store const& fixt_framing_tags(
        table_view const& tv) noexcept {
        return tv.fixt_framing_tags_;
    }
    [[nodiscard]] static framing_types_store const& fixt_framing_types(
        table_view const& tv) noexcept {
        return tv.fixt_framing_types_;
    }
    [[nodiscard]] static pair_store const& length_pair_data_tag(table_view const& tv) noexcept {
        return tv.length_pair_data_tag_;
    }
    [[nodiscard]] static pair_store const& data_pair_length_tag(table_view const& tv) noexcept {
        return tv.data_pair_length_tag_;
    }
    [[nodiscard]] static bool has_nonstandard_pair(table_view const& tv) noexcept {
        return tv.has_nonstandard_pair_;
    }
};

}  // namespace fixpp::dict
