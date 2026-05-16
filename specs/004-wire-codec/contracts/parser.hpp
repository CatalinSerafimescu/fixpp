// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.3]. Authority: 2b-wire.md v0.2.
// Header-only template (OSS-006). Mode is compile-time — no hot-path branch.
#pragma once
#include <cstdint>
#include <memory_resource>
#include <string_view>
#include <fixpp/core/error.hpp>
#include "view.hpp"
#include "framer.hpp"

namespace fixpp::dict { class table_view; }   // value type, owned by 2c

namespace fixpp::wire {

enum class access_mode { Iter, Index };

template <access_mode Mode>
class MessageView : public View {
public:
    [[nodiscard]] constexpr std::string_view
    msg_type() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] constexpr std::uint32_t msg_seq_num() const noexcept;

    class field_iterator;  // Iter: static constexpr Length+Data pair table
    [[nodiscard]] field_iterator begin() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] field_iterator end()   const noexcept [[clang::lifetimebound]];

    // Index-only
    [[nodiscard]] class OffsetTable const&
    offsets() const noexcept [[clang::lifetimebound]]
        requires (Mode == access_mode::Index);
    template <std::uint16_t Tag>
    [[nodiscard]] core::expected_t<class field_view> get() const noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Index);
    [[nodiscard]] core::expected_t<class field_view>
    get(std::uint16_t tag) const noexcept [[clang::lifetimebound]]
        requires (Mode == access_mode::Index);
    template <std::uint16_t NoTag, class GroupT>
    [[nodiscard]] class group_view<GroupT>
    group() const noexcept [[clang::lifetimebound]]
        requires (Mode == access_mode::Index);
    [[nodiscard]] class unknown_fields_view
    unknown_fields() const noexcept [[clang::lifetimebound]]
        requires (Mode == access_mode::Index);

    // Cross-strand escape hatch — DEFINED IN 2c, not here:
    //   reify(mr) -> fixpp::vXX::owning_message_t<...>   (deep copy)
};

template <access_mode Mode = access_mode::Index>
class Parser {
public:
    explicit Parser(fixpp::dict::table_view dict_metadata) noexcept;

    [[nodiscard]] core::expected_t<MessageView<Mode>>
    parse(frame_view const& frame [[clang::lifetimebound]],
          std::pmr::memory_resource* mr) noexcept [[clang::lifetimebound]];

    [[nodiscard]] core::expected_t<MessageView<access_mode::Iter>>
    parse_iter(frame_view const& frame [[clang::lifetimebound]]) noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Iter);
};

}  // namespace fixpp::wire
