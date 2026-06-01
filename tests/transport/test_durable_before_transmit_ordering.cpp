// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_durable_before_transmit_ordering.cpp
// T015 — [2h §9 seam #8] — durable-before-transmit ordering contract.
//
// Verifies [2e §6.1.4] + spec FR-030 + [2h §6.7]:
//   Writer::commit → store(committed_span) success → Transport::async_write
//   → inject cancel mid-flight →
//     (a) persisted frame is intact under MessageStore::retrieve.
//     (b) async_write awaitable completes transport_write_cancelled.
//     (c) NO rollback of the persisted frame.
//
// This is a pure ORDERING test — it verifies the invariant that a cancelled
// async_write does NOT trigger any state rollback of an already-durably-stored
// frame. The transport is the wire-side hop only.
//
// Phase 3a — DISABLED cells require asio_tls_transport (T026) + MessageStore.
// The compile-time contract assertions run NOW.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <span>
#include <vector>

namespace {

namespace fc = fixpp::core;

// ─────────────────────────────────────────────────────────────────────────────
// Invariant contract statements (compile-time / documentation-level).
// ─────────────────────────────────────────────────────────────────────────────

// The durable-before-transmit contract requires:
//   1. Store success BEFORE async_write is called.
//   2. Cancel of async_write MUST NOT roll back the persisted frame.
//   3. The error returned is transport_write_cancelled, NOT any store error.

TEST(DurableBeforeTransmit, ErrorCodeContract) {
    // transport_write_cancelled is the correct result when async_write is
    // cancelled; transport_write_short is the result for partial writes under
    // cancellation per [2h §6.6]:1182.
    EXPECT_EQ(static_cast<int>(fixpp::core::error::transport_write_cancelled), 113);
    EXPECT_EQ(static_cast<int>(fixpp::core::error::transport_write_short), 105);
    // These are distinct (short-write ≠ cancelled).
    EXPECT_NE(fixpp::core::error::transport_write_cancelled,
              fixpp::core::error::transport_write_short);
}

TEST(DurableBeforeTransmit, TransportWriteIsNotRollbackAware) {
    // The Transport interface has NO method for rolling back a persisted frame.
    // This is by design: Transport::async_write is wire-side only. The FSM
    // sequences: Writer::commit → store → async_write. The transport layer
    // cannot undo a store call it never made.
    //
    // Structural check: Transport::cancel() is not a rollback. cancel() only
    // cancels the in-flight async operation.
    using T = fixpp::transport::Transport;
    static_assert(std::is_same_v<decltype(&T::cancel), fc::expected_t<void> (T::*)() noexcept>,
                  "Transport::cancel() signature changed — durable-before-transmit "
                  "invariant may be at risk");
}

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED integration cells — require asio_tls_transport + MessageStore.
// ─────────────────────────────────────────────────────────────────────────────

// Cell 1: store → async_write → cancel → check persisted frame intact.
// [2h §9 seam #8] primary cell.
TEST(DISABLED_DurableBeforeTransmit, CancelledWriteDoesNotRollbackStore) {
    // Flow:
    //   1. Construct MemoryStore; construct asio_tls_transport over loopback.
    //   2. Build a FIX frame via Writer::commit (span valid; store it).
    //   3. store(committed_span) → success → note retrieve() returns frame.
    //   4. Start async_write(committed_span) in a co_spawn coroutine.
    //   5. Immediately emit cancellation_signal::total.
    //   6. Await the coroutine.
    //   7. ASSERT: async_write returned unexpected{transport_write_cancelled}.
    //   8. ASSERT: store.retrieve(seqnum) still returns the original frame bytes.
    //   9. ASSERT: no rollback call was made (MemoryStore has no rollback).
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + MessageStore";
}

// Cell 2: partial write then cancel → transport_write_short, frame still durable.
// [2h §6.6]:1182 Edge Cases — torn write under cancellation.
TEST(DISABLED_DurableBeforeTransmit, PartialWriteThenCancelFrameStillDurable) {
    // Flow:
    //   1. Same as Cell 1 but slow-write peer: peer reads only N/2 bytes
    //      before the cancellation signal fires.
    //   2. ASSERT: async_write returned unexpected{transport_write_short}.
    //   3. ASSERT: store still intact (no rollback).
    GTEST_SKIP() << "Requires asio_tls_transport (T026) + slow loopback peer";
}

}  // namespace
