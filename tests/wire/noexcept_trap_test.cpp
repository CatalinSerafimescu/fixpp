// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/noexcept_trap_test.cpp — T020 (US1, FR-013, [arch §5.3]).
// A throwing 2a/2c trait wrapper MUST trap (be translated to an expected_t
// error) and MUST NOT propagate across the noexcept parse->fromApp window.
// New per /analyze finding C1. Authored red; GREEN against the T024/T027
// core::detail::trap_throw fence. Holds in both debug and release.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/decimal_helpers.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::core::error;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// Structural fence: the trait-crossing accessor and the parse entry point
// sit inside a noexcept window — the compiler must enforce it.
static_assert(noexcept(std::declval<MessageView<access_mode::Index>>().get_decimal(44, nullptr)),
              "get_decimal must be noexcept (FR-013 parse->fromApp window)");
static_assert(noexcept(std::declval<Parser<access_mode::Index>>().parse(
                  std::declval<fixpp::wire::frame_view const&>(), nullptr)),
              "Parser::parse must be noexcept");
static_assert(noexcept(fixpp::core::detail::trap_throw([] { return 0; })),
              "trap_throw is the noexcept fence itself");

std::vector<std::byte> make_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireNoexceptTrap, ThrowingTraitIsTranslatedNotPropagated) {
    // Simulate a throwing custom FIXPP_DECIMAL_T trait call. trap_throw MUST
    // catch it and yield an expected_t error — never let it escape.
    bool escaped = false;
    fixpp::core::expected_t<int> r{0};
    try {
        r = fixpp::core::detail::trap_throw(
            []() -> int { throw std::runtime_error{"trait blew up"}; });
    } catch (...) {
        escaped = true;  // MUST NOT happen — would breach the noexcept window
    }
    EXPECT_FALSE(escaped) << "an exception escaped the trap_throw fence";
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input);

    auto oom = fixpp::core::detail::trap_throw([]() -> int { throw std::bad_alloc{}; });
    ASSERT_FALSE(oom.has_value());
    EXPECT_EQ(oom.error(), error::out_of_memory);
}

TEST(WireNoexceptTrap, GetDecimalStaysInsideTheWindow) {
    // End-to-end: the trait-crossing accessor returns an error (not throws)
    // even for a value the trait rejects; the call itself is noexcept.
    auto buf = make_frame(
        "35=D\x01"
        "34=1\x01"
        "44=not-a-number\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    bool escaped = false;
    try {
        auto d = mv->get_decimal(44, &arena);
        EXPECT_FALSE(d.has_value()) << "an unparseable FLOAT must be an error, not a value";
    } catch (...) {
        escaped = true;
    }
    EXPECT_FALSE(escaped) << "get_decimal must not propagate an exception";
}

}  // namespace
