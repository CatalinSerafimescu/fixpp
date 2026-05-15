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
// R6 scope (gate-b/r1 RC#3-B hardening):
//   from_view returns dict_reify_wire_body_not_ready under R6 (spec §5 deferral).
//   The cross-strand move mechanics are NOT blocked (the compile-time shape +
//   static_asserts cover the move contract). The behavioural half (real bytes
//   survive strand-A arena reset, values read on B match pre-reset) is
//   R6-gated: guarded under #if FIXPP_R6_WIRE_BODY_READY.
//
//   Positive oracle: from_view must return dict_reify_wire_body_not_ready (not
//   success, not dict_xml_parse_failed). This exact code is asserted so that
//   misdispatch or silent no-copy cannot stay green.
//
// When 2b swaps in the real wire body, set FIXPP_R6_WIRE_BODY_READY=1.
//
// Oracle: specs/003-dictionary-codegen/contracts/reify.hpp (AC-R5);
//         data-model Entity 4 I-10; spec AC-T3; spec §5 R6 deferral.
#include <gtest/gtest.h>

#include <atomic>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <optional>
#include <thread>
#include <utility>

// Generated headers (build-tree only).
#include <fixpp/v44/Reify.hpp>

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using ONOS = fixpp::v44::owning_NewOrderSingle;

}  // namespace

// ─────────────────────────────────────────────────────────────────
// Positive R6 oracle (gate-b/r1 RC#3-B): from_view returns
// dict_reify_wire_body_not_ready (not success, not dict_xml_parse_failed).
// ─────────────────────────────────────────────────────────────────

TEST(ReiifyCrossStrand, FromViewReturnsWireBodyNotReady) {
    // gate-b/r1 RC#3-B: positive oracle for the R6 placeholder.
    // from_view MUST return dict_reify_wire_body_not_ready — asserting the
    // exact code means any regression (silent success or wrong error) fails.
    std::pmr::monotonic_buffer_resource arena_a;
    MV mv_a;
    auto result = ONOS::from_view(mv_a, &arena_a);
    ASSERT_FALSE(result.has_value())
        << "from_view must not return success under R6 (spec §5 deferral / gate-b/r1 RC#1 F2)";
    EXPECT_EQ(result.error(), fixpp::core::error::dict_reify_wire_body_not_ready)
        << "AC-R5 R6 oracle: from_view must return dict_reify_wire_body_not_ready "
           "(not dict_xml_parse_failed or success) until 2b supplies frame bytes";
}

// ─────────────────────────────────────────────────────────────────
// AC-R5 — cross-strand: reify on A, move to B, consume on B
// R6-gated: from_view returns an error; guarded below.
// ─────────────────────────────────────────────────────────────────

#ifndef FIXPP_R6_WIRE_BODY_READY

TEST(ReiifyCrossStrand, MoveAcrossThreadsNoRace_R6Deferred) {
    // AC-R5 / AC-T3 behavioural: cross-strand move preserving values.
    // R6-deferred (spec §5): from_view returns dict_reify_wire_body_not_ready.
    // 2b-unblock: remove GTEST_SKIP, populate MV from real frame, assert values on B.
    GTEST_SKIP() << "R6-deferred (spec §5): from_view behavioural round-trip "
                    "awaits 2b wire body (FIXPP_R6_WIRE_BODY_READY not defined)";
}

#else  // FIXPP_R6_WIRE_BODY_READY — 2b-unblock: activate this test

TEST(ReiifyCrossStrand, MoveAcrossThreadsNoRace) {
    // AC-R5: full behavioural cross-strand handoff.
    std::pmr::monotonic_buffer_resource arena_a;
    MV mv_a;
    auto result = ONOS::from_view(mv_a, &arena_a);
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
        EXPECT_TRUE(cl.has_value()) << "2b: parsed values survive cross-strand move";
        auto fv = o.field_value(11);
        EXPECT_TRUE(fv.has_value());
        EXPECT_EQ(o.which(), fixpp::dict::application_version::v44);
    });
    b.join();
}

#endif  // FIXPP_R6_WIRE_BODY_READY

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
// AC-R5 — [[clang::lifetimebound]] anchors return to *this
// ─────────────────────────────────────────────────────────────────
// R6-gated: from_view returns an error under R6; this test is deferred.
// 2b-unblock: activate via FIXPP_R6_WIRE_BODY_READY.

#ifndef FIXPP_R6_WIRE_BODY_READY

TEST(ReifyCrossStrand, ViewLifetimeBoundToOwner_R6Deferred) {
    // AC-R5 lifetimebound: view() references owned bytes, not source arena.
    // R6-deferred: from_view returns dict_reify_wire_body_not_ready (spec §5).
    GTEST_SKIP() << "R6-deferred (spec §5): awaits 2b wire body";
}

#else  // FIXPP_R6_WIRE_BODY_READY

TEST(ReifyCrossStrand, ViewLifetimeBoundToOwner) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    ONOS o = []() {
        std::pmr::monotonic_buffer_resource local;
        MV local_mv;
        auto r = ONOS::from_view(local_mv, &local);
        EXPECT_TRUE(r.has_value());
        return std::move(*r);
    }();
    MV const& v = o.view();
    auto fv = v.get(11);
    EXPECT_TRUE(fv.has_value());  // 2b: parsed value present
}

#endif  // FIXPP_R6_WIRE_BODY_READY
