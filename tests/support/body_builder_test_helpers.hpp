#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/body_builder_test_helpers.hpp
//
// Test-side helpers for driving a wire::body_builder to commit: byte-view
// conversions, the INV-4 "out untouched on failure" sentinel, commit wrappers,
// and a one-instance group arrangement. Header-only; every function is inline.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/body_builder.hpp>
#include <span>
#include <string>
#include <string_view>

namespace fixpp::test_support::body_builder_helpers {

// Size of the scratch `out` buffer every commit helper below commits into.
inline constexpr std::size_t kBufSize = 8192;

inline std::string bytes_to_string(std::span<const std::byte> b) {
    return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
}

inline std::span<const std::byte> octets(std::string_view sv) {
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
}

// Fill a byte span with a sentinel pattern so INV-4 "untouched on failure"
// can be asserted by comparing before/after.
inline void fill_sentinel(std::span<std::byte> s) {
    for (auto& b : s) b = std::byte{0xABU};
}

inline bool all_sentinel(std::span<const std::byte> s) {
    return std::ranges::all_of(s, [](std::byte b) { return b == std::byte{0xABU}; });
}

// Commits `b` and returns the body as a string, or the commit's error.
inline fixpp::core::expected_t<std::string> commit_body(fixpp::wire::body_builder& b) {
    std::array<std::byte, kBufSize> buf{};
    auto r = b.commit(std::span<std::byte>{buf});
    if (!r.has_value()) return std::unexpected(r.error());
    return bytes_to_string(*r);
}

// Commits `b`, which must succeed (a non-fatal failure otherwise), and returns
// the body, or "" when the commit was refused.
inline std::string expect_commit_ok(fixpp::wire::body_builder& b) {
    auto const r = commit_body(b);
    EXPECT_TRUE(r.has_value()) << "commit refused; error "
                               << (r.has_value() ? 0 : static_cast<int>(r.error()));
    return r.value_or(std::string{});
}

// The commit is refused with `want` and leaves `out` untouched.
inline void expect_commit_refused(fixpp::wire::body_builder& b, fixpp::core::error want) {
    std::array<std::byte, kBufSize> buf{};
    fill_sentinel(std::span<std::byte>{buf});
    auto r = b.commit(std::span<std::byte>{buf});
    ASSERT_FALSE(r.has_value()) << "commit accepted the body: " << bytes_to_string(*r);
    EXPECT_EQ(r.error(), want);
    EXPECT_TRUE(all_sentinel(std::span<const std::byte>{buf}))
        << "out must be untouched on a refused commit";
}

// Adds a closed group `no_tag` (delimiter AllocAccount(79)) with one populated
// instance.
inline void add_populated_group(fixpp::wire::body_builder& b, std::uint16_t no_tag) {
    auto g = b.group_begin(no_tag, 79);
    ASSERT_TRUE(g.has_value());
    auto e = g->add_entry();
    ASSERT_TRUE(e.has_value());
    ASSERT_TRUE(e->set_string(79, "A1").has_value());
    ASSERT_TRUE(b.group_end(*g).has_value());
}

}  // namespace fixpp::test_support::body_builder_helpers
