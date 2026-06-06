// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/store_double.hpp
//
// Seam #0 — in-memory MessageStore test double (T014).
//
// Satisfies the [2e §4.1] 4-pure-virtual MessageStore seam:
//   store() / retrieve() / next_seqnum() / reset() + retrieve_visitor
//
// This is NOT the S-012 production MemoryStore. It is a minimal, synchronous
// (non-mutex-guarded) test double for driving Phase 3–6 FSM seam tests that
// need a store but are not testing store behaviour itself. For tests that DO
// test store behaviour (Phase 4 seam #3, etc.) use the production MemoryStore.
//
// Key simplifications vs production:
//   - Counters are plain std::uint32_t, NOT guarded by async_mutex (seam-test
//     threads are single-threaded by design; no actual concurrency in seam tests).
//   - store() is a no-suspend coroutine (co_return immediately after deep-copy).
//   - retrieve() is a no-suspend walk (calls visitor synchronously, no gap errors).
//   - No PMR arena — uses std::vector<std::byte> on the global heap (test only).
//
// Anchors: data-model.md E6 (D-4); research D-4; contracts/session_errors.hpp;
// include/fixpp/session/message_store.hpp ([2e §4.1]).
//
// No asio::awaitable in this header beyond the return types required by the
// MessageStore interface (the awaitable is returned, not co_awaited here,
// so no std::mutex is visible in an awaitable context — [const §XV.9]).
#pragma once

#include <asio/awaitable.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <span>
#include <vector>

namespace fixpp::session::test {

// ── StoreDouble ───────────────────────────────────────────────────────────────

class StoreDouble final : public MessageStore {
public:
    // No flush-for-session-close hook (MemoryStore path — nullptr thunk).
    StoreDouble() : MessageStore(flush_thunk_for<StoreDouble>()) {}

    StoreDouble(const StoreDouble&) = delete;
    StoreDouble& operator=(const StoreDouble&) = delete;
    StoreDouble(StoreDouble&&) = delete;
    StoreDouble& operator=(StoreDouble&&) = delete;
    ~StoreDouble() noexcept(false) override = default;

    // ── 4-pure-virtual interface ([2e §4.1]) ──────────────────────────────────

    // store: deep-copy the frame; advance the direction counter.
    // Returns immediately (no I/O, no mutex — test only).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame [[clang::lifetimebound]],
        direction_t dir) noexcept override {
        (void)seq;
        auto& buf = (dir == direction_t::outbound) ? outbound_ : inbound_;
        stored_frames_.emplace_back(frame.begin(), frame.end());
        buf.push_back(&stored_frames_.back());
        if (dir == direction_t::outbound) {
            next_out_++;
        } else {
            next_in_++;
        }
        co_return fixpp::core::expected_t<void>{};
    }

    // retrieve: walk frames in [begin, end] (end==0 → to current tail).
    // Synchronous; calls visitor sequentially. No gap errors for test use.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept override {
        (void)begin;
        (void)end;
        (void)dir;
        (void)visitor;
        // Retrieval is the deferred recovery feature's path (D-4 / FR-010).
        // Return ok — tests that need retrieve() use the production MemoryStore.
        co_return fixpp::core::expected_t<void>{};
    }

    // next_seqnum: read (increment=false) or read-then-increment (increment=true).
    // No overflow check (test double — production store handles overflow via
    // store_seqnum_overflow / session-fatal path, D-1 / I-8).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& counter = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t current = counter;
        if (increment) {
            ++counter;
        }
        co_return fixpp::core::expected_t<seqnum_t>{current};
    }

    // reset: clear both frame lists and rewind counters to 1.
    //
    // 024 seams: counts every awaitable reset() call (reset_call_count() — the
    // single-fire-guard / single-combined-decision witnesses assert exactly one
    // observable store reset); honours a one-shot "fail next reset" toggle
    // (fail_next_reset() — the durable-disposition witnesses inject a store
    // failure on the reset path). The failure injection short-circuits BEFORE
    // mutating state, mirroring a FileStore::reset() that faults on I/O.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        ++reset_calls_;
        if (fail_next_reset_) {
            fail_next_reset_ = false;
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        inbound_.clear();
        outbound_.clear();
        stored_frames_.clear();
        next_in_ = seqnum_min;
        next_out_ = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

    // ── Test-only inspection API ──────────────────────────────────────────────

    [[nodiscard]] seqnum_t current_next_inbound() const noexcept { return next_in_; }
    [[nodiscard]] seqnum_t current_next_outbound() const noexcept { return next_out_; }

    // 024 single-fire / single-combined-decision witnesses: observable count of
    // awaitable reset() calls (reset_sync() seeding is NOT counted).
    [[nodiscard]] std::size_t reset_call_count() const noexcept { return reset_calls_; }

    // 024 durable-disposition witnesses: make the NEXT awaitable reset() fail
    // once with store_io_failure, then auto-clear.
    void fail_next_reset() noexcept { fail_next_reset_ = true; }

    [[nodiscard]] std::size_t stored_inbound_count() const noexcept { return inbound_.size(); }
    [[nodiscard]] std::size_t stored_outbound_count() const noexcept { return outbound_.size(); }

    void reset_sync() {
        inbound_.clear();
        outbound_.clear();
        stored_frames_.clear();
        next_in_ = seqnum_min;
        next_out_ = seqnum_min;
    }

private:
    // Stable storage so pointers in inbound_/outbound_ never invalidate.
    std::vector<std::vector<std::byte>> stored_frames_;
    std::vector<std::vector<std::byte>*> inbound_;   // ordered frames
    std::vector<std::vector<std::byte>*> outbound_;  // ordered frames

    seqnum_t next_in_ = seqnum_min;   // next expected inbound  seqnum
    seqnum_t next_out_ = seqnum_min;  // next expected outbound seqnum

    std::size_t reset_calls_ = 0;     // 024: observable awaitable reset() count
    bool fail_next_reset_ = false;    // 024: one-shot reset failure injection
};

}  // namespace fixpp::session::test
