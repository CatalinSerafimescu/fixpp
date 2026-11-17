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
// Feature 103 (specs/103-resend-logon-reset-fidelity/, research D3; tasks T041, T042), fixpp#580:
// a ResendRequest from BeginSeqNo(7)=0 is answered exactly as one from 7=1 (FR-012), an
// absent 7 draws a Reject 373=1, 371=7 (FR-013), and no frame a resend-answer cell of this
// file reads carries MsgSeqNum(34)=0 (SC-003, test_103_support.hpp's no_frame_carries_34_zero).
//
// The unbuildable-replay control of D-538-4's row "class 1 controls" is an existing
// cell, ResendAnswerReplayTest.Replay_NoClockAndNoStoredSendingTime_SlotIsGapFilled
// (test_resend_answer_field_order.cpp); it is not copied here.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "session/logon_arm_fixtures.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/test_103_support.hpp"
#include "support/calling_thread_readings.hpp"
#include "support/fix44_dictionary.hpp"
#include "support/recording_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
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

    ActiveSession() = default;
    explicit ActiveSession(DirectAcceptor::Options o) : f{std::move(o)} {}

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
            auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*f.session, f.ioc),
                                      f.session->send(std::span<const std::byte>{payloads.back()}),
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

    // Hands the peer's ResendRequest (its number `seq`, 2 by default) with the fields
    // `request` to on_inbound_frame and reads the outcome. `aimed_at` selects the store call
    // the cell reports on.
    [[nodiscard]] Answer answer(std::string_view const request,
                                std::function<bool(StoreCall const&)> const& aimed_at,
                                std::uint32_t const seq = 2) {
        Answer& a = pending;
        auto const mark = f.log->mark();
        request_frame = f.msg("2", seq, request);
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
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
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
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
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
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
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
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
}

// ── Class 2 (R-10): slot 0; BeginSeqNo(7) 0 and absent (fixpp#580) ────────────────────

// The validate gate (SessionConfig::validate_inbound_messages) is off in every cell of this
// section but the one that names it: its default.
constexpr unsigned kStoredFor580 = 2;  // the reply Logon at 1, application messages at 2 and 3

bool is_outbound_retrieve(StoreCall const& call) {
    return call.op == store_op::retrieve && call.dir == direction_t::outbound;
}
bool is_inbound_persist(StoreCall const& call) {
    return call.op == store_op::next_seqnum && call.dir == direction_t::inbound && call.increment;
}

// Slot 0 (D-538-4; feature 103 tasks T042; FR-012; research.md, "The re-aim of the 538
// slot-zero cell"): a request from number 0 is clamped to 1 at the walk's entry, so the walk
// never asks the store for slot 0. Its first slot read is 1, and the answer's first frame is
// the GapFill over the reply Logon at 34=1.
TEST(ResendAnswerStoreFailure, Guard_BeginSeqNoZero_ClampedToOne_SlotZeroNeverRead) {
    ActiveSession s;
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    ASSERT_FALSE(s.f.cfg.validate_inbound_messages);
    auto const a = s.answer(
        "7=0\x01"
        "16=0\x01",
        is_outbound_retrieve);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);
    ASSERT_TRUE(a.aimed.has_value()) << "the walk read no slot";
    EXPECT_EQ(a.aimed->first, seqnum_t{1}) << "the walk's first slot read";
    EXPECT_FALSE(std::ranges::any_of(s.f.log->calls(), is_slot_read(0)))
        << "the walk asked the store for slot 0";
    ASSERT_FALSE(a.written.empty());
    EXPECT_TRUE(plain_rig::field_value(a.written.front(), "35") == "4" &&
                plain_rig::field_value(a.written.front(), "123") == "Y" &&
                plain_rig::field_value(a.written.front(), "34") == "1")
        << "the answer's first frame must be a GapFill whose MsgSeqNum(34) is 1:\n"
        << printable(a.written);
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
}

// D-538-4's absent-7 slot-0 cell, now the absent-7 Reject (103 tasks T041, T042; FR-013): a
// ResendRequest in sequence with no BeginSeqNo(7) draws Reject 373=1 (Required tag missing),
// 371=7, Text(58) "Required tag missing" (QuickFIX C++'s text, evidence T005), and is not
// answered by a walk. Guard (4) consumed its number: NextNumIn is 3 and the advance is
// persisted before the Reject is stored (fixpp#423's order). The store reports itself
// persistent, so the persist is a store call this cell can read.
TEST(ResendAnswerStoreFailure,
     Guard_BeginSeqNoAbsent_RejectRequiredTagMissing_ConsumedAndPersisted) {
    ActiveSession s{DirectAcceptor::Options{.reports_persistent = true}};
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    ASSERT_FALSE(s.f.cfg.validate_inbound_messages);
    auto const a = s.answer("16=0\x01", is_inbound_persist);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);

    EXPECT_TRUE(replayed_numbers(a.written).empty()) << "a replay was written:\n"
                                                     << printable(a.written);
    EXPECT_EQ(count_of_type(a.written, "4"), 0U) << "a GapFill was written:\n"
                                                 << printable(a.written);
    ASSERT_EQ(a.written.size(), 1U) << printable(a.written);
    auto const& rj = a.written.front();
    EXPECT_EQ(plain_rig::field_value(rj, "35"), "3") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "373"), "1") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "371"), "7") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "45"), "2") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "58"), "Required tag missing") << printable(a.written);

    EXPECT_EQ(session_test_access::seqnum_mgr(*s.f.session).next_inbound_unsafe(), seqnum_t{3})
        << "the ResendRequest's number must be consumed";
    ASSERT_TRUE(a.aimed.has_value() && a.aimed->returned() && !a.aimed->error.has_value())
        << "the consumed number was not persisted: " << record_text(a.aimed);
    auto const* const reject_store = s.f.log->first_after(0, [](StoreCall const& c) {
        return c.op == store_op::store && c.dir == direction_t::outbound;
    });
    ASSERT_NE(reject_store, nullptr) << "the Reject was not stored";
    EXPECT_LT(a.aimed->called_at, reject_store->called_at)
        << "the advance must be persisted before the Reject is stored";
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
}

// ── fixpp#580: 7=0 answered as 7=1; the gate's Reject; no store ────────────────────────

// The (tag, value) fields of `frame`, in order, without the tags `skip` names.
std::vector<std::pair<std::string, std::string>> fields_but(
    std::string_view const frame, std::initializer_list<std::string_view> const skip) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t at = 0;
    while (at < frame.size()) {
        auto const soh = frame.find('\x01', at);
        auto const end = soh == std::string_view::npos ? frame.size() : soh;
        auto const field = frame.substr(at, end - at);
        auto const eq = field.find('=');
        if (eq != std::string_view::npos) {
            std::string tag{field.substr(0, eq)};
            if (std::ranges::find(skip, std::string_view{tag}) == skip.end()) {
                out.emplace_back(std::move(tag), std::string{field.substr(eq + 1)});
            }
        }
        at = end + 1;
    }
    return out;
}

// contracts/session-wire-endings.md E5: equal field for field, apart from SendingTime(52), a
// GapFill's OrigSendingTime(122) and CheckSum(10); MsgSeqNum(34) included.
::testing::AssertionResult equal_as_e5(std::vector<std::string> const& got,
                                       std::vector<std::string> const& want) {
    if (got.size() != want.size()) {
        return ::testing::AssertionFailure()
               << got.size() << " frames, the 7=1 answer has " << want.size() << ":\n"
               << printable(got) << "vs\n"
               << printable(want);
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        bool const gap_fill = plain_rig::field_value(want[i], "35") == "4";
        auto const g =
            gap_fill ? fields_but(got[i], {"52", "122", "10"}) : fields_but(got[i], {"52", "10"});
        auto const w =
            gap_fill ? fields_but(want[i], {"52", "122", "10"}) : fields_but(want[i], {"52", "10"});
        if (g != w) {
            return ::testing::AssertionFailure() << "frame " << i << " differs:\n"
                                                 << printable({got[i]}) << "vs\n"
                                                 << printable({want[i]});
        }
    }
    return ::testing::AssertionSuccess();
}

// 103 tasks T041 (FR-012, SC-003; contracts/session-wire-endings.md E5): a ResendRequest from
// BeginSeqNo(7)=0 through the current end gets exactly the answer of one from 7=1: a GapFill
// over the reply Logon at 34=1 with NewSeqNo(36)=2, then the replays of 2 and 3. The 7=1 arm
// is the control and the comparison's other half.
TEST(ResendAnswerBeginSeqNo580, SevenZero_AnsweredExactlyAsSevenOne) {
    ActiveSession zero;
    ASSERT_TRUE(zero.setup(kStoredFor580)) << "setup (7=0): the session did not reach Active";
    auto const a0 = zero.answer(
        "7=0\x01"
        "16=0\x01",
        is_outbound_retrieve);
    ActiveSession one;
    ASSERT_TRUE(one.setup(kStoredFor580)) << "setup (7=1): the session did not reach Active";
    auto const a1 = one.answer(
        "7=1\x01"
        "16=0\x01",
        is_outbound_retrieve);

    // The control: the 7=1 answer is the GapFill over the Logon, then the two replays.
    ASSERT_TRUE(a1.call.completed && a1.call.returned_success()) << a1.call.call_text();
    ASSERT_EQ(a1.written.size(), 3U) << printable(a1.written);
    EXPECT_EQ(plain_rig::field_value(a1.written[0], "35"), "4") << printable(a1.written);
    EXPECT_EQ(plain_rig::field_value(a1.written[0], "123"), "Y") << printable(a1.written);
    EXPECT_EQ(plain_rig::field_value(a1.written[0], "34"), "1") << printable(a1.written);
    EXPECT_EQ(plain_rig::field_value(a1.written[0], "36"), "2") << printable(a1.written);
    EXPECT_EQ(replayed_numbers(a1.written), (std::vector<std::string>{"2", "3"}))
        << printable(a1.written);

    ASSERT_TRUE(a0.call.completed) << "on_inbound_frame (7=0) did not complete";
    EXPECT_TRUE(a0.call.returned_success()) << "on_inbound_frame " << a0.call.call_text();
    EXPECT_EQ(a0.state_after_call, fsm_state::Active);
    EXPECT_TRUE(equal_as_e5(a0.written, a1.written));
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a0.written))  // SC-003
        << printable(a0.written);
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a1.written))  // SC-003
        << printable(a1.written);
}

// 103 tasks T041 (FR-013; evidence T004): on a session that validates against the vendored
// FIX 4.4 dictionary, where BeginSeqNo is required in ResendRequest, the gate Rejects an
// absent 7 before the arm runs. Its Reject equals the non-validating session's (the arm's)
// field for field, apart from MsgSeqNum(34), SendingTime(52) and CheckSum(10). Both sessions
// are FIX.4.4 over the same dictionary; only the validate gate differs.
TEST(ResendAnswerBeginSeqNo580, SevenAbsent_ValidatingSession_GateRejectEqualsTheArmsReject) {
    auto const fix44 = [](bool const validate) {
        return DirectAcceptor::Options{.tweak_cfg = [validate](fixpp::session::SessionConfig& cfg) {
            cfg.begin_string = "FIX.4.4";
            cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
            cfg.validate_inbound_messages = validate;
        }};
    };
    ActiveSession gate{fix44(true)};
    ASSERT_TRUE(gate.setup(kStoredFor580))
        << "setup (validating): the session did not reach Active";
    auto const ag = gate.answer("16=0\x01", is_outbound_retrieve);
    ActiveSession arm{fix44(false)};
    ASSERT_TRUE(arm.setup(kStoredFor580))
        << "setup (not validating): the session did not reach Active";
    auto const aa = arm.answer("16=0\x01", is_outbound_retrieve);

    ASSERT_EQ(ag.written.size(), 1U) << printable(ag.written);
    EXPECT_EQ(plain_rig::field_value(ag.written.front(), "35"), "3") << printable(ag.written);
    EXPECT_EQ(plain_rig::field_value(ag.written.front(), "373"), "1") << printable(ag.written);
    EXPECT_EQ(plain_rig::field_value(ag.written.front(), "371"), "7") << printable(ag.written);
    ASSERT_EQ(aa.written.size(), 1U) << printable(aa.written);
    EXPECT_EQ(fields_but(aa.written.front(), {"34", "52", "10"}),
              fields_but(ag.written.front(), {"34", "52", "10"}))
        << "the arm's Reject:\n"
        << printable(aa.written) << "the gate's Reject:\n"
        << printable(ag.written);
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(aa.written))  // SC-003
        << printable(aa.written);
}

// 103 tasks T041, the storeless arm (`/speckit-analyze` C4; FR-012, SC-003): 7=0 on a
// session with no store takes the walk's empty-store branch, one GapFill over the whole
// range, which starts at 34=1 and never at 0.
TEST(ResendAnswerBeginSeqNo580, SevenZero_NoStore_OneGapFillFromOne) {
    ActiveSession s{DirectAcceptor::Options{
        .tweak_cfg = [](fixpp::session::SessionConfig& cfg) { cfg.store_factory = nullptr; }}};
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    ASSERT_EQ(session_test_access::store(*s.f.session), nullptr) << "setup: a store was made";
    auto const a = s.answer(
        "7=0\x01"
        "16=0\x01",
        is_outbound_retrieve);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    ASSERT_EQ(a.written.size(), 1U) << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(a.written.front(), "35"), "4") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(a.written.front(), "123"), "Y") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(a.written.front(), "34"), "1") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(a.written.front(), "36"),
              std::to_string(kFirstApp + kStoredFor580))
        << printable(a.written);
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
}

// 103 (fixpp#580; B-103-5): a ResendRequest whose BeginSeqNo(7) is present but empty, or
// present but not a number. On a session that does not validate, each reads as 0 and is
// answered as 7=1 is (contracts/session-wire-endings.md E5's comparison). On a session that
// validates against the vendored FIX 4.4 dictionary the gate Rejects each before the arm
// runs, with RefTagID(371)=7, and the session stays up.
//
// The validating arms pin a departure, not a conformance witness. The validator maps an
// empty value and a value its type check refuses to SessionRejectReason(373)=5; the FIX
// Session Layer Test Cases (June 2020), test 14, require 373=4 for a tag with no value
// (sub-case 14d) and 373=6 for a value in the wrong data format (sub-case 14f), and QuickFIX
// sends 4 or 6. That gap is B-075-4 and L-075-1; fixpp#694 tracks it. This cell makes no
// claim on TC-010.
TEST(ResendAnswerBeginSeqNo580,
     SevenEmptyOrNonNumeric_ValidatingRejects_OtherwiseAnsweredAsSevenOne) {
    auto const fix44 = [](bool const validate) {
        return DirectAcceptor::Options{.tweak_cfg = [validate](fixpp::session::SessionConfig& cfg) {
            cfg.begin_string = "FIX.4.4";
            cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
            cfg.validate_inbound_messages = validate;
        }};
    };
    struct Arm {
        char const* name;
        char const* begin_field;
    };
    for (Arm const arm : {Arm{"empty", "7=\x01"}, Arm{"non-numeric", "7=X\x01"}}) {
        SCOPED_TRACE(arm.name);
        ActiveSession gate{fix44(true)};
        ASSERT_TRUE(gate.setup(kStoredFor580))
            << "setup (validating): the session did not reach Active";
        auto const ag =
            gate.answer(std::string{arm.begin_field} + "16=0\x01", is_outbound_retrieve);
        ASSERT_TRUE(ag.call.completed) << "on_inbound_frame did not complete";
        EXPECT_EQ(ag.state_after_call, fsm_state::Active);
        ASSERT_EQ(ag.written.size(), 1U) << printable(ag.written);
        EXPECT_EQ(plain_rig::field_value(ag.written.front(), "35"), "3") << printable(ag.written);
        EXPECT_EQ(plain_rig::field_value(ag.written.front(), "371"), "7") << printable(ag.written);
        EXPECT_EQ(plain_rig::field_value(ag.written.front(), "373"), "5") << printable(ag.written);
        EXPECT_TRUE(replayed_numbers(ag.written).empty()) << printable(ag.written);

        ActiveSession lax{fix44(false)};
        ASSERT_TRUE(lax.setup(kStoredFor580))
            << "setup (not validating): the session did not reach Active";
        auto const al = lax.answer(std::string{arm.begin_field} + "16=0\x01", is_outbound_retrieve);
        ActiveSession one{fix44(false)};
        ASSERT_TRUE(one.setup(kStoredFor580)) << "setup (7=1): the session did not reach Active";
        auto const a1 = one.answer(
            "7=1\x01"
            "16=0\x01",
            is_outbound_retrieve);
        ASSERT_TRUE(al.call.completed) << "on_inbound_frame did not complete";
        EXPECT_TRUE(al.call.returned_success()) << al.call.call_text();
        EXPECT_EQ(al.state_after_call, fsm_state::Active);
        ASSERT_EQ(a1.written.size(), 3U) << printable(a1.written);
        EXPECT_TRUE(equal_as_e5(al.written, a1.written));
        EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(al.written))  // SC-003
            << printable(al.written);
    }
}

// ── fixpp#577 with #580: a ResendRequest above NextNumIn (route (a) and (b)) ───────────────

// NextNumIn, the session's and its store's. The store's is read with a store call of its own.
struct InboundNumbers {
    seqnum_t session = 0;
    std::optional<seqnum_t> store;
};
InboundNumbers inbound_numbers(ActiveSession& s) {
    InboundNumbers out{
        .session = session_test_access::seqnum_mgr(*s.f.session).next_inbound_unsafe(),
        .store = std::nullopt};
    auto* const st = session_test_access::store(*s.f.session);
    if (st == nullptr) return out;
    auto fut =
        asio::co_spawn(s.f.ioc, st->next_seqnum(direction_t::inbound, false), asio::use_future);
    if (!s.f.run_until([&] { return DirectAcceptor::ready(fut); })) return out;
    if (auto const r = fut.get(); r) out.store = *r;
    return out;
}

// The index in `frames` of the first frame of MsgType `type`, or frames.size().
std::size_t first_of_type(std::vector<std::string> const& frames, std::string_view const type) {
    auto const it = std::ranges::find_if(
        frames, [type](auto const& f) { return plain_rig::field_value(f, "35") == type; });
    return static_cast<std::size_t>(it - frames.begin());
}

// Opens a gap: a Heartbeat above NextNumIn, which draws fixpp's own ResendRequest and leaves
// a resend outstanding. The frames it wrote are cleared, so the cell reads only its answer.
[[nodiscard]] bool open_gap(ActiveSession& s, std::uint32_t const seq) {
    s.request_frame = s.f.msg("0", seq);
    CallingThreadReadings call;
    if (!s.f.feed_plain(s.request_frame, call) || s.f.sent_count("2") != 1U ||
        !session_test_access::resend_outstanding(*s.f.session)) {
        return false;
    }
    s.f.sent.clear();
    return true;
}

// 103 tasks T048, route (a) (FR-013 through research D4.1's route; B-103-5): a ResendRequest
// with no BeginSeqNo(7), above NextNumIn, with no resend outstanding, draws the Reject 373=1,
// 371=7, consumes nothing and persists nothing; then fixpp's own ResendRequest from N,
// written after the Reject. The store reports itself persistent, so a persist is a store call.
TEST(ResendAnswerTooHigh577, SevenAbsent_TooHigh_NoResendOutstanding_RejectThenOneResendRequest) {
    ActiveSession s{DirectAcceptor::Options{.reports_persistent = true}};
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    auto const before = inbound_numbers(s);
    ASSERT_TRUE(before.store.has_value()) << "setup: the store's NextNumIn was not read";
    seqnum_t const n = before.session;
    auto const a = s.answer("16=0\x01", is_inbound_persist, n + 3);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);

    ASSERT_EQ(count_of_type(a.written, "3"), 1U) << printable(a.written);
    auto const& rj = a.written[first_of_type(a.written, "3")];
    EXPECT_EQ(plain_rig::field_value(rj, "373"), "1") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "371"), "7") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "45"), std::to_string(n + 3)) << printable(a.written);
    EXPECT_TRUE(replayed_numbers(a.written).empty()) << printable(a.written);
    EXPECT_EQ(count_of_type(a.written, "4"), 0U) << printable(a.written);
    ASSERT_EQ(count_of_type(a.written, "2"), 1U) << printable(a.written);
    auto const& rr = a.written[first_of_type(a.written, "2")];
    EXPECT_EQ(plain_rig::field_value(rr, "7"), std::to_string(n)) << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rr, "16"), "0") << printable(a.written);
    EXPECT_GT(first_of_type(a.written, "2"), first_of_type(a.written, "3"))
        << "fixpp's ResendRequest must follow the Reject:\n"
        << printable(a.written);

    EXPECT_FALSE(a.aimed.has_value())
        << "an inbound advance was persisted: " << record_text(a.aimed);
    auto const after = inbound_numbers(s);
    EXPECT_EQ(after.session, n) << "NextNumIn moved";
    ASSERT_TRUE(after.store.has_value());
    EXPECT_LE(*after.store, after.session) << "the store's NextNumIn is ahead of the session's";
    EXPECT_EQ(*after.store, *before.store) << "the store's NextNumIn moved";
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
}

// 103 tasks T048, route (a) on a validating session (B-103-5; the orchestrator's ruling): the
// validate gate runs before the sequence check, so the frame draws only the gate's Reject
// 373=1, 371=7; NextNumIn does not move and no ResendRequest of fixpp's own is sent. The gap
// is not lost: the next frame above NextNumIn opens it with fixpp's ResendRequest from N.
// QuickFIX C++ does the same: Session::next() validates before its MsgType dispatch, and
// generateReject increments only at the expected number.
TEST(ResendAnswerTooHigh577, SevenAbsent_TooHigh_ValidatingSession_RejectOnlyThenGapAtNextFrame) {
    ActiveSession s{DirectAcceptor::Options{.tweak_cfg = [](fixpp::session::SessionConfig& cfg) {
        cfg.begin_string = "FIX.4.4";
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        cfg.validate_inbound_messages = true;
    }}};
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    seqnum_t const n = session_test_access::seqnum_mgr(*s.f.session).next_inbound_unsafe();
    auto const a = s.answer("16=0\x01", is_outbound_retrieve, n + 3);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_EQ(a.state_after_call, fsm_state::Active);
    ASSERT_EQ(a.written.size(), 1U) << printable(a.written);
    auto const& rj = a.written.front();
    EXPECT_EQ(plain_rig::field_value(rj, "35"), "3") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "373"), "1") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "371"), "7") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "45"), std::to_string(n + 3)) << printable(a.written);
    EXPECT_EQ(session_test_access::seqnum_mgr(*s.f.session).next_inbound_unsafe(), n)
        << "NextNumIn moved";
    EXPECT_FALSE(session_test_access::resend_outstanding(*s.f.session))
        << "a resend is outstanding after the gate's Reject";

    s.f.sent.clear();
    s.request_frame = s.f.msg("0", n + 4);
    CallingThreadReadings next;
    ASSERT_TRUE(s.f.feed_plain(s.request_frame, next)) << "the next frame did not complete";
    ASSERT_EQ(s.f.sent_count("2"), 1U)
        << "the next frame above NextNumIn did not request the gap:\n"
        << printable(s.f.sent);
    auto const& rr = s.f.sent[first_of_type(s.f.sent, "2")];
    EXPECT_EQ(plain_rig::field_value(rr, "7"), std::to_string(n)) << printable(s.f.sent);
    EXPECT_EQ(plain_rig::field_value(rr, "16"), "0") << printable(s.f.sent);
    EXPECT_TRUE(session_test_access::resend_outstanding(*s.f.session));
    EXPECT_EQ(s.f.session->state(), fsm_state::Active);
}

// 103 tasks T048, route (b) (FR-009, FR-013; B-103-5): the same frame while a resend is
// outstanding draws the Reject, consumes and persists nothing, and no second ResendRequest.
TEST(ResendAnswerTooHigh577, SevenAbsent_TooHigh_ResendOutstanding_RejectAndNoSecondResendRequest) {
    ActiveSession s{DirectAcceptor::Options{.reports_persistent = true}};
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    seqnum_t const n = session_test_access::seqnum_mgr(*s.f.session).next_inbound_unsafe();
    ASSERT_TRUE(open_gap(s, n + 5)) << "setup: no gap was opened:\n" << printable(s.f.sent);
    auto const before = inbound_numbers(s);
    ASSERT_TRUE(before.store.has_value()) << "setup: the store's NextNumIn was not read";
    auto const a = s.answer("16=0\x01", is_inbound_persist, n + 7);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);

    ASSERT_EQ(a.written.size(), 1U) << printable(a.written);
    auto const& rj = a.written.front();
    EXPECT_EQ(plain_rig::field_value(rj, "35"), "3") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "373"), "1") << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(rj, "371"), "7") << printable(a.written);
    EXPECT_EQ(count_of_type(a.written, "2"), 0U) << printable(a.written);
    EXPECT_TRUE(session_test_access::resend_outstanding(*s.f.session));

    EXPECT_FALSE(a.aimed.has_value())
        << "an inbound advance was persisted: " << record_text(a.aimed);
    auto const after = inbound_numbers(s);
    EXPECT_EQ(after.session, n) << "NextNumIn moved";
    ASSERT_TRUE(after.store.has_value());
    EXPECT_LE(*after.store, after.session) << "the store's NextNumIn is ahead of the session's";
    EXPECT_EQ(*after.store, *before.store) << "the store's NextNumIn moved";
}

// 103 tasks T048's guard note (research D4.1; the walk's final persist): a ResendRequest that
// carries 7, above NextNumIn, is answered by the walk out of sequence, and the walk persists
// nothing. Route (a): the answer, then fixpp's own ResendRequest from N.
TEST(ResendAnswerTooHigh577, SevenPresent_TooHigh_AnsweredAndNothingPersisted) {
    ActiveSession s{DirectAcceptor::Options{.reports_persistent = true}};
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    auto const before = inbound_numbers(s);
    ASSERT_TRUE(before.store.has_value()) << "setup: the store's NextNumIn was not read";
    seqnum_t const n = before.session;
    auto const a = s.answer(
        "7=1\x01"
        "16=0\x01",
        is_inbound_persist, n + 3);
    ASSERT_TRUE(a.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(a.call.returned_success()) << "on_inbound_frame " << a.call.call_text();
    EXPECT_EQ(a.state_after_call, fsm_state::Active);

    EXPECT_EQ(replayed_numbers(a.written), (std::vector<std::string>{"2", "3"}))
        << printable(a.written);
    ASSERT_EQ(count_of_type(a.written, "2"), 1U) << printable(a.written);
    EXPECT_EQ(plain_rig::field_value(a.written[first_of_type(a.written, "2")], "7"),
              std::to_string(n))
        << printable(a.written);

    EXPECT_FALSE(a.aimed.has_value())
        << "an inbound advance was persisted: " << record_text(a.aimed);
    auto const after = inbound_numbers(s);
    EXPECT_EQ(after.session, n) << "NextNumIn moved";
    ASSERT_TRUE(after.store.has_value());
    EXPECT_LE(*after.store, after.session) << "the store's NextNumIn is ahead of the session's";
    EXPECT_EQ(*after.store, *before.store) << "the store's NextNumIn moved";
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(a.written))  // SC-003
        << printable(a.written);
}

// 103 tasks T049 (research D4.1 route (a): "a failed answer records Disconnected, as for an
// in-sequence request"): a too-high ResendRequest whose walk fails at the tail read ends the
// session with the store's code, and fixpp's own ResendRequest is not sent after it.
TEST(ResendAnswerTooHigh577, SevenPresent_TooHigh_TailReadFails_EndsWithNoResendRequest) {
    ActiveSession s;
    ASSERT_TRUE(s.setup(kStoredFor580)) << "setup: the session did not reach Active and send";
    seqnum_t const n = session_test_access::seqnum_mgr(*s.f.session).next_inbound_unsafe();
    s.f.log->fail_in = is_tail_read;
    s.f.log->fail_code = error::store_io_failure;
    auto const a = s.answer(
        "7=1\x01"
        "16=0\x01",
        is_tail_read, n + 3);
    ASSERT_TRUE(a.call.completed && a.aimed.has_value() && a.aimed->returned() &&
                a.aimed->error == error::store_io_failure)
        << "the path was not exercised: call_completed=" << a.call.completed
        << " the tail read: " << record_text(a.aimed);
    EXPECT_EQ(count_of_type(a.written, "2"), 0U)
        << "fixpp's ResendRequest was sent after a failed answer:\n"
        << printable(a.written);
    expect_nothing_written_and_ended(a, error::store_io_failure);
}

// ── fixpp#588: a live frame does not land inside a resend answer (R-588-a, b, d, f) ──────

// Application messages the session sends before the request; stored at 2 to 4.
constexpr unsigned kStoredFor588 = 3;

// A frame of `frames` that the walk wrote: a replay (43=Y) or a GapFill (123=Y).
bool is_answer_frame(std::string const& f) {
    return plain_rig::field_value(f, "43") == "Y" || plain_rig::field_value(f, "123") == "Y";
}
// The index of the last answer frame in `frames`, or frames.size() when there is none.
std::size_t last_answer_index(std::vector<std::string> const& frames) {
    std::size_t at = frames.size();
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (is_answer_frame(frames[i])) at = i;
    }
    return at;
}
std::size_t first_answer_index(std::vector<std::string> const& frames) {
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (is_answer_frame(frames[i])) return i;
    }
    return frames.size();
}
// The highest MsgSeqNum(34) the answer covers: a replay's 34, or a GapFill's NewSeqNo(36) - 1.
seqnum_t answer_end(std::vector<std::string> const& frames) {
    seqnum_t end = 0;
    for (auto const& f : frames) {
        if (plain_rig::field_value(f, "43") == "Y" && plain_rig::field_value(f, "35") != "4") {
            end = std::max(end, static_cast<seqnum_t>(std::stoul(plain_rig::field_value(f, "34"))));
        } else if (plain_rig::field_value(f, "123") == "Y") {
            end = std::max(end,
                           static_cast<seqnum_t>(std::stoul(plain_rig::field_value(f, "36")) - 1U));
        }
    }
    return end;
}

// An acceptor in Active that has sent kStoredFor588 application messages, then given a
// ResendRequest from 1 through 0 whose walk the recording store holds at one chosen call.
// What a coroutine of the fixture refers to is declared before `s`, so it outlives the drain
// ~DirectAcceptor runs; the Session is destroyed before its io_context (`s.f.ioc` is
// DirectAcceptor's first member).
struct HeldWalk {
    CallingThreadReadings rr_call;
    asio::cancellation_signal rr_none;
    std::future<void> rr_fut;
    std::deque<std::vector<std::byte>> live_payloads;
    std::deque<CallingThreadReadings> live_calls;
    std::deque<asio::cancellation_signal> live_signals;
    std::deque<std::future<void>> live_futs;
    ActiveSession s;

    HeldWalk() = default;
    HeldWalk(HeldWalk const&) = delete;
    HeldWalk& operator=(HeldWalk const&) = delete;
    // spawn_send binds a co_spawn to a signal of this fixture's, and a co_spawn leaves its
    // cancellation handler, which holds the session's strand, in that signal; no copy of a
    // strand may outlive the io_context. So the drain runs here, while every member is alive,
    // and those handlers are destroyed before `s`'s io_context.
    ~HeldWalk() {
        fixpp::test_support::cancel_and_drain_or_report(s.f.ioc, *s.f.clock, "538::HeldWalk");
        for (auto& sig : live_signals) sig.slot().clear();
    }
    explicit HeldWalk(DirectAcceptor::Options o) : s{std::move(o)} {}

    // Setup, then the request, held at the first call `hold` accepts; `held` is that call's
    // result when the hold ends (none: forwarded). False when the walk did not reach the hold.
    [[nodiscard]] bool start(std::function<bool(StoreCall const&)> hold,
                             std::optional<error> held = std::nullopt, bool request = true) {
        if (!s.setup(kStoredFor588)) return false;
        s.f.log->hold_in = std::move(hold);
        s.f.log->held_result = held;
        if (!request) return true;
        return feed_request();
    }
    [[nodiscard]] bool feed_request() {
        s.request_frame = s.f.msg("2", 2,
                                  "7=1\x01"
                                  "16=0\x01");
        rr_fut = s.f.feed(s.request_frame, rr_call, rr_none);
        return s.f.run_until([&] { return s.f.log->holding(); });
    }
    // Starts Session::send of an application message from a coroutine of the cell's, bound to
    // a signal of its own; returns its index.
    std::size_t spawn_send(std::string const& clordid) {
        live_payloads.push_back(
            plain_rig::to_bytes("35=D\x01"
                                "11=" +
                                clordid +
                                "\x01"
                                "55=AAPL\x01"));
        auto const& p = live_payloads.back();
        live_calls.emplace_back();
        live_signals.emplace_back();
        auto& out = live_calls.back();
        live_futs.push_back(asio::co_spawn(
            fixpp::test_support::session_executor_of(*s.f.session, s.f.ioc),
            fixpp::test_support::await_and_read_thread(
                [this, &p] { return s.f.session->send(std::span<const std::byte>{p}); }, out),
            asio::bind_cancellation_slot(live_signals.back().slot(), asio::use_future)));
        return live_futs.size() - 1;
    }
    [[nodiscard]] bool send_done(std::size_t i) const {
        return DirectAcceptor::ready(live_futs[i]);
    }
    // A bounded window of pumping that no predicate ends: `d` of real time.
    void pump_for(std::chrono::milliseconds d) {
        (void)s.f.run_until([] { return false; }, d);
    }
    void release() {
        s.f.log->release_hold();
        (void)s.f.run_until([&] { return !rr_fut.valid() || DirectAcceptor::ready(rr_fut); });
    }
};

// The frames written for the live message whose ClOrdID(11) is `clordid` (not a replay).
std::vector<std::string> live_frames(std::vector<std::string> const& frames,
                                     std::string const& clordid) {
    std::vector<std::string> out;
    for (auto const& f : frames) {
        if (plain_rig::field_value(f, "11") == clordid && plain_rig::field_value(f, "43") != "Y")
            out.push_back(f);
    }
    return out;
}

// A window long enough for a send that nothing holds to run to its end several times over,
// and short against the hold's own bound (recording_store.hpp's kBodyHoldBound).
constexpr std::chrono::milliseconds kHeldWindow{50};
// R-588-c's HeartBtInt.
constexpr std::chrono::seconds kHb588{30};

// 103 tasks T055, R-588-a (FR-014, SC-005): a send started while the answer is held at
// the read of slot 3 is not written while the hold lasts; once the answer ends, every frame
// numbered above the answer's range follows its last frame, and the live message carries the
// number after the answer's end.
TEST(ReplayGate588, R588a_SendDuringTheAnswer_WrittenAfterIt) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto const i = w.spawn_send("LIVE-A");
    w.pump_for(kHeldWindow);
    EXPECT_TRUE(live_frames(w.s.f.sent, "LIVE-A").empty())
        << "the live frame was written while the answer is held:\n"
        << printable(w.s.f.sent);
    w.release();
    ASSERT_TRUE(w.s.f.run_until([&] { return w.send_done(i); })) << "the send never ended";
    EXPECT_TRUE(w.live_calls[i].returned_success()) << w.live_calls[i].call_text();
    auto const& sent = w.s.f.sent;
    auto const end = answer_end(sent);
    ASSERT_GE(end, kFirstApp + kStoredFor588 - 1) << printable(sent);
    auto const last = last_answer_index(sent);
    for (std::size_t k = 0; k < sent.size(); ++k) {
        auto const n = std::stoul(plain_rig::field_value(sent[k], "34"));
        if (!is_answer_frame(sent[k]) && n > end) {
            EXPECT_GT(k, last) << "frame " << k << " (34=" << n
                               << ") above the answer's range precedes its last frame:\n"
                               << printable(sent);
        }
    }
    auto const live = live_frames(sent, "LIVE-A");
    ASSERT_EQ(live.size(), 1U) << printable(sent);
    EXPECT_EQ(plain_rig::field_value(live.front(), "34"), std::to_string(end + 1))
        << printable(sent);
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(sent))  // SC-003
        << printable(sent);
}

// R-588-a's store-fault arm: the held read fails; the answer ends the session, and the send
// contended behind it ends with session_invalid_state_for_send, writing nothing.
TEST(ReplayGate588, R588a_HeldReadFails_ContendedSendRefused) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3), error::store_io_failure))
        << "setup: the walk did not reach the held read";
    auto const i = w.spawn_send("LIVE-F");
    w.pump_for(kHeldWindow);
    w.release();
    ASSERT_TRUE(w.s.f.run_until([&] { return w.send_done(i); })) << "the send never ended";
    EXPECT_TRUE(w.live_calls[i].returned_error(error::session_invalid_state_for_send))
        << w.live_calls[i].call_text();
    EXPECT_TRUE(live_frames(w.s.f.sent, "LIVE-F").empty()) << printable(w.s.f.sent);
    EXPECT_EQ(w.s.f.session->state(), fsm_state::Disconnected);
}

// R-588-a's control: with no ResendRequest the same send is written in the pump that starts
// it.
TEST(ReplayGate588, R588a_Control_NoAnswer_SendWrittenAtOnce) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3), std::nullopt, /*request=*/false));
    auto const i = w.spawn_send("LIVE-C");
    w.s.f.ioc.restart();
    w.s.f.ioc.poll();
    EXPECT_TRUE(w.send_done(i)) << "the send did not end in one poll";
    EXPECT_EQ(live_frames(w.s.f.sent, "LIVE-C").size(), 1U) << printable(w.s.f.sent);
}

// 103 tasks T056, R-588-b (FR-014): a send numbered S that is still in its store call
// when the ResendRequest arrives is written before the answer's first frame, and the
// answer covers S.
TEST(ReplayGate588, R588b_SendInItsStoreCall_WrittenBeforeTheAnswerWhichCoversIt) {
    HeldWalk w;
    constexpr seqnum_t s_num = kFirstApp + kStoredFor588;
    ASSERT_TRUE(w.start(
        [](StoreCall const& c) {
            return c.op == store_op::store && c.dir == direction_t::outbound && c.first == s_num;
        },
        std::nullopt, /*request=*/false));
    auto const i = w.spawn_send("LIVE-S");
    ASSERT_TRUE(w.s.f.run_until([&] { return w.s.f.log->holding(); }))
        << "setup: the send did not reach its store call";
    w.s.request_frame = w.s.f.msg("2", 2,
                                  "7=1\x01"
                                  "16=0\x01");
    w.rr_fut = w.s.f.feed(w.s.request_frame, w.rr_call, w.rr_none);
    w.pump_for(kHeldWindow);
    w.release();
    ASSERT_TRUE(w.s.f.run_until([&] { return w.send_done(i) && DirectAcceptor::ready(w.rr_fut); }))
        << "the send or the answer never ended";
    auto const& sent = w.s.f.sent;
    auto const live = live_frames(sent, "LIVE-S");
    ASSERT_EQ(live.size(), 1U) << printable(sent);
    EXPECT_EQ(plain_rig::field_value(live.front(), "34"), std::to_string(s_num)) << printable(sent);
    std::size_t live_at = 0;
    for (std::size_t k = 0; k < sent.size(); ++k) {
        if (sent[k] == live.front()) live_at = k;
    }
    EXPECT_LT(live_at, first_answer_index(sent))
        << "an answer frame precedes the original frame S:\n"
        << printable(sent);
    EXPECT_GE(answer_end(sent), s_num) << "the answer does not cover S:\n" << printable(sent);
}

// R-588-b's Disconnected arm (research D5′.4): the ResendRequest arrives while a send numbered S
// is in its store call, so the walk waits for the send's shared hold. close(terminal) records
// Disconnected before the send returns, and its wake ends the walk there: the walk writes
// nothing, and it ends before the send's store call is released.
TEST(ReplayGate588, R588b_WalkWaitingForASend_CloseTerminal_EndsBeforeTheSendNothingAnswered) {
    HeldWalk w;
    constexpr seqnum_t s_num = kFirstApp + kStoredFor588;
    ASSERT_TRUE(w.start(
        [](StoreCall const& c) {
            return c.op == store_op::store && c.dir == direction_t::outbound && c.first == s_num;
        },
        std::nullopt, /*request=*/false));
    auto const i = w.spawn_send("LIVE-W");
    ASSERT_TRUE(w.s.f.run_until([&] { return w.s.f.log->holding(); }))
        << "setup: the send did not reach its store call";
    auto const before = w.s.f.sent.size();
    w.s.request_frame = w.s.f.msg("2", 2,
                                  "7=1\x01"
                                  "16=0\x01");
    w.rr_fut = w.s.f.feed(w.s.request_frame, w.rr_call, w.rr_none);
    w.pump_for(kHeldWindow);
    ASSERT_FALSE(DirectAcceptor::ready(w.rr_fut)) << "setup: the walk did not wait for the send";
    auto close_fut = w.s.f.spawn_close(close_mode::terminal);
    EXPECT_TRUE(w.s.f.run_until([&] { return DirectAcceptor::ready(w.rr_fut); }, kHeldWindow))
        << "the walk did not end at the Disconnected wake";
    EXPECT_TRUE(w.s.f.log->holding()) << "setup: the send's store call was released";
    std::vector<std::string> const after(w.s.f.sent.begin() + static_cast<std::ptrdiff_t>(before),
                                         w.s.f.sent.end());
    for (auto const& f : after) {
        EXPECT_FALSE(is_answer_frame(f)) << "the walk answered after the wake:\n"
                                         << printable(w.s.f.sent);
    }
    w.release();
    (void)w.s.f.run_until([&] { return w.send_done(i) && DirectAcceptor::ready(close_fut); });
}

// 103 tasks T057, R-588-c (FR-014): with a HeartBtInt, the liveness loop's Heartbeat and
// TestRequest fall due while the answer is held (the clock moves past HeartBtInt, so both
// the outbound and the inbound side are idle). Neither is written inside the answer; each
// one written after it carries the next number. The loop waits at the gate from its
// Heartbeat arm, and after the release writes the Heartbeat and then the TestRequest,
// because both deadlines have passed.
TEST(ReplayGate588, R588c_LivenessDuringTheAnswer_NothingInsideItNextNumbersAfter) {
    HeldWalk w{DirectAcceptor::Options{
        .tweak_cfg = [](fixpp::session::SessionConfig& cfg) { cfg.heartbeat_interval = kHb588; }}};
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    // KIND F: a lost advance writes neither liveness frame, which the ASSERT_GTs after the
    // release report by name.
    w.s.f.clock->advance(kHb588 + std::chrono::seconds{1});
    w.pump_for(kHeldWindow);
    EXPECT_EQ(w.s.f.sent_count("0") + w.s.f.sent_count("1"), 0U)
        << "a liveness frame was written while the answer is held:\n"
        << printable(w.s.f.sent);
    w.release();
    w.pump_for(kHeldWindow);
    auto const& sent = w.s.f.sent;
    ASSERT_GT(w.s.f.sent_count("0"), 0U)
        << "setup: the liveness loop wrote no Heartbeat; the cell exercised no contention:\n"
        << printable(sent);
    ASSERT_GT(w.s.f.sent_count("1"), 0U)
        << "setup: the liveness loop wrote no TestRequest; the cell exercised no contention:\n"
        << printable(sent);
    auto const last = last_answer_index(sent);
    auto next = answer_end(sent) + 1;
    for (std::size_t k = 0; k < sent.size(); ++k) {
        auto const t = plain_rig::field_value(sent[k], "35");
        if (t != "0" && t != "1") continue;
        EXPECT_GT(k, last) << "a 35=" << t << " lies inside the answer:\n" << printable(sent);
        EXPECT_EQ(plain_rig::field_value(sent[k], "34"), std::to_string(next)) << printable(sent);
        ++next;
    }
}

// 103 tasks T058, R-588-d (FR-014): close(graceful) started while the answer is held writes
// no Logout during it; once the answer ends the Logout follows it with the next number.
TEST(ReplayGate588, R588d_GracefulCloseDuringTheAnswer_LogoutAfterIt) {
    HeldWalk w{DirectAcceptor::Options{.logout_disconnect_timeout_ms = 2000}};
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto close_fut = w.s.f.spawn_close(close_mode::graceful);
    w.pump_for(kHeldWindow);
    EXPECT_EQ(w.s.f.sent_count("5"), 0U) << "a Logout was written while the answer is held:\n"
                                         << printable(w.s.f.sent);
    w.release();
    (void)w.s.f.run_until([&] { return w.s.f.sent_count("5") > 0U; },
                          std::chrono::milliseconds{w.s.f.cfg.logout_disconnect_timeout_ms} / 2);
    auto const& sent = w.s.f.sent;
    ASSERT_EQ(w.s.f.sent_count("5"), 1U) << printable(sent);
    std::size_t lo = 0;
    for (std::size_t k = 0; k < sent.size(); ++k) {
        if (plain_rig::field_value(sent[k], "35") == "5") lo = k;
    }
    EXPECT_GT(lo, last_answer_index(sent)) << printable(sent);
    EXPECT_EQ(plain_rig::field_value(sent[lo], "34"), std::to_string(answer_end(sent) + 1))
        << printable(sent);
    // The peer's Logout ends the graceful close.
    w.s.request_frame = w.s.f.msg("5", 3);
    CallingThreadReadings peer_logout;
    ASSERT_TRUE(w.s.f.feed_plain(w.s.request_frame, peer_logout));
    EXPECT_TRUE(w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); }));
}

// R-588-d's second arm: an answer held for longer than the close's grace. No Logout is
// written, and the close ends the session when its grace runs out. The grace is real time,
// so the window below is derived from it.
TEST(ReplayGate588, R588d_AnswerLongerThanTheGrace_NoLogoutAndTheCloseEnds) {
    HeldWalk w;  // logout_disconnect_timeout_ms: the fixture's 200
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto const grace = std::chrono::milliseconds{w.s.f.cfg.logout_disconnect_timeout_ms};
    auto close_fut = w.s.f.spawn_close(close_mode::graceful);
    EXPECT_TRUE(w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); }, grace * 4))
        << "the close did not end within its grace";
    EXPECT_EQ(w.s.f.sent_count("5"), 0U) << "a Logout was written during the answer:\n"
                                         << printable(w.s.f.sent);
    EXPECT_EQ(w.s.f.session->state(), fsm_state::Disconnected);
    w.release();
}

// R-588-d's Disconnected arm (research D5′.4; FR-014): close(graceful) waits at the gate behind
// a held answer whose held read then fails, so the answer ends the session. The wake finds
// phase 1 out of Active: no Logout is written, and the close ends at the wake, not at its
// real-time grace, which the window below is derived from.
TEST(ReplayGate588, R588d_GracefulCloseBehindAFailingAnswer_NoLogoutTheCloseEndsAtTheWake) {
    HeldWalk w{DirectAcceptor::Options{.logout_disconnect_timeout_ms = 2000}};
    ASSERT_TRUE(w.start(is_slot_read(3), error::store_io_failure))
        << "setup: the walk did not reach the held read";
    auto const grace = std::chrono::milliseconds{w.s.f.cfg.logout_disconnect_timeout_ms};
    auto close_fut = w.s.f.spawn_close(close_mode::graceful);
    w.pump_for(kHeldWindow);
    ASSERT_FALSE(DirectAcceptor::ready(close_fut)) << "setup: the close did not wait";
    w.release();
    EXPECT_TRUE(w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); }, grace / 2))
        << "the close did not end at the wake";
    EXPECT_EQ(w.s.f.sent_count("5"), 0U) << "a Logout was written after the failed answer:\n"
                                         << printable(w.s.f.sent);
    EXPECT_EQ(w.s.f.session->state(), fsm_state::Disconnected);
    (void)w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); }, grace * 2);
}

// FR-013's in-sequence absent-7 arm on a persistent store whose inbound persist fails: the
// arm returns that error and sends no Reject, and the session is Disconnected.
TEST(ResendAnswerBeginSeqNo580, SevenAbsent_InSequence_PersistFails_NoRejectDisconnected) {
    ActiveSession a{DirectAcceptor::Options{.reports_persistent = true}};
    ASSERT_TRUE(a.setup(kStoredFor580)) << "setup: the session did not reach Active";
    a.f.log->fail_in = [](StoreCall const& c) {
        return c.op == store_op::next_seqnum && c.dir == direction_t::inbound && c.increment;
    };
    auto const before = a.f.sent.size();
    auto const ans = a.answer("16=0\x01", is_outbound_retrieve);
    ASSERT_TRUE(ans.call.completed) << "on_inbound_frame did not complete";
    EXPECT_TRUE(ans.call.returned_error(error::store_io_failure)) << ans.call.call_text();
    EXPECT_EQ(ans.written.size(), before) << "a frame was written:\n" << printable(ans.written);
    EXPECT_EQ(ans.state_after_call, fsm_state::Disconnected);
}

// R-588-d's control: close(graceful) with no answer writes its Logout at once.
TEST(ReplayGate588, R588d_Control_NoAnswer_LogoutAtOnce) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3), std::nullopt, /*request=*/false));
    auto close_fut = w.s.f.spawn_close(close_mode::graceful);
    w.pump_for(kHeldWindow);
    EXPECT_EQ(w.s.f.sent_count("5"), 1U) << printable(w.s.f.sent);
    w.s.request_frame = w.s.f.msg("5", 2);
    CallingThreadReadings peer_logout;
    ASSERT_TRUE(w.s.f.feed_plain(w.s.request_frame, peer_logout));
    EXPECT_TRUE(w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); }));
}

// 103 tasks T059, R-588-f (FR-015): a send contended behind a held answer, ended by
// close(terminal), by the Engine's stop, or by its own cancellation; and a send contended
// behind an answer while close(graceful) runs.
TEST(ReplayGate588, R588f1_ContendedSend_CloseTerminal_RefusedNothingWritten) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto const i = w.spawn_send("LIVE-T");
    w.pump_for(kHeldWindow);
    auto close_fut = w.s.f.spawn_close(close_mode::terminal);
    EXPECT_TRUE(w.s.f.run_until([&] { return w.send_done(i); }))
        << "the contended send did not end after close(terminal)";
    EXPECT_TRUE(w.live_calls[i].returned_error(error::session_invalid_state_for_send))
        << w.live_calls[i].call_text();
    EXPECT_TRUE(live_frames(w.s.f.sent, "LIVE-T").empty()) << printable(w.s.f.sent);
    w.release();
    (void)w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); });
}

TEST(ReplayGate588, R588f2_ContendedSend_EngineStop_RefusedNothingWritten) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto const i = w.spawn_send("LIVE-E");
    w.pump_for(kHeldWindow);
    // Engine::stop() calls it on the session's strand.
    asio::post(w.s.f.session->executor().underlying(),
               [&w] { session_test_access::note_engine_stop(*w.s.f.session); });
    EXPECT_TRUE(w.s.f.run_until([&] { return w.send_done(i); }))
        << "the contended send did not end after the Engine's stop";
    EXPECT_TRUE(w.live_calls[i].returned_error(error::session_invalid_state_for_send))
        << w.live_calls[i].call_text();
    EXPECT_TRUE(live_frames(w.s.f.sent, "LIVE-E").empty()) << printable(w.s.f.sent);
    w.release();
}

// 103 tasks T072 (1)'s witness: a send contended behind a held answer while the session goes
// Disconnected with no close() and no end of the walk, at the liveness loop's TestRequest
// grace expiry. Only the Disconnected edge's wake can end the send's wait before the release:
// close()'s first-commit wake and the guard's release, which also wake it, are absent, so
// the two edges do not cover each other here. The TestRequest is written before the request,
// so the loop sleeps in its grace window, outside any hold, while the answer is held. The
// bound is derived from the hold's own real-time bound, the other thing that can end the
// walk and so wake the send.
TEST(ReplayGate588, R588f4_ContendedSend_LivenessDisconnects_RefusedNothingWritten) {
    HeldWalk w{DirectAcceptor::Options{
        .tweak_cfg = [](fixpp::session::SessionConfig& cfg) { cfg.heartbeat_interval = kHb588; }}};
    ASSERT_TRUE(w.start(is_slot_read(3), std::nullopt, /*request=*/false)) << "setup";
    // KIND F: a lost advance writes no TestRequest, which the bounded run_until below
    // reports by name ("no TestRequest was written").
    w.s.f.clock->advance(kHb588 + std::chrono::seconds{1});
    ASSERT_TRUE(w.s.f.run_until([&] { return w.s.f.sent_count("1") != 0; }))
        << "setup: no TestRequest was written:\n"
        << printable(w.s.f.sent);
    ASSERT_TRUE(w.feed_request()) << "setup: the walk did not reach the held read";
    auto const i = w.spawn_send("LIVE-L");
    w.pump_for(kHeldWindow);
    ASSERT_FALSE(w.send_done(i)) << "setup: the send did not wait behind the held answer";
    w.s.f.clock->advance(kHb588 + std::chrono::seconds{1});
    EXPECT_TRUE(
        w.s.f.run_until([&] { return w.send_done(i); }, fixpp::test_support::kBodyHoldBound / 2))
        << "the contended send did not end after the liveness loop's Disconnected";
    EXPECT_EQ(w.s.f.session->state(), fsm_state::Disconnected);
    EXPECT_EQ(session_test_access::lifecycle_value(*w.s.f.session), 1)
        << "the session's lifecycle: 1 (open), no close() ran";
    EXPECT_TRUE(w.live_calls[i].returned_error(error::session_invalid_state_for_send))
        << w.live_calls[i].call_text();
    EXPECT_TRUE(live_frames(w.s.f.sent, "LIVE-L").empty()) << printable(w.s.f.sent);
    w.release();
}

// (3): the send's own slot emits a terminal cancellation while it waits. It returns
// dispatch_aborted; the session stays Active, no number is consumed, and the caller's
// cancellation is still pending when the call returns.
TEST(ReplayGate588, R588f3_ContendedSend_OwnCancellation_AbortedStateUnchanged) {
    HeldWalk w;
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto const next_out = session_test_access::seqnum_mgr(*w.s.f.session).peek_outbound();
    auto const i = w.spawn_send("LIVE-X");
    w.pump_for(kHeldWindow);
    w.live_signals[i].emit(asio::cancellation_type::terminal);
    EXPECT_TRUE(w.s.f.run_until([&] { return w.send_done(i); }))
        << "the contended send did not end after its own cancellation";
    EXPECT_TRUE(w.live_calls[i].returned_error(error::dispatch_aborted))
        << w.live_calls[i].call_text();
    EXPECT_TRUE(w.live_calls[i].cancellation_recorded)
        << "the caller's cancellation is not pending after the call";
    EXPECT_EQ(w.s.f.session->state(), fsm_state::Active);
    EXPECT_EQ(session_test_access::seqnum_mgr(*w.s.f.session).peek_outbound(), next_out)
        << "a number was consumed";
    EXPECT_TRUE(live_frames(w.s.f.sent, "LIVE-X").empty()) << printable(w.s.f.sent);
    w.release();
}

// The close(graceful) arm: a send contended behind the answer while close(graceful) waits
// to send its Logout; no application frame follows the Logout.
TEST(ReplayGate588, R588f_GracefulClose_ContendedSend_NoApplicationFrameAfterTheLogout) {
    HeldWalk w{DirectAcceptor::Options{.logout_disconnect_timeout_ms = 2000}};
    ASSERT_TRUE(w.start(is_slot_read(3))) << "setup: the walk did not reach the held read";
    auto close_fut = w.s.f.spawn_close(close_mode::graceful);
    auto const i = w.spawn_send("LIVE-G");
    w.pump_for(kHeldWindow);
    w.release();
    (void)w.s.f.run_until([&] { return w.send_done(i) && w.s.f.sent_count("5") > 0U; },
                          std::chrono::milliseconds{w.s.f.cfg.logout_disconnect_timeout_ms} / 2);
    auto const& sent = w.s.f.sent;
    std::size_t lo = sent.size();
    for (std::size_t k = 0; k < sent.size(); ++k) {
        if (plain_rig::field_value(sent[k], "35") == "5") {
            lo = k;
            break;
        }
    }
    ASSERT_LT(lo, sent.size()) << "no Logout was written:\n" << printable(sent);
    for (std::size_t k = lo + 1; k < sent.size(); ++k) {
        EXPECT_NE(plain_rig::field_value(sent[k], "35"), "D")
            << "an application frame follows the Logout:\n"
            << printable(sent);
    }
    w.s.request_frame = w.s.f.msg("5", 3);
    CallingThreadReadings peer_logout;
    (void)w.s.f.feed_plain(w.s.request_frame, peer_logout);
    (void)w.s.f.run_until([&] { return DirectAcceptor::ready(close_fut); });
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
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(f.sent))  // SC-003
        << printable(f.sent);
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
    EXPECT_TRUE(fixpp::test_support::t103::no_frame_carries_34_zero(f.sent))  // SC-003
        << printable(f.sent);
}

}  // namespace
