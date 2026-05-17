// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.1]. Pins the public surface for
// /tasks + the flyweight_shape tests. Authority: .specify/2b-wire.md v0.2.
// This is not the implementation; src/include carries the real header.
#pragma once
#include <cstddef>
#include <span>

namespace fixpp::wire {

namespace detail {
// 4-byte POD (pool_id:uint16, gen:uint16). Release: zero-sized via
// [[no_unique_address]] + #ifndef NDEBUG. Debug: traps in check_alive().
struct generation_token { /* (pool_id, gen) */ };
}  // namespace detail

class View {
public:
    constexpr View() noexcept = default;

    [[nodiscard]] constexpr std::span<const std::byte>
    bytes() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] constexpr bool empty() const noexcept;

protected:
    constexpr View(std::byte const* data [[clang::lifetimebound]],
                    std::size_t len,
                    detail::generation_token gen) noexcept;
#ifndef NDEBUG
    void check_alive() const noexcept;   // traps on use-after-buffer-reuse
#else
    constexpr void check_alive() const noexcept {}
#endif
private:
    std::byte const* data_ {};
    std::size_t      len_  {};
#ifndef NDEBUG
    detail::generation_token gen_ {};
#endif
};

}  // namespace fixpp::wire
