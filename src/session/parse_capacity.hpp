#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/parse_capacity.hpp — the per-session parse buffer B(L) and the entry cap
// N(L) every inbound parse runs under, derived from the session's inbound limit L
// (093-inbound-frame-dispositions, data-model E-2, research R-3). Kept apart from
// inbound_limit.hpp so a wire-level test can size a buffer the session's way without
// the session's configuration headers. Private: not installed.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/wire/offset_table.hpp>

namespace fixpp::session {

// L when no MaxMessageSize(383) is advertised (spec FR-010).
inline constexpr std::uint32_t kDefaultInboundLimit = 65536U;

namespace detail {

// MSVC's debug STL draws one container proxy from a pmr container's allocator when the
// container is constructed (include/fixpp/core/pmr_arena_upstream.hpp). Recipe (research
// R-3, T014): on the MSVC debug sandbox, log each request a pmr_carry_buffer makes of
// a tracking resource over new_delete, and find the smallest slack s for which a
// monotonic resource over exactly capacity + s serves the carry without an upstream
// call.
inline constexpr std::size_t kContainerSlack = 16;

// The pmr containers one inbound parse constructs over the parse buffer, each drawing a
// kContainerSlack proxy on MSVC debug. Recipe (research R-3): count the std::pmr::vector
// members MessageView<Index> and OffsetTable construct from the parse resource, times one
// plus the number of times Session's parse path moves the view before a callback or the
// validator reads it (MSVC's vector move constructor draws a fresh proxy). Re-derive at
// a change to either class or to Parser::parse's reserve overload.
inline constexpr std::size_t kParseContainers = 10;

// The padding before a proxy: it is pointer-aligned, so after a request of smaller
// alignment it is placed at most alignof(void*) - 1 bytes further on.
inline constexpr std::size_t kProxyAlignPad = alignof(void*) - 1U;

// Aligning the entry block and the overlay block inside one monotonic resource: at most
// each block's alignment minus one.
inline constexpr std::size_t kAlignPad =
    (alignof(fixpp::wire::OffsetTable::entry) - 1U) + (alignof(std::uint32_t) - 1U);

// The room the buffer keeps for lazy reads inside a callback (contract C-3 I-5). Its
// sizing condition (data-model E-2): for every frame the base delivers, B(L) minus that
// frame's up-front reserve and its parse leaves at least the room the base's stack
// parse arena leaves after the same parse. This term is set to that arena's size, so
// the room after a parse here is at least the base arena's whole size and the condition
// holds at every L. Re-derive the base size with
// `git log -S kInboundParseArena -- src/session/session.cpp`: the newest commit it lists
// removed the arena, and that commit's parent defines it (data-model E-2 names the arena).
inline constexpr std::size_t kCallbackReadHeadroom = 16384;

// The capacities every inbound parse runs under, from the session's inbound limit L.
// A struct so OffsetTable can befriend it: the overlay term calls the table's own
// private overlay_cap_for, so B(L) budgets whatever overlay the table assigns.
struct parse_capacity {
    // N(L): the most fields a frame of L bytes holds at the densest layout, three bytes
    // a field ("1=<SOH>"), plus one. It is the entry cap of every inbound parse.
    [[nodiscard]] static constexpr std::size_t entry_cap_for(std::uint32_t limit) noexcept {
        return (std::size_t{limit} / 3U) + 1U;
    }

    // The entries one parse reserves up front: the same densest-layout count over the
    // frame's own bytes, at most the parse's entry cap.
    [[nodiscard]] static constexpr std::size_t reserve_for(std::size_t entry_cap,
                                                           std::size_t frame_bytes) noexcept {
        return std::min(entry_cap, (frame_bytes / 3U) + 1U);
    }

    // The overlay slots a table of n entries assigns.
    [[nodiscard]] static std::size_t overlay_cap_for(std::size_t n) noexcept {
        return fixpp::wire::OffsetTable::overlay_cap_for(n);
    }

    // B(L), the per-session parse buffer (data-model E-2): N(L) entries, the overlay for
    // N(L), and the three named terms plus the container slack.
    [[nodiscard]] static std::size_t buffer_bytes(std::uint32_t limit) noexcept {
        std::size_t const n = entry_cap_for(limit);
        return (sizeof(fixpp::wire::OffsetTable::entry) * n) +
               (sizeof(std::uint32_t) * overlay_cap_for(n)) + kAlignPad + kCallbackReadHeadroom +
               (kParseContainers * (kContainerSlack + kProxyAlignPad));
    }
};

}  // namespace detail

}  // namespace fixpp::session
