// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_611_store_offload_error_arms.cpp
//
// fixpp#611: the error arms of FileStore's offloaded writes. Each cell drives one arm
// through the generalised seam arm_force_offload_write_fail_once (FIXPP_TEST_HOOKS),
// which fails the Nth raw pwrite, or the fdatasync, of the next offloaded store() or
// next_seqnum(increment) job without writing. Each cell asserts:
//   - the call's returned error;
//   - that the seam fired, so a failure cannot come from another cause;
//   - what the log holds after the store is closed and opened again: whether the open
//     accepts it, the next outbound number, and the frames a retrieve visits.
//
// What the reopen holds follows from FileStoreImpl::restart_scan in
// src/session/file_store.cpp: a record cut short at the tail is truncated away, a
// complete frame record is indexed, and the counters come from the last complete
// counter record. Re-derive an expectation from that scan, not from this file.
//
// The store() job writes, in order: the frame header, the payload, the padding, the
// counter header, the counter payload, then the fdatasync (commit_per_message). The
// test frame is chosen so that it has a payload and padding; a cell checks that before
// relying on the order. The next_seqnum(increment) job writes the counter header and
// the counter payload, then the fdatasync.
//
// ResetThrowAfterRename (fixpp#599, D-599-1 in .specify/624-offload-completion-lifetime.md):
// the reset's job throws after its rename has replaced the live log
// (arm_force_reset_throw_after_rename_once). The reset must end as a failure after the
// rename ends: store_io_failure, the store poisoned so no later write reaches the
// replaced log's inode, and the reopened log the reset's own.
//
// Not here: the arms of reset_store_to's job that fixpp#611 also names (initialise_fresh
// on the temporary file, the rename, the reopen after it, the reopen's lock). The reset
// seams sit before the rename, at the directory's open or fsync, and (for
// install_post_rename_reopen_fail_hook) at a return placed before the reopen's own `open`
// call; none sits at those four statements. Re-derive by reading the reset job in
// reset_store_to.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"
#include "support/temp_dir.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MessageStore;
using fixpp::session::seqnum_t;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::run_on_pool;

constexpr std::size_t kHeaderBytes = 16;  // FileStore's record header
constexpr std::size_t kAlign = 8;         // FileStore's record alignment

// The FileStore's directory, its file I/O pool and the cells' coroutine pool.
struct Fixture {
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("fixpp611_arms");
        temp_dir() = default;
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir;
    asio::thread_pool file_io{1};
    asio::thread_pool pool{1};

    // Opens (or opens again) the FileStore on the fixture's directory; null when the
    // open refuses the log.
    [[nodiscard]] std::unique_ptr<MessageStore> open() {
        FileStore::Config cfg;
        cfg.directory = dir.path;
        cfg.sender_comp_id = "SENDER";
        cfg.target_comp_id = "TARGET";
        cfg.max_frame_bytes = 4096;
        cfg.file_io_executor = file_io.get_executor();
        auto made = FileStoreFactory{cfg}.make("SENDER", "TARGET", nullptr, std::size_t{1} << 30,
                                               file_io.get_executor());
        return made ? std::move(*made) : nullptr;
    }
};

// Clears an armed fault that a cell's call did not take, so it cannot reach a later cell.
struct disarm_on_exit {
    disarm_on_exit() { (void)fixpp::session::read_and_reset_offload_write_fail_count(); }
    ~disarm_on_exit() {
        fixpp::session::arm_force_offload_write_fail_once(0, false);
        (void)fixpp::session::read_and_reset_offload_write_fail_count();
    }
    disarm_on_exit(disarm_on_exit const&) = delete;
    disarm_on_exit& operator=(disarm_on_exit const&) = delete;
};

asio::awaitable<fixpp::core::expected_t<void>> store_seq(MessageStore& store, seqnum_t const seq) {
    auto const frame = make_test_frame(seq, direction_t::outbound);
    co_return co_await store.store(seq, std::span<const std::byte>{frame}, direction_t::outbound);
}

// What the store opened again holds.
struct reopened {
    bool accepted = false;
    seqnum_t next_outbound = 0;
    std::vector<seqnum_t> frames;  // outbound, as a retrieve from 1 to the tail visits them
    bool frames_intact = true;     // every visited frame's bytes are its make_test_frame
    bool frame2_on_disk = false;   // retrieve(2, 2) visits seq 2's frame
};

asio::awaitable<reopened> reopen(Fixture& fx) {
    reopened out;
    auto const store = fx.open();
    out.accepted = store != nullptr;
    if (!store) co_return out;
    auto const next = co_await store->next_seqnum(direction_t::outbound, false);
    out.next_outbound = next ? *next : seqnum_t{0};
    byte_collecting_visitor v;
    auto const r = co_await store->retrieve(1, 0, direction_t::outbound, v);
    if (!r) out.frames_intact = false;
    for (auto const& e : v.entries()) {
        out.frames.push_back(e.seq);
        if (e.bytes != make_test_frame(e.seq, direction_t::outbound)) out.frames_intact = false;
    }
    // "To the tail" stops at the next outbound number less one, so a frame record past
    // the last counter record is read by its own number.
    byte_collecting_visitor two;
    auto const r2 = co_await store->retrieve(2, 2, direction_t::outbound, two);
    out.frame2_on_disk = r2.has_value() && two.entries().size() == 1 &&
                         two.entries()[0].bytes == make_test_frame(2, direction_t::outbound);
    co_return out;
}

std::string text(std::vector<seqnum_t> const& seqs) {
    std::string t;
    for (auto const s : seqs) t += " " + std::to_string(s);
    return seqs.empty() ? " none" : t;
}

// store(1) succeeds, then store(2) runs with the fault armed.
void store_arm(int const pwrite_nth, bool const datasync, std::vector<seqnum_t> const& frames,
               bool const frame2_on_disk, seqnum_t const next_outbound) {
    disarm_on_exit const disarm;
    Fixture fx;
    auto const frame = make_test_frame(2, direction_t::outbound);
    ASSERT_TRUE(!frame.empty() && (kHeaderBytes + frame.size()) % kAlign != 0)
        << "precondition: the frame has a payload and padding, so the write order holds";
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            auto const first = co_await store_seq(*store, 1);
            EXPECT_TRUE(first.has_value()) << "setup: store(1)";
            if (!first) co_return;
            fixpp::session::arm_force_offload_write_fail_once(pwrite_nth, datasync);
            auto const second = co_await store_seq(*store, 2);
            EXPECT_TRUE(!second && second.error() == error::store_io_failure)
                << "store(2) must return store_io_failure";
            EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 1)
                << "the seam did not fire for store(2)";
        }
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failure must accept the log";
        EXPECT_EQ(again.next_outbound, next_outbound) << "next outbound number after reopen";
        EXPECT_EQ(again.frames, frames) << "frames after reopen:" << text(again.frames);
        EXPECT_TRUE(again.frames_intact) << "a frame read back differs from what was stored";
        EXPECT_EQ(again.frame2_on_disk, frame2_on_disk) << "retrieve(2, 2) after reopen";
    });
}

// next_seqnum(outbound, increment) succeeds once, then runs again with the fault armed.
void next_seqnum_arm(int const pwrite_nth, bool const datasync, seqnum_t const next_outbound) {
    disarm_on_exit const disarm;
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            auto const first = co_await store->next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(first.has_value() && *first == 1) << "setup: the first increment";
            if (!first) co_return;
            fixpp::session::arm_force_offload_write_fail_once(pwrite_nth, datasync);
            auto const second = co_await store->next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(!second && second.error() == error::store_io_failure)
                << "the second increment must return store_io_failure";
            EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 1)
                << "the seam did not fire for the second increment";
        }
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failure must accept the log";
        EXPECT_EQ(again.next_outbound, next_outbound) << "next outbound number after reopen";
        EXPECT_TRUE(again.frames.empty()) << "frames after reopen:" << text(again.frames);
    });
}

// ── store() ───────────────────────────────────────────────────────────────────

// Only the frame header is on disk: a record cut short at the tail, truncated away.
TEST(StoreOffloadErrorArms, Store_PayloadPwriteFails) { store_arm(2, false, {1}, false, 2); }

// Header and payload, no padding: the record is short of its padded size, truncated.
TEST(StoreOffloadErrorArms, Store_PaddingPwriteFails) { store_arm(3, false, {1}, false, 2); }

// The frame record is complete and no counter record follows it: the frame is indexed,
// and the counters come from store(1)'s counter record, so the tail ends at 1 and the
// frame is read only by its own number.
TEST(StoreOffloadErrorArms, Store_CounterHeaderPwriteFails) { store_arm(4, false, {1}, true, 2); }

// A counter header with no payload is cut short and truncated; the complete frame
// record before it is indexed, as in the cell above.
TEST(StoreOffloadErrorArms, Store_CounterPayloadPwriteFails) { store_arm(5, false, {1}, true, 2); }

// Every record was written; only the flush reported failure.
TEST(StoreOffloadErrorArms, Store_DatasyncFails) { store_arm(0, true, {1, 2}, true, 3); }

// ── next_seqnum(outbound, increment) ──────────────────────────────────────────

// Nothing of the second counter record is on disk.
TEST(StoreOffloadErrorArms, NextSeqnum_CounterHeaderPwriteFails) { next_seqnum_arm(1, false, 2); }

// A counter header with no payload, truncated.
TEST(StoreOffloadErrorArms, NextSeqnum_CounterPayloadPwriteFails) { next_seqnum_arm(2, false, 2); }

// The counter record was written; only the flush reported failure.
TEST(StoreOffloadErrorArms, NextSeqnum_DatasyncFails) { next_seqnum_arm(0, true, 3); }

// ── reset() whose job throws after the rename (fixpp#599) ─────────────────────

TEST(ResetThrowAfterRename, PoisonsTheStoreAndReturnsTheResetsError) {
    (void)fixpp::session::read_and_reset_reset_atomicity_fault_count();
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            auto const one = co_await store_seq(*store, 1);
            auto const two = co_await store_seq(*store, 2);
            EXPECT_TRUE(one.has_value() && two.has_value()) << "setup: store(1), store(2)";
            if (!one || !two) co_return;

            fixpp::session::arm_force_reset_throw_after_rename_once();
            bool reset_threw = false;
            fixpp::core::expected_t<void> reset{};
            try {
                reset = co_await store->reset();
            } catch (std::exception const&) {
                reset_threw = true;
            }
            EXPECT_EQ(fixpp::session::read_and_reset_reset_atomicity_fault_count(), 1)
                << "the throw-after-rename seam did not fire";
            EXPECT_FALSE(reset_threw) << "the reset's job threw, and the reset passed it on";
            EXPECT_TRUE(!reset_threw && !reset && reset.error() == error::store_io_failure)
                << "the reset must return store_io_failure";

            // A store after the failed reset: the store is poisoned, so it is refused.
            auto const later = co_await store_seq(*store, 3);
            EXPECT_TRUE(!later && later.error() == error::store_io_failure)
                << "store(3) after the failed reset must be refused; it returned "
                << (later ? "success" : "an error other than store_io_failure");
        }
        // The live name is the reset's fresh log: no frame, next outbound 1. store(3) is
        // not in it, whatever store(3) returned.
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failed reset must accept the log";
        EXPECT_EQ(again.next_outbound, seqnum_t{1}) << "next outbound number after reopen";
        EXPECT_TRUE(again.frames.empty()) << "frames after reopen:" << text(again.frames);
    });
}

}  // namespace
