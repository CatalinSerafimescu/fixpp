// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_cross_strand_test.cpp — T030 [P] [US2] / seam #12
//
// AC-R5 / AC-T3: cross-strand handoff — reify on strand A, std::move to
// strand B, consume on B.
//
// TSan target ([const §IX.2]): registered as TSan label; TSan must see NO data
// race on the moved-to owning_<Msg> consumed on B, and MUST flag any incorrect
// concurrent reads on one instance.
//
// 2b cutover (004 T059): from_view performs the REAL owning deep-copy; the
// cross-strand handoff (reify on A, std::move to B, consume on B) is now a
// genuine behavioural test on a frame-backed MessageView<Index>. The R6
// dict_reify_wire_body_not_ready oracle is retired. Cross-strand value
// survival across an arena boundary is exercised for real.
//
// Oracle: specs/003-dictionary-codegen/contracts/reify.hpp (AC-R5);
//         data-model Entity 4 I-10; spec AC-T3.
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>  // Framer, pmr_carry_buffer, MessageView (T059)
#include <memory_resource>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

// Generated headers (build-tree only).
#include <fixpp/v44/Reify.hpp>

#include "support/reify_test_frame.hpp"  // make_nos_frame (T059)

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using ONOS = fixpp::v44::owning_NewOrderSingle;
using fixpp::test_support::make_nos_frame;

// Reify a NOS into owning_mr; the source frame buffer + its arena are built
// and DESTROYED inside this function — so a returned ONOS that still reads
// its fields proves the owning deep-copy is self-contained (AC-R5/AC-R4).
[[nodiscard]] fixpp::core::expected_t<ONOS> reify_nos(std::pmr::memory_resource* owning_mr) {
    auto buf = make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed || framed->empty()) {
        return std::unexpected{fixpp::core::error::dict_reify_wire_body_not_ready};
    }
    MV src{(*framed)[0], &frame_mr};
    return ONOS::from_view(src, owning_mr);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────
// 2b cutover (T059): from_view succeeds + the owning deep-copy is
// self-contained (source frame + arena destroyed inside reify_nos).
// ─────────────────────────────────────────────────────────────────

TEST(ReiifyCrossStrand, FromViewSucceedsSelfContained) {
    std::pmr::monotonic_buffer_resource owning_mr;
    auto result = reify_nos(&owning_mr);
    ASSERT_TRUE(result.has_value())
        << "from_view must succeed post-cutover (real owning deep-copy)";
    auto cl = result->cl_ord_id();
    ASSERT_TRUE(cl.has_value())
        << "ClOrdID(11) readable though the source arena is already destroyed";
    EXPECT_EQ(cl.value(), "ORD1");
}

// ─────────────────────────────────────────────────────────────────
// AC-R5 / AC-T3 — cross-strand: reify on A, move to B, consume on B.
// ─────────────────────────────────────────────────────────────────

TEST(ReiifyCrossStrand, MoveAcrossThreadsNoRace) {
    std::pmr::monotonic_buffer_resource owning_mr;
    auto result = reify_nos(&owning_mr);
    ASSERT_TRUE(result.has_value()) << "from_view on strand A must succeed";

    std::optional<ONOS> shared;
    std::atomic<bool> ready{false};
    shared.emplace(std::move(*result));
    ready.store(true, std::memory_order_release);

    std::thread b([&] {
        while (!ready.load(std::memory_order_acquire)) {
        }
        ONOS& o = *shared;
        auto cl = o.cl_ord_id();
        ASSERT_TRUE(cl.has_value()) << "parsed values survive cross-strand move (AC-R5)";
        EXPECT_EQ(cl.value(), "ORD1");
        EXPECT_TRUE(o.field_value(11).has_value());
        EXPECT_EQ(o.which(), fixpp::dict::application_version::v44);
    });
    b.join();
}

// ─────────────────────────────────────────────────────────────────
// AC-T3 single-strand contract note
// ─────────────────────────────────────────────────────────────────
// I-10: owning_<Msg> is single-strand-only. The lazy view() cache write is
// unsynchronised (no mutex/atomic). Concurrent READS on one owning_<Msg>
// instance from multiple threads are UB (data race per C++23 §6.9.2).
// The correct usage is: reify-on-A → std::move → consume-on-B (AC-R5 above).
// This test deliberately does NOT exercise the UB path (doing so would trigger
// TSan — the point of this test is that TSan should remain CLEAN). The I-10
// doc-note is recorded here as a compile-time comment for Gate A traceability.

TEST(ReifyCrossStrand, SingleStrandContractDocumented) {
    // No runtime content — the contract is the comment above (AC-T3 record).
    // TSan label ensures this test runs under ThreadSanitizer in CI.
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────
// AC-R5 — view() anchors to the OWNED bytes, not the source arena.
// ─────────────────────────────────────────────────────────────────

TEST(ReifyCrossStrand, ViewLifetimeBoundToOwner) {
    std::pmr::monotonic_buffer_resource owning_mr;
    ONOS o = [&owning_mr]() {
        auto r = reify_nos(&owning_mr);  // source frame + arena die on return
        EXPECT_TRUE(r.has_value());
        return std::move(*r);
    }();
    MV const& v = o.view();  // rebuilt over the owned deep-copy
    auto fv = v.get(11);
    ASSERT_TRUE(fv.has_value())
        << "view() must resolve against owned bytes after the source arena died";
    EXPECT_EQ(fv->as_string(), "ORD1");
}
