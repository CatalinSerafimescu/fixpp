// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_538_resend_answer_store_failure.cpp
//
// fixpp#538 (.specify/536-537-538-recovery-liveness.md §2.2, D-538-4; §5, the table
// "#538"): what the resend answer does with the result of a store read, by the code the
// read returns.
//
//   not retained (store_seqnum_gap)             the slot joins the GapFill run;
//   a refused argument (store_seqnum_invalid,   the slot joins the GapFill run and the
//   store_invalid_range)                        session stays up (ruling R-10);
//   every other code, store_cancelled among     the answer stops there, nothing more is
//   them (rulings R-7, R-13)                    written, and on_inbound_frame returns
//                                               the store's own code. The walk's first
//                                               store call, the read of the store's
//                                               next outbound number, is under the same
//                                               rule.
//
// Every cell drives a Session with no Engine (logon_arm_fixtures.hpp): an acceptor on a
// MemoryStore behind the recording wrapper (tests/support/recording_store.hpp), which
// returns a chosen code in place of forwarding one chosen call; a transport callback
// that captures every frame the session writes; and the session's event ring. The cells
// an Engine's read pump drives are in test_538_resend_answer_store_failure_engine.cpp.
//
// The unbuildable-replay control of D-538-4's row "class 1 controls" is an existing
// cell, ResendAnswerReplayTest.Replay_NoClockAndNoStoredSendingTime_SlotIsGapFilled
// (test_resend_answer_field_order.cpp); it is not copied here.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "session/logon_arm_fixtures.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/calling_thread_readings.hpp"
#include "support/recording_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::core::error;
using fixpp::session::close_mode;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_event_resend_slot_gap_filled;
using fixpp::session::session_test_access;
using fixpp::test_support::CallingThreadReadings;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;
using fixpp::test_support::logon_arm::DirectAcceptor;
using fixpp::test_support::logon_arm::ring_text;

// The acceptor's reply Logon is its number 1, so the application messages a cell sends
// are stored from 2.
constexpr seqnum_t kFirstApp = 2;

std::string record_text(std::optional<StoreCall> const& c) {
    if (!c) return "not called";
    if (!c->entered()) return "called, and its body never started";
    if (!c->returned()) return "entered, and never returned";
    return c->error ? "returned error " + std::to_string(static_cast<int>(*c->error))
                    : std::string{"returned a value"};
}

// `frames`, one per line, with SOH shown as '|': for a failure message.
std::string printable(std::vector<std::string> const& frames) {
    std::string out;
    for (auto const& f : frames) {
        for (char const c : f) out += (c == '\x01') ? '|' : c;
        out += '\n';
    }
    return out.empty() ? std::string{"(no frame)\n"} : out;
}

bool is_tail_read(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::outbound && !call.increment;
}
std::function<bool(StoreCall const&)> is_slot_read(seqnum_t const slot) {
    return [slot](StoreCall const& call) {
        return call.op == store_op::retrieve && call.dir == direction_t::outbound &&
               call.first == slot;
    };
}

// The frames of `frames` that replay an application message: MsgType D with
// PossDupFlag(43)=Y, as their MsgSeqNum(34) values in order.
std::vector<std::string> replayed_numbers(std::vector<std::string> const& frames) {
    std::vector<std::string> out;
    for (auto const& f : frames) {
        if (plain_rig::field_value(f, "35") == "D" && plain_rig::field_value(f, "43") == "Y") {
            out.push_back(plain_rig::field_value(f, "34"));
        }
    }
    return out;
}
std::size_t count_of_type(std::vector<std::string> const& frames, std::string_view const type) {
    return static_cast<std::size_t>(std::ranges::count_if(
        frames, [type](auto const& f) { return plain_rig::field_value(f, "35") == type; }));
}

// What a cell read after the peer's ResendRequest was handed to on_inbound_frame.
struct Answer {
    CallingThreadReadings call;
    fsm_state state_after_call = fsm_state::NotConnected;
    std::vector<std::string> written;  // every frame the session wrote for the request
    std::optional<StoreCall> aimed;    // the first store call the cell's predicate accepts
    bool gap_filled_event = false;     // a session_event_resend_slot_gap_filled was recorded
};

// An acceptor in Active that has sent `apps` application messages, stored from kFirstApp.
struct ActiveSession {
    // What a coroutine of the fixture refers to is declared before `f`, so it outlives
    // the drain ~DirectAcceptor runs.
    std::vector<std::vector<std::byte>> payloads;  // each outlives its send
    std::vector<std::byte> request_frame;
    asio::cancellation_signal none;
    Answer pending;
    DirectAcceptor f{DirectAcceptor::Options{}};

    [[nodiscard]] bool setup(unsigned const apps) {
        if (!f.open()) return false;
        CallingThreadReadings logon_call;
        auto const logon = f.logon();
        if (!f.feed_plain(logon, logon_call) || f.session->state() != fsm_state::Active) {
            return false;
        }
        for (unsigned i = 0; i < apps; ++i) {
            payloads.push_back(
                plain_rig::to_bytes("35=D\x01"
                                    "11=ORD" +
                                    std::to_string(i) +
                                    "\x01"
                                    "55=AAPL\x01"));
            auto fut =
                asio::co_spawn(f.ioc, f.session->send(std::span<const std::byte>{payloads.back()}),
                               asio::use_future);
            if (!f.run_until([&] { return DirectAcceptor::ready(fut); }) ||
                !fut.get().has_value()) {
                return false;
            }
        }
        f.sent.clear();
        f.log->arm();
        return true;
    }

    // Hands the peer's ResendRequest (its number 2) with the fields `request` to
    // on_inbound_frame and reads the outcome. `aimed_at` selects the store call the cell
    // reports on.
    [[nodiscard]] Answer answer(std::string_view const request,
                                std::function<bool(StoreCall const&)> const& aimed_at) {
        Answer& a = pending;
        auto const mark = f.log->mark();
        request_frame = f.msg("2", 2, request);
        auto fut = f.feed(request_frame, a.call, none,
                          [this] { pending.state_after_call = f.session->state(); });
        (void)f.run_until([&] { return DirectAcceptor::ready(fut); });
        if (auto const* r = f.log->first_after(mark, aimed_at)) a.aimed = *r;
        a.written = f.sent;
        a.gap_filled_event = std::ranges::any_of(f.session->recent_events(), [](auto const& ev) {
            return std::holds_alternative<session_event_resend_slot_gap_filled>(ev);
        });
        return a;
    }
};

std::string range_request(seqnum_t const begin, seqnum_t const end) {
    return "7=" + std::to_string(begin) + "\x01" + "16=" + std::to_string(end) + "\x01";
}

// ── A slot's retrieve fails ──────────────────────────────────────────────────────────

constexpr unsigned kApps = 3;
constexpr seqnum_t kFailedSlot = kFirstApp + 1;  // a stored message on each side of it
constexpr seqnum_t kLastApp = kFirstApp + kApps - 1;

// The wrapper returns `code` for the retrieve of kFailedSlot while the session answers
// a ResendRequest over the three stored application messages.
void run_failed_slot_read(error const code, Answer& a) {
    ActiveSession s;
    ASSERT_TRUE(s.setup(kApps)) << "setup: the session did not reach Active and send";
    s.f.log->fail_in = is_slot_read(kFailedSlot);
    s.f.log->fail_code = code;
    a = s.answer(range_request(kFirstApp, kLastApp), is_slot_read(kFailedSlot));
    ASSERT_TRUE(a.call.completed && a.aimed.has_value() && a.aimed->returned() &&
                a.aimed->error == code)
        << "the path was not exercised: call_completed=" << a.call.completed
        << " the slot's read: " << record_text(a.aimed);
}

// What every aborted answer asserts: the session ended at the slot with the store's own
// code, and the replay of the stored message before the slot is the only frame written.
void expect_aborted_at_the_slot(Answer const& a, error const code) {
    EXPECT_TRUE(a.call.returned_error(code)) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_EQ(replayed_numbers(a.written), (std::vector<std::string>{std::to_string(kFirstApp)}))
        << "the stored message before the slot must be replayed, and none after it:\n"
        << printable(a.written);
    EXPECT_EQ(count_of_type(a.written, "4"), 0U) << "a GapFill was written:\n"
                                                 << printable(a.written);
    EXPECT_EQ(a.written.size(), 1U) << "a frame was written beyond the replay:\n"
                                    << printable(a.written);
}

// D-538-4, "a wrapper whose retrieve returns store_io_failure for one slot".
TEST(ResendAnswerStoreFailure, SlotReadFailsWithStoreIoFailure_AbortsAndEndsTheSession) {
    Answer a;
    ASSERT_NO_FATAL_FAILURE(run_failed_slot_read(error::store_io_failure, a));
    expect_aborted_at_the_slot(a, error::store_io_failure);
    EXPECT_EQ(count_of_type(a.written, "5"), 0U) << "a Logout was written";
    EXPECT_FALSE(a.gap_filled_event) << "a session_event_resend_slot_gap_filled was recorded";
}

// D-538-4, "a cancelled read (R-13)", the slot input: the code a store returns for a
// read that a cancellation ended.
TEST(ResendAnswerStoreFailure, SlotReadReturnsStoreCancelled_AbortsAndEndsTheSession) {
    Answer a;
    ASSERT_NO_FATAL_FAILURE(run_failed_slot_read(error::store_cancelled, a));
    expect_aborted_at_the_slot(a, error::store_cancelled);
}

// D-538-4, "class 3 from another code": a code outside the two folded classes that no
// shipped retrieve returns. Re-derive the shipped set from the
// `co_return std::unexpected` sites of MemoryStore::retrieve and FileStore::retrieve.
TEST(ResendAnswerStoreFailure, SlotReadFailsWithACodeNoShippedStoreReturns_AbortsWithThatCode) {
    Answer a;
    ASSERT_NO_FATAL_FAILURE(run_failed_slot_read(error::store_capacity_exhausted, a));
    expect_aborted_at_the_slot(a, error::store_capacity_exhausted);
}

// D-538-4, class 2 (R-10), the second refused-argument code: the wrapper returns
// store_invalid_range for the slot's retrieve. The slot joins the GapFill run between the
// two replays and the session stays up.
TEST(ResendAnswerStoreFailure,
     SlotReadRefusedWithStoreInvalidRange_FoldsIntoAGapFillAndStaysActive) {
    Answer a;
    ASSERT_NO_FATAL_FAILURE(run_failed_slot_read(error::store_invalid_range, a));
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active) << "the state after the call";
    EXPECT_EQ(replayed_numbers(a.written),
              (std::vector<std::string>{std::to_string(kFirstApp), std::to_string(kLastApp)}))
        << "the stored messages on both sides of the slot must be replayed:\n"
        << printable(a.written);
    ASSERT_EQ(a.written.size(), 3U) << printable(a.written);
    auto const& gap_fill = a.written[1];
    EXPECT_EQ(plain_rig::field_value(gap_fill, "35"), "4") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(gap_fill, "123"), "Y") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(gap_fill, "34"), std::to_string(kFailedSlot))
        << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(gap_fill, "36"), std::to_string(kFailedSlot + 1))
        << printable(a.written);
}

// ── The tail read fails ──────────────────────────────────────────────────────────────

// The wrapper returns `code` for the walk's first store call, the read of the store's
// next outbound number.
void run_failed_tail_read(error const code, Answer& a) {
    ActiveSession s;
    ASSERT_TRUE(s.setup(kApps)) << "setup: the session did not reach Active and send";
    s.f.log->fail_in = is_tail_read;
    s.f.log->fail_code = code;
    a = s.answer(range_request(kFirstApp, kLastApp), is_tail_read);
    ASSERT_TRUE(a.call.completed && a.aimed.has_value() && a.aimed->returned() &&
                a.aimed->error == code)
        << "the path was not exercised: call_completed=" << a.call.completed
        << " the tail read: " << record_text(a.aimed);
}

void expect_nothing_written_and_ended(Answer const& a, error const code) {
    EXPECT_TRUE(a.call.returned_error(code)) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Disconnected) << "the state after the call";
    EXPECT_TRUE(a.written.empty()) << "a frame was written:\n" << printable(a.written);
}

// D-538-4, "the tail read".
TEST(ResendAnswerStoreFailure, TailReadFailsWithStoreIoFailure_WritesNothingAndEndsTheSession) {
    Answer a;
    ASSERT_NO_FATAL_FAILURE(run_failed_tail_read(error::store_io_failure, a));
    expect_nothing_written_and_ended(a, error::store_io_failure);
}

// D-538-4, "a cancelled read (R-13)", the tail input.
TEST(ResendAnswerStoreFailure, TailReadReturnsStoreCancelled_WritesNothingAndEndsTheSession) {
    Answer a;
    ASSERT_NO_FATAL_FAILURE(run_failed_tail_read(error::store_cancelled, a));
    expect_nothing_written_and_ended(a, error::store_cancelled);
}

// ── Class 1: a number the store does not retain ──────────────────────────────────────

// D-538-4, "class 1 controls", the plain gap. Two messages are stored; then the
// store's outbound counter and the session's are advanced by one with no frame stored,
// so the number after them is one the store was never given. The shipped store reports
// it as a gap; the slot is folded and the session stays up.
TEST(ResendAnswerStoreFailure, Control_SlotNotRetained_FoldsIntoAGapFillAndStaysActive) {
    constexpr unsigned kStored = 2;
    constexpr seqnum_t kHole = kFirstApp + kStored;
    ActiveSession s;
    ASSERT_TRUE(s.setup(kStored)) << "setup: the session did not reach Active and send";
    auto* const store = session_test_access::store(*s.f.session);
    ASSERT_NE(store, nullptr);
    auto advanced =
        asio::co_spawn(s.f.ioc, store->next_seqnum(direction_t::outbound, true), asio::use_future);
    ASSERT_TRUE(s.f.run_until([&] { return DirectAcceptor::ready(advanced); }));
    ASSERT_TRUE(advanced.get().has_value()) << "setup: the store's outbound counter";
    fixpp::session::seqnum_manager_test_access::set_counters(
        session_test_access::seqnum_mgr(*s.f.session), 2, kHole + 1);

    auto const a = s.answer(range_request(kFirstApp, kHole), is_slot_read(kHole));
    ASSERT_TRUE(a.call.completed && a.aimed.has_value() && a.aimed->returned() &&
                a.aimed->error == error::store_seqnum_gap)
        << "the path was not exercised: the shipped store must report the hole as a gap; its "
           "read: "
        << record_text(a.aimed);

    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);
    EXPECT_EQ(replayed_numbers(a.written),
              (std::vector<std::string>{std::to_string(kFirstApp), std::to_string(kFirstApp + 1)}))
        << printable(a.written);
    ASSERT_EQ(count_of_type(a.written, "4"), 1U) << printable(a.written);
    auto const& gap_fill = a.written.back();
    EXPECT_EQ(plain_rig::field_value(gap_fill, "35"), "4") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(gap_fill, "123"), "Y") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(gap_fill, "34"), std::to_string(kHole))
        << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(gap_fill, "36"), std::to_string(kHole + 1))
        << printable(a.written);
    EXPECT_FALSE(a.gap_filled_event) << "a plain gap must fold with no event";
}

// ── Class 2 (R-10): slot 0 ───────────────────────────────────────────────────────────

// D-538-4, "class 2 (R-10), slot 0". `request` asks from number 0, through the current
// end, over a store that holds the reply Logon and two application messages. The
// validate gate (SessionConfig::validate_inbound_messages) is off, its default.
void run_slot_zero(std::string_view const request) {
    constexpr unsigned kStored = 2;
    ActiveSession s;
    ASSERT_TRUE(s.setup(kStored)) << "setup: the session did not reach Active and send";
    ASSERT_FALSE(s.f.cfg.validate_inbound_messages);
    auto const a = s.answer(request, is_slot_read(0));
    ASSERT_TRUE(a.call.completed && a.aimed.has_value() && a.aimed->returned() &&
                a.aimed->error == error::store_seqnum_invalid)
        << "the path was not exercised: the walk must ask the shipped store for number 0 and "
           "be refused; that read: "
        << record_text(a.aimed);

    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);
    EXPECT_EQ(replayed_numbers(a.written),
              (std::vector<std::string>{std::to_string(kFirstApp), std::to_string(kFirstApp + 1)}))
        << printable(a.written);
    ASSERT_FALSE(a.written.empty());
    // fixpp#580 flips this assertion: it clamps the per-slot walk's start to 1.
    EXPECT_TRUE(plain_rig::field_value(a.written.front(), "35") == "4" &&
                plain_rig::field_value(a.written.front(), "123") == "Y" &&
                plain_rig::field_value(a.written.front(), "34") == "0")
        << "the answer's first frame must be a GapFill whose MsgSeqNum(34) is 0:\n"
        << printable(a.written);
}

TEST(ResendAnswerStoreFailure, Guard_BeginSeqNoZero_SlotZeroIsFoldedAndTheSessionStaysActive) {
    run_slot_zero(
        "7=0\x01"
        "16=0\x01");
}

TEST(ResendAnswerStoreFailure, Guard_BeginSeqNoAbsent_SlotZeroIsFoldedAndTheSessionStaysActive) {
    run_slot_zero("16=0\x01");
}

// ── The 789 path superseded (R-13) ───────────────────────────────────────────────────

// D-538-4, "the 789 path superseded": the `closing` leg of logon_arm_superseded_. The
// peer's Logon asks for a replay through NextExpectedMsgSeqNum(789); the wrapper holds
// the walk's tail read; the driver's close() runs to its end; the wrapper then returns
// store_cancelled. The arm was superseded by the close, so it returns success, writes
// nothing and records no state of its own.
TEST(ResendAnswerStoreFailure, Control_Superseded789Arm_TailReadCancelled_RecordsNothing) {
    constexpr unsigned kSeeded = 3;
    // What the fixture's coroutines and hooks refer to is declared before it, so it
    // outlives the drain ~DirectAcceptor runs.
    bool reply_stored = false;
    CallingThreadReadings call;
    asio::cancellation_signal none;
    std::vector<fsm_state> ring_after_call;
    std::size_t written_after_call = 0;
    std::vector<std::byte> logon;
    DirectAcceptor f{DirectAcceptor::Options{.enable_789 = true}};
    ASSERT_TRUE(f.open() && f.seed_outbound(kSeeded)) << "the fixture did not open and seed";
    f.log->arm();
    // The walk's tail read: the first outbound next_seqnum(_, false) after the reply
    // Logon's store().
    f.log->on_call = [&](StoreCall const& call) {
        reply_stored =
            reply_stored || (call.op == store_op::store && call.dir == direction_t::outbound);
    };
    f.log->hold_in = [&reply_stored](StoreCall const& call) {
        return reply_stored && is_tail_read(call);
    };
    f.log->held_result = error::store_cancelled;

    auto const mark = f.log->mark();
    logon = f.logon("789=1\x01");
    auto fut = f.feed(logon, call, none, [&] {
        ring_after_call = f.ring();
        written_after_call = f.sent.size();
    });
    ASSERT_TRUE(f.run_until([&] { return f.log->holding(); }))
        << "the path was not exercised: the tail read's hold was not entered";

    // The driver's close(). The bound: close(graceful)'s grace wait is the one
    // real-time timer here, and it is the fixture's logout_disconnect_timeout_ms, far
    // below the pump's budget; the hold is released only below.
    auto close_fut = f.spawn_close(close_mode::graceful);
    bool const close_returned = f.run_until([&] { return DirectAcceptor::ready(close_fut); });
    bool const held_through_the_close = f.log->holding() && !DirectAcceptor::ready(fut);
    auto const ring_before_release = f.ring();
    auto const written_before_release = f.sent.size();

    f.log->release_hold();
    bool const call_done = f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_call = nullptr;
    std::optional<StoreCall> read;
    if (auto const* r = f.log->first_after(mark, is_tail_read)) read = *r;

    ASSERT_TRUE(close_returned && held_through_the_close && call_done && call.completed &&
                !f.log->hold_timed_out() && read.has_value() && read->returned() &&
                read->error == error::store_cancelled)
        << "the path was not exercised: close_returned=" << close_returned
        << " read_held_through_the_close=" << held_through_the_close << " call_done=" << call_done
        << " hold_timed_out=" << f.log->hold_timed_out() << " the read: " << record_text(read);

    EXPECT_TRUE(call.returned_success()) << "on_inbound_frame " << call.call_text();
    EXPECT_EQ(written_after_call, written_before_release)
        << "a frame was written after the read returned:\n"
        << printable(f.sent);
    // The arm recorded no state of its own after the read returned.
    EXPECT_EQ(ring_after_call, ring_before_release)
        << "ring when the call returned: " << ring_text(ring_after_call)
        << "; ring before the read was released: " << ring_text(ring_before_release);
}

// D-CAN-5's returned timing when the arm is not superseded (D-CAN-4): the peer's Logon asks
// for a replay through NextExpectedMsgSeqNum(789), and the wrapper returns store_cancelled
// for the walk's tail read, with no close() and no Engine::stop(). The call returns the
// store's code, the state is Disconnected, and nothing is written after the reply Logon.
TEST(ResendAnswerStoreFailure, NotSuperseded789Arm_TailReadCancelled_ReturnsStoreCancelled) {
    constexpr unsigned kSeeded = 3;
    // What the fixture's coroutines and hooks refer to is declared before it, so it
    // outlives the drain ~DirectAcceptor runs.
    bool reply_stored = false;
    CallingThreadReadings call;
    asio::cancellation_signal none;
    fsm_state state_after_call = fsm_state::NotConnected;
    std::vector<std::byte> logon;
    DirectAcceptor f{DirectAcceptor::Options{.enable_789 = true}};
    ASSERT_TRUE(f.open() && f.seed_outbound(kSeeded)) << "the fixture did not open and seed";
    f.log->arm();
    // The walk's tail read: the first outbound next_seqnum(_, false) after the reply
    // Logon's store().
    f.log->on_call = [&](StoreCall const& c) {
        reply_stored = reply_stored || (c.op == store_op::store && c.dir == direction_t::outbound);
    };
    f.log->fail_in = [&reply_stored](StoreCall const& c) {
        return reply_stored && is_tail_read(c);
    };
    f.log->fail_code = error::store_cancelled;

    auto const mark = f.log->mark();
    logon = f.logon("789=1\x01");
    auto fut = f.feed(logon, call, none, [&] { state_after_call = f.session->state(); });
    bool const call_done = f.run_until([&] { return DirectAcceptor::ready(fut); });
    f.log->on_call = nullptr;
    std::optional<StoreCall> read;
    if (auto const* r = f.log->first_after(mark, [](StoreCall const& c) {
            return is_tail_read(c) && c.error == error::store_cancelled;
        })) {
        read = *r;
    }

    ASSERT_TRUE(call_done && call.completed && read.has_value() && read->returned())
        << "the path was not exercised: call_done=" << call_done
        << " the tail read: " << record_text(read);

    EXPECT_TRUE(call.returned_error(error::store_cancelled))
        << "on_inbound_frame " << call.call_text();
    EXPECT_EQ(state_after_call, fsm_state::Disconnected) << "the state after the call";
    ASSERT_EQ(f.sent.size(), 1U) << "a frame was written after the reply Logon:\n"
                                 << printable(f.sent);
    EXPECT_EQ(plain_rig::field_value(f.sent.front(), "35"), "A") << printable(f.sent);
}

}  // namespace
