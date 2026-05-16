// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/lifetime_trap_test.cpp — T015 (US1, seam #7).
// The View flyweight's debug generation-token trap: a captured token whose
// pool generation no longer matches the live generation is a
// use-after-buffer-reuse and MUST trap (std::abort — it sits inside the
// noexcept parse->fromApp window, it does NOT throw). Release strips the
// token entirely so a View is exactly {data, len} and trivially copyable.
//
// Active here: the generation MECHANISM (debug) + the release strip
// invariant. The end-to-end "a parser-minted view over a recycled per-
// message arena traps" is DISABLED pending the three-arena pool wiring
// (seam #13 / T018) — currently parser views carry the untracked pool 0
// sentinel, which by design never traps. That gap is the honest red marker.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <fixpp/wire/field_view.hpp>
#include <fixpp/wire/view.hpp>

namespace {

// Release strip + flyweight invariant ([2b §4.1]). A View must stay
// trivially copyable in every build config (debug carries the 4-byte token;
// release strips it).
static_assert(std::is_trivially_copyable_v<fixpp::wire::View>);
static_assert(std::is_trivially_copyable_v<fixpp::wire::field_view>);
#ifdef NDEBUG
// Release: exactly {data ptr, length}, no generation member.
static_assert(sizeof(fixpp::wire::View) == sizeof(std::byte const*)
                                           + sizeof(std::size_t));
#endif

#ifndef NDEBUG
// Test probe: exposes View's protected ctor + check_alive so the trap
// mechanism can be exercised directly (it is not yet wired into the parse
// accessors — that wiring is seam #13 / T018, see DISABLED test below).
struct probe : fixpp::wire::View {
    probe(std::byte const* d, std::size_t n,
          fixpp::wire::detail::generation_token g) noexcept
        : fixpp::wire::View{d, n, g} {}
    using fixpp::wire::View::check_alive;
};

TEST(WireLifetimeTrapDeath, StaleGenerationAborts) {
    constexpr std::uint16_t kPool = 7;  // any tracked (non-zero) pool id
    auto tok = fixpp::wire::detail::current_pool_token(kPool);
    std::byte storage{};
    probe v{&storage, 1, tok};

    // Same generation: alive, no trap.
    v.check_alive();

    // The arena recycles its backing storage -> bump the pool generation.
    // The view still holds the previous generation: next check must abort.
    (void)fixpp::wire::detail::bump_pool_generation(kPool);
    EXPECT_DEATH(v.check_alive(), "");
}

TEST(WireLifetimeTrap, UntrackedPoolNeverTraps) {
    // pool_id 0 is the "untracked" sentinel — framer-emitted views currently
    // carry it; it must never trap even after a generation bump elsewhere.
    fixpp::wire::detail::generation_token untracked{};  // pool_id == 0
    std::byte storage{};
    probe v{&storage, 1, untracked};
    (void)fixpp::wire::detail::bump_pool_generation(0);
    v.check_alive();  // no abort
    SUCCEED();
}

// RED marker: the use-after-buffer-reuse trap is not yet wired into the
// parser's value accessors (field_view::bytes()/as_string() do not call
// check_alive(), and parser views carry the untracked pool 0 token). Enable
// when the three-arena per-message pool is wired (seam #13 / T018) so a
// recycled arena invalidates outstanding field_views end-to-end.
TEST(WireLifetimeTrapDeath, DISABLED_ParserViewTrapsAfterArenaRecycle) {
    FAIL() << "pending three-arena pool wiring (seam #13 / T018)";
}
#else
TEST(WireLifetimeTrap, ReleaseStripsTokenNoTrapMachinery) {
    // In release check_alive() is a constexpr no-op; nothing to abort on.
    SUCCEED();
}
#endif

}  // namespace
