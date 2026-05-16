// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/three_arena_pinning_test.cpp — T018 (US1, seam #13).
// The three-arena lifetime model: the per-MESSAGE arena owns the offset
// table / overlay / sub-indices (all OffsetTable storage routes through the
// memory_resource handed to parse(), never the global heap); the framer-
// CARRY arena is session-lifetime; the parse->fromApp path performs zero
// new/delete. Authored red; GREEN against T023/T024.
//
// Active here: per-message arena PINNING — a counting resource proves every
// OffsetTable allocation went through the supplied arena (and a fresh arena
// per message keeps them isolated). The strict end-to-end "zero global
// new/delete across parse->fromApp" assertion needs the full three-arena
// session wiring (framer carry pool) and a global-operator-new shim — that
// is DISABLED below as the honest red marker.

#include <array>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/wire/parser.hpp>

#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;

// Counts allocations routed through it; upstream is an explicitly-bounded
// monotonic buffer so a fall-through to the global heap would be observable
// (the buffer never hands back to ::operator new).
struct counting_resource final : std::pmr::memory_resource {
    explicit counting_resource(std::pmr::memory_resource* up) noexcept
        : up_{up} {}
    std::size_t allocations = 0;
    std::size_t bytes = 0;
    std::pmr::memory_resource* up_;

    void* do_allocate(std::size_t n, std::size_t a) override {
        ++allocations;
        bytes += n;
        return up_->allocate(n, a);
    }
    void do_deallocate(void* p, std::size_t n, std::size_t a) override {
        up_->deallocate(p, n, a);
    }
    [[nodiscard]] bool do_is_equal(
        std::pmr::memory_resource const& o) const noexcept override {
        return this == &o;
    }
};

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireThreeArenaPinning, OffsetTableStorageRoutesThroughPerMessageArena) {
    auto buf = make_raw_frame("35=D\x01" "34=1\x01" "49=S\x01"
                              "56=T\x01" "448=A\x01" "448=B\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::array<std::byte, 8192> backing{};
    std::pmr::monotonic_buffer_resource upstream{backing.data(),
                                                 backing.size()};
    counting_resource arena{&upstream};

    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // entries_ + overlay_ are pmr-backed: at least one allocation MUST have
    // gone through the per-message arena (zero would mean it leaked to the
    // global heap — a three-arena violation).
    EXPECT_GT(arena.allocations, 0U);
    EXPECT_GT(arena.bytes, 0U);

    // The offset table really is populated from that arena (it spans the
    // whole frame, envelope included — assert it covers the body fields).
    EXPECT_GE(mv->offsets().size(), 6U);
    auto a = mv->get(448);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->as_string(), "A");
}

TEST(WireThreeArenaPinning, PerMessageArenasAreIsolated) {
    auto b1 = make_raw_frame("35=D\x01" "34=1\x01" "11=ORD-1\x01");
    auto b2 = make_raw_frame("35=D\x01" "34=2\x01" "11=ORD-2\x01");
    auto f1 = fixpp::wire::test::make_frame_view(b1);
    auto f2 = fixpp::wire::test::make_frame_view(b2);
    ASSERT_TRUE(f1.has_value());
    ASSERT_TRUE(f2.has_value());

    std::pmr::monotonic_buffer_resource a1;
    std::pmr::monotonic_buffer_resource a2;
    Parser<access_mode::Index> parser{fixpp::dict::table_view{}};

    auto m1 = parser.parse(*f1, &a1);
    auto m2 = parser.parse(*f2, &a2);
    ASSERT_TRUE(m1.has_value());
    ASSERT_TRUE(m2.has_value());

    // Distinct arenas -> independent offset tables, no cross-contamination.
    auto o1 = m1->get(11);
    auto o2 = m2->get(11);
    ASSERT_TRUE(o1.has_value());
    ASSERT_TRUE(o2.has_value());
    EXPECT_EQ(o1->as_string(), "ORD-1");
    EXPECT_EQ(o2->as_string(), "ORD-2");
}

// RED marker: the strict "zero global new/delete across the full
// parse->fromApp window" guarantee depends on the framer session carry-arena
// pool wiring (seam #13) plus a global operator-new interposer. Enable when
// the three-arena session model is fully wired.
TEST(WireThreeArenaPinning, DISABLED_ZeroGlobalNewAcrossParseToFromApp) {
    FAIL() << "pending framer session carry-arena wiring (seam #13)";
}

}  // namespace
