// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.4]. Authority: 2b-wire.md v0.2.
#pragma once
#include <cstdint>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <fixpp/core/error.hpp>
#include "view.hpp"
#include "framer.hpp"

namespace fixpp::wire {

inline constexpr std::size_t default_max_offset_entries           = 4096;  // occurrence space
inline constexpr std::size_t default_max_group_entries_per_instance = 4096;

class OffsetTable {
public:
    struct entry {
        std::uint16_t tag;               // 0..65535 ([2b §1.2])
        std::uint32_t offset;            // byte offset into originating frame
        std::uint32_t length;            // value length (after '=', before SOH)
        std::uint16_t group_index_link;  // 0 = top-level; else group sub-index ref
    };
    static_assert(sizeof(entry) == 12);
    static_assert(alignof(entry) == 4);

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]]) noexcept;

    [[nodiscard]] core::expected_t<entry> find(std::uint16_t tag) const noexcept;
    [[nodiscard]] std::span<entry const>
    entries() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] std::size_t size() const noexcept;

    class group_index;
    [[nodiscard]] core::expected_t<group_index const&>
    group(std::uint16_t no_tag) const noexcept [[clang::lifetimebound]];  // lazy
};

}  // namespace fixpp::wire
