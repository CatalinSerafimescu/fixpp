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
//   - that the restore's datasync was made and succeeded (read_and_reset_restore_datasync_count);
//   - that the log's size after the failed call is its size before it (fixpp#635, D-635-1:
//     a failed call restores the log's committed end);
//   - what the log holds after the store is closed and opened again: whether the open
//     accepts it, the next outbound number, and the frames a retrieve visits.
//
// What the reopen holds follows from D-635-1 and FileStoreImpl::restart_scan in
// src/session/file_store.cpp: a failed call leaves the committed records only, unless its
// restore failed too; then a record cut short at the tail is truncated away, a complete
// frame record is indexed, and the counters come from the last complete counter record.
// Re-derive an expectation from those, not from this file.
//
// The store() job writes, in order: the frame header, the payload, the padding, the
// counter header, the counter payload, then the fdatasync (commit_per_message). The
// test frame is chosen so that it has a payload and padding; a cell checks that before
// relying on the order. The next_seqnum(increment) job writes the counter header and
// the counter payload, then the fdatasync.
//
// The reset arms drive reset() through the reset-step fault seam
// (arm_force_reset_step_fault_once), which fails the next reset's job at one step:
// initialise_fresh on the temporary log, the rename, the reopen's open or its lock, or
// throws right after the rename (fixpp#599, D-599-1 in
// .specify/624-offload-completion-lifetime.md). Every arm must end the reset with
// store_io_failure and no exception. A step before the rename leaves the old log the
// live one and the store usable; a step after it poisons the store, so a later store()
// is refused and the reopened log is the reset's own.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fstream>
#include <iterator>
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

// The size of the fixture's log file; 0 when there is none.
std::uintmax_t log_size(Fixture const& fx) {
    for (auto const& e : std::filesystem::directory_iterator(fx.dir.path)) {
        if (e.path().extension() == ".log") return std::filesystem::file_size(e.path());
    }
    return 0;
}

// Clears an armed fault that a cell's call did not take, so it cannot reach a later cell.
struct disarm_on_exit {
    disarm_on_exit() {
        (void)fixpp::session::read_and_reset_offload_write_fail_count();
        (void)fixpp::session::read_and_reset_restore_datasync_count();
    }
    ~disarm_on_exit() {
        fixpp::session::arm_force_offload_write_fail_once(0, false);
        fixpp::session::arm_force_offload_restore_fail_once(false, false);
        (void)fixpp::session::read_and_reset_offload_write_fail_count();
        (void)fixpp::session::read_and_reset_restore_datasync_count();
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
            auto const committed_end = log_size(fx);
            fixpp::session::arm_force_offload_write_fail_once(pwrite_nth, datasync);
            auto const second = co_await store_seq(*store, 2);
            EXPECT_TRUE(!second && second.error() == error::store_io_failure)
                << "store(2) must return store_io_failure";
            EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 1)
                << "the seam did not fire for store(2)";
            EXPECT_EQ(fixpp::session::read_and_reset_restore_datasync_count(), 1)
                << "the restore's datasync did not run and succeed for store(2)";
            // D-635-1: a failed store leaves no byte past the log's committed end.
            EXPECT_EQ(log_size(fx), committed_end) << "the log's size after the failed store(2)";
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
            auto const committed_end = log_size(fx);
            fixpp::session::arm_force_offload_write_fail_once(pwrite_nth, datasync);
            auto const second = co_await store->next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(!second && second.error() == error::store_io_failure)
                << "the second increment must return store_io_failure";
            EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 1)
                << "the seam did not fire for the second increment";
            EXPECT_EQ(fixpp::session::read_and_reset_restore_datasync_count(), 1)
                << "the restore's datasync did not run and succeed for the second increment";
            // D-635-1: a failed increment leaves no byte past the log's committed end.
            EXPECT_EQ(log_size(fx), committed_end) << "the log's size after the failed increment";
        }
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failure must accept the log";
        EXPECT_EQ(again.next_outbound, next_outbound) << "next outbound number after reopen";
        EXPECT_TRUE(again.frames.empty()) << "frames after reopen:" << text(again.frames);
    });
}

// ── store() ───────────────────────────────────────────────────────────────────

// D-635-1: every arm of a failed store() restores the log's committed end, so the reopen
// finds store(1)'s records only, whatever the failed job wrote before it failed.
TEST(StoreOffloadErrorArms, Store_FrameHeaderPwriteFails) { store_arm(1, false, {1}, false, 2); }

// The seam fails the frame header's pwrite before it writes a byte, so the log's size
// cannot show whether this arm restores the committed end. Arming the restore's truncate
// fault as well can: the restore runs only if this arm calls it, and when it runs and
// fails the store is poisoned. (No shorter-retry cell for this arm: nothing of the failed
// call is on disk to be left behind a shorter record.)
TEST(StoreOffloadErrorArms, Store_FrameHeaderPwriteFails_RunsTheRestore) {
    disarm_on_exit const disarm;
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto store = fx.open();
        EXPECT_NE(store, nullptr) << "setup: the FileStore";
        if (!store) co_return;
        EXPECT_TRUE((co_await store_seq(*store, 1)).has_value()) << "setup: store(1)";
        fixpp::session::arm_force_offload_write_fail_once(1, false);  // the frame header
        fixpp::session::arm_force_offload_restore_fail_once(true, false);
        auto const r = co_await store_seq(*store, 2);
        EXPECT_TRUE(!r && r.error() == error::store_io_failure)
            << "store(2) must return store_io_failure";
        EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 2)
            << "the frame header's fault and the restore's fault must both fire";
        auto const later = co_await store_seq(*store, 2);
        EXPECT_TRUE(!later && later.error() == error::store_io_failure)
            << "the failed restore must poison the store";
    });
}

TEST(StoreOffloadErrorArms, Store_PayloadPwriteFails) { store_arm(2, false, {1}, false, 2); }

TEST(StoreOffloadErrorArms, Store_PaddingPwriteFails) { store_arm(3, false, {1}, false, 2); }

TEST(StoreOffloadErrorArms, Store_CounterHeaderPwriteFails) { store_arm(4, false, {1}, false, 2); }

TEST(StoreOffloadErrorArms, Store_CounterPayloadPwriteFails) { store_arm(5, false, {1}, false, 2); }

// The records were written and the flush reported failure: the store reports failure, so
// the records are removed with the rest.
TEST(StoreOffloadErrorArms, Store_DatasyncFails) { store_arm(0, true, {1}, false, 2); }

// The job's datasync fault is the job's own flush only: a job that fails at a pwrite never
// reaches its flush, and its restore's datasync does not take that fault. So the restore
// succeeds, the store is not poisoned, and the fault stays untaken.
TEST(StoreOffloadErrorArms, Store_PwriteFails_TheRestoreDoesNotTakeTheJobsDatasyncFault) {
    disarm_on_exit const disarm;
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto store = fx.open();
        EXPECT_NE(store, nullptr) << "setup: the FileStore";
        if (!store) co_return;
        EXPECT_TRUE((co_await store_seq(*store, 1)).has_value()) << "setup: store(1)";
        fixpp::session::arm_force_offload_write_fail_once(2, true);  // the payload, and the flush
        auto const r = co_await store_seq(*store, 2);
        EXPECT_TRUE(!r && r.error() == error::store_io_failure)
            << "store(2) must return store_io_failure";
        EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 1)
            << "only the payload's fault may fire";
        EXPECT_EQ(fixpp::session::read_and_reset_restore_datasync_count(), 1)
            << "the restore's datasync did not run and succeed";
        EXPECT_TRUE((co_await store_seq(*store, 2)).has_value())
            << "the restore succeeded, so a later store(2) must be accepted";
    });
}

// ── next_seqnum(outbound, increment) ──────────────────────────────────────────

// D-635-1: every arm of a failed increment restores the log's committed end, so the
// reopen reads the first increment's counter record.
TEST(StoreOffloadErrorArms, NextSeqnum_CounterHeaderPwriteFails) { next_seqnum_arm(1, false, 2); }

TEST(StoreOffloadErrorArms, NextSeqnum_CounterPayloadPwriteFails) { next_seqnum_arm(2, false, 2); }

TEST(StoreOffloadErrorArms, NextSeqnum_DatasyncFails) { next_seqnum_arm(0, true, 2); }

// ── reset(): one cell per step of the reset's job (fixpp#611, fixpp#599) ───────

// store(1) and store(2), then reset() with `step` failing, then store(3).
void reset_arm(int const step, bool const poisons) {
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

            fixpp::session::arm_force_reset_step_fault_once(step);
            bool reset_threw = false;
            fixpp::core::expected_t<void> reset{};
            MessageStore& ms = *store;
            try {
                reset = co_await ms.reset();
            } catch (std::exception const&) {
                reset_threw = true;
            }
            EXPECT_EQ(fixpp::session::read_and_reset_reset_atomicity_fault_count(), 1)
                << "the reset-step fault did not fire";
            EXPECT_FALSE(reset_threw) << "the reset's job threw, and the reset passed it on";
            EXPECT_TRUE(!reset_threw && !reset && reset.error() == error::store_io_failure)
                << "the reset must return store_io_failure";

            auto const later = co_await store_seq(*store, 3);
            if (poisons) {
                EXPECT_TRUE(!later && later.error() == error::store_io_failure)
                    << "store(3) after a failure past the rename must be refused; it returned "
                    << (later ? "success" : "an error other than store_io_failure");
            } else {
                EXPECT_TRUE(later.has_value())
                    << "store(3) after a failure before the rename must be accepted";
            }
        }
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failed reset must accept the log";
        if (poisons) {
            // The live name is the reset's fresh log: no frame, next outbound 1. store(3)
            // is not in it, whatever store(3) returned.
            EXPECT_EQ(again.next_outbound, seqnum_t{1}) << "next outbound number after reopen";
            EXPECT_TRUE(again.frames.empty()) << "frames after reopen:" << text(again.frames);
        } else {
            // The old log is still the live one, with store(3) appended.
            EXPECT_EQ(again.next_outbound, seqnum_t{4}) << "next outbound number after reopen";
            EXPECT_EQ(again.frames, (std::vector<seqnum_t>{1, 2, 3}))
                << "frames after reopen:" << text(again.frames);
            EXPECT_TRUE(again.frames_intact) << "a frame read back differs from what was stored";
        }
    });
}

TEST(ResetStepArms, InitialiseFreshFails_TheOldLogStands) {
    reset_arm(fixpp::session::kResetStepFaultInitialiseFresh, false);
}

TEST(ResetStepArms, RenameFails_TheOldLogStands) {
    reset_arm(fixpp::session::kResetStepFaultRename, false);
}

TEST(ResetStepArms, ReopenOpenFails_PoisonsTheStore) {
    reset_arm(fixpp::session::kResetStepFaultReopenOpen, true);
}

TEST(ResetStepArms, ReopenLockFails_PoisonsTheStore) {
    reset_arm(fixpp::session::kResetStepFaultReopenLock, true);
}

TEST(ResetThrowAfterRename, PoisonsTheStoreAndReturnsTheResetsError) {
    reset_arm(fixpp::session::kResetStepFaultThrowAfterRename, true);
}

// ── A failed store()'s frame record, then a second store() of the same number ─────────
//
// Fixes #632 (D-632-1 in .specify/624-offload-completion-lifetime.md): the log holds a
// complete frame record for n with no counter record after it, as a crash between the two
// leaves it, and the reopened store's next number is still n. (In a live process a failed
// store restores the log's end, D-635-1; the cells reach that state by failing the restore
// too, through arm_force_offload_restore_fail_once.) A second store(n), with
// different bytes, appends another record for n. The last record of a number wins: a lookup
// of n returns the second store's frame, in the process that stored it and after every
// reopen, and the numbers after n are looked up at their own place.

// What one direction holds: the frames a range walk visits, with the stored bytes checked
// against `expected` by number.
struct walked {
    bool ok = false;
    std::vector<seqnum_t> seqs;
    bool bytes_match = true;
};

asio::awaitable<walked> walk(MessageStore& store, direction_t const dir, seqnum_t const begin,
                             seqnum_t const end,
                             std::vector<std::vector<std::byte>> const& expected) {
    walked out;
    byte_collecting_visitor v;
    out.ok = (co_await store.retrieve(begin, end, dir, v)).has_value();
    for (auto const& e : v.entries()) {
        out.seqs.push_back(e.seq);
        if (e.seq >= expected.size() || e.bytes != expected[e.seq]) out.bytes_match = false;
    }
    co_return out;
}

void last_record_of_a_number_wins(direction_t const dir) {
    disarm_on_exit const disarm;
    Fixture fx;
    // expected[n]: the bytes a lookup of n must return once every store below has run.
    std::vector<std::vector<std::byte>> expected(4);
    expected[1] = make_test_frame(1, dir);
    auto const failed = make_test_frame(2, dir);
    expected[2] = make_test_frame(2, dir, 8);
    expected[3] = make_test_frame(3, dir);
    ASSERT_NE(failed, expected[2]) << "precondition: the two frames for 2 differ";
    auto const put = [&](MessageStore& store, seqnum_t const seq,
                         std::vector<std::byte> const& frame) {
        return store.store(seq, std::span<const std::byte>{frame}, dir);
    };
    // The lookups after each step: 2 alone, 3 alone, and the walk from 1 to the tail.
    auto const check = [&](MessageStore& store, char const* when,
                           seqnum_t const last) -> asio::awaitable<void> {
        SCOPED_TRACE(when);
        auto const two = co_await walk(store, dir, 2, 2, expected);
        EXPECT_TRUE(two.ok && two.seqs == std::vector<seqnum_t>{2} && two.bytes_match)
            << "retrieve(2, 2) must return the second store(2)'s frame; visited" << text(two.seqs)
            << (two.bytes_match ? "" : ", with other bytes");
        if (last >= 3) {
            auto const three = co_await walk(store, dir, 3, 3, expected);
            EXPECT_TRUE(three.ok && three.seqs == std::vector<seqnum_t>{3} && three.bytes_match)
                << "retrieve(3, 3) must return store(3)'s frame; visited" << text(three.seqs)
                << (three.bytes_match ? "" : ", with other bytes");
        }
        std::vector<seqnum_t> each;
        for (seqnum_t s = 1; s <= last; ++s) each.push_back(s);
        auto const all = co_await walk(store, dir, 1, 0, expected);
        EXPECT_TRUE(all.ok && all.seqs == each && all.bytes_match)
            << "the walk to the tail must visit each number once, with its last frame; visited"
            << text(all.seqs) << (all.bytes_match ? "" : ", with other bytes");
    };
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            EXPECT_TRUE((co_await put(*store, 1, expected[1])).has_value()) << "setup: store(1)";
            // The state a crash leaves between the frame record and the counter record:
            // store(2) fails at its counter write and its restore of the committed end
            // (D-635-1) fails too, so the frame record stays and the store is poisoned
            // until the next open.
            fixpp::session::arm_force_offload_write_fail_once(4, false);  // counter header
            fixpp::session::arm_force_offload_restore_fail_once(true, false);
            auto const r = co_await put(*store, 2, failed);
            EXPECT_TRUE(!r && fixpp::session::read_and_reset_offload_write_fail_count() == 2)
                << "setup: store(2) fails at its counter write, and its restore fails";
        }
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "the first reopen";
            if (!store) co_return;
            EXPECT_TRUE((co_await put(*store, 2, expected[2])).has_value())
                << "the second store(2)";
            co_await check(*store, "after the second store(2), same process", 2);
            EXPECT_TRUE((co_await put(*store, 3, expected[3])).has_value()) << "store(3)";
            co_await check(*store, "after store(3), same process", 3);
        }
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "the second reopen";
            if (!store) co_return;
            co_await check(*store, "after the second reopen", 3);
        }
        auto store = fx.open();
        EXPECT_NE(store, nullptr) << "the third reopen";
        if (!store) co_return;
        co_await check(*store, "after the third reopen", 3);
    });
}

TEST(StoreOffloadErrorArms, FailedFrameRecordThenASecondStore_LastRecordWins_Outbound) {
    last_record_of_a_number_wins(direction_t::outbound);
}

TEST(StoreOffloadErrorArms, FailedFrameRecordThenASecondStore_LastRecordWins_Inbound) {
    last_record_of_a_number_wins(direction_t::inbound);
}

// D-632-1: store() admits only the next number, so no log it writes holds a frame record
// below a direction's last number. A log that does is refused at open. The log is built
// by hand: the record for 2 is copied, byte for byte, to the end of a log that holds 1, 2
// and 3. A record's CRC covers its header and payload and not its offset, so the copy is
// a valid record, and the open meets it after 3.
TEST(StoreOffloadErrorArms, AFrameRecordBelowTheLastNumber_RefusesTheOpen) {
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        auto store = fx.open();
        EXPECT_NE(store, nullptr) << "setup: the FileStore";
        if (!store) co_return;
        for (seqnum_t s = 1; s <= 3; ++s) {
            EXPECT_TRUE((co_await store_seq(*store, s)).has_value()) << "setup: store(" << s << ")";
        }
    });

    std::filesystem::path log;
    for (auto const& e : std::filesystem::directory_iterator(fx.dir.path)) {
        if (e.path().extension() == ".log") log = e.path();
    }
    ASSERT_FALSE(log.empty()) << "setup: the store's log file";
    std::vector<char> bytes;
    {
        std::ifstream in{log, std::ios::binary};
        bytes.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    // Walk the records: a 16-byte header (kind, dir, two reserved bytes, then seq and len,
    // little-endian u32 each, then the CRC), the payload, padding to 8 bytes. kind 0 is a
    // frame, dir 1 outbound.
    auto const u32 = [&](std::size_t const at) {
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + k])) << (8 * k);
        }
        return v;
    };
    std::vector<char> record_two;
    for (std::size_t at = 0; at + kHeaderBytes <= bytes.size();) {
        std::size_t const len = u32(at + 8);
        std::size_t const size = (kHeaderBytes + len + kAlign - 1) / kAlign * kAlign;
        if (bytes[at] == 0 && bytes[at + 1] == 1 && u32(at + 4) == 2) {
            record_two.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                              bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
        }
        at += size;
    }
    ASSERT_FALSE(record_two.empty()) << "setup: the frame record for 2 in the log";
    {
        std::ofstream out{log, std::ios::binary | std::ios::app};
        out.write(record_two.data(), static_cast<std::streamsize>(record_two.size()));
    }
    EXPECT_EQ(fx.open(), nullptr) << "the open must refuse a frame record below the last number";
}

// ── A store() retried in the same process after it failed (fixpp#635, D-635-1) ──────────
//
// store(2) fails at one arm. The retry store(2) runs in the SAME process, from the same
// write position, with a frame of its own, then store(3) runs, then the store is opened
// again. The reopen must accept the log, with next outbound 4 and the frames 1, the retry's
// 2 and 3. A retry shorter than the failed frame is the case fixpp#635 found: before
// D-635-1 the failed record's last bytes stayed behind store(3) and the open refused the
// log. Equal and longer retries are the controls.
void retry_after_a_failed_store(int const pwrite_nth, bool const datasync,
                                std::vector<std::byte> const& retry) {
    disarm_on_exit const disarm;
    Fixture fx;
    auto const failed = make_test_frame(2, direction_t::outbound, 200);
    std::vector<std::vector<std::byte>> expected(4);
    expected[1] = make_test_frame(1, direction_t::outbound);
    expected[2] = retry;
    expected[3] = make_test_frame(3, direction_t::outbound);
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            EXPECT_TRUE((co_await store_seq(*store, 1)).has_value()) << "setup: store(1)";
            fixpp::session::arm_force_offload_write_fail_once(pwrite_nth, datasync);
            auto const r =
                co_await store->store(2, std::span<const std::byte>{failed}, direction_t::outbound);
            EXPECT_TRUE(!r && fixpp::session::read_and_reset_offload_write_fail_count() == 1)
                << "setup: store(2) fails at the armed step";
            auto const two =
                co_await store->store(2, std::span<const std::byte>{retry}, direction_t::outbound);
            EXPECT_TRUE(two.has_value()) << "the retry store(2)";
            EXPECT_TRUE((co_await store_seq(*store, 3)).has_value()) << "store(3)";
        }
        auto const store = fx.open();
        EXPECT_NE(store, nullptr) << "the reopen must accept the log";
        if (!store) co_return;
        auto const next = co_await store->next_seqnum(direction_t::outbound, false);
        EXPECT_TRUE(next && *next == 4) << "next outbound after reopen";
        auto const all = co_await walk(*store, direction_t::outbound, 1, 0, expected);
        EXPECT_TRUE(all.ok && all.seqs == (std::vector<seqnum_t>{1, 2, 3}) && all.bytes_match)
            << "the walk after reopen visited" << text(all.seqs)
            << (all.bytes_match ? "" : ", with other bytes than stored last");
    });
}

std::vector<std::byte> shorter_retry() { return make_test_frame(2, direction_t::outbound); }

TEST(StoreOffloadErrorArms, RetryInTheSameProcess_Shorter) {
    retry_after_a_failed_store(4, false, shorter_retry());
}

TEST(StoreOffloadErrorArms, RetryInTheSameProcess_EqualLength) {
    auto frame = make_test_frame(2, direction_t::outbound, 200);
    frame.back() = std::byte{0xEE};  // the same length, different bytes
    retry_after_a_failed_store(4, false, frame);
}

TEST(StoreOffloadErrorArms, RetryInTheSameProcess_Longer) {
    retry_after_a_failed_store(4, false, make_test_frame(2, direction_t::outbound, 400));
}

// One shorter retry per other failing arm of store().
TEST(StoreOffloadErrorArms, RetryAfterPayloadPwriteFails_Shorter) {
    retry_after_a_failed_store(2, false, shorter_retry());
}

TEST(StoreOffloadErrorArms, RetryAfterPaddingPwriteFails_Shorter) {
    retry_after_a_failed_store(3, false, shorter_retry());
}

TEST(StoreOffloadErrorArms, RetryAfterCounterPayloadPwriteFails_Shorter) {
    retry_after_a_failed_store(5, false, shorter_retry());
}

TEST(StoreOffloadErrorArms, RetryAfterDatasyncFails_Shorter) {
    retry_after_a_failed_store(0, true, shorter_retry());
}

// ── The restore of the committed end fails (fixpp#635, D-635-1) ───────────────────────
//
// store(2) fails at its counter write, and the job's restore of the log's committed end
// fails at its truncate or at the datasync after it. The store is poisoned: a later store()
// and a later increment are refused, and nothing more is written to the log. The reopen
// accepts the log; after a failed truncate it holds store(2)'s frame record with no counter
// record (read by its own number, D-632-1), after a failed datasync it ends at the committed
// end.
void restore_fails(bool const truncate, bool const frame2_on_disk) {
    disarm_on_exit const disarm;
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            EXPECT_TRUE((co_await store_seq(*store, 1)).has_value()) << "setup: store(1)";
            fixpp::session::arm_force_offload_write_fail_once(4, false);  // counter header
            fixpp::session::arm_force_offload_restore_fail_once(truncate, !truncate);
            auto const r = co_await store_seq(*store, 2);
            EXPECT_TRUE(!r && r.error() == error::store_io_failure)
                << "store(2) must return store_io_failure";
            EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 2)
                << "the write fault and the restore fault must both fire";
            EXPECT_EQ(fixpp::session::read_and_reset_restore_datasync_count(), 0)
                << "a restore that failed reported its datasync as made";
            auto const after_failure = log_size(fx);
            auto const again = co_await store_seq(*store, 2);
            EXPECT_TRUE(!again && again.error() == error::store_io_failure)
                << "a store() after a failed restore must be refused";
            auto const increment = co_await store->next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(!increment && increment.error() == error::store_io_failure)
                << "an increment after a failed restore must be refused";
            EXPECT_EQ(log_size(fx), after_failure)
                << "nothing may be written to the log after a failed restore";
        }
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failed restore must accept the log";
        EXPECT_EQ(again.next_outbound, seqnum_t{2}) << "next outbound number after reopen";
        EXPECT_EQ(again.frames, (std::vector<seqnum_t>{1}))
            << "frames after reopen:" << text(again.frames);
        EXPECT_EQ(again.frame2_on_disk, frame2_on_disk) << "retrieve(2, 2) after reopen";
    });
}

TEST(StoreOffloadErrorArms, RestoreTruncateFails_PoisonsTheStore) { restore_fails(true, true); }

TEST(StoreOffloadErrorArms, RestoreDatasyncFails_PoisonsTheStore) { restore_fails(false, false); }

// The same for a failed increment: next_seqnum(increment) fails at its counter payload,
// after its counter header is written, and the restore's truncate fails.
TEST(StoreOffloadErrorArms, IncrementRestoreTruncateFails_PoisonsTheStore) {
    disarm_on_exit const disarm;
    Fixture fx;
    run_on_pool(fx.pool, [&]() -> asio::awaitable<void> {
        {
            auto store = fx.open();
            EXPECT_NE(store, nullptr) << "setup: the FileStore";
            if (!store) co_return;
            EXPECT_TRUE((co_await store_seq(*store, 1)).has_value()) << "setup: store(1)";
            fixpp::session::arm_force_offload_write_fail_once(2, false);  // counter payload
            fixpp::session::arm_force_offload_restore_fail_once(true, false);
            auto const r = co_await store->next_seqnum(direction_t::outbound, true);
            EXPECT_TRUE(!r && r.error() == error::store_io_failure)
                << "the increment must return store_io_failure";
            EXPECT_EQ(fixpp::session::read_and_reset_offload_write_fail_count(), 2)
                << "the write fault and the restore fault must both fire";
            auto const after_failure = log_size(fx);
            auto const later = co_await store_seq(*store, 2);
            EXPECT_TRUE(!later && later.error() == error::store_io_failure)
                << "a store() after a failed restore must be refused";
            EXPECT_EQ(log_size(fx), after_failure)
                << "nothing may be written to the log after a failed restore";
        }
        auto const again = co_await reopen(fx);
        EXPECT_TRUE(again.accepted) << "the open after the failed restore must accept the log";
        EXPECT_EQ(again.next_outbound, seqnum_t{2}) << "next outbound number after reopen";
    });
}

}  // namespace
