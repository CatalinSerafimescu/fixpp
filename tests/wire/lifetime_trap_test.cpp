// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/lifetime_trap_test.cpp — T015 (US1, seam #7).
// The View flyweight's debug generation-token trap: a captured token whose
// pool generation no longer matches the live generation is a
// use-after-buffer-reuse and MUST trap (std::abort — it sits inside the
// noexcept parse->fromApp window, it does NOT throw). Release strips the
// token entirely so a View is exactly {data, len} and trivially copyable.
//
// Active here: the generation MECHANISM (debug) + the release strip
// invariant + the end-to-end "parser-minted view over a recycled per-
// message arena traps" test ([2b §6.4] / FR-016, gate-b/r1 wiring).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/wire/field_view.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/view.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

// Release strip + flyweight invariant ([2b §4.1]). A View must stay
// trivially copyable in every build config (debug carries the 4-byte token;
// release strips it).
static_assert(std::is_trivially_copyable_v<fixpp::wire::View>);
static_assert(std::is_trivially_copyable_v<fixpp::wire::field_view>);
#ifdef NDEBUG
// Release: exactly {data ptr, length}, no generation member.
static_assert(sizeof(fixpp::wire::View) == sizeof(std::byte const*) + sizeof(std::size_t));
#endif

#ifndef NDEBUG
// Test probe: exposes View's protected ctor + check_alive so the trap
// mechanism can be exercised directly (isolated mechanism test).
struct probe : fixpp::wire::View {
    probe(std::byte const* d, std::size_t n, fixpp::wire::detail::generation_token g) noexcept
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
    // pool_id 0 is the "untracked" sentinel; it must never trap even after a
    // generation bump elsewhere (e.g. from a different Framer's recycle).
    fixpp::wire::detail::generation_token untracked{};  // pool_id == 0
    std::byte storage{};
    probe v{&storage, 1, untracked};
    (void)fixpp::wire::detail::bump_pool_generation(0);
    v.check_alive();  // no abort
    SUCCEED();
}

// End-to-end trap test (gate-b/r1: pool token now threaded from Framer into
// frame_view and MessageView; View::bytes() calls check_alive(); Framer
// exposes recycle_pool() to simulate arena reset). A MessageView captured
// past the Framer's recycle_pool() call must trap on its next bytes() /
// msg_type() / get() access ([2b §6.4] / FR-016).
TEST(WireLifetimeTrapDeath, ParserViewTrapsAfterArenaRecycle) {
    // Build a checksum-valid FIX frame.
    std::string body{
        "35=D\x01"
        "34=1\x01"};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned chksum = 0;
    for (unsigned char c : pre) {
        chksum += c;
    }
    char chk[16];
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", chksum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> raw(full.size());
    std::memcpy(raw.data(), full.data(), full.size());

    // Use the real Framer (which now assigns a non-zero pool_id_).
    fixpp::wire::Framer framer;
    fixpp::wire::pmr_carry_buffer carry{1024, std::pmr::get_default_resource()};
    std::array<fixpp::wire::frame_view, 4> out_buf{};
    auto frames = framer.feed(raw, carry, std::span<fixpp::wire::frame_view>{out_buf});
    ASSERT_TRUE(frames.has_value()) << "feed must succeed";
    ASSERT_GE(frames->size(), 1U) << "must have produced at least one frame";

    fixpp::wire::frame_view fv = (*frames)[0];

    // Parse the frame into a MessageView (Index mode).
    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{};
    auto mv_result = parser.parse(fv, &arena);
    ASSERT_TRUE(mv_result.has_value()) << "parse must succeed";
    auto mv = *mv_result;

    // Before recycling: bytes() is safe (same generation).
    EXPECT_FALSE(mv.bytes().empty()) << "MessageView must have bytes";

    // Simulate per-message arena recycle: bump the pool's generation.
    framer.recycle_pool();

    // After recycling: any bytes() call must abort (use-after-buffer-reuse).
    EXPECT_DEATH(mv.bytes(), "") << "must trap after pool recycle";
}
#else
TEST(WireLifetimeTrap, ReleaseStripsTokenNoTrapMachinery) {
    // In release check_alive() is a constexpr no-op; nothing to abort on.
    SUCCEED();
}
#endif

}  // namespace
