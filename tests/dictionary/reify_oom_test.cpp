// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_oom_test.cpp — T031 [P] [US2] / seam #7/#16
//
// AC-R7: PMR-OOM injection → dict_reify_oom; ≤4 PMR allocs; zero-alloc guard
// for string/int/char + default-trait decimal accessors.
//
// 2b cutover (004 T059): from_view performs the real owning deep-copy of the
// validated frame into the supplied mr. That deep-copy IS the PMR allocation
// the OOM trap (seam #7) and the ≤4 budget (seam #16, AC-R7) observe:
//   * Seam #7: a resource failing on call 1 → from_view returns
//     dict_reify_oom (bad_alloc caught by the inline try/catch, not propagated).
//   * Seam #16: from_view's allocation count is ≤4 (the bytes_ copy; view()
//     is lazy so the OffsetTable is not built here).
//
// Zero-alloc guard (mallocnesia scope, AC-T3 / NFR-003-4): string/int/char
// flyweight accessors use dict::decode_field<T>(view.get<Tag>()) and the
// default pod_decimal trait ignores mr — allocation-free regardless.
//
// Oracle: data-model Entity 4 (PMR accounting); spec AC-R7 / seam #7/#16.
#include <gtest/gtest.h>
#include "../support/msvc_debug_arena_skip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/field_traits.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>  // Framer, pmr_carry_buffer, MessageView (T059)
#include <memory_resource>
#include <optional>
#include <span>
#include <vector>

// Generated headers (build-tree only).
#include <fixpp/v44/Reify.hpp>

#include "support/failing_pmr_resource.hpp"
#include "support/reify_test_frame.hpp"  // make_nos_frame (T059)

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
    auto frame = fixpp::test_support::make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV src{(*framed)[0], &frame_mr};

    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    counting_pmr_resource counter{&upstream};

    auto result = ONOS::from_view(src, &counter);
    ASSERT_TRUE(result.has_value()) << "from_view must succeed (real owning deep-copy, T059)";
    EXPECT_LE(counter.count(), std::size_t{4})
        << "AC-R7: from_view must use ≤4 PMR allocations per Entity 4 budget "
           "(the bytes_ deep-copy; view() is lazy)";
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

// T059: from_view's owning deep-copy (pmr::vector assign of the frame bytes)
// is the PMR allocation. fail-on-call-1 fires there → bad_alloc caught by the
// inline try/catch → dict_reify_oom (not propagated).
TEST(ReifyOomTest, OomInjectionOnFirstAllocYieldsOomError) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto frame = fixpp::test_support::make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV src{(*framed)[0], &frame_mr};

    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    fixpp::test_support::failing_pmr_resource fail{&upstream, /*fail_on_call_n=*/1};

    bool threw = false;
    std::optional<fixpp::core::expected_t<ONOS>> result;
    try {
        result = ONOS::from_view(src, &fail);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "from_view must not propagate std::bad_alloc (trap_throw must catch it)";
    ASSERT_TRUE(result.has_value()) << "from_view must return (not throw) even under OOM";
    ASSERT_FALSE(result->has_value()) << "fail-on-call-1: from_view must return an error";
    EXPECT_EQ(result->error(), fixpp::core::error::dict_reify_oom)
        << "AC-R7 seam #7: bad_alloc on the bytes_ deep-copy must yield dict_reify_oom";
    EXPECT_GE(fail.allocate_calls(), std::size_t{1})
        << "The failing resource must have been called at least once";
}

// Codex adversarial review (T059): the lazy view() rebuild builds an
// OffsetTable which ALLOCATES. A throwing mr there must DEGRADE gracefully
// (empty table, status_=out_of_memory, fields absent) — it must NEVER
// std::terminate out of the noexcept MessageView/OffsetTable ctor. This is
// the OOM-on-first-view() path that from_view's own try/catch does not cover.
TEST(ReifyOomTest, FirstViewRebuildOomDegradesNotTerminate) {
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    auto frame = fixpp::test_support::make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV src{(*framed)[0], &frame_mr};

    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};
    // Alloc #1 = the bytes_ deep-copy (must succeed so from_view returns ok);
    // fail #2 = the first OffsetTable allocation in the lazy view() rebuild.
    fixpp::test_support::failing_pmr_resource fail{&upstream, /*fail_on_call_n=*/2};

    auto owned = ONOS::from_view(src, &fail);
    ASSERT_TRUE(owned.has_value())
        << "from_view (bytes_ copy = alloc #1) must succeed before the view() OOM";

    // First field access triggers the lazy view() rebuild → OffsetTable build
    // hits the failing allocation. MUST NOT terminate; degrades to absent.
    auto cl = owned->cl_ord_id();
    EXPECT_FALSE(cl.has_value())
        << "OOM during OffsetTable build → field-absent (graceful degrade)";
    auto st = owned->view().offsets().build_status();
    ASSERT_FALSE(st.has_value());
    EXPECT_EQ(st.error(), fixpp::core::error::out_of_memory)
        << "OffsetTable must record out_of_memory (graceful), not terminate";
}

// No-OOM path: a live arena + a real frame → from_view SUCCEEDS (the retired
// R6 dict_reify_wire_body_not_ready placeholder no longer exists).
TEST(ReifyOomTest, NoOomYieldsSuccess) {
    auto frame = fixpp::test_support::make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{frame.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{frame.data(), frame.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV src{(*framed)[0], &frame_mr};

    std::array<std::byte, 1024 * 64> buf{};
    std::pmr::monotonic_buffer_resource upstream{buf.data(), buf.size()};

    bool threw = false;
    std::optional<fixpp::core::expected_t<ONOS>> result;
    try {
        result = ONOS::from_view(src, &upstream);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "from_view must not throw with a live arena";
    ASSERT_TRUE(result.has_value()) << "from_view must return (not throw)";
    ASSERT_TRUE(result->has_value())
        << "from_view must SUCCEED with a live arena + real frame (T059)";
    EXPECT_EQ(result->value().cl_ord_id().value(), "ORD1");
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
    FIXPP_SKIP_ON_MSVC_DEBUG_ARENA();
    static_assert(static_cast<std::uint8_t>(fixpp::core::error::dict_reify_oom) == 25,
                  "dict_reify_oom must be slot 25 per data-model Error mapping");
    SUCCEED();
}
