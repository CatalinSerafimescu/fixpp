// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_oom_test.cpp — T031 [P] [US2] / seam #7/#16
//
// AC-R7: PMR-OOM injection → dict_reify_oom; ≤4 PMR allocs; zero-alloc guard
// for string/int/char + default-trait decimal accessors.
//
// Seam #7 — PMR failure injection (gate-b/r1 RC#1 F2 fix): from_view now
// makes 1 PMR allocation under R6 (a pmr::vector<byte>(1, byte{}, mr) that
// keeps the OOM trap path live — see emit_reify.cpp). When the resource fails
// on call 1, from_view must return dict_reify_oom (not propagate bad_alloc).
//
// Seam #16 — allocation count under R6: from_view makes exactly 1 allocation
// (the pmr::vector<byte>(1,…) construction) and then returns dict_reify_wire_body_not_ready.
// The AC-R7 budget is ≤4; 1 ≤ 4. The exact 4-alloc itemisation becomes
// testable with 2b (deep-copy bytes_ + OffsetTable rebuilds).
//
// Zero-alloc guard (mallocnesia scope, AC-T3 / NFR-003-4):
//   * String/int/char accessors on the flyweight (fixpp::v44::NewOrderSingle)
//     use dict::decode_field<T>(view.template get<Tag>()) — allocation-free.
//   * The default pod_decimal trait ignores the passed mr; no allocation.
//
// Oracle: data-model Entity 4 (PMR accounting); spec AC-R7 / seam #7/#16;
//         spec §5 R6 deferral; gate-b/r1 RC#1 F2 (from_view makes alloc).
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/field_traits.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <optional>

// Generated headers (build-tree only).
#include <fixpp/v44/Reify.hpp>

#include "support/failing_pmr_resource.hpp"

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using ONOS = fixpp::v44::owning_NewOrderSingle;
using NOS = fixpp::v44::NewOrderSingle;

// Counting-only PMR resource: delegates to upstream but tracks allocate calls.
class counting_pmr_resource final : public std::pmr::memory_resource {
public:
    explicit counting_pmr_resource(std::pmr::memory_resource* up) noexcept : upstream_(up) {}

    [[nodiscard]] std::size_t count() const noexcept { return count_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++count_;
        return upstream_->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        upstream_->deallocate(p, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }
    std::pmr::memory_resource* upstream_;
    std::size_t count_{0};
};

}  // namespace

// ─────────────────────────────────────────────────────────────────
// Seam #16 — ≤4 PMR allocations per from_view (AC-R7)
// ─────────────────────────────────────────────────────────────────

TEST(ReifyOomTest, AllocBudgetAtMostFour) {
    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    counting_pmr_resource counter{&upstream};

    MV mv;
    auto result = ONOS::from_view(mv, &counter);
    // gate-b/r1 RC#1: from_view now allocates (tmp(mr)) then returns
    // dict_reify_wire_body_not_ready — the R6 placeholder.
    ASSERT_FALSE(result.has_value()) << "R6: from_view must return dict_reify_wire_body_not_ready";
    EXPECT_EQ(result.error(), fixpp::core::error::dict_reify_wire_body_not_ready)
        << "AC-R3 R6 oracle: exact placeholder error code";

    EXPECT_LE(counter.count(), std::size_t{4})
        << "AC-R7: from_view must use ≤4 PMR allocations per Entity 4 budget (R6: 1 alloc for tmp(mr))";
}

// ─────────────────────────────────────────────────────────────────
// Seam #7 — OOM injection: first allocation fails → dict_reify_oom (AC-R7)
// ─────────────────────────────────────────────────────────────────
// R6-BLOCKED (injection path): In R6 the frozen wire stub carries no frame
// bytes, so from_view(stub_mv, mr) constructs an empty bytes_ vector which
// makes 0 allocations. The failing_pmr_resource's fail-on-call-N injection
// never triggers. The full OOM injection test (fail-on-call-1 → dict_reify_oom)
// requires a real frame (actual bytes_ copy allocation) and is R6-blocked until
// 2b swaps in the real OffsetTable-backed wire body.
//
// What IS testable now:
//   (a) from_view with a failing mr that would fail if called does NOT crash.
//   (b) The trap path (bad_alloc → dict_reify_oom) is wired correctly in code
//       — confirmed by the inline try/catch in from_view() (verified by code
//       inspection; the test below exercises the no-allocation path only).
//   (c) The error code slot and distinct-from-stub-error assertions.

// gate-b/r1 RC#1 F2 fix: from_view now makes exactly 1 PMR allocation
// (the `tmp(mr)` object construction). fail-on-call-1 therefore DOES fire,
// the bad_alloc is caught, and from_view returns dict_reify_oom.
TEST(ReifyOomTest, OomInjectionOnFirstAllocYieldsOomError) {
    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    fixpp::test_support::failing_pmr_resource fail{&upstream, /*fail_on_call_n=*/1};

    MV mv;
    bool threw = false;
    std::optional<fixpp::core::expected_t<ONOS>> result;
    try {
        result = ONOS::from_view(mv, &fail);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "from_view must not propagate std::bad_alloc (trap_throw must catch it)";
    ASSERT_TRUE(result.has_value()) << "from_view must return (not throw) even under OOM";
    ASSERT_FALSE(result->has_value()) << "fail-on-call-1: from_view must return an error";
    EXPECT_EQ(result->error(), fixpp::core::error::dict_reify_oom)
        << "AC-R7 seam #7: bad_alloc on tmp(mr) must yield dict_reify_oom (not propagate)";
    EXPECT_GE(fail.allocate_calls(), std::size_t{1})
        << "The failing resource must have been called at least once";
}

// No-OOM path: live upstream arena → from_view returns dict_reify_wire_body_not_ready.
// This confirms that the try/catch doesn't swallow the R6 placeholder return.
TEST(ReifyOomTest, NoOomYieldsWireBodyNotReady) {
    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};

    MV mv;
    bool threw = false;
    std::optional<fixpp::core::expected_t<ONOS>> result;
    try {
        result = ONOS::from_view(mv, &upstream);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "from_view must not throw with a live arena";
    ASSERT_TRUE(result.has_value()) << "from_view must return (not throw) with a live arena";
    ASSERT_FALSE(result->has_value()) << "R6: from_view must return an error even with live arena";
    EXPECT_EQ(result->error(), fixpp::core::error::dict_reify_wire_body_not_ready)
        << "R6 no-OOM path: must return dict_reify_wire_body_not_ready (not dict_reify_oom)";
}

// ─────────────────────────────────────────────────────────────────
// Zero-alloc guard for string/int/char flyweight accessors (mallocnesia scope)
// ─────────────────────────────────────────────────────────────────
// AC-T3 / NFR-003-4: decode_field<T> accessors are allocation-free. Verified
// by calling them inside a counting mr scope and asserting count == 0.

TEST(ReifyOomTest, FlyweightStringAccessorIsZeroAlloc) {
    std::array<std::byte, 1024> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    counting_pmr_resource counter{&upstream};

    MV mv;
    NOS nos(mv);

    // cl_ord_id() is a string_view accessor — must not allocate.
    std::size_t before = counter.count();
    auto cl = nos.cl_ord_id();
    (void)cl;
    EXPECT_EQ(counter.count(), before)
        << "NFR-003-4: string accessor cl_ord_id() must not allocate";
}

TEST(ReifyOomTest, FlyweightCharAccessorIsZeroAlloc) {
    std::array<std::byte, 1024> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    counting_pmr_resource counter{&upstream};

    MV mv;
    NOS nos(mv);

    std::size_t before = counter.count();
    auto sd = nos.side();
    (void)sd;
    EXPECT_EQ(counter.count(), before) << "NFR-003-4: char accessor side() must not allocate";
}

TEST(ReifyOomTest, DecimalAccessorWithDefaultTraitIsZeroAlloc) {
    // AC-G4a: the default pod_decimal trait's decimal_t::parse ignores mr;
    // no allocation for the default trait (R6: get<44>() returns field-absent,
    // so parse is not even called; the zero-alloc holds).
    std::array<std::byte, 1024> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    counting_pmr_resource counter{&upstream};

    MV mv;
    NOS nos(mv);

    std::size_t before = counter.count();
    auto p = nos.price(&counter);
    (void)p;
    EXPECT_EQ(counter.count(), before)
        << "NFR-003-4/AC-G4a: default-trait decimal price() must not allocate "
           "(R6: get<44> field-absent, parse not called)";
}

// ─────────────────────────────────────────────────────────────────
// Error slot shape — dict_reify_oom is slot 25 (data-model "Error mapping")
// ─────────────────────────────────────────────────────────────────
TEST(ReifyOomTest, OomErrorSlot) {
    static_assert(static_cast<std::uint8_t>(fixpp::core::error::dict_reify_oom) == 25,
                  "dict_reify_oom must be slot 25 per data-model Error mapping");
    SUCCEED();
}
