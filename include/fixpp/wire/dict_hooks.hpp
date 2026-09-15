// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/dict_hooks.hpp — the dictionary a wire view reads through.
//
// One value carries the type-erased dictionary pointer and every callback that
// resolves through it, so a table, a view, a nested sub-table and a group entry
// can only ever be handed ALL of them together or NONE (fixpp#426, design
// `.specify/426-428-length-data-pairs.md` §3).
//
// Why a bundle and not loose parameters: #384 made the un-informed spelling a
// compile error by removing defaults, but a caller could still pair one table's
// membership oracle with another table's delimiter oracle, and each new callback
// widened that hazard (brain/components/wire.md, "#384"). Production code builds a
// dict_hooks only through `none()` or `for_table_view()`, which fill every field
// from one dictionary; the field-by-field constructor is reachable from tests only.
//
// Include-graph note: offset_table.hpp includes this header, so it may name
// `group_context` only by forward declaration (group_view.hpp owns the type and
// includes offset_table.hpp). `for_table_view` is therefore DEFINED in parser.hpp,
// where `group_context` and `table_view` are both complete.

#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "length_data_pairs.hpp"

namespace fixpp::dict {
class table_view;
}  // namespace fixpp::dict

namespace fixpp::wire {

struct group_context;

class dict_hooks {
public:
    using classify_fn_t = bool (*)(void const*, std::string_view, std::uint16_t) noexcept;
    using group_member_fn_t = bool (*)(void const*, group_context const&, std::uint16_t,
                                       std::uint16_t) noexcept;
    using group_delim_fn_t = std::uint16_t (*)(void const*, group_context const&,
                                               std::uint16_t) noexcept;
    using length_pair_fn_t = std::uint16_t (*)(void const*, std::uint16_t) noexcept;

    // No dictionary: every predicate is absent and pairs come from the standard
    // table alone.
    constexpr dict_hooks() noexcept = default;
    [[nodiscard]] static constexpr dict_hooks none() noexcept { return {}; }

    // Every callback resolves through `dict`, which must outlive every holder of
    // the result. Defined in parser.hpp.
    [[nodiscard]] static dict_hooks for_table_view(fixpp::dict::table_view const& dict) noexcept;

    [[nodiscard]] constexpr void const* opaque_dict() const noexcept { return opaque_dict_; }
    [[nodiscard]] constexpr classify_fn_t classify_fn() const noexcept { return classify_; }
    [[nodiscard]] constexpr group_member_fn_t group_member_fn() const noexcept {
        return group_member_;
    }
    [[nodiscard]] constexpr group_delim_fn_t group_delim_fn() const noexcept {
        return group_delim_;
    }

    // The Data tag counted by `length_tag`, or 0. The standard FIX pairs govern
    // any tag they name; a dictionary pair applies only when neither of its tags
    // appears in the standard table (design §3, r3 R3-1). So a dictionary cannot
    // re-pair or retype a standard pair, and every scanner splits a message the
    // same way whichever dictionary it holds.
    [[nodiscard]] constexpr std::uint16_t data_tag_for_length(
        std::uint16_t length_tag) const noexcept {
        if (std::uint16_t const standard = detail::standard_data_tag_for_length(length_tag);
            standard != 0) {
            return standard;
        }
        if (length_pair_ == nullptr || detail::is_standard_pair_tag(length_tag)) {
            return 0;
        }
        std::uint16_t const data = length_pair_(opaque_dict_, length_tag);
        return (data != 0 && !detail::is_standard_pair_tag(data)) ? data : 0;
    }

private:
#ifdef FIXPP_TEST_HOOKS
    friend struct dict_hooks_test_access;
#endif

    constexpr dict_hooks(void const* opaque_dict, classify_fn_t classify,
                         group_member_fn_t group_member, group_delim_fn_t group_delim,
                         length_pair_fn_t length_pair) noexcept
        : opaque_dict_{opaque_dict},
          classify_{classify},
          group_member_{group_member},
          group_delim_{group_delim},
          length_pair_{length_pair} {}

    void const* opaque_dict_ = nullptr;
    classify_fn_t classify_ = nullptr;
    group_member_fn_t group_member_ = nullptr;
    group_delim_fn_t group_delim_ = nullptr;
    length_pair_fn_t length_pair_ = nullptr;
};

// Every holder (OffsetTable, MessageView, entry_context, field_iterator)
// carries a dict_hooks by value (FR-004-class zero-alloc-by-value context) —
// see group_context's identical requirement in group_view.hpp.
static_assert(std::is_trivially_copyable_v<dict_hooks>);

}  // namespace fixpp::wire
