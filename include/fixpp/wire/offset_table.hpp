#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/offset_table.hpp
// [2b §4.4] OffsetTable + entry. Authority: .specify/2b-wire.md v0.2; shape
// oracle contracts/offset_table.hpp. Eager entry[] in document order +
// open-address robin-hood overlay for O(1)-by-tag first-occurrence find;
// lazy group sub-index on first group(no_tag). All storage from the captured
// per-message memory_resource. DoS caps: wire_offset_table_full (>4096 occ),
// wire_tag_out_of_range, wire_group_too_large — bounded memory, no
// unbounded growth ([2b §1.2]).

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <vector>

#include <fixpp/core/error.hpp>

#include "framer.hpp"
#include "view.hpp"

namespace fixpp::wire {

inline constexpr std::size_t default_max_offset_entries = 4096;  // occ space
inline constexpr std::size_t default_max_group_entries_per_instance = 4096;

class OffsetTable {
public:
    struct entry {
        // Members ordered offset/length first so the struct packs to exactly
        // 12 bytes with alignof 4 (the [2b §4.4] invariant). The shape-oracle
        // extract lists them tag-first; member order in an extract is
        // non-binding — the named set + sizeof==12/alignof==4 are.
        std::uint32_t offset;            // value offset into the frame
        std::uint32_t length;            // value length (after '=', pre-SOH)
        std::uint16_t tag;               // 0..65535 ([2b §1.2])
        std::uint16_t group_index_link;  // 0 = top-level
    };
    static_assert(sizeof(entry) == 12);
    static_assert(alignof(entry) == 4);

    // A default-constructed table is empty and reports every field absent
    // (status_ = ok, no entries/overlay -> find() = wire_required_field_
    // missing). Required so a default MessageView<Index>{} is well-formed —
    // the 2b cutover's dict::reify kEmpty sentinel + the seam #18
    // flyweight_shape_test default-construct it (T028).
    OffsetTable() = default;

    // Eagerly scans the frame's tag=value<SOH> stream into mr-backed
    // storage. On a DoS-cap breach the table is left empty and the breach
    // is reported by find()/build_status(). Likewise, if `mr` throws
    // bad_alloc mid-build it degrades the SAME way (empty table, status_ =
    // out_of_memory) — a noexcept ctor must not let bad_alloc escape and
    // std::terminate (004 T059 / Codex adversarial review).
    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]]) noexcept;

    // Non-RED build status (ok, or the wire_* cap/format error hit).
    [[nodiscard]] core::expected_t<void> build_status() const noexcept {
        return status_;
    }

    [[nodiscard]] core::expected_t<entry>
    find(std::uint16_t tag) const noexcept;  // first occurrence, O(1)

    [[nodiscard]] std::span<entry const>
    entries() const noexcept [[clang::lifetimebound]] {
        return {entries_.data(), entries_.size()};
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

    // Lazily-built group sub-index: the contiguous entry range owned by the
    // first occurrence of `no_tag` (the count field) through the last field
    // before the next top-level field. Bounded by
    // default_max_group_entries_per_instance.
    class group_index {
    public:
        group_index() = default;
        group_index(std::uint16_t no_tag, std::size_t first,
                    std::size_t count) noexcept
            : no_tag_{no_tag}, first_{first}, count_{count} {}
        [[nodiscard]] std::uint16_t no_tag() const noexcept { return no_tag_; }
        [[nodiscard]] std::size_t first_entry() const noexcept {
            return first_;
        }
        [[nodiscard]] std::size_t entry_count() const noexcept {
            return count_;
        }

    private:
        std::uint16_t no_tag_ = 0;
        std::size_t first_ = 0;
        std::size_t count_ = 0;
    };

    [[nodiscard]] core::expected_t<group_index>
    group(std::uint16_t no_tag) const noexcept;  // lazy

private:
    [[nodiscard]] static std::size_t overlay_cap_for(std::size_t n) noexcept;

    std::pmr::vector<entry> entries_;
    // Open-address robin-hood overlay: slot value = index into entries_ + 1
    // (0 = empty). Holds the FIRST occurrence per tag.
    std::pmr::vector<std::uint32_t> overlay_;
    core::expected_t<void> status_;
};

}  // namespace fixpp::wire
