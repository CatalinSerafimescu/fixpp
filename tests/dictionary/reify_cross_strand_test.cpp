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
// R6 scope: The behavioural test (real bytes survive strand-A arena reset,
// values read on B match pre-reset) is R6-blocked — the frozen wire stub
// carries no frame bytes (spec §11 R6). What IS testable now:
//   1. from_view on A's arena succeeds.
//   2. std::move to B (inter-thread) is safe — it is a simple pmr::vector move
//      (noexcept, no shared-state aliasing after move).
//   3. Accessors on B return field-absent (R6) without races.
//   4. I-10 single-strand contract: concurrent reads on one owning_<Msg>
//      are documented UB (NOT tested here to avoid a deliberate race; the
//      contract note is recorded in a comment per AC-T3).
//
// When 2b swaps in the real wire body, replace the R6 stubs in this file:
//   - Populate the MV from a real frame buffer on A.
//   - After std::move and arena reset on A, assert B's accessor values match.
//
// Oracle: specs/003-dictionary-codegen/contracts/reify.hpp (AC-R5);
//         data-model Entity 4 I-10; spec AC-T3.
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
// AC-R5 — cross-strand: reify on A, move to B, consume on B
// ─────────────────────────────────────────────────────────────────

TEST(ReiifyCrossStrand, MoveAcrossThreadsNoRace) {
    // Strand A: create arena + reify.
    std::pmr::monotonic_buffer_resource arena_a;
    MV mv_a;
    auto result = ONOS::from_view(mv_a, &arena_a);
    ASSERT_TRUE(result.has_value()) << "from_view on strand A must succeed";

    // Synchronisation: A publishes the owning_<Msg> to B via an optional.
    // The std::atomic bool acts as the release/acquire fence so B sees the
    // fully-constructed owning_<Msg> (SC happens-before).
    std::optional<ONOS> shared;
    std::atomic<bool> ready{false};

    // Move the owning instance into the shared slot before spawning B.
    // (All writes to `shared` happen on A before the atomic store.)
    shared.emplace(std::move(*result));
    ready.store(true, std::memory_order_release);

    // Strand B: wait, then consume.
    std::thread b([&] {
        // Spin until A publishes (R6 test has no blocking; spin is fine).
        while (!ready.load(std::memory_order_acquire)) {
        }

        ONOS& o = *shared;
        // AC-R5: accessor surface usable from B after the move.
        // R6: all accessors return field-absent (stub MV); behavioural values
        // (real bytes surviving arena_a reset) are R6-blocked until 2b.
        auto cl = o.cl_ord_id();
        EXPECT_FALSE(cl.has_value())
            << "R6: stub MV returns field-absent; real values tested with 2b";
        auto fv = o.field_value(11);
        EXPECT_FALSE(fv.has_value());
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
// AC-R5 — [[clang::lifetimebound]] anchors return to *this
// ─────────────────────────────────────────────────────────────────
// Verify the view() return is bound to the owning_<Msg> (not to a source MV
// that may have been destroyed): after from_view() the owning_<Msg> owns the
// storage, and view() returns a reference into its own bytes/cache.
// R6: cache is a default-constructed MV in stub form; the reference is still
// valid as long as the owning_<Msg> is live (which is what lifetimebound pins).

TEST(ReifyCrossStrand, ViewLifetimeBoundToOwner) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    ONOS o = []() {
        std::pmr::monotonic_buffer_resource local;
        MV local_mv;
        auto r = ONOS::from_view(local_mv, &local);
        // local arena destroyed here — but owning_<Msg> owns its bytes_
        // and does NOT hold a reference to local_mv or local.
        return std::move(*r);
    }();
    // R6: accessing view() should not crash (it lazily constructs a stub MV).
    MV const& v = o.view();
    auto fv = v.get(11);
    EXPECT_FALSE(fv.has_value());  // R6: field-absent
}
