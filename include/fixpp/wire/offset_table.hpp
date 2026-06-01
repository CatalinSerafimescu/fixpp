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
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>
#include <vector>

#include "framer.hpp"
#include "view.hpp"  // group_slice (mr-backed group instance slices)

namespace fixpp::wire {

inline constexpr std::size_t default_max_offset_entries = 4096;  // occ space
inline constexpr std::size_t default_max_group_entries_per_instance = 4096;

class OffsetTable {
public:
    using group_member_fn_t = bool (*)(void const*, std::uint16_t, std::uint16_t) noexcept;

    // Caller-tunable DoS caps (FR-015 / [2b §1.2] "configurable").
    // Defaults match the module-level inline constexpr above.
    struct Config {
        std::size_t max_offset_entries = default_max_offset_entries;
        std::size_t max_group_entries_per_instance = default_max_group_entries_per_instance;
    };

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
    // The Config overload threads FR-015 / [2b §1.2] caller-tunable caps.
    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]]) noexcept;

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]], void const* opaque_dict,
                group_member_fn_t group_member_fn) noexcept;

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]], Config cfg) noexcept;

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]], Config cfg,
                void const* opaque_dict, group_member_fn_t group_member_fn) noexcept;

    // Non-RED build status (ok, or the wire_* cap/format error hit).
    [[nodiscard]] core::expected_t<void> build_status() const noexcept { return status_; }

    [[nodiscard]] core::expected_t<entry> find(
        std::uint16_t tag) const noexcept;  // first occurrence, O(1)

    [[nodiscard]] std::span<entry const> entries() const noexcept [[clang::lifetimebound]] {
        return {entries_.data(), entries_.size()};
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Lazily-built group sub-index spanning entries owned by the first
    // occurrence of `no_tag` (the count field).
    //
    // Dict-AWARE construction (Parser{dict} path — all production callers):
    //   Group extent is bounded by real dictionary membership: the group ends
    //   at the first entry whose tag the dict says is not a group member.
    //   This is the [2b §4.7]-conformant path (dictionary's first-field-of-
    //   group rule; cap = default_max_group_entries_per_instance).
    //
    // Dict-FREE construction (OffsetTable(frame, mr) / OffsetTable(frame, mr,
    //   Config) — test/utility callers only; no production wire caller uses
    //   this path):
    //   When no dict predicate is threaded, the single-instance group extent
    //   degrades to rest-of-message (group_end = entries_.size()). This is a
    //   deliberate, scoped degradation documented as a [2b §4.7] deviation —
    //   see .specify/decisions/004-wire-codec-completeness.md §4.7 deviation
    //   note. The dict-aware path is fully §4.7-conformant.
    class group_index {
    public:
        group_index() = default;
        group_index(std::uint16_t no_tag, std::size_t first, std::size_t count) noexcept
            : no_tag_{no_tag}, first_{first}, count_{count} {}
        [[nodiscard]] std::uint16_t no_tag() const noexcept { return no_tag_; }
        [[nodiscard]] std::size_t first_entry() const noexcept { return first_; }
        [[nodiscard]] std::size_t entry_count() const noexcept { return count_; }

    private:
        std::uint16_t no_tag_ = 0;
        std::size_t first_ = 0;
        std::size_t count_ = 0;
    };

    [[nodiscard]] core::expected_t<group_index> group(std::uint16_t no_tag) const noexcept;  // lazy

    // Repeating-group instance slices for `no_tag`, in document order,
    // materialized once into this table's per-message mr arena (append-only,
    // reserved once → no reallocation), then cached per no_tag. The returned
    // span is valid for the message lifetime and is NOT clobbered by another
    // group's access — replaces the prior static thread_local
    // (zero-alloc-after-build, no cross-view aliasing). Empty span if the
    // group is absent or the table is RED.
    [[nodiscard]] std::span<group_slice const> group_slices(std::uint16_t no_tag) const noexcept
        [[clang::lifetimebound]];

private:
    [[nodiscard]] static std::size_t overlay_cap_for(std::size_t n) noexcept;
    void build(frame_view const& frame) noexcept;  // shared build impl (both ctors)

    std::byte const* frame_base_ = nullptr;  // for group_slice (ptr,len)
    Config cfg_{};                           // caller-tunable caps (FR-015 / [2b §1.2])
    void const* opaque_dict_ = nullptr;
    group_member_fn_t group_member_fn_ = nullptr;
    std::pmr::vector<entry> entries_;
    // Open-address robin-hood overlay: slot value = index into entries_ + 1
    // (0 = empty). Holds the FIRST occurrence per tag.
    std::pmr::vector<std::uint32_t> overlay_;
    core::expected_t<void> status_;
    // Lazy mr-backed group slices. Append-only and reserved once to the
    // entry-count upper bound, so no push_back ever reallocates — every
    // span handed out stays valid for the message lifetime even when
    // several distinct groups are accessed and held simultaneously
    // (the prior static thread_local clobbered across calls; this does not).
    struct group_span {
        std::uint16_t no_tag;
        std::uint32_t start;  // index into group_slices_
        std::uint32_t count;
    };
    mutable std::pmr::vector<group_slice> group_slices_;
    mutable std::pmr::vector<group_span> group_index_;
    mutable bool group_slices_reserved_ = false;
};

}  // namespace fixpp::wire
