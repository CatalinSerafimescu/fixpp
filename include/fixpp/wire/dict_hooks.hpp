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
//
// 100-wire-group-read-integrity (research D2, data-model `dict_hooks`): the bundle
// also carries the registration resolver, which decides whether a count tag is a
// group in a read's context and may name the group's definition there with the
// same lookup. A dict-aware bundle without it declines group reads.

#pragma once

#include <cstdint>
#include <span>
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
    // 100 (research D2; T071 fix, one lookup per read): what a read in `ctx` needs
    // about `no_tag`. `registered`: whether `no_tag` is registered as a group in
    // `ctx`. `definition_known`: whether `delimiter` and `members` are supplied; they
    // are then the answers the delimiter callback and the membership callback give
    // for `(ctx, no_tag)`, and a read uses them in place of those callbacks. False:
    // the read asks those callbacks, as a test seam's stubs need.
    struct group_resolution {
        bool registered = false;
        bool definition_known = false;
        std::uint16_t delimiter = 0;
        std::span<std::uint16_t const> members;
    };
    using group_resolve_fn_t = group_resolution (*)(void const*, group_context const&,
                                                    std::uint16_t) noexcept;
    // Which half of a pair a dictionary lookup starts from.
    enum class pair_side : std::uint8_t { length, data };
    // Given one half of a dictionary-declared pair, the other half, or 0.
    using length_pair_fn_t = std::uint16_t (*)(void const*, std::uint16_t, pair_side) noexcept;

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
    // 100: null on `none()`. `OffsetTable::group()` declines when it is null.
    [[nodiscard]] constexpr group_resolve_fn_t group_resolve_fn() const noexcept {
        return group_resolve_;
    }
    // 100 (research D2 step 1, D11 step 1): whether the bundle can establish a group:
    // false when the dictionary, the membership callback or the registration resolver
    // is null. `OffsetTable::group()` declines a read without them, and a nested read
    // keeps a failure only with them.
    [[nodiscard]] constexpr bool can_establish_groups() const noexcept {
        return opaque_dict_ != nullptr && group_member_ != nullptr && group_resolve_ != nullptr;
    }
    // Null when the dictionary declares no Length+Data pair of its own; the two
    // lookups below then answer from the standard table alone.
    [[nodiscard]] constexpr length_pair_fn_t length_pair_fn() const noexcept {
        return length_pair_;
    }

    // The Data tag counted by `length_tag`, or 0. The standard FIX pairs govern
    // any tag they name; a dictionary pair applies only when neither of its tags
    // appears in the standard table (design §3, r3 R3-1). So a dictionary cannot
    // re-pair or retype a standard pair, and every scanner splits a message the
    // same way whichever dictionary it holds.
    //
    // Every scanner calls this for every field, so a tag the standard table does not
    // name costs one constexpr bit test here, and nothing more when the dictionary
    // declares no pair of its own (`for_table_view` then installs no callback).
    [[nodiscard]] constexpr std::uint16_t data_tag_for_length(
        std::uint16_t length_tag) const noexcept {
        // A standard pair tag is answered by the standard table alone: a Length gets
        // its Data tag, and a standard Data tag cannot be a Length (0).
        if (detail::standard_pair_tag_bit(length_tag)) {
            return detail::standard_data_tag_for_length(length_tag);
        }
        if (length_pair_ == nullptr) {
            return 0;
        }
        std::uint16_t const data = length_pair_(opaque_dict_, length_tag, pair_side::length);
        return (data != 0 && !detail::is_standard_pair_tag(data)) ? data : 0;
    }

    // The Length tag that counts `data_tag`, or 0, by the same precedence: the
    // standard pairs first, a dictionary pair only when neither tag is standard.
    [[nodiscard]] constexpr std::uint16_t length_tag_for_data(
        std::uint16_t data_tag) const noexcept {
        if (detail::standard_pair_tag_bit(data_tag)) {
            return detail::standard_length_tag_for_data(data_tag);
        }
        if (length_pair_ == nullptr) {
            return 0;
        }
        std::uint16_t const length = length_pair_(opaque_dict_, data_tag, pair_side::data);
        return (length != 0 && !detail::is_standard_pair_tag(length)) ? length : 0;
    }

private:
    // Test-only raw-constructor seam, defined in tests/support/dict_hooks_test_access.hpp
    // (never installed). Unconditional (fixpp#511): a gated friend would make a test
    // TU's dict_hooks a different class from the library's, an ODR violation.
    friend struct dict_hooks_test_access;

    constexpr dict_hooks(void const* opaque_dict, classify_fn_t classify,
                         group_member_fn_t group_member, group_delim_fn_t group_delim,
                         length_pair_fn_t length_pair, group_resolve_fn_t group_resolve) noexcept
        : opaque_dict_{opaque_dict},
          classify_{classify},
          group_member_{group_member},
          group_delim_{group_delim},
          length_pair_{length_pair},
          group_resolve_{group_resolve} {}

    void const* opaque_dict_ = nullptr;
    classify_fn_t classify_ = nullptr;
    group_member_fn_t group_member_ = nullptr;
    group_delim_fn_t group_delim_ = nullptr;
    length_pair_fn_t length_pair_ = nullptr;
    group_resolve_fn_t group_resolve_ = nullptr;
};

// A holder that carries a dict_hooks by value copies it with no allocation
// (FR-004-class zero-alloc-by-value context) — see group_context's identical
// requirement in group_view.hpp. Re-derive the by-value holders with
// `command grep -rnE 'dict_hooks +[A-Za-z_]+ *(\{ *\})?;|hooks_holder_t +[A-Za-z_]+' include src`.
static_assert(std::is_trivially_copyable_v<dict_hooks>);

namespace detail {
// 100 (B-100-7): `dict_hooks::none()` as an object with static storage, for a
// holder of a `dict_hooks const*` (an Iter view, a field iterator) built without
// a dictionary, so that pointer is never null and never dangles.
inline constexpr dict_hooks no_dict_hooks{};
}  // namespace detail

}  // namespace fixpp::wire
