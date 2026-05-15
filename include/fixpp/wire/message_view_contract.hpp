// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/message_view_contract.hpp
//
// FROZEN CONTRACT — surface locked by [2b §4.3] / [2b §4.7].
// Do NOT extend. 2b replaces the BODY, not the SURFACE (R6 / research.md D-2).
//
// VENDORED FROZEN R6 STUB (003-dictionary-codegen). include/fixpp/wire/ was
// empty (.gitkeep only); the 2b wire feature is downstream of dictionary/ in
// module order, but generated typed messages + owning_<Msg> compile against
// this surface. This stub lets codegen output compile and the AC-G*/AC-R*/
// AC-D*/AC-C*/conformance suite run in THIS PR (DoD §12). Drift guard:
// tests/codegen/flyweight_shape_test.cpp static_asserts the exact member
// signatures + the sizeof(MessageView<Index>) == pointer invariant
// (AC-G7 / I-1 / I-12). When 2b lands it swaps in the real OffsetTable-backed
// body against THIS surface; any signature drift fails the contract test in
// 2b's own Gate B. The vendored body stays a FROZEN R6 temporary (dictionary
// does NOT become wire/'s owner).
//
// Bridge surface (arch §2.4 v0.3 dual-compile carve-out, RC#3): NOT a cyclic
// dictionary→wire module link edge — check_layers.py BRIDGE_EXEMPT_INCLUDES.
//
// Oracle: specs/003-dictionary-codegen/contracts/wire_message_view_contract.hpp;
// data-model Entity 9 (I-12).
#pragma once
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>  // fixpp::core::expected_t, fixpp::core::error
#include <span>
#include <string_view>

namespace fixpp::wire {

// FROZEN SURFACE: the base type, member shapes and static-ness below are
// locked by [2b §4.3]/[2b §4.7]; 2b owns the body and may NOT change the
// surface (R6 / I-12 drift guard, seam #18). clang-tidy "shape improvement"
// fixes (smaller enum base, convert-stub-bodies-to-static) are therefore
// suppressed for this vendored bridge stub ONLY — fixing them would break
// fidelity to the locked 2b contract. Tracked with the arch §2.4 v0.3 bridge
// carve-out (RC#3 / T049 / T051).
// NOLINTNEXTLINE(performance-enum-size)
enum class access_mode { Index /* eager offset table — [2b §4.3] */ };

// Borrowed view over one field's bytes ([2b §4.3]).
//
// FROZEN SURFACE / STUB BODY: the [2b §4.3]-locked surface declares NO data
// members or constructors, so this vendored R6 stub cannot carry a parsed
// frame — bodies are inert (empty span / empty view). It exists so codegen
// output and the shape/static_assert suites COMPILE in this PR; the running
// AC-G*/AC-R*/conformance suites are R6-blocked until 2b swaps in the real
// OffsetTable-backed body against THIS exact surface (spec §11 R6 / D-2). The
// inline keyword keeps the stub header-only (no src/wire lib, no cyclic
// dictionary→wire link edge — arch §2.4 v0.3 bridge carve-out, RC#3).
class field_view {
public:
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] std::span<std::byte const> bytes() const noexcept { return {}; }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] std::string_view as_string() const noexcept { return {}; }
};

// Borrowed view over a repeating group ([2b §4.7]). T = generated Leg-style
// flyweight; operator[] yields a T over the i-th entry.
template <class T>
class group_view {
public:
    [[nodiscard]] std::size_t size() const noexcept { return 0; }
    [[nodiscard]] T operator[](std::size_t /*i*/) const noexcept [[clang::lifetimebound]] {
        return T{};
    }
};

template <access_mode Mode>
class MessageView {
public:
    // The ONLY typed-tag accessor [2b §4.3] exposes (lines 281-288).
    // Stub body (see field_view banner): no frame state on the frozen
    // surface, so every lookup reports field-absent. 2b replaces the body.
    template <std::uint16_t Tag>
    [[nodiscard]] core::expected_t<field_view> get() const noexcept [[clang::lifetimebound]] {
        return std::unexpected{core::error::dict_xml_parse_failed};
    }

    [[nodiscard]] core::expected_t<field_view> get(std::uint16_t /*tag*/) const noexcept
        [[clang::lifetimebound]] {
        return std::unexpected{core::error::dict_xml_parse_failed};
    }

    template <std::uint16_t NoTag, class T>
    [[nodiscard]] group_view<T> group() const noexcept [[clang::lifetimebound]] {
        return group_view<T>{};
    }

    [[nodiscard]] auto unknown_fields() const noexcept [[clang::lifetimebound]] {
        return group_view<field_view>{};
    }

    // [2b §6.4] debug generation-counter trap: access after the originating
    // frame buffer is reused traps in debug builds (AC-G8). Release: UB.
};

}  // namespace fixpp::wire
