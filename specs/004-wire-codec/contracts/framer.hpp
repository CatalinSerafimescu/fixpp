// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.2]. Authority: 2b-wire.md v0.2.
#pragma once
#include <cstddef>
#include <span>
#include <fixpp/core/error.hpp>   // core::expected_t
#include "view.hpp"

namespace fixpp::wire {

inline constexpr std::size_t default_max_frame_bytes = 256u * 1024u;  // 256 KiB

// Backed by SessionConfig::framer_carry_arena (SESSION lifetime, NOT
// per-message — carry spans messages). One alloc at construction; never
// reallocates; growth past max_frame_bytes -> wire_frame_too_large.
class pmr_carry_buffer { /* fixed-capacity PMR vector */ };

class frame_view : public View {
public:
    // bytes between 9=BodyLength's SOH and 10=CheckSum's first digit.
    [[nodiscard]] constexpr std::span<const std::byte>
    body() const noexcept [[clang::lifetimebound]];
    // NOTE: v0.1 checksum()/body_length() intentionally removed (Codex P3).
};

class Framer {
public:
    struct Config {
        std::size_t max_frame_bytes = default_max_frame_bytes;
        // CheckSum verification is MANDATORY and not configurable ([2b §2]).
    };
    explicit Framer(Config c = {}) noexcept;

    [[nodiscard]] core::expected_t<std::span<frame_view>>
    feed(std::span<const std::byte> incoming [[clang::lifetimebound]],
         pmr_carry_buffer&          carry    [[clang::lifetimebound]],
         std::span<frame_view>      out      [[clang::lifetimebound]]) noexcept
        [[clang::lifetimebound]];

    [[nodiscard]] std::size_t pending_bytes() const noexcept;
};

}  // namespace fixpp::wire
