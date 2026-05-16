// SPDX-License-Identifier: AGPL-3.0-or-later
//
// FROZEN CONTRACT — surface locked by [2b §4.3] / [2b §4.7].
// Do NOT extend. 2b replaces the BODY, not the SURFACE (R6 / research.md D-2).
//
// Phase 1 contract for the header SHIPPED at
// include/fixpp/wire/message_view_contract.hpp. include/fixpp/wire/ is
// currently empty (.gitkeep only); the 2b wire feature is downstream of
// dictionary/ in module order, but generated typed messages + owning_<Msg>
// compile against this surface. This stub lets codegen output compile and the
// FULL AC-G*/AC-R*/AC-D*/AC-C*/conformance suite run in THIS PR (DoD §12).
// Drift guard: tests/codegen/flyweight_shape_test.cpp static_asserts the exact
// member signatures + the sizeof(MessageView<Index>) == pointer invariant
// (AC-G7 / I-1 / I-12). When 2b lands it swaps in the real OffsetTable-backed
// body against THIS surface; any signature drift fails the contract test in
// 2b's own Gate B.
#pragma once
#include <fixpp/core/error.hpp>   // fixpp::core::expected_t, fixpp::core::error
#include <cstdint>
#include <span>
#include <string_view>

namespace fixpp::wire {

enum class access_mode { Index /* eager offset table — [2b §4.3] */ };

// Borrowed view over one field's bytes ([2b §4.3]).
class field_view {
public:
    [[nodiscard]] std::span<std::byte const> bytes() const noexcept;
    [[nodiscard]] std::string_view as_string() const noexcept;
};

// Borrowed view over a repeating group ([2b §4.7]). T = generated Leg-style
// flyweight; operator[] yields a T over the i-th entry.
template <class T>
class group_view {
public:
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] T operator[](std::size_t i) const noexcept [[clang::lifetimebound]];
};

template <access_mode Mode>
class MessageView {
public:
    // The ONLY typed-tag accessor [2b §4.3] exposes (lines 281–288).
    template <std::uint16_t Tag>
    [[nodiscard]] expected_t<field_view> get() const noexcept [[clang::lifetimebound]];

    [[nodiscard]] expected_t<field_view>
        get(std::uint16_t tag) const noexcept [[clang::lifetimebound]];

    template <std::uint16_t NoTag, class T>
    [[nodiscard]] group_view<T> group() const noexcept [[clang::lifetimebound]];

    [[nodiscard]] auto unknown_fields() const noexcept [[clang::lifetimebound]];

    // [2b §6.4] debug generation-counter trap: access after the originating
    // frame buffer is reused traps in debug builds (AC-G8). Release: UB.
};

}  // namespace fixpp::wire
