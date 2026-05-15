// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_oom_test.cpp — T031 [P] [US2] / seam #7/#16
//
// AC-R7: PMR-OOM injection → dict_reify_oom; ≤4 PMR allocs; zero-alloc guard
// for string/int/char + default-trait decimal accessors.
//
// Seam #7 — PMR failure injection: a failing_pmr_resource wrapping a live
// upstream is injected into from_view(src, mr). When the resource fails on
// call N, from_view must return dict_reify_oom (not propagate std::bad_alloc,
// not crash, not silently succeed).
//
// Seam #16 — ≤4-alloc budget: count the allocate() calls made by a successful
// from_view() on a counting-only mr. The count must be ≤4 (Entity 4 budget).
//
// Zero-alloc guard (mallocnesia scope, AC-T3 / NFR-003-4):
//   * String/int/char accessors on the flyweight (fixpp::v44::NewOrderSingle)
//     use dict::decode_field<T>(view.template get<Tag>()) — allocation-free.
//   * The default pod_decimal trait ignores the passed mr; no allocation.
//   These are verified by calling the accessors inside a counting mr scope
//   with count expected to remain 0.
//
// R6 note: the frozen wire stub carries no frame bytes. OOM injection still
// works because from_view allocates bytes_ from mr regardless of frame content
// (the PMR vector construction uses mr). The ≤4-alloc count covers the bytes_
// vector allocation at minimum; in R6 form there may be fewer than 4 (the stub
// allocates bytes_ only, not a real OffsetTable). The AC-R7 contract says ≤4,
// not exactly 4. The exact 4-alloc itemisation becomes testable with 2b.
//
// Oracle: data-model Entity 4 (PMR accounting); spec AC-R7 / seam #7/#16.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/field_traits.hpp>
#include <fixpp/wire/message_view_contract.hpp>

// Generated headers (build-tree only).
#include <fixpp/v44/Reify.hpp>

#include "support/failing_pmr_resource.hpp"

namespace {

using MV   = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using ONOS = fixpp::v44::owning_NewOrderSingle;
using NOS  = fixpp::v44::NewOrderSingle;

// Counting-only PMR resource: delegates to upstream but tracks allocate calls.
class counting_pmr_resource final : public std::pmr::memory_resource {
public:
    explicit counting_pmr_resource(std::pmr::memory_resource* up) noexcept
        : upstream_(up) {}

    [[nodiscard]] std::size_t count() const noexcept { return count_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++count_;
        return upstream_->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        upstream_->deallocate(p, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(
        std::pmr::memory_resource const& other) const noexcept override {
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
    ASSERT_TRUE(result.has_value())
        << "from_view must succeed with a valid arena (seam #16)";

    EXPECT_LE(counter.count(), std::size_t{4})
        << "AC-R7: reify_as must use ≤4 PMR allocations per Entity 4 budget";
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

TEST(ReifyOomTest, OomTrapPathDoesNotThrowWithZeroAllocs) {
    // R6: from_view makes 0 allocations (stub MV has no bytes to copy);
    // the fail_on_call_n=1 resource is never triggered → result is success.
    // This tests that from_view does NOT throw even with an OOM resource
    // standing by (the try/catch is inert but well-formed).
    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    fixpp::test_support::failing_pmr_resource fail{&upstream, /*fail_on_call_n=*/1};

    MV mv;
    bool threw = false;
    std::optional<bool> success;
    try {
        auto result = ONOS::from_view(mv, &fail);
        success = result.has_value();
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw)
        << "from_view must not propagate std::bad_alloc (trap_throw is wired)";
    // R6: result is success because 0 allocations were made.
    ASSERT_TRUE(success.has_value());
    EXPECT_TRUE(*success)
        << "R6: from_view succeeds with stub MV (0 allocs, fail never triggered)";
    // Confirm the failing resource was NOT called.
    EXPECT_EQ(fail.allocate_calls(), std::size_t{0})
        << "R6: stub MV causes 0 allocations in from_view";
    // NOTE (AC-R7 / seam #7): Full OOM injection (fail-on-call-1 → dict_reify_oom)
    // is R6-blocked. When 2b swaps in the real body, update this test:
    //   EXPECT_FALSE(result.has_value());
    //   EXPECT_EQ(result.error(), core::error::dict_reify_oom);
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
    EXPECT_EQ(counter.count(), before)
        << "NFR-003-4: char accessor side() must not allocate";
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
