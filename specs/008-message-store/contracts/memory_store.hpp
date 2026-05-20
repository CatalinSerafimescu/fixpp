// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/memory_store.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::MemoryStore.
// Anchor: .specify/2e-msgstore.md v0.4 §4.2. Catalogue S-012 (NEW done at
// this feature's Gate-B merge).
//
// `final` impl of MessageStore. Bounded(10_000)-per-direction default.
// One-PMR-allocation-at-construction (fixed-slot + fixed-slab) — store()
// performs ZERO allocator calls under capacity_policy::bounded after
// construction (FR-007 / I-10). capacity_policy::evict_oldest is
// UNREPRESENTABLE per [const §XV.15] (I-09).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory_resource>

#include <fixpp/session/message_store.hpp>

namespace fixpp::session {

// Closed 2-value enum + [[clang::enum_extensibility(closed)]] where supported
// + static_assert at every switch + runtime out-of-range-cast reject.
// `evict_oldest` is NOT a public name and NOT a numeric value (I-09).
enum class capacity_policy : std::uint8_t {
    bounded   = 0,   // overflow → store_capacity_exhausted (I-08)
    unbounded = 1,   // grows without limit; test/embedded only
};

class MemoryStore final : public MessageStore {
public:
    struct Config {
        capacity_policy             policy             = capacity_policy::bounded;
        std::size_t                 inbound_capacity   = 10'000;
        std::size_t                 outbound_capacity  = 10'000;
        std::size_t                 max_frame_bytes    = 256 * 1024; // 256 KiB default per design-doc §4.2 line 448
        std::pmr::memory_resource*  store_resource     = nullptr;  // null → engine-provided dedicated monotonic_buffer_resource (FR-026)
    };

    explicit MemoryStore(Config cfg) noexcept;
    ~MemoryStore() override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t seq,
          std::span<const std::byte> frame [[clang::lifetimebound]],
          direction_t dir) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t begin,
             seqnum_t end,
             direction_t dir,
             retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool increment) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept override;
};

}  // namespace fixpp::session
