// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/view_test.cpp — T010 (Foundational).
// Pins the [2b §4.1] View base shape: release is trivially copyable and
// excludes the debug generation token from sizeof; debug embeds it. A
// public probe subclass exposes the protected ctor for the runtime checks.

#include <cstddef>
#include <span>
#include <type_traits>

#include <gtest/gtest.h>

#include <fixpp/wire/view.hpp>

namespace {

using fixpp::wire::View;
using fixpp::wire::detail::generation_token;

// Probe: a minimal concrete View exposing the protected ctor + check_alive.
class ProbeView : public View {
public:
    ProbeView(std::byte const* d, std::size_t n, generation_token g) noexcept
        : View{d, n, g} {}
    using View::check_alive;
};

static_assert(std::is_trivially_copyable_v<View>,
              "[2b §4.1] View must stay trivially copyable in all configs");
static_assert(std::is_nothrow_default_constructible_v<View>);

// generation_token is a 4-byte POD (pool_id:u16, gen:u16).
static_assert(sizeof(generation_token) == 4);
static_assert(std::is_trivially_copyable_v<generation_token>);

#ifdef NDEBUG
// Release: token stripped — View is exactly {data ptr, length}.
static_assert(sizeof(View) == sizeof(std::byte const*) + sizeof(std::size_t),
              "[2b §4.1] release View excludes the generation token");
#else
// Debug: token embedded for the use-after-buffer-reuse trap.
static_assert(sizeof(View) >= sizeof(std::byte const*) + sizeof(std::size_t)
                                   + sizeof(generation_token),
              "[2b §4.1] debug View embeds the generation token");
#endif

TEST(WireView, BytesAndEmpty) {
    std::byte buf[8]{};
    ProbeView v{buf, sizeof(buf), generation_token{}};
    EXPECT_EQ(v.bytes().size(), sizeof(buf));
    EXPECT_EQ(v.bytes().data(), buf);
    EXPECT_FALSE(v.empty());

    View def{};
    EXPECT_TRUE(def.empty());
    EXPECT_EQ(def.bytes().size(), 0u);
}

TEST(WireView, DefaultTokenNeverTraps) {
    std::byte buf[4]{};
    ProbeView v{buf, sizeof(buf), generation_token{}};
    v.check_alive();  // pool_id 0 == untracked: must not abort
    SUCCEED();
}

#ifndef NDEBUG
TEST(WireView, StaleGenerationTrapsInDebug) {
    std::byte buf[4]{};
    auto tok = fixpp::wire::detail::bump_pool_generation(7);  // gen for pool 7
    ProbeView live{buf, sizeof(buf), tok};
    live.check_alive();  // current generation: ok

    fixpp::wire::detail::bump_pool_generation(7);  // buffer reused
    ProbeView stale{buf, sizeof(buf), tok};
    EXPECT_DEATH(stale.check_alive(), "");  // use-after-buffer-reuse trap
}
#endif

}  // namespace
