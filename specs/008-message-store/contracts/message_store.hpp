// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/message_store.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::MessageStore.
// Frozen at /plan time; /tasks generates implementation against this header;
// the eventual include/fixpp/session/message_store.hpp source header MUST
// static_assert against this contract's signatures where feasible.
//
// Anchor: .specify/2e-msgstore.md v0.4 §4.1 / §4.1.1 / §4.1.2.
// Pure-virtual count: 4 / 5 ([const §XIV.2] cap; within budget).
// NO public flush() (N2). NO store_concurrent_writer variant (Codex P1-5).
// All four methods acquire the per-instance async_mutex on entry (I-01).
#pragma once

#include <span>
#include <cstddef>

#include <asio/awaitable.hpp>

#include <fixpp/core/error.hpp>                    // expected_t / error
#include <fixpp/session/direction.hpp>             // direction_t
#include <fixpp/session/seqnum.hpp>                // seqnum_t

namespace fixpp::session {

class retrieve_visitor;   // forward-decl; full type in retrieve_visitor.hpp

class MessageStore {
public:
    MessageStore()                                 = default;
    MessageStore(const MessageStore&)              = delete;
    MessageStore& operator=(const MessageStore&)   = delete;
    MessageStore(MessageStore&&)                   = delete;
    MessageStore& operator=(MessageStore&&)        = delete;
    virtual ~MessageStore()                        = default;

    // store: persist a single frame. frame is taken as a non-owning span and
    // MUST be deep-copied into store-owned storage AFTER acquiring the writer
    // mutex and BEFORE any further suspension that could release the session
    // strand (i.e., before pwrite / fdatasync posts to file_io_executor) per
    // [2b §6.4] view-escape and design-doc §6.3.3 step 3 (I-02; under the
    // v1.0 single-session-serialisation-domain discipline the uncontended
    // async_mutex::async_lock() does NOT suspend per [2f §4.3.2] fast-path).
    // On mutex-acquire, verifies seq == next_seqnum(dir, false) inside the
    // critical section; mismatch → store_seqnum_out_of_order (I-05). Under
    // capacity_policy::bounded, MemoryStore returns store_capacity_exhausted
    // when the per-direction cap is reached (I-08). FileStore returns
    // store_io_failure on disk-side faults.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t seq,
          std::span<const std::byte> frame [[clang::lifetimebound]],
          direction_t dir) noexcept = 0;

    // retrieve: walk [begin, end] (end == 0 → infinity / to current tail).
    // Acquires the writer mutex to validate begin/end and snapshot the index,
    // releases the mutex BEFORE invoking visitor.on_frame's co_await (I-03).
    // begin == 0 → store_seqnum_invalid; end != 0 && end < begin →
    // store_invalid_range; gap → store_seqnum_gap (I-19). Mid-traversal
    // mutation is detected and the next visitor call observes the new state
    // without UB; already-visited frames are not re-visited; iteration stops
    // at the original end.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t begin,
             seqnum_t end,
             direction_t dir,
             retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept = 0;

    // next_seqnum: read (increment=false) or read-then-increment
    // (increment=true). The increment is serialised on the writer mutex
    // (Opus N2-P2-2; the v0.2 atomic-fetch-add wording was retired).
    // Overflow on increment when current == seqnum_max → store_seqnum_overflow
    // and session-fatal (I-18).
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool increment) noexcept = 0;

    // reset: clear all frames (both directions) and rewind counters to
    // next_inbound = next_outbound = 1. FileStore: atomic at the rename of
    // <live>.log.reset.tmp PLUS the platform durability primitive (Linux:
    // parent-dir fsync MANDATORY; Windows: MOVEFILE_WRITE_THROUGH MANDATORY;
    // I-15). MemoryStore: entry-array zero pass under writer mutex.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept = 0;
};

}  // namespace fixpp::session
