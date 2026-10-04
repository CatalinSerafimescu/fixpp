#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/parse_capacity.hpp — the per-session parse buffer B(L) and the entry cap
// N(L) every inbound parse runs under, derived from the session's inbound limit L
// (093-inbound-frame-dispositions, data-model E-2, research R-3). Kept apart from
// inbound_limit.hpp so a wire-level test can size a buffer the session's way without
// the session's configuration headers. Private: not installed (no install() rule names
// src/).

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

// N(L): the most fields a frame of L bytes holds at the densest layout, three bytes a
// field ("1=<SOH>"), plus one. It is the entry cap of every inbound parse.
[[nodiscard]] constexpr std::size_t inbound_entry_cap_for(std::uint32_t limit) noexcept {
    return std::size_t{limit} / 3U + 1U;
}

// The overlay slots a table of n entries assigns: the same rule as the private
// OffsetTable::overlay_cap_for (src/wire/offset_table.cpp), which this must equal. If it
// budgeted fewer, the callback headroom would shrink by the difference unnoticed until
// the difference exceeded it. InboundFrameDispositionsQ11.
// TheBufferBudgetsTheOverlayTheTableAssigns compares the two at the default limit and
// at each end of the advertised range.
[[nodiscard]] constexpr std::size_t inbound_overlay_cap_for(std::size_t n) noexcept {
    std::size_t const want = ((n * 5U) / 4U) + 1U;
    std::size_t cap = 8U;
    while (cap < want) {
        cap <<= 1U;
    }
    return cap;
}

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
// parse arena leaves after the same parse. The base arena's whole size meets it at
// every L, since the room after a parse here is at least this term.
inline constexpr std::size_t kCallbackReadHeadroom = 16384;

// B(L), the per-session parse buffer (data-model E-2): N(L) entries, the overlay for
// N(L), and the three named terms plus the container slack.
[[nodiscard]] constexpr std::size_t inbound_parse_buffer_bytes(std::uint32_t limit) noexcept {
    std::size_t const n = inbound_entry_cap_for(limit);
    return sizeof(fixpp::wire::OffsetTable::entry) * n +
           sizeof(std::uint32_t) * inbound_overlay_cap_for(n) + kAlignPad + kCallbackReadHeadroom +
           kParseContainers * (kContainerSlack + kProxyAlignPad);
}

}  // namespace detail

}  // namespace fixpp::session
