// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_563_564_callback_send.cpp
//
// fixpp#563 and fixpp#564 (batch B41).
//
// Subject: a Session::send started from inside an Application callback, on the
// session's own executor. co_spawn's first step is a dispatch, so when the
// spawning thread is already running that executor the send's body begins
// inline, inside the callback.
//
// #563: send_impl peeks the outbound MsgSeqNum before toApp, stamps the frame
// with the peeked value, and advances the counter after toApp. A send that ran
// its own body inside toApp would peek the same value. What a send started
// inside each callback does is the table "What an application observes" of
// `.specify/563-564-callback-send.md` §2.1.
//
// #564: the session's debug guard, on a build without NDEBUG. Which nesting of
// one callback inside another it aborts is the table "Every nesting on one
// thread, against the checks" of `.specify/563-564-callback-send.md` §2.2. No
// cell depends on another: run each alone with --gtest_filter.
//
// Cells:
//   ToAppStartsSend           #563's shape. toApp for message ONE starts a send
//                             of message TWO. Asserts "deferred", and that the
//                             two frames carry consecutive MsgSeqNums in the
//                             order they are transmitted in, ONE first.
//   ToAppStartsSend_OnStrand  the same cell with a strand as the fixture's
//                             executor, over which the session's strand is a
//                             second one.
//   ToAppStartsSend_FileStore the same cell on a persistent FileStore, where
//                             a refused store() is session-fatal.
//   SequentialSends           control. The same two messages, the second
//                             started after the first completed.
//   SequentialSends_FileStore control for the FileStore cell.
//   ToAppStartsSend_NextSendIsRetained
//                             what follows that pair: a third message, sent on
//                             its own after both completed, must be transmitted
//                             and retained under its MsgSeqNum.
//   SequentialSends_NextSendIsRetained
//                             its control.
//   FromAppSendsReply         #564's acceptance shape. fromApp for an inbound
//                             35=D starts a send of a reply. Asserts "nested",
//                             on a build with asserts too: fromApp is not an
//                             outbound callback, so the send's toApp may run
//                             inside it.
//   OnLogonStartsSend, FromAdminStartsSend
//                             the same contract for a send started in onLogon
//                             (the peer's Logon acknowledges an initiator) and
//                             in fromAdmin (an inbound Heartbeat). Each asserts
//                             "nested".
//   ToAdminStartsSend         batch B41. The peer sends a TestRequest; toAdmin
//                             for the Heartbeat that answers it starts a send
//                             of an application message. The reply leaf reads
//                             the next outbound MsgSeqNum before toAdmin and
//                             advances the counter after it, so this is the
//                             toApp cell's shape with an admin frame as the
//                             outer one. Asserts "deferred", and that the
//                             Heartbeat is transmitted first.
//   TestRequestThenSend       its control: the same send started after the
//                             TestRequest was fully processed.
//   ToAdminStartsSend_AssignFirstSite
//                             batch B41. The peer sends an in-sequence 35=D with
//                             PossDupFlag(43)=Y and no OrigSendingTime(122); the
//                             session answers with a Reject and stays Active.
//                             That Reject's leaf advances the outbound counter
//                             BEFORE toAdmin, so the Reject owns its MsgSeqNum
//                             when toAdmin starts a send of an application
//                             message. The question is order: whether the two
//                             frames are transmitted, and offered to the store,
//                             in ascending MsgSeqNum order. Asserts "deferred",
//                             like ToAdminStartsSend.
//   ToAdminStartsSend_AssignFirstSite_Sequential
//                             its control: the same send started after the
//                             inbound frame was fully processed.
//   ToAdminStartsSend_AssignFirstSite_FileStore
//                             ToAdminStartsSend_AssignFirstSite on a persistent
//                             FileStore.
//   OverlappingSends          batch B41. No callback is involved: one handler
//                             on the session's executor starts send A and then
//                             send B without waiting for A, repeated over many
//                             pairs. This is the shape a send deferred out of a
//                             callback has while the frame that callback
//                             belongs to is still being stored. Asserts that
//                             wire order, MsgSeqNum order and start order
//                             agree, and that every frame is retained under its
//                             own number; prints how many pairs ran and how
//                             many did not keep the order.
//   OverlappingSends_FileStore
//                             the same on a persistent FileStore, whose store()
//                             suspends on a file-I/O pool. The fixture's pool
//                             has one thread.
//   FromAppSendsReply_NestedToAppThrows
//                             batch B41. fromApp starts a send whose toApp
//                             throws, which makes Session::send await a
//                             terminal close while fromApp is on the stack.
//   FromAppSendsReply_NestedToAppThrows_FileStore
//                             the same on a FileStore, where the session
//                             persists the inbound number after fromApp.
//   FromAppSendsReply_NestedToAppThrows_FromAppRejects
//                             the same, with fromApp returning an error after
//                             it started the send, which asks the session for a
//                             BusinessMessageReject.
//   FromAppStartsClose_Terminal
//                             batch B41; no send is involved. fromApp starts
//                             Session::close(terminal) with a co_spawn on the
//                             session's executor.
//   FromAppStartsClose_Graceful
//                             the same with close(graceful), whose Logout goes
//                             through toAdmin. No peer Logout is fed; the
//                             cell ends the close's wait for it by advancing
//                             the mock clock.
//   FromAppStartsClose_Terminal_FileStore
//                             the terminal cell on a FileStore, where the
//                             inbound arm persists its number after fromApp.
//   FromAdminStartsClose_Terminal
//                             fromAdmin for an inbound TestRequest starts
//                             close(terminal); the arm goes on to its Heartbeat
//                             reply after fromAdmin returns.
//   ToAppStartsClose_Graceful, ToAppStartsClose_Terminal,
//   ToAppStartsClose_Graceful_FileStore, ToAdminStartsClose_Graceful,
//   ToAdminStartsClose_Graceful_AssignFirstSite
//                             batch B41. An OUTBOUND callback starts
//                             Session::close: the toApp of an application
//                             send, the toAdmin of the Heartbeat that answers a
//                             TestRequest (callback before the counter
//                             advances), or the toAdmin of the PossDup Reject
//                             (counter advanced first). The close is started
//                             once.
//   each close cell above, also with _LiveWrite, _FileStore, _NoStore or two of
//   them after its name; ToAppStartsClose_Terminal_ResetOnDisconnect_*
//                             the arms of the close cells: the same cell on the
//                             live transport, on a FileStore, on a session with
//                             no store, and, for the terminal close started in
//                             toApp, with reset_on_disconnect set. What every
//                             arm asserts is the list "The arms every close
//                             cell has" of `.specify/563-564-callback-send.md`
//                             §3 and the cell's row of "Cells that must turn"
//                             there. Each arm's values are spelled out at its
//                             TEST.
//   ToAdminStartsClose_Terminal, ToAdminStartsClose_Terminal_AssignFirstSite
//                             batch B41. toAdmin starts close(terminal): the
//                             toAdmin of the Heartbeat that answers a
//                             TestRequest, or of the PossDup Reject. With the
//                             arms of the close cells.
//   ToAppStartsCloseThenSend_Terminal, ToAppStartsCloseThenSend_Graceful,
//   ToAppStartsSendThenClose_Graceful, LogoutToAdminStartsSend_GracefulClose,
//   FromAppStartsCloseThenSend_Graceful, LogoutToAdminStartsSend_PeerLogout
//                             batch B41. One callback starts a close and a
//                             send, in the order the name gives; or the toAdmin
//                             of a Logout starts a send, where the Logout is a
//                             graceful close's or the one that confirms a
//                             logout the peer started. With the arms of the
//                             close cells; the last one on the synchronous sink
//                             only. What each shape asserts is spelled out in
//                             run_close_and_send_in_one_callback.
//   FromAppStartsCloseBehindAnotherClose_Terminal,
//   FromAppStartsCloseBehindAnotherClose_Graceful
//                             batch B41. fromApp starts a close while a handler
//                             that starts another close of the same mode is
//                             already queued behind the frame's delivery, so
//                             the other close runs first. The graceful one with
//                             the arms of the close cells.
//   FromAppStartsClose_Terminal_TwoFramesInOneRead
//                             batch B41. Two application frames delivered by
//                             one coroutine with nothing between them; fromApp
//                             for the first starts a terminal close. On a
//                             MemoryStore and, with _FileStore, on a FileStore;
//                             each also with _LiveWrite.
//   CloseCancelledWhileItWaits_Terminal_EmitTerminal,
//   CloseCancelledWhileItWaits_Terminal_EmitTotal,
//   CloseCancelledWhileItWaits_Graceful_EmitTerminal,
//   CloseCancelledWhileItWaits_Graceful_EmitTotal
//                             batch B41. fromApp starts a close of the named
//                             mode by a co_spawn bound to a cancellation slot
//                             of the cell, and emits on that slot before it
//                             returns. With the arms of the close cells.
//   OnLogonStartsClose_<role>_<mode>, OnLogonStartsNoClose_<role>_FileStore,
//   OnLogoutOfGracefulCloseStartsClose_Terminal,
//   OnLogoutAfterAnotherBeginStringStartsClose_<mode>,
//   OnCreateStartsClose_<role>_<mode>
//                             batch B41. A close started in a lifecycle
//                             callback; <role> is Initiator or Acceptor, <mode>
//                             Terminal or Graceful. The onLogon cells set a
//                             heartbeat interval, feed the peer's Logon on the
//                             session's executor, run on a MemoryStore and,
//                             with _FileStore, on a FileStore, and read the
//                             liveness loop's sleep through a clock that counts
//                             the sleeps in flight; the NoClose cells are the
//                             positive control of that reading. The onLogout
//                             cells have the arms of the close cells. The
//                             onCreate cells run on each store kind. The
//                             onLogon and onCreate cells run on the synchronous
//                             sink only. What each callback's cells assert is
//                             spelled out in run_lifecycle_callback_starts_close.
//   ToAdminOfGracefulCloseStartsClose_Terminal,
//   ToAdminOfGracefulCloseStartsNoClose
//                             batch B41. The test body starts a graceful
//                             close; the toAdmin of its Logout starts a
//                             terminal one, which joins it and ends its wait
//                             for the peer's Logout. The NoClose cells are the
//                             positive control: with no close started there,
//                             the wait ends when the cell advances the mock
//                             clock. Both with the arms of the close cells.
//   ToAppStartsSend_NoStore, ToAdminStartsSend_NoStore,
//   ToAdminStartsSend_AssignFirstSite_NoStore, OverlappingSends_NoStore
//                             batch B41. The callback-started and overlapping
//                             shapes on a session with no MessageStore
//                             (SessionConfig::store_factory left null), where
//                             store_then_emit goes straight to the transport.
//                             This fixture's capture is a synchronous function,
//                             so the write is not a suspension point here. The
//                             three cells that start a send in a callback
//                             assert "deferred" and consecutive MsgSeqNums in
//                             transmit order. (c) and (d) do not apply.
//   the same four with _LiveWrite
//                             the same with a live transport attached, whose
//                             async_write records the frame and then suspends
//                             once, so the send path goes through the write
//                             gate and yields there.
//   ToAppStartsSendOnAnotherSession
//                             two sessions on one io_context. toApp of session
//                             A starts a send on session B, on B's executor.
//                             Asserts that B's send is not deferred: the depth
//                             the send reads is B's own.
//   DeferredSendCancelled_Terminal, DeferredSendCancelled_Total,
//   DeferredSendCancelled_Terminal_ThrowIfCancelledOff
//                             toApp starts a send by a co_spawn whose completion
//                             token is bound to a cancellation slot the cell
//                             owns, and emits on that slot before it returns,
//                             while the send waits to start. Terminal: the send
//                             returns dispatch_aborted, consumes nothing and
//                             leaves the session Active. Total: the chain's
//                             filter drops the emission and the send completes;
//                             a control for the first. The third: the send is
//                             awaited from a coroutine that turned
//                             throw_if_cancelled off, so it is not refused
//                             before it starts.
//   StoreCompletingOutOfOrder_OverlappingSends,
//   StoreCompletingOutOfOrder_SendStartedInToApp
//                             a store that returns from its first store() call
//                             only after a later frame has been transmitted.
//                             The later frame goes out first. This is the
//                             consequence of a store that breaks the ordering
//                             requirement on MessageStore::store; it is not a
//                             wanted behaviour.
//   ToAppStartsSend_OverABufferItFrees
//                             toApp starts a detached send over a heap buffer
//                             and frees the buffer before it returns. The
//                             transmitted frame carries the payload. The memory
//                             checker is the witness that nothing read the
//                             freed buffer.
//   ToAppRunsTheIoContext_DirectExecutor_SendNestsAndThePollEnds
//                             builds with NDEBUG only. direct_executor over the
//                             bare io_context; toApp starts a send and then
//                             runs the io_context itself. That setup is outside
//                             the contract (the design note's §2.1, "When the
//                             post does not move the send out of the
//                             callback"), so this cell pins behaviour outside
//                             the contract: it is there only to fail when the
//                             deferred send defers again each time it resumes,
//                             which keeps the callback's loop from ending.
//   ToAppRunsTheIoContext_OverAStrand_SendIsDeferred
//                             the same callback on a session in the default
//                             mode, with a strand as the fixture's executor.
//                             The send still starts after toApp returned.
//   DISABLED_MemoryCheckerPositiveControl_UseAfterFree
//                             a control for a memory checker, not a measurement
//                             of fixpp: reads a heap block after deleting it.
//                             It runs only with --gtest_also_run_disabled_tests,
//                             and then only in a binary built with
//                             AddressSanitizer or with
//                             B41_RUN_UNDER_MEMORY_CHECKER=1 in the environment
//                             (for valgrind's memcheck on an uninstrumented
//                             binary); otherwise it is skipped. A checker that
//                             reports nothing for this cell cannot report a
//                             use-after-free for any other.
//
// Suite CallbackGuard: the debug guard of the callback sites (fixpp#564,
// `.specify/563-564-callback-send.md` §2.2), on builds with asserts only; under
// NDEBUG every cell of the suite skips. Death cells: each expects the process to
// abort at one named assertion and matches a piece of that assertion's text,
// spelled out in this file.
//   ASecondThreadsEntryDies, ASecondThreadsEntryDiesAfterANestedScopeEnded,
//   NestedEntryOnOneThreadPasses, ASecondThreadsEntryDiesWithNoDataRaceOnTheToken
//                             the owner token, entered through
//                             session_test_access::enter_callback_scope on a
//                             session that was never opened. The last one runs
//                             only in a ThreadSanitizer build.
//   AFrameFedFromToAppDies, AFrameFedFromToAdminDies, AFrameFedFromOnLogonDies
//                             a callback feeds the next expected application
//                             frame to on_inbound_frame, by a co_spawn that
//                             begins inline. Death at the parse-site assertion.
//   OnLogoutEnteredInsideToAppDies
//                             toApp feeds a frame under another BeginString,
//                             which disconnects the session with no parse and no
//                             emit. Death at the onLogout site's assertion.
//   ToAdminEnteredInsideToAppDies
//                             direct_executor over the bare io_context, with a
//                             heartbeat interval: toApp advances the mock clock
//                             and runs the io_context, so the liveness loop's
//                             Heartbeat begins inside toApp. Death at the
//                             assertion of the overload that calls toApp and
//                             toAdmin.
//   DeferredSendResumedInsideToAppDies
//                             direct_executor over the bare io_context: toApp
//                             starts a send and then runs the io_context, so
//                             the send resumes inside toApp. Death at the
//                             assertion of the deferred path.
//   WaitedCloseResumedInsideFromAppDies
//                             direct_executor over the bare io_context: fromApp
//                             starts a terminal close and then runs the
//                             io_context, so the close resumes inside fromApp.
//                             Death at the assertion of the close's wait.
//
// Running the callback-starts-close cells under a memory checker: this fixture
// keeps the Session on the test's stack for the whole cell, so no cell frees the
// Session while one of its arms is running or suspended. What a sanitizer can
// see here is a read or write of something close() itself released.
//
// Witness, in every cell that starts a send inside a callback: where the
// send's toApp ran. "nested": while the starting callback was on the stack.
// "deferred": after the cell's hook had seen its co_spawn return. "never ran".
// A cell requires one of the first two and fails on the other; "never ran" is
// reported as PATH NOT DRIVEN, because a pass of the sequence-number
// assertions would then say nothing about the send.
//
// Store: a volatile bounded MemoryStore, except in the cells named _FileStore,
// _NoStore or StoreCompletingOutOfOrder_.
// The interleaving of the two sends depends on where a send first suspends,
// and that is inside the store (MemoryStore::store starts with a post). A
// persistent store turns a refused store() into a session-fatal error.
// A store is also what assertion (c) reads.
//
// Not covered here: Engine::send from a callback. It hops to the engine's
// control strand before returning to the session's, and needs an Engine with a
// live transport; tests/session/test_563_564_engine_callback_send.cpp holds
// the cells that start it directly inside a callback (batch B41), and
// tests/session/test_application_engine_send.cpp the ones that post it.
//
// mock_clock: nothing here depends on elapsed time. HeartBtInt is 0, so no
// liveness loop runs, except in CallbackGuard.ToAdminEnteredInsideToAppDies,
// which sets an interval and advances the mock clock itself, and in the
// OnLogonStartsClose and OnLogonStartsNoClose cells, which set an interval so
// that a started liveness loop has a sleep in flight. A cell that starts
// a graceful close ends the close's wait for the peer's Logout by advancing the
// mock clock (Fixture::end_logout_wait_when).

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/wire/parser.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "support/hooked_store.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

namespace fixpp::session::test {

// printf stays: std::println needs <print>, which libstdc++ 13 (the linux-gcc-release
// toolchain) does not ship.
// NOLINTBEGIN(modernize-use-std-print)

namespace {

constexpr std::string_view kSender = "ISLD";
constexpr std::string_view kTarget = "TW";
constexpr std::string_view kBegin = "FIX.4.2";
constexpr std::string_view kStamp52 = "20240101-00:00:00.000";

constexpr std::string_view kIdOne = "B28-ONE";
constexpr std::string_view kIdTwo = "B28-TWO";
constexpr std::string_view kPayloadOne =
    "35=D\x01"
    "11=B28-ONE\x01"
    "55=AAA\x01";
constexpr std::string_view kPayloadTwo =
    "35=D\x01"
    "11=B28-TWO\x01"
    "55=BBB\x01";
constexpr std::string_view kIdThree = "B28-THREE";
constexpr std::string_view kPayloadThree =
    "35=D\x01"
    "11=B28-THREE\x01"
    "55=CCC\x01";

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

std::string text_of(std::span<const std::byte> b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

// An inbound frame from the peer under BeginString `begin`: MsgType, 34, 49, 52,
// 56, then `rest`.
std::vector<std::byte> peer_frame_with_begin(std::string_view begin, std::string_view msg_type,
                                             std::uint32_t seq, std::string_view rest) {
    std::string body = "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(kTarget) + "\x01";
    body += "52=" + std::string(kStamp52) + "\x01";
    body += "56=" + std::string(kSender) + "\x01";
    body += rest;
    std::string full = "8=" + std::string(begin) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    return bytes_of(full);
}

// The same under the session's own BeginString.
std::vector<std::byte> peer_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view rest) {
    return peer_frame_with_begin(kBegin, msg_type, seq, rest);
}

// Value of tag `tag` in a SOH-delimited frame, matched at a field boundary.
std::string field(std::string_view frame, std::uint32_t tag) {
    const std::string needle = "\x01" + std::to_string(tag) + "=";
    auto pos = frame.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    const auto end = frame.find('\x01', pos);
    return std::string(
        frame.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
}

// MsgSeqNum(34) of a frame, or 0 when the field is absent or not a number.
seqnum_t seq_of(std::string_view frame) {
    const std::string v = field(frame, 34);
    seqnum_t out = 0;
    const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc{} || ptr != v.data() + v.size()) return 0;
    return out;
}

std::string describe(std::string_view frame) {
    return "[35=" + field(frame, 35) + " 34=" + field(frame, 34) + " 11=" + field(frame, 11) + "]";
}

std::string describe(const std::vector<std::string>& frames) {
    std::string out;
    for (const auto& f : frames) out += describe(f) + " ";
    return out.empty() ? "(none)" : out;
}

class CallbackApp final : public Application {
public:
    int from_app_calls = 0;
    int to_app_calls = 0;
    std::function<void()> on_from_app;
    std::function<void()> on_to_app;
    // Called with the admin frame's MsgType(35).
    std::function<void(std::string_view)> on_to_admin;
    // Called with "onLogon" or "onLogout".
    std::function<void(const char*)> on_lifecycle;
    // Called from onCreate, which a Session fires at the end of open(). Set it
    // before open().
    std::function<void()> on_create;
    // Called with the inbound admin frame's MsgType(35).
    std::function<void(std::string_view)> on_from_admin;
    // What fromApp returns, after on_from_app ran.
    expected_t<void> from_app_result;
    // What toApp returns, after on_to_app ran.
    expected_t<void> to_app_result;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& /*mv*/,
                             const SessionId& /*id*/) override {
        ++from_app_calls;
        if (on_from_app) on_from_app();
        return from_app_result;
    }
    expected_t<void> fromAdmin(const MessageView<access_mode::Index>& mv,
                               const SessionId& /*id*/) override {
        if (on_from_admin) {
            const auto mt = mv.get(35);
            on_from_admin(mt ? mt->as_string() : std::string_view{});
        }
        return {};
    }
    void onCreate(const SessionId& /*id*/) override {
        if (on_create) on_create();
    }
    void onLogon(const SessionId& /*id*/) override {
        if (on_lifecycle) on_lifecycle("onLogon");
    }
    void onLogout(const SessionId& /*id*/) override {
        if (on_lifecycle) on_lifecycle("onLogout");
    }
    expected_t<void> toApp(const MessageView<access_mode::Index>& /*mv*/,
                           const SessionId& /*id*/) override {
        ++to_app_calls;
        if (on_to_app) on_to_app();
        return to_app_result;
    }
    void toAdmin(const MessageView<access_mode::Index>& mv, const SessionId& /*id*/) override {
        if (!on_to_admin) return;
        const auto mt = mv.get(35);
        on_to_admin(mt ? mt->as_string() : std::string_view{});
    }
};

struct SendOutcome {
    bool done = false;
    bool threw = false;
    std::optional<error> err;
};

auto record_into(SendOutcome& o) {
    return [&o](std::exception_ptr ep, expected_t<void> r) {
        o.done = true;
        o.threw = static_cast<bool>(ep);
        if (!ep && !r) o.err = r.error();
    };
}

std::string describe(const SendOutcome& o) {
    if (!o.done) return "not completed";
    if (o.threw) return "threw";
    if (o.err) return "error " + std::to_string(static_cast<int>(*o.err));
    return "ok";
}

class CollectVisitor final : public retrieve_visitor {
public:
    std::vector<std::string> frames;

    asio::awaitable<expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        frames.push_back(text_of(frame));
        co_return visit_result::cont;
    }
};

// What the store returns for one outbound sequence number.
struct Retained {
    expected_t<void> status;
    std::vector<std::string> frames;
};

enum class ExecKind { io_context, strand };
// `none` leaves SessionConfig::store_factory null: the session has no store.
enum class StoreKind { memory, file, none };

struct LiveWrites {
    std::size_t started = 0;
    int in_flight = 0;
    int max_in_flight = 0;
    // close() calls on the transport, the writes begun after the first one, and
    // how many of those the transport refused.
    std::size_t closes = 0;
    std::size_t begun_after_close = 0;
    std::size_t refused_after_close = 0;
    // Set it before the transport is attached. True: a write begun after close()
    // returns transport_write_error, is not counted in `started` and hands
    // nothing to `on_write`, as a closed socket does. False: such a write is
    // handed to `on_write` like any other, which a closed socket does not do.
    bool refuse_after_close = true;
};

// A live transport for a session that is already Active: async_write hands the
// frame to `on_write`, then suspends once on its executor before completing, so
// a write is a real suspension point and two writes in flight would show in
// `stats`. Reads report end of stream; no read pump is run on it. close() is
// counted in `stats`; LiveWrites::refuse_after_close says what a later write does.
class SuspendingWriteTransport final : public fixpp::transport::Transport {
public:
    SuspendingWriteTransport(std::function<void(std::span<const std::byte>)> on_write,
                             LiveWrites& stats)
        : on_write_{std::move(on_write)}, stats_{stats} {}

    [[nodiscard]] asio::awaitable<expected_t<fixpp::transport::ConnectInfo>> async_connect(
        fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }
    [[nodiscard]] asio::awaitable<expected_t<std::size_t>> async_read_some(
        std::span<std::byte> /*buf*/) override {
        co_return std::unexpected{error::transport_read_eof};
    }
    [[nodiscard]] asio::awaitable<expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf) override {
        if (stats_.closes > 0) {
            ++stats_.begun_after_close;
            if (stats_.refuse_after_close) {
                ++stats_.refused_after_close;
                co_return std::unexpected{error::transport_write_error};
            }
        }
        ++stats_.started;
        stats_.max_in_flight = std::max(stats_.max_in_flight, ++stats_.in_flight);
        on_write_(buf);
        const std::size_t n = buf.size();
        co_await asio::post(co_await asio::this_coro::executor, asio::deferred);
        --stats_.in_flight;
        co_return n;
    }
    [[nodiscard]] expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] expected_t<void> close() noexcept override {
        ++stats_.closes;
        return {};
    }

private:
    std::function<void(std::span<const std::byte>)> on_write_;
    LiveWrites& stats_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

enum class TransportKind { sync_capture, live_suspending_write };

// SessionConfig::logout_disconnect_timeout_ms of a cell that starts a graceful
// close. The close waits that long, on the session's clock, for a peer Logout
// the cell never feeds, and the cell ends that wait by advancing the mock clock.
// close() also arms a timer on real time for the same duration; the value is one
// that no run of a cell reaches, so that timer does not end the wait.
constexpr std::uint32_t kLogoutWaitMs = 60000;

struct Fixture {
    asio::io_context ioc;
    // The FileStore cell's file-I/O executor. Declared after ioc, so it is
    // joined before ioc is destroyed.
    asio::thread_pool file_pool{1};
    std::filesystem::path file_dir;
    StoreKind store_kind;
    // The MemoryStore's outbound capacity; a cell that sends more frames raises
    // it before calling cfg().
    std::size_t memory_outbound_capacity = 16;
    // SessionConfig::role, read by cfg(). open_to_active() drives either role
    // to Active.
    session_role role = session_role::initiator;
    // Read by cfg(). True: threading_mode::direct_executor, attested, so the
    // session runs on `exec` itself with no strand of its own. With
    // ExecKind::io_context that is the bare io_context executor, and a callback
    // that runs the io_context can then run a handler posted to the session's
    // executor. False: the default mode, a strand made over `exec`.
    bool direct_executor = false;
    // SessionConfig::heartbeat_interval, read by cfg(). Zero: the liveness loop
    // returns at once. A cell that sets another value uses the 108= of
    // open_to_active()'s peer Logon, and closes the session before it leaves,
    // because the liveness loop is a detached coroutine over the Session.
    // The Logon arm spawns that loop on the executor the arm itself runs on, and
    // open_to_active() feeds the peer's Logon on `exec`. So in the default mode
    // the loop then runs on `exec` and not on the session's strand; a
    // default-mode cell that sets another value than zero brings the session to
    // Active with open_to_active_on_session_executor(), or feeds the Logon with
    // feed_peer_logon_on_session_executor().
    std::chrono::seconds heartbeat_interval{0};
    // The peer's Logon. A member, so that it outlives a delivery a cell starts
    // and does not wait for.
    const std::vector<std::byte> peer_logon = peer_frame("A", 1,
                                                         "98=0\x01"
                                                         "108=30\x01");
    // Called with every frame the session transmits, after it is appended to `wire`.
    std::function<void(const std::string&)> on_transmit;
    LiveWrites live_writes;
    asio::any_io_executor exec;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<CallbackApp> app = std::make_shared<CallbackApp>();
    fixpp::core::EngineConfig engine;
    std::vector<std::string> wire;

    explicit Fixture(ExecKind kind, StoreKind store = StoreKind::memory)
        : store_kind(store),
          exec(kind == ExecKind::strand ? asio::any_io_executor{asio::make_strand(ioc)}
                                        : asio::any_io_executor{ioc.get_executor()}) {
        clock = std::make_shared<fixpp::core::mock_clock>(
            std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200},
            fixpp::core::steady_time_point{}, exec);
        engine.clock = clock;
        engine.executor = exec;
        engine.application = app;
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ~Fixture() {
        if (!file_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(file_dir, ec);
        }
    }

    SessionConfig cfg() {
        SessionConfig c;
        c.sender_comp_id = std::string(kSender);
        c.target_comp_id = std::string(kTarget);
        c.begin_string = std::string(kBegin);
        c.role = role;
        if (direct_executor) {
            c.mode = threading_mode::direct_executor;
            c.already_serialized_executor = true;
        }
        c.heartbeat_interval = heartbeat_interval;
        c.security_profile = fixpp::test_support::make_minimal_security_profile();
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.executor_override = exec;
        c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        c.transport_send = [this](std::span<const std::byte> f) { transmitted(f); };
        if (store_kind == StoreKind::none) {
            // No store_factory: the session runs without a MessageStore.
        } else if (store_kind == StoreKind::file) {
            file_dir = fixpp::test_support::unique_temp_dir("b28_563");
            FileStore::Config fc;
            fc.directory = file_dir;
            fc.max_frame_bytes = 4096;
            fc.file_io_executor = file_pool.get_executor();
            c.store_factory = std::make_shared<FileStoreFactory>(fc);
        } else {
            MemoryStore::Config sc;
            sc.inbound_capacity = 16;
            sc.outbound_capacity = memory_outbound_capacity;
            sc.max_frame_bytes = 4096;
            c.store_factory = std::make_shared<MemoryStoreFactory>(sc);
        }
        return c;
    }

    template <class T>
    expected_t<T> run(asio::awaitable<expected_t<T>> a, const char* site) {
        auto fut = asio::co_spawn(exec, std::move(a), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 500ms, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // Pumps until `pred` holds. On a miss the spawned sends are drained before
    // this returns, so the caller may leave the test body.
    template <class Pred>
    [[nodiscard]] bool pump_done(Pred pred, const char* site) {
        if (fixpp::test_support::pump_until(ioc, pred, site)) return true;
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
        ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << site;
        return false;
    }

    void transmitted(std::span<const std::byte> f) {
        wire.push_back(text_of(f));
        if (on_transmit) on_transmit(wire.back());
    }

    // Attaches a SuspendingWriteTransport to an Active session. From here on the
    // session writes through it, and the config-time transport_send is not used.
    void attach_live_transport(Session& s) {
        s.attach_accepted_transport(
            std::make_unique<SuspendingWriteTransport>(
                [this](std::span<const std::byte> f) { transmitted(f); }, live_writes),
            fixpp::transport::handshake_result{});
    }

    // Initiator: open() emits the Logon, then the peer's Logon acknowledges it.
    // Acceptor: open() emits nothing, and the session answers the peer's Logon.
    bool open_to_active(Session& s) {
        if (!run(s.open(), "CallbackSend::open")) return false;
        if (!run(s.on_inbound_frame(std::span<const std::byte>{peer_logon}),
                 "CallbackSend::logon")) {
            return false;
        }
        return s.state() == fsm_state::Active;
    }

    // Delivers the peer's Logon to `s` on the session's own executor; `token`
    // receives the result of on_inbound_frame. The Logon arm, and the liveness
    // loop it spawns, then run on the session's strand in the default mode too.
    // Call it after open(), which binds that executor.
    template <class Token>
    void feed_peer_logon_on_session_executor(Session& s, Token&& token) {
        asio::co_spawn(s.executor().underlying(),
                       s.on_inbound_frame(std::span<const std::byte>{peer_logon}),
                       std::forward<Token>(token));
    }

    // open_to_active(), with the peer's Logon fed on the session's own executor.
    bool open_to_active_on_session_executor(Session& s) {
        if (!run(s.open(), "CallbackSend::open_before_logon_on_session_executor")) return false;
        SendOutcome fed;
        feed_peer_logon_on_session_executor(s, record_into(fed));
        if (!pump_done([&] { return fed.done; }, "CallbackSend::logon_on_session_executor")) {
            return false;
        }
        return !fed.threw && !fed.err && s.state() == fsm_state::Active;
    }

    // Ends a graceful close's wait for the peer's Logout without real time. Pumps
    // until `ready` holds, then advances the mock clock past `timeout`, the
    // session's logout_disconnect_timeout_ms. `ready` says that everything the
    // cell expects before the end of that wait has happened. When `ready` does
    // not hold within the pump's bound, the clock is not advanced, a failure
    // that carries `missing()` is reported, and false is returned; the caller
    // then drains what it spawned before it leaves the test body.
    template <class Ready, class Missing>
    [[nodiscard]] bool end_logout_wait_when(Ready ready, Missing missing,
                                            std::chrono::milliseconds timeout, const char* site) {
        if (!fixpp::test_support::pump_until(ioc, std::move(ready), site)) {
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << site
                          << ": the mock clock was not advanced, because " << missing();
            return false;
        }
        clock->advance(timeout + std::chrono::milliseconds{1});
        return true;
    }

    // Delivers `frames` the way the Engine's read pump delivers the complete
    // frames of one read: one coroutine, one co_await of on_inbound_frame per
    // frame, nothing between two frames. Like the pump it stops at the first
    // frame whose delivery returns an error; unlike the pump it does not close
    // the session then. Returns one result per frame delivered. Start it on the
    // session's executor.
    static asio::awaitable<std::vector<expected_t<void>>> deliver_in_one_read(
        Session& s, std::vector<std::vector<std::byte>> frames) {
        std::vector<expected_t<void>> results;
        for (const auto& frame : frames) {
            results.push_back(co_await s.on_inbound_frame(std::span<const std::byte>{frame}));
            if (!results.back()) break;
        }
        co_return results;
    }

    Retained retained(Session& s, seqnum_t seq) {
        Retained out;
        MessageStore* store = session_test_access::store(s);
        if (store == nullptr || seq == 0) {
            out.status = std::unexpected(error::session_invalid_argument);
            return out;
        }
        CollectVisitor v;
        out.status =
            run(store->retrieve(seq, seq, direction_t::outbound, v), "CallbackSend::retrieve");
        out.frames = std::move(v.frames);
        return out;
    }

    std::string store_next_out(Session& s) {
        MessageStore* store = session_test_access::store(s);
        if (store == nullptr) return "no store";
        const auto r =
            run(store->next_seqnum(direction_t::outbound, false), "CallbackSend::store_next");
        return r ? std::to_string(*r) : "error " + std::to_string(static_cast<int>(r.error()));
    }
};

seqnum_t next_out(Session& s) { return session_test_access::seqnum_mgr(s).next_outbound_unsafe(); }
seqnum_t next_in(Session& s) { return session_test_access::seqnum_mgr(s).next_inbound_unsafe(); }

const std::string* frame_with_id(const std::vector<std::string>& frames, std::string_view id) {
    const auto it =
        std::ranges::find_if(frames, [&](const std::string& f) { return field(f, 11) == id; });
    return it == frames.end() ? nullptr : &*it;
}

// (c): the store's frame under the MsgSeqNum a transmitted frame carries is
// that frame.
void expect_retained_as_sent(Fixture& f, Session& sess, const std::string& sent,
                             const std::string& context) {
    const seqnum_t seq = seq_of(sent);
    const Retained got = f.retained(sess, seq);
    EXPECT_TRUE(got.status.has_value())
        << "(c) retrieve of outbound 34=" << seq << " failed with error "
        << (got.status ? 0 : static_cast<int>(got.status.error())) << ". " << context;
    ASSERT_EQ(got.frames.size(), 1U)
        << "(c) the store holds no frame under outbound 34=" << seq << ", which " << describe(sent)
        << " was transmitted with. " << context;
    EXPECT_EQ(got.frames.front(), sent)
        << "(c) the store's frame under outbound 34=" << seq << " is "
        << describe(got.frames.front()) << ", but " << describe(sent)
        << " was transmitted with that MsgSeqNum. " << context;
}

std::string describe_retained(const Retained& r) {
    std::string out = describe(r.frames);
    if (!r.status)
        out += " (retrieve error " + std::to_string(static_cast<int>(r.status.error())) + ")";
    return out;
}

// What was transmitted: exactly message ONE and then message TWO, under the
// MsgSeqNums that follow the Logon's.
void expect_one_then_two(const std::vector<std::string>& sent, const std::string& context) {
    ASSERT_EQ(sent.size(), 2U) << "exactly two frames must be transmitted. " << context;
    EXPECT_EQ(field(sent[0], 11), kIdOne) << "message ONE must be transmitted first. " << context;
    EXPECT_EQ(seq_of(sent[0]), 2U) << "message ONE must carry MsgSeqNum 2. " << context;
    EXPECT_EQ(field(sent[1], 11), kIdTwo) << "message TWO must be transmitted second. " << context;
    EXPECT_EQ(seq_of(sent[1]), 3U) << "message TWO must carry MsgSeqNum 3. " << context;
}

enum class Shape { started_in_to_app, sequential };

// What a cell asserts once ONE and TWO have been sent: (b)-(d) on that pair, or
// that a third message, sent afterwards on its own, is transmitted and retained.
enum class Then { check_pair, third_send };

// Two application messages, ONE then TWO, on an Active initiator whose Logon
// consumed outbound MsgSeqNum 1.
void run_two_sends(Shape shape, ExecKind kind, Then then = Then::check_pair,
                   StoreKind store = StoreKind::memory) {
    Fixture f{kind, store};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";

    const auto ex = sess.executor().underlying();
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);
    const std::size_t wire_before = f.wire.size();

    SendOutcome first;
    SendOutcome second;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    // The session's NextNumOut inside the second send's toApp, and in the outer
    // toApp once the second send's co_spawn returned. Printed with every
    // failure, not asserted.
    std::string seen_in_second_to_app = "not recorded";
    std::string seen_after_spawn = "not recorded";

    if (shape == Shape::started_in_to_app) {
        f.app->on_to_app = [&] {
            if (f.app->to_app_calls == 1) {
                outer_live = true;
                asio::co_spawn(ex, sess.send(two), record_into(second));
                outer_live = false;
                seen_after_spawn =
                    std::to_string(next_out(sess)) +
                    (second.done ? " (second send completed)" : " (second send pending)");
            } else if (f.app->to_app_calls == 2) {
                inner_saw_outer_live = outer_live;
                seen_in_second_to_app = std::to_string(next_out(sess));
            }
        };
        asio::co_spawn(ex, sess.send(one), record_into(first));
        if (!f.pump_done([&] { return first.done && second.done; }, "CallbackSend::in_to_app")) {
            return;
        }
    } else {
        asio::co_spawn(ex, sess.send(one), record_into(first));
        if (!f.pump_done([&] { return first.done; }, "CallbackSend::first")) return;
        asio::co_spawn(ex, sess.send(two), record_into(second));
        if (!f.pump_done([&] { return second.done; }, "CallbackSend::second")) return;
    }

    // Both sends have completed: nothing below runs with a send in flight.
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        "Transmitted, in wire order: " + describe(sent) + "; send(ONE): " + describe(first) +
        "; send(TWO): " + describe(second) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + f.store_next_out(sess) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) +
        "; NextNumOut in the second send's toApp=" + seen_in_second_to_app +
        ", after the second send's co_spawn returned=" + seen_after_spawn + ".";

    // (a) Witness, then the classification: a send started in toApp is deferred.
    if (shape == Shape::started_in_to_app) {
        ASSERT_TRUE(inner_saw_outer_live.has_value())
            << "PATH NOT DRIVEN: the second send's toApp never ran. " << context;
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the ASSERT_TRUE above requires it
        EXPECT_FALSE(*inner_saw_outer_live)
            << "a send started in toApp must be deferred, but it ran nested: its toApp ran while "
               "the outer toApp was on the stack. "
            << context;
        // (e) Both sends report success.
        EXPECT_EQ(describe(first), "ok") << "(e) " << context;
        EXPECT_EQ(describe(second), "ok") << "(e) " << context;
    }
    EXPECT_EQ(f.app->to_app_calls, 2) << "each send must reach toApp once. " << context;

    if (then == Then::third_send) {
        if (shape == Shape::started_in_to_app) {
            // The pair itself, before the third send is started.
            expect_one_then_two(sent, context);
            if (::testing::Test::HasFatalFailure()) return;
            expect_retained_as_sent(f, sess, sent[0], context);
            expect_retained_as_sent(f, sess, sent[1], context);
        }
        const auto three = bytes_of(kPayloadThree);
        SendOutcome third;
        asio::co_spawn(ex, sess.send(three), record_into(third));
        if (!f.pump_done([&] { return third.done; }, "CallbackSend::third")) return;
        const std::vector<std::string> all(
            f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before), f.wire.end());
        const std::string after = "Before the third send: " + context +
                                  " With it, in wire order: " + describe(all) +
                                  "; send(THREE): " + describe(third) +
                                  "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
                                  ", store=" + f.store_next_out(sess) + ".";
        const std::string* sent_three = frame_with_id(all, kIdThree);
        ASSERT_NE(sent_three, nullptr) << "message THREE was not transmitted. " << after;
        EXPECT_EQ(seq_of(*sent_three), 4U)
            << "the third message must carry the MsgSeqNum after the first two. " << after;
        expect_retained_as_sent(f, sess, *sent_three, after);
        EXPECT_EQ(next_out(sess), 5U) << after;
        if (shape == Shape::started_in_to_app) {
            EXPECT_EQ(f.store_next_out(sess), "5")
                << "the store's NextNumOut must agree with the manager's. " << after;
        }
        return;
    }

    // (b) Both frames transmitted, under distinct and consecutive MsgSeqNums.
    ASSERT_EQ(sent.size(), 2U) << "(b) exactly two frames must be transmitted. " << context;
    const std::string* sent_one = frame_with_id(sent, kIdOne);
    const std::string* sent_two = frame_with_id(sent, kIdTwo);
    ASSERT_NE(sent_one, nullptr) << "(b) message ONE was not transmitted. " << context;
    ASSERT_NE(sent_two, nullptr) << "(b) message TWO was not transmitted. " << context;
    std::vector<seqnum_t> seqs{seq_of(*sent_one), seq_of(*sent_two)};
    std::ranges::sort(seqs);
    EXPECT_EQ(seqs, (std::vector<seqnum_t>{2U, 3U}))
        << "(b) the two frames must carry distinct, consecutive MsgSeqNum(34) values, 2 and 3. "
        << context;
    if (shape == Shape::started_in_to_app) expect_one_then_two(sent, context);

    // (c) The store retains each frame under the MsgSeqNum it was sent with.
    expect_retained_as_sent(f, sess, *sent_one, context);
    expect_retained_as_sent(f, sess, *sent_two, context);

    // (d) Two sends consume two sequence numbers.
    EXPECT_EQ(next_out(sess), 4U) << "(d) NextNumOut must have advanced by two. " << context;
    if (shape == Shape::started_in_to_app) {
        EXPECT_EQ(f.store_next_out(sess), "4")
            << "(d) the store's NextNumOut must agree with the manager's. " << context;
    }
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

}  // namespace

TEST(CallbackSend, ToAppStartsSend) {
    run_two_sends(Shape::started_in_to_app, ExecKind::io_context);
}

TEST(CallbackSend, ToAppStartsSend_OnStrand) {
    run_two_sends(Shape::started_in_to_app, ExecKind::strand);
}

TEST(CallbackSend, ToAppStartsSend_FileStore) {
    run_two_sends(Shape::started_in_to_app, ExecKind::io_context, Then::check_pair,
                  StoreKind::file);
}

TEST(CallbackSend, SequentialSends) { run_two_sends(Shape::sequential, ExecKind::io_context); }

TEST(CallbackSend, SequentialSends_FileStore) {
    run_two_sends(Shape::sequential, ExecKind::io_context, Then::check_pair, StoreKind::file);
}

TEST(CallbackSend, ToAppStartsSend_NextSendIsRetained) {
    run_two_sends(Shape::started_in_to_app, ExecKind::io_context, Then::third_send);
}

TEST(CallbackSend, SequentialSends_NextSendIsRetained) {
    run_two_sends(Shape::sequential, ExecKind::io_context, Then::third_send);
}

TEST(CallbackSend, FromAppSendsReply) {
    Fixture f{ExecKind::io_context};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const auto reply = bytes_of(kPayloadOne);
    const std::size_t wire_before = f.wire.size();

    SendOutcome nested;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    f.app->on_from_app = [&] {
        outer_live = true;
        asio::co_spawn(ex, sess.send(reply), record_into(nested));
        outer_live = false;
    };
    f.app->on_to_app = [&] { inner_saw_outer_live = outer_live; };

    const auto order = peer_frame("D", 2,
                                  "11=PEER-1\x01"
                                  "54=1\x01"
                                  "55=AAPL\x01");
    const auto in_r =
        f.run(sess.on_inbound_frame(std::span<const std::byte>{order}), "CallbackSend::order");
    // fromApp not reached means no send was started: nothing to wait for.
    if (!f.pump_done([&] { return nested.done || f.app->from_app_calls == 0; },
                     "CallbackSend::reply")) {
        return;
    }

    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        "Transmitted, in wire order: " + describe(sent) + "; send(reply): " + describe(nested) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + f.store_next_out(sess) + "; NextNumIn=" + std::to_string(next_in(sess)) + ".";

    // Witness, then the classification: a send started in fromApp runs nested.
    ASSERT_EQ(f.app->from_app_calls, 1)
        << "PATH NOT DRIVEN: the inbound 35=D did not reach fromApp. " << context;
    ASSERT_TRUE(inner_saw_outer_live.has_value())
        << "PATH NOT DRIVEN: the send's toApp never ran. " << context;
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the ASSERT_TRUE above requires it
    ASSERT_TRUE(*inner_saw_outer_live)
        << "a send started in fromApp must run nested, but it was deferred: its toApp ran only "
           "after fromApp had returned. "
        << context;

    // The reply: sent once, under the next outbound MsgSeqNum, and retained.
    EXPECT_EQ(describe(nested), "ok") << context;
    ASSERT_EQ(sent.size(), 1U) << "the reply must be transmitted exactly once. " << context;
    EXPECT_EQ(field(sent.front(), 11), kIdOne) << context;
    EXPECT_EQ(seq_of(sent.front()), 2U)
        << "the reply must carry the next outbound MsgSeqNum. " << context;
    expect_retained_as_sent(f, sess, sent.front(), context);
    EXPECT_EQ(next_out(sess), 3U) << "one send consumes one sequence number. " << context;

    // The triggering frame: processed normally.
    EXPECT_TRUE(in_r.has_value()) << "on_inbound_frame failed with error "
                                  << (in_r ? 0 : static_cast<int>(in_r.error())) << ". " << context;
    EXPECT_EQ(next_in(sess), 3U) << "the inbound 35=D must have been consumed. " << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

enum class StartedIn { on_logon, from_admin };

// A Session::send started in onLogon or in fromAdmin, on the session's executor.
// Neither is an outbound callback, so the send runs nested: its toApp runs while
// the starting callback is on the stack.
//   on_logon:   an initiator after open(); the peer's Logon makes it Active, and
//               onLogon starts the send.
//   from_admin: an Active initiator; the peer's Heartbeat reaches fromAdmin, which
//               starts the send. A Heartbeat draws no reply, so the send's frame is
//               the only one transmitted.
void run_send_started_in(StartedIn site) {
    const bool in_on_logon = site == StartedIn::on_logon;
    const char* site_name = in_on_logon ? "onLogon" : "fromAdmin";
    Fixture f{ExecKind::io_context};
    Session sess{f.engine, f.cfg()};
    if (in_on_logon) {
        ASSERT_TRUE(f.run(sess.open(), "CallbackSend::open").has_value()) << "setup: open failed";
        ASSERT_EQ(next_in(sess), 1U) << "setup: no inbound frame may have been consumed yet";
    } else {
        ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
        ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";
    }
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";

    const auto ex = sess.executor().underlying();
    const auto payload = bytes_of(kPayloadOne);
    const std::size_t wire_before = f.wire.size();

    SendOutcome send;
    SendOutcome inbound;
    bool started = false;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    const auto start_send = [&] {
        started = true;
        outer_live = true;
        asio::co_spawn(ex, sess.send(payload), record_into(send));
        outer_live = false;
    };
    if (in_on_logon) {
        f.app->on_lifecycle = [&](const char* name) {
            if (!started && std::string_view{name} == "onLogon") start_send();
        };
    } else {
        f.app->on_from_admin = [&](std::string_view msg_type) {
            if (!started && msg_type == "0") start_send();
        };
    }
    f.app->on_to_app = [&] { inner_saw_outer_live = outer_live; };

    const auto frame = in_on_logon ? peer_frame("A", 1,
                                                "98=0\x01"
                                                "108=30\x01")
                                   : peer_frame("0", 2, "");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{frame}),
                   record_into(inbound));
    // The callback not reached means no send was started: nothing more to wait for.
    if (!f.pump_done([&] { return inbound.done && (send.done || !started); },
                     "CallbackSend::send_started_in")) {
        return;
    }

    const std::string classification = !inner_saw_outer_live.has_value() ? "never ran"
                                       : *inner_saw_outer_live           ? "nested"
                                                                         : "deferred";
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        std::string("Started in: ") + site_name + ". Classification: " + classification +
        ". Transmitted, in wire order: " + describe(sent) + "; send: " + describe(send) +
        "; on_inbound_frame: " + describe(inbound) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + f.store_next_out(sess) + "; NextNumIn=" + std::to_string(next_in(sess)) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) + ".";
    ::testing::Test::RecordProperty("classification", classification);

    // Witness, then the classification.
    ASSERT_TRUE(started) << "PATH NOT DRIVEN: the inbound frame did not reach " << site_name << ". "
                         << context;
    ASSERT_NE(classification, "never ran")
        << "PATH NOT DRIVEN: the send's toApp never ran. " << context;
    EXPECT_EQ(classification, "nested")
        << "a send started in " << site_name << " must run nested. " << context;

    // The send: transmitted once, under the next outbound MsgSeqNum, and retained.
    EXPECT_EQ(describe(send), "ok") << context;
    ASSERT_EQ(sent.size(), 1U) << "exactly one frame must be transmitted. " << context;
    EXPECT_EQ(field(sent.front(), 11), kIdOne) << context;
    EXPECT_EQ(seq_of(sent.front()), 2U)
        << "the frame must carry the next outbound MsgSeqNum. " << context;
    expect_retained_as_sent(f, sess, sent.front(), context);
    EXPECT_EQ(next_out(sess), 3U) << "one send consumes one sequence number. " << context;
    EXPECT_EQ(f.store_next_out(sess), "3")
        << "the store's NextNumOut must agree with the manager's. " << context;

    // The inbound frame: processed normally.
    EXPECT_EQ(describe(inbound), "ok") << context;
    EXPECT_EQ(next_in(sess), in_on_logon ? 2U : 3U)
        << "the inbound frame must have been consumed. " << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

TEST(CallbackSend, OnLogonStartsSend) { run_send_started_in(StartedIn::on_logon); }

TEST(CallbackSend, FromAdminStartsSend) { run_send_started_in(StartedIn::from_admin); }

enum class AdminShape { send_inside_to_admin, send_after_test_request };

// A TestRequest from the peer on an Active initiator. The session answers with
// a Heartbeat; an application message is sent either from inside that
// Heartbeat's toAdmin or after the TestRequest was fully processed.
void run_test_request_then_send(AdminShape shape) {
    Fixture f{ExecKind::io_context};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const auto order = bytes_of(kPayloadOne);
    const std::size_t wire_before = f.wire.size();
    const bool inside = shape == AdminShape::send_inside_to_admin;

    SendOutcome send;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    int heartbeat_to_admin_calls = 0;
    std::string seen_in_to_app = "not recorded";
    std::string seen_after_spawn = "not recorded";

    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (msg_type != "0") return;
        ++heartbeat_to_admin_calls;
        if (!inside || heartbeat_to_admin_calls != 1) return;
        outer_live = true;
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] toAdmin(Heartbeat): starting Session::send\n");
        asio::co_spawn(ex, sess.send(order), record_into(send));
        std::fprintf(stderr, "[ B41 ] toAdmin(Heartbeat): co_spawn returned\n");
        outer_live = false;
        seen_after_spawn =
            std::to_string(next_out(sess)) + (send.done ? " (send completed)" : " (send pending)");
    };
    f.app->on_to_app = [&] {
        inner_saw_outer_live = outer_live;
        seen_in_to_app = std::to_string(next_out(sess));
    };

    const auto test_request = peer_frame("1", 2, "112=B41-TR\x01");
    const auto in_r = f.run(sess.on_inbound_frame(std::span<const std::byte>{test_request}),
                            "CallbackSend::test_request");
    if (!inside) asio::co_spawn(ex, sess.send(order), record_into(send));
    // No Heartbeat toAdmin means no send was started inside it: nothing to wait for.
    if (!f.pump_done([&] { return send.done || (inside && heartbeat_to_admin_calls == 0); },
                     "CallbackSend::admin_send")) {
        return;
    }

    const std::string classification = !inside                             ? "sequential (control)"
                                       : !inner_saw_outer_live.has_value() ? "never ran"
                                       : *inner_saw_outer_live             ? "nested"
                                                                           : "deferred";
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        "Classification: " + classification + ". Transmitted, in wire order: " + describe(sent) +
        "; send: " + describe(send) + "; on_inbound_frame(TestRequest): " +
        (in_r ? "ok" : "error " + std::to_string(static_cast<int>(in_r.error()))) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + f.store_next_out(sess) + "; NextNumIn=" + std::to_string(next_in(sess)) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) +
        "; NextNumOut in the send's toApp=" + seen_in_to_app +
        ", in toAdmin after the co_spawn returned=" + seen_after_spawn + ".";
    ::testing::Test::RecordProperty("classification", classification);
    std::printf("[ MEASURED ] %s\n", context.c_str());
    std::fflush(stdout);

    // Witness.
    ASSERT_EQ(heartbeat_to_admin_calls, 1)
        << "PATH NOT DRIVEN: the TestRequest was not answered through one Heartbeat toAdmin. "
        << context;
    ASSERT_NE(classification, "never ran")
        << "PATH NOT DRIVEN: the send's toApp never ran. " << context;
    if (inside) {
        EXPECT_EQ(classification, "deferred")
            << "a send started in toAdmin must be deferred. " << context;
    }

    EXPECT_EQ(describe(send), "ok") << context;
    EXPECT_TRUE(in_r.has_value()) << context;

    // The Heartbeat and the application message: both transmitted, under
    // distinct and consecutive MsgSeqNums, each retained under its own.
    ASSERT_EQ(sent.size(), 2U) << "exactly two frames must be transmitted. " << context;
    const auto hb_it =
        std::ranges::find_if(sent, [](const std::string& fr) { return field(fr, 35) == "0"; });
    const std::string* sent_order = frame_with_id(sent, kIdOne);
    ASSERT_NE(hb_it, sent.end()) << "the Heartbeat was not transmitted. " << context;
    ASSERT_NE(sent_order, nullptr) << "the application message was not transmitted. " << context;
    EXPECT_EQ(field(*hb_it, 112), "B41-TR") << context;
    std::vector<seqnum_t> seqs{seq_of(*hb_it), seq_of(*sent_order)};
    std::ranges::sort(seqs);
    EXPECT_EQ(seqs, (std::vector<seqnum_t>{2U, 3U}))
        << "the two frames must carry distinct, consecutive MsgSeqNum(34) values, 2 and 3. "
        << context;
    if (inside) {
        EXPECT_EQ(field(sent[0], 35), "0")
            << "the Heartbeat must be transmitted first. " << context;
        EXPECT_EQ((std::vector<seqnum_t>{seq_of(sent[0]), seq_of(sent[1])}),
                  (std::vector<seqnum_t>{2U, 3U}))
            << "the two frames must be transmitted in ascending MsgSeqNum(34) order, 2 then 3. "
            << context;
    }
    expect_retained_as_sent(f, sess, *hb_it, context);
    expect_retained_as_sent(f, sess, *sent_order, context);

    EXPECT_EQ(next_out(sess), 4U) << "NextNumOut must have advanced by two. " << context;
    EXPECT_EQ(f.store_next_out(sess), "4")
        << "the store's NextNumOut must agree with the manager's. " << context;
    EXPECT_EQ(next_in(sess), 3U) << "the TestRequest must have been consumed. " << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

TEST(CallbackSend, ToAdminStartsSend) {
    run_test_request_then_send(AdminShape::send_inside_to_admin);
}

TEST(CallbackSend, TestRequestThenSend) {
    run_test_request_then_send(AdminShape::send_after_test_request);
}

// An in-sequence PossDup 35=D without OrigSendingTime(122) on an Active
// initiator. The session answers with a Reject(35=3) and stays Active; an
// application message is sent either from inside that Reject's toAdmin or after
// the inbound frame was fully processed.
void run_poss_dup_reject_then_send(AdminShape shape, StoreKind store = StoreKind::memory) {
    Fixture f{ExecKind::io_context, store};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const auto order = bytes_of(kPayloadOne);
    const std::size_t wire_before = f.wire.size();
    const bool inside = shape == AdminShape::send_inside_to_admin;

    SendOutcome send;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    int reject_to_admin_calls = 0;
    std::string seen_in_to_admin = "not recorded";
    std::string seen_in_to_app = "not recorded";
    std::string seen_after_spawn = "not recorded";

    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (msg_type != "3") return;
        ++reject_to_admin_calls;
        seen_in_to_admin = std::to_string(next_out(sess));
        if (!inside || reject_to_admin_calls != 1) return;
        outer_live = true;
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] toAdmin(Reject): starting Session::send\n");
        asio::co_spawn(ex, sess.send(order), record_into(send));
        std::fprintf(stderr, "[ B41 ] toAdmin(Reject): co_spawn returned\n");
        outer_live = false;
        seen_after_spawn =
            std::to_string(next_out(sess)) + (send.done ? " (send completed)" : " (send pending)");
    };
    f.app->on_to_app = [&] {
        inner_saw_outer_live = outer_live;
        seen_in_to_app = std::to_string(next_out(sess));
    };

    const auto poss_dup = peer_frame("D", 2,
                                     "43=Y\x01"
                                     "11=PEER-DUP\x01"
                                     "54=1\x01"
                                     "55=AAPL\x01");
    const auto in_r = f.run(sess.on_inbound_frame(std::span<const std::byte>{poss_dup}),
                            "CallbackSend::poss_dup");
    if (!inside) asio::co_spawn(ex, sess.send(order), record_into(send));
    // No Reject toAdmin means no send was started inside it: nothing to wait for.
    if (!f.pump_done([&] { return send.done || (inside && reject_to_admin_calls == 0); },
                     "CallbackSend::reject_send")) {
        return;
    }

    const std::string classification = !inside                             ? "sequential (control)"
                                       : !inner_saw_outer_live.has_value() ? "never ran"
                                       : *inner_saw_outer_live             ? "nested"
                                                                           : "deferred";
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        "Classification: " + classification + ". Transmitted, in wire order: " + describe(sent) +
        "; send: " + describe(send) + "; on_inbound_frame(PossDup 35=D): " +
        (in_r ? "ok" : "error " + std::to_string(static_cast<int>(in_r.error()))) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + f.store_next_out(sess) + "; NextNumIn=" + std::to_string(next_in(sess)) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) +
        "; NextNumOut in the Reject's toAdmin=" + seen_in_to_admin +
        ", in the send's toApp=" + seen_in_to_app +
        ", in toAdmin after the co_spawn returned=" + seen_after_spawn +
        "; the store holds under outbound 34=2: " + describe_retained(f.retained(sess, 2)) +
        ", under 34=3: " + describe_retained(f.retained(sess, 3)) + ".";
    ::testing::Test::RecordProperty("classification", classification);
    std::printf("[ MEASURED ] %s\n", context.c_str());
    std::fflush(stdout);

    // Witness.
    ASSERT_EQ(reject_to_admin_calls, 1)
        << "PATH NOT DRIVEN: the inbound frame was not answered through one Reject toAdmin. "
        << context;
    ASSERT_NE(classification, "never ran")
        << "PATH NOT DRIVEN: the send's toApp never ran. " << context;
    if (inside) {
        EXPECT_EQ(classification, "deferred")
            << "a send started in toAdmin must be deferred. " << context;
    }

    ASSERT_EQ(sent.size(), 2U) << "exactly two frames must be transmitted. " << context;
    const auto rj_it =
        std::ranges::find_if(sent, [](const std::string& fr) { return field(fr, 35) == "3"; });
    const std::string* sent_order = frame_with_id(sent, kIdOne);
    ASSERT_NE(rj_it, sent.end()) << "the Reject was not transmitted. " << context;
    ASSERT_NE(sent_order, nullptr) << "the application message was not transmitted. " << context;

    // (a) Distinct MsgSeqNums.
    EXPECT_NE(seq_of(*rj_it), seq_of(*sent_order))
        << "(a) the Reject and the application message must carry distinct MsgSeqNum(34) values. "
        << context;
    // (b) Transmitted in ascending MsgSeqNum order.
    EXPECT_EQ((std::vector<seqnum_t>{seq_of(sent[0]), seq_of(sent[1])}),
              (std::vector<seqnum_t>{2U, 3U}))
        << "(b) the two frames must be transmitted in ascending MsgSeqNum(34) order, 2 then 3. "
        << context;
    if (inside) {
        EXPECT_EQ(field(sent[0], 35), "3")
            << "(b) the Reject must be transmitted first. " << context;
    }
    // (c) The store holds each frame under its own number.
    expect_retained_as_sent(f, sess, *rj_it, context);
    expect_retained_as_sent(f, sess, *sent_order, context);
    // (d) The store and the manager agree on the next outbound number.
    EXPECT_EQ(f.store_next_out(sess), std::to_string(next_out(sess)))
        << "(d) the store's NextNumOut must agree with the manager's. " << context;
    EXPECT_EQ(next_out(sess), 4U) << "(d) NextNumOut must have advanced by two. " << context;
    // (e) Both operations report what happened.
    EXPECT_EQ(describe(send), "ok") << "(e) " << context;
    EXPECT_TRUE(in_r.has_value()) << "(e) " << context;

    EXPECT_EQ(next_in(sess), 3U) << "the rejected frame must have been consumed. " << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

TEST(CallbackSend, ToAdminStartsSend_AssignFirstSite) {
    run_poss_dup_reject_then_send(AdminShape::send_inside_to_admin);
}

TEST(CallbackSend, ToAdminStartsSend_AssignFirstSite_Sequential) {
    run_poss_dup_reject_then_send(AdminShape::send_after_test_request);
}

TEST(CallbackSend, ToAdminStartsSend_AssignFirstSite_FileStore) {
    run_poss_dup_reject_then_send(AdminShape::send_inside_to_admin, StoreKind::file);
}

// Pairs of sends, A then B, both started from one handler on the session's
// executor, B without waiting for A. No Application callback starts anything.
void run_overlapping_sends(StoreKind store, TransportKind transport = TransportKind::sync_capture) {
    constexpr std::size_t kPairs = 200;
    Fixture f{ExecKind::io_context, store};
    f.memory_outbound_capacity = (2 * kPairs) + 16;
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";

    const auto ex = sess.executor().underlying();
    const auto payload = [](char which, std::size_t i) {
        return bytes_of(std::string("35=D\x01"
                                    "11=OV-") +
                        which + "-" + std::to_string(i) +
                        "\x01"
                        "55=AAA\x01");
    };
    const auto id_of = [](char which, std::size_t i) {
        return std::string("OV-") + which + "-" + std::to_string(i);
    };
    const std::size_t wire_start = f.wire.size();
    const auto started = std::chrono::steady_clock::now();

    std::size_t pairs_run = 0;
    std::size_t wire_not_in_start_order = 0;     // B transmitted before A, or a frame missing
    std::size_t numbers_not_in_start_order = 0;  // A, B not stamped 2+2i, 3+2i
    std::size_t same_number = 0;                 // A and B stamped alike
    std::size_t sends_failed = 0;
    std::string first_anomaly;

    for (std::size_t i = 0; i < kPairs; ++i) {
        const auto a_payload = payload('A', i);
        const auto b_payload = payload('B', i);
        SendOutcome a;
        SendOutcome b;
        std::string out_after_a = "not recorded";
        std::string out_after_b = "not recorded";
        const std::size_t wire_before = f.wire.size();
        asio::post(ex, [&] {
            asio::co_spawn(ex, sess.send(a_payload), record_into(a));
            out_after_a =
                std::to_string(next_out(sess)) + (a.done ? " (A completed)" : " (A pending)");
            asio::co_spawn(ex, sess.send(b_payload), record_into(b));
            out_after_b = std::to_string(next_out(sess));
        });
        if (!f.pump_done([&] { return a.done && b.done; }, "CallbackSend::overlapping")) return;
        ++pairs_run;

        const std::vector<std::string> sent(
            f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before), f.wire.end());
        const auto want_a = static_cast<seqnum_t>(2 + (2 * i));
        const std::string* sent_a = frame_with_id(sent, id_of('A', i));
        const std::string* sent_b = frame_with_id(sent, id_of('B', i));
        const bool wire_ok = sent.size() == 2 && sent_a == sent.data() && sent_b == sent.data() + 1;
        const bool numbers_ok = sent_a != nullptr && sent_b != nullptr &&
                                seq_of(*sent_a) == want_a && seq_of(*sent_b) == want_a + 1;
        const bool alike =
            sent_a != nullptr && sent_b != nullptr && seq_of(*sent_a) == seq_of(*sent_b);
        const bool ok = !a.err && !a.threw && !b.err && !b.threw;
        if (!wire_ok) ++wire_not_in_start_order;
        if (!numbers_ok) ++numbers_not_in_start_order;
        if (alike) ++same_number;
        if (!ok) ++sends_failed;
        if (first_anomaly.empty() && (!wire_ok || !numbers_ok || !ok)) {
            // Built for the first anomaly only: a diagnostic, not a string grown per pass.
            // NOLINTBEGIN(performance-inefficient-string-concatenation)
            first_anomaly = "pair " + std::to_string(i) + ": transmitted " + describe(sent) +
                            "; send(A): " + describe(a) + "; send(B): " + describe(b) +
                            "; NextNumOut after A's co_spawn returned=" + out_after_a +
                            ", after B's=" + out_after_b;
            // NOLINTEND(performance-inefficient-string-concatenation)
        }
        if (sess.state() != fsm_state::Active) break;
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();

    // Retention: one range retrieve over every number the pairs used, compared
    // with what was transmitted under each number.
    const std::vector<std::string> all(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_start),
                                       f.wire.end());
    std::size_t not_retained_as_sent = 0;
    std::string retrieve_status = "not run";
    if (MessageStore* st = session_test_access::store(sess); st != nullptr && pairs_run > 0) {
        CollectVisitor v;
        const auto last = static_cast<seqnum_t>(1 + (2 * pairs_run));
        const auto r = f.run(st->retrieve(2, last, direction_t::outbound, v),
                             "CallbackSend::overlapping_retrieve");
        retrieve_status = r ? "ok" : "error " + std::to_string(static_cast<int>(r.error()));
        for (const auto& fr : all) {
            const auto held = std::ranges::find_if(
                v.frames, [&](const std::string& h) { return seq_of(h) == seq_of(fr); });
            if (held == v.frames.end() || *held != fr) ++not_retained_as_sent;
        }
    }

    const std::string context =
        "Pairs requested=" + std::to_string(kPairs) + ", run=" + std::to_string(pairs_run) +
        "; pairs not transmitted in start order=" + std::to_string(wire_not_in_start_order) +
        "; pairs whose MsgSeqNums were not in start order=" +
        std::to_string(numbers_not_in_start_order) +
        "; pairs sharing one MsgSeqNum=" + std::to_string(same_number) +
        "; pairs with a failed send=" + std::to_string(sends_failed) +
        "; frames transmitted=" + std::to_string(all.size()) +
        ", of them not retained under their own number=" + std::to_string(not_retained_as_sent) +
        " (range retrieve: " + retrieve_status +
        "); NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + f.store_next_out(sess) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) +
        "; live writes started=" + std::to_string(f.live_writes.started) +
        ", most in flight at once=" + std::to_string(f.live_writes.max_in_flight) +
        "; elapsed ms=" + std::to_string(elapsed_ms) +
        "; first anomaly: " + (first_anomaly.empty() ? std::string("none") : first_anomaly) + ".";
    std::printf("[ MEASURED ] %s\n", context.c_str());
    std::fflush(stdout);

    EXPECT_EQ(pairs_run, kPairs) << context;
    EXPECT_EQ(same_number, 0U) << "(a) two overlapping sends must not share a MsgSeqNum. "
                               << context;
    EXPECT_EQ(wire_not_in_start_order, 0U)
        << "every pair must be transmitted in the order its sends were started. " << context;
    EXPECT_EQ(numbers_not_in_start_order, 0U)
        << "every pair must take consecutive MsgSeqNums in the order its sends were started. "
        << context;
    EXPECT_EQ(not_retained_as_sent, 0U)
        << "every frame must be retained under the MsgSeqNum it was transmitted with. " << context;
    if (store != StoreKind::none) {
        EXPECT_EQ(f.store_next_out(sess), std::to_string(next_out(sess)))
            << "the store's NextNumOut must agree with the manager's. " << context;
    }
    EXPECT_EQ(next_out(sess), 2U + (2U * kPairs)) << "every send must take one number. " << context;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << context;
    EXPECT_EQ(sends_failed, 0U) << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

TEST(CallbackSend, OverlappingSends) { run_overlapping_sends(StoreKind::memory); }

TEST(CallbackSend, OverlappingSends_FileStore) { run_overlapping_sends(StoreKind::file); }

TEST(CallbackSend, OverlappingSends_NoStore) { run_overlapping_sends(StoreKind::none); }

TEST(CallbackSend, OverlappingSends_NoStore_LiveWrite) {
    run_overlapping_sends(StoreKind::none, TransportKind::live_suspending_write);
}

// ── The close cells ──────────────────────────────────────────────────────────
//
// A callback of an Active session starts Session::close, or starts a send whose
// own toApp throws, which makes Session::send await a terminal close. The arms
// and what each asserts: `.specify/563-564-callback-send.md` §3, "The arms every
// close cell has" and the rows of "Cells that must turn".

const char* name_of(fsm_state s) {
    switch (s) {
        case fsm_state::NotConnected:
            return "NotConnected";
        case fsm_state::LogonSent:
            return "LogonSent";
        case fsm_state::LogonReceived:
            return "LogonReceived";
        case fsm_state::Active:
            return "Active";
        case fsm_state::LogoutSent:
            return "LogoutSent";
        case fsm_state::Disconnected:
            return "Disconnected";
    }
    return "unknown";
}

// The lifecycle of Session's close model, by the value
// session_test_access::lifecycle_value returns.
const char* lifecycle_name(int value) {
    switch (value) {
        case 0:
            return "never_opened";
        case 1:
            return "open";
        case 2:
            return "closing";
        case 3:
            return "closed_drained";
        default:
            return "unknown";
    }
}

// The session's FSM state and lifecycle, read together.
std::string where_is(const Session& s) {
    return std::string(name_of(s.state())) + " " +
           lifecycle_name(session_test_access::lifecycle_value(s));
}

// MsgType(35), MsgSeqNum(34) and, where the frame has one, ClOrdID(11).
std::string brief(std::string_view frame) {
    std::string out = "35=" + field(frame, 35) + " 34=" + field(frame, 34);
    if (const std::string id = field(frame, 11); !id.empty()) out += " 11=" + id;
    return out;
}

// The last outbound MsgSeqNum a close cell reads from the store.
constexpr seqnum_t kRetainedThrough = 4;

// What one arm of a close cell must show. Each TEST spells its arm's values out.
struct CloseExpect {
    // In the order they happen, each followed by the session's FSM state and
    // lifecycle at that moment: the starting callback's return, onLogout, every
    // transmit, the completion of the operation the callback started and the
    // completion of the operation that called the callback.
    std::vector<std::string> order;
    // Every toApp and toAdmin other than the starting callback, in order, each
    // followed by the session's FSM state and lifecycle on its entry.
    std::vector<std::string> callbacks;
    // The result of the operation that called the callback, and of the one the
    // callback started. Empty: success.
    std::optional<error> outer;
    std::optional<error> started;
    // The frames transmitted after the Logon exchange.
    std::vector<std::string> wire;
    // What the store returns for each outbound MsgSeqNum from 1 to
    // kRetainedThrough. Empty: the session has no store.
    std::vector<std::string> retained;
    // The manager's next numbers.
    seqnum_t next_out = 0;
    seqnum_t next_in = 0;
    // The store's next numbers. Empty: not asserted. The inbound one is asserted
    // on a FileStore, where the session persists the inbound advance, and in the
    // reset_on_disconnect arms, whose subject is what the reset left in the store.
    std::optional<seqnum_t> store_next_out;
    std::optional<seqnum_t> store_next_in;
    // On the live transport: the writes begun after the transport's close(), and
    // how many of them the transport refused.
    std::size_t writes_after_close = 0;
    std::size_t writes_refused = 0;
};

// The recorder of a close cell. The cell's hooks call it; check_close_arm()
// compares what it holds with the arm's CloseExpect.
struct CloseCell {
    Fixture& f;     // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    Session& sess;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::size_t wire_before;
    std::vector<std::string> order;
    std::vector<std::string> callbacks;
    int on_logout_calls = 0;
    int logout_to_admin_calls = 0;
    // The operation that called the starting callback, and the one it started.
    SendOutcome outer;
    SendOutcome started;
    // Read inside the starting callback, after the co_spawn that starts the
    // operation returned.
    struct Inside {
        bool read = false;
        std::string state;
        std::string lifecycle;
        int on_logout_calls = 0;
        int logout_to_admin_calls = 0;
        bool started_completed = false;
    } inside;

    CloseCell(Fixture& fixture, Session& session)
        : f(fixture), sess(session), wire_before(fixture.wire.size()) {}
    CloseCell(const CloseCell&) = delete;
    CloseCell& operator=(const CloseCell&) = delete;

    void note(const std::string& what) { order.push_back(what + " [" + where_is(sess) + "]"); }
    void callback(const std::string& what) {
        callbacks.push_back(what + " [" + where_is(sess) + "]");
    }
    void transmitted(const std::string& frame) { note("transmit " + brief(frame)); }
    void lifecycle_callback(const char* name) {
        if (std::string_view{name} != "onLogout") return;
        ++on_logout_calls;
        note("onLogout");
    }
    // A toAdmin other than the starting callback.
    void to_admin(std::string_view msg_type) {
        if (msg_type == "5") ++logout_to_admin_calls;
        callback("toAdmin 35=" + std::string(msg_type));
    }
    void read_inside_callback() {
        inside.read = true;
        inside.state = name_of(sess.state());
        inside.lifecycle = lifecycle_name(session_test_access::lifecycle_value(sess));
        inside.on_logout_calls = on_logout_calls;
        inside.logout_to_admin_calls = logout_to_admin_calls;
        inside.started_completed = started.done;
    }
    // What had not completed when the cell's pump reached its bound. The cell
    // records it before it drains, because the drain completes what it cancels.
    std::string pending_at_the_bound;
    void record_pending() {
        pending_at_the_bound =
            std::string(outer.done ? "" : "the operation that called the callback had not; ") +
            (started.done ? "" : "the operation the callback started had not; ");
    }
    // A completion handler that records the outcome into `o` and notes
    // "<what> completes".
    auto completes(SendOutcome& o, const char* what) {
        return [this, &o, what](std::exception_ptr ep, expected_t<void> r) {
            record_into(o)(ep, std::move(r));
            note(std::string(what) + " completes");
        };
    }
};

std::string literal(const std::vector<std::string>& v) {
    std::string out = "{";
    for (const auto& e : v) out += (out.size() > 1 ? ", \"" : "\"") + e + "\"";
    return out + "}";
}

std::string literal(const std::optional<seqnum_t>& v) { return v ? std::to_string(*v) : "{}"; }

// The store's next number in one direction. Empty: the session has no store,
// or the read failed.
std::optional<seqnum_t> store_next(Fixture& f, Session& s, direction_t dir) {
    MessageStore* store = session_test_access::store(s);
    if (store == nullptr) return std::nullopt;
    const auto r = f.run(store->next_seqnum(dir, false), "CallbackSend::close_cell_store_next");
    if (!r) return std::nullopt;
    return *r;
}

// What the store returns for each outbound MsgSeqNum from 1 to kRetainedThrough:
// the frame, "none" where the store reports that it holds no frame under that
// number, or the read's error. Empty when the session has no store. `full`
// receives the frames' bytes, one entry per number.
std::vector<std::string> retained_frames(Fixture& f, Session& s, std::vector<std::string>& full) {
    std::vector<std::string> out;
    if (session_test_access::store(s) == nullptr) return out;
    for (seqnum_t seq = 1; seq <= kRetainedThrough; ++seq) {
        const Retained got = f.retained(s, seq);
        full.emplace_back(got.frames.size() == 1 ? got.frames.front() : std::string{});
        if (!got.status) {
            out.push_back(got.status.error() == error::store_seqnum_gap
                              ? "none"
                              : "error " + std::to_string(static_cast<int>(got.status.error())));
        } else if (got.frames.size() == 1) {
            out.push_back(brief(got.frames.front()));
        } else {
            out.push_back(std::to_string(got.frames.size()) + " frames");
        }
    }
    return out;
}

// The assertions every arm of a close cell makes. `callback` names the starting
// callback; `completed` says whether the cell's pump saw both operations
// complete within its bound.
void check_close_arm(CloseCell& cell, const CloseExpect& want, const char* callback, bool completed,
                     const std::string& context) {
    Fixture& f = cell.f;
    Session& sess = cell.sess;
    const bool has_store = session_test_access::store(sess) != nullptr;

    std::vector<std::string> retained_full;
    const std::vector<std::string> retained = retained_frames(f, sess, retained_full);
    const std::optional<seqnum_t> store_out = store_next(f, sess, direction_t::outbound);
    const std::optional<seqnum_t> store_in = store_next(f, sess, direction_t::inbound);
    // The wire, the order and the state are read after the store reads: those
    // run the io_context, so an event that arrives after both operations
    // completed is read too.
    const std::vector<std::string> wire_full(
        f.wire.begin() + static_cast<std::ptrdiff_t>(cell.wire_before), f.wire.end());
    std::vector<std::string> wire;
    // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
    for (const auto& frame : wire_full) wire.push_back(brief(frame));
    const std::string at_the_end = where_is(sess);

    const std::string reading =
        std::string("starting callback: ") + callback +
        "; inside it after the start: " + cell.inside.state + " " + cell.inside.lifecycle +
        ", onLogout calls " + std::to_string(cell.inside.on_logout_calls) +
        ", Logout toAdmin calls " + std::to_string(cell.inside.logout_to_admin_calls) +
        ", the started operation " +
        (cell.inside.started_completed ? "completed" : "not completed") +
        "; at the end: " + at_the_end + "; .order = " + literal(cell.order) +
        ", .callbacks = " + literal(cell.callbacks) + ", .outer = " + describe(cell.outer) +
        ", .started = " + describe(cell.started) + ", .wire = " + literal(wire) +
        ", .retained = " + literal(retained) + ", .next_out = " + std::to_string(next_out(sess)) +
        ", .next_in = " + std::to_string(next_in(sess)) +
        ", .store_next_out = " + literal(store_out) + ", .store_next_in = " + literal(store_in) +
        ", .writes_after_close = " + std::to_string(f.live_writes.begun_after_close) +
        ", .writes_refused = " + std::to_string(f.live_writes.refused_after_close) +
        "; transport close() calls " + std::to_string(f.live_writes.closes) +
        ", live writes started " + std::to_string(f.live_writes.started) + ", most in flight " +
        std::to_string(f.live_writes.max_in_flight) + ". " + context;
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    ASSERT_TRUE(cell.inside.read) << "PATH NOT DRIVEN: " << callback
                                  << " did not start the operation. " << reading;
    EXPECT_TRUE(completed) << "both operations must complete within the pump's bound; "
                           << cell.pending_at_the_bound << reading;

    EXPECT_EQ(cell.inside.lifecycle, "open")
        << "inside the callback after the start, the lifecycle is open. " << reading;
    EXPECT_EQ(cell.inside.state, "Active")
        << "inside the callback after the start, the session is Active. " << reading;
    EXPECT_EQ(cell.inside.on_logout_calls, 0)
        << "inside the callback after the start, onLogout has not run. " << reading;
    EXPECT_EQ(cell.inside.logout_to_admin_calls, 0)
        << "inside the callback after the start, the Logout's toAdmin has not run. " << reading;
    EXPECT_FALSE(cell.inside.started_completed)
        << "inside the callback after the start, the operation it started has not completed. "
        << reading;

    EXPECT_EQ(cell.order, want.order)
        << "the order of the starting callback's return, onLogout, each transmit and the two "
           "completions, each with the session's FSM state and lifecycle at that moment. "
        << reading;
    EXPECT_EQ(cell.callbacks, want.callbacks)
        << "the other toApp and toAdmin calls, each with the session's FSM state and lifecycle "
           "on its entry. "
        << reading;
    EXPECT_EQ(at_the_end, "Disconnected closed_drained")
        << "at the end the session is closed. " << reading;

    EXPECT_TRUE(cell.outer.done && !cell.outer.threw)
        << "the operation that called the callback must complete without throwing. " << reading;
    EXPECT_EQ(cell.outer.err, want.outer)
        << "the result of the operation that called the callback. " << reading;
    EXPECT_TRUE(cell.started.done && !cell.started.threw)
        << "the operation the callback started must complete without throwing. " << reading;
    EXPECT_EQ(cell.started.err, want.started)
        << "the result of the operation the callback started. " << reading;

    EXPECT_EQ(wire, want.wire) << "what was transmitted. " << reading;
    EXPECT_EQ(has_store, !want.retained.empty())
        << "the arm's values and the session disagree on whether there is a store. " << reading;
    EXPECT_EQ(retained, want.retained) << "what the store retained. " << reading;
    if (has_store) {
        // A transmitted frame is retained byte for byte under its own number.
        for (const auto& frame : wire_full) {
            const seqnum_t seq = seq_of(frame);
            ASSERT_TRUE(seq >= 1 && seq <= kRetainedThrough)
                << "a transmitted frame carries a MsgSeqNum outside the range read. " << reading;
            EXPECT_EQ(retained_full[seq - 1], frame)
                << "the store's frame under outbound 34=" << seq
                << " is not the frame transmitted with that number. " << reading;
        }
    }
    EXPECT_EQ(next_out(sess), want.next_out) << "the manager's next outbound number. " << reading;
    EXPECT_EQ(next_in(sess), want.next_in) << "the manager's next inbound number. " << reading;
    if (has_store) {
        EXPECT_EQ(store_out, want.store_next_out)
            << "the store's next outbound number. " << reading;
    }
    if (want.store_next_in) {
        EXPECT_EQ(store_in, want.store_next_in) << "the store's next inbound number. " << reading;
    }
    EXPECT_EQ(f.live_writes.begun_after_close, want.writes_after_close)
        << "the writes begun after the transport's close(). " << reading;
    EXPECT_EQ(f.live_writes.refused_after_close, want.writes_refused)
        << "the writes the transport refused. " << reading;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << reading;
}

enum class FromAppReturns { ok, reject };

// fromApp for an inbound 35=D starts a send whose toApp throws, so the send
// awaits a terminal close while fromApp is on the stack.
void run_nested_to_app_throws(StoreKind store, FromAppReturns from_app_returns,
                              TransportKind transport, const CloseExpect& expect) {
    Fixture f{ExecKind::io_context, store};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const auto reply = bytes_of(kPayloadOne);
    CloseCell cell{f, sess};
    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    bool in_from_app = false;
    std::optional<bool> throwing_to_app_inside_from_app;

    f.app->on_from_app = [&] {
        in_from_app = true;
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] fromApp: starting Session::send\n");
        asio::co_spawn(ex, sess.send(reply), cell.completes(cell.started, "send"));
        std::fprintf(stderr, "[ B41 ] fromApp: co_spawn returned\n");
        cell.read_inside_callback();
        cell.note("fromApp returns");
        in_from_app = false;
    };
    f.app->on_to_app = [&] {
        if (throwing_to_app_inside_from_app.has_value()) {
            cell.callback("toApp");
            return;
        }
        throwing_to_app_inside_from_app = in_from_app;
        cell.callback("toApp throws");
        throw std::runtime_error{"B41: toApp throws"};
    };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    f.app->on_to_admin = [&](std::string_view msg_type) { cell.to_admin(msg_type); };
    if (from_app_returns == FromAppReturns::reject) {
        f.app->from_app_result = std::unexpected(error::session_invalid_argument);
    }

    const auto inbound = peer_frame("D", 2,
                                    "11=PEER-1\x01"
                                    "54=1\x01"
                                    "55=AAPL\x01");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                   cell.completes(cell.outer, "on_inbound_frame"));
    // Bounded: a call that does not complete is reported below, not waited for.
    const auto all_done = [&] {
        return cell.outer.done && (cell.started.done || f.app->from_app_calls == 0);
    };
    const bool completed = fixpp::test_support::pump_until(f.ioc, all_done, std::chrono::seconds{5},
                                                           fixpp::test_support::kPumpSlice,
                                                           "CallbackSend::nested_to_app_throws");
    if (!completed) {
        cell.record_pending();
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::nested_to_app_throws");
    }

    const std::string classification = !throwing_to_app_inside_from_app.has_value() ? "never ran"
                                       : *throwing_to_app_inside_from_app           ? "nested"
                                                                                    : "deferred";
    const std::string context =
        "The send's toApp that throws: " + classification + "; fromApp returned " +
        (from_app_returns == FromAppReturns::reject ? "an error" : "success") + ".";
    ASSERT_EQ(f.app->from_app_calls, 1)
        << "PATH NOT DRIVEN: the inbound 35=D did not reach fromApp. " << context;
    ASSERT_EQ(classification, "nested")
        << "PATH NOT DRIVEN: the send's toApp must run, and throw, while fromApp is on the "
           "stack. "
        << context;
    check_close_arm(cell, expect, "fromApp", completed, context);
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows) {
    run_nested_to_app_throws(StoreKind::memory, FromAppReturns::ok, TransportKind::sync_capture,
                             CloseExpect{.order = {"fromApp returns [Active open]",
                                                   "on_inbound_frame completes [Active open]",
                                                   "onLogout [Disconnected closing]",
                                                   "send completes [Disconnected closed_drained]"},
                                         .callbacks = {"toApp throws [Active open]"},
                                         .outer = {},
                                         .started = error::app_callback_threw,
                                         .wire = {},
                                         .retained = {"35=A 34=1", "none", "none", "none"},
                                         .next_out = 2,
                                         .next_in = 3,
                                         .store_next_out = 2,
                                         .store_next_in = {},
                                         .writes_after_close = 0,
                                         .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_LiveWrite) {
    run_nested_to_app_throws(StoreKind::memory, FromAppReturns::ok,
                             TransportKind::live_suspending_write,
                             CloseExpect{.order = {"fromApp returns [Active open]",
                                                   "on_inbound_frame completes [Active open]",
                                                   "onLogout [Disconnected closing]",
                                                   "send completes [Disconnected closed_drained]"},
                                         .callbacks = {"toApp throws [Active open]"},
                                         .outer = {},
                                         .started = error::app_callback_threw,
                                         .wire = {},
                                         .retained = {"35=A 34=1", "none", "none", "none"},
                                         .next_out = 2,
                                         .next_in = 3,
                                         .store_next_out = 2,
                                         .store_next_in = {},
                                         .writes_after_close = 0,
                                         .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_NoStore) {
    run_nested_to_app_throws(StoreKind::none, FromAppReturns::ok, TransportKind::sync_capture,
                             CloseExpect{.order = {"fromApp returns [Active open]",
                                                   "on_inbound_frame completes [Active open]",
                                                   "onLogout [Disconnected closing]",
                                                   "send completes [Disconnected closed_drained]"},
                                         .callbacks = {"toApp throws [Active open]"},
                                         .outer = {},
                                         .started = error::app_callback_threw,
                                         .wire = {},
                                         .retained = {},
                                         .next_out = 2,
                                         .next_in = 3,
                                         .store_next_out = {},
                                         .store_next_in = {},
                                         .writes_after_close = 0,
                                         .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_NoStore_LiveWrite) {
    run_nested_to_app_throws(StoreKind::none, FromAppReturns::ok,
                             TransportKind::live_suspending_write,
                             CloseExpect{.order = {"fromApp returns [Active open]",
                                                   "on_inbound_frame completes [Active open]",
                                                   "onLogout [Disconnected closing]",
                                                   "send completes [Disconnected closed_drained]"},
                                         .callbacks = {"toApp throws [Active open]"},
                                         .outer = {},
                                         .started = error::app_callback_threw,
                                         .wire = {},
                                         .retained = {},
                                         .next_out = 2,
                                         .next_in = 3,
                                         .store_next_out = {},
                                         .store_next_in = {},
                                         .writes_after_close = 0,
                                         .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FileStore) {
    run_nested_to_app_throws(
        StoreKind::file, FromAppReturns::ok, TransportKind::sync_capture,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]"},
                    .outer = {},
                    .started = error::app_callback_threw,
                    .wire = {},
                    .retained = {"35=A 34=1", "none", "none", "none"},
                    .next_out = 2,
                    .next_in = 3,
                    .store_next_out = 2,
                    .store_next_in = 3,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FileStore_LiveWrite) {
    run_nested_to_app_throws(
        StoreKind::file, FromAppReturns::ok, TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]"},
                    .outer = {},
                    .started = error::app_callback_threw,
                    .wire = {},
                    .retained = {"35=A 34=1", "none", "none", "none"},
                    .next_out = 2,
                    .next_in = 3,
                    .store_next_out = 2,
                    .store_next_in = 3,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects) {
    run_nested_to_app_throws(
        StoreKind::memory, FromAppReturns::reject, TransportKind::sync_capture,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closed_drained]",
                              "transmit 35=j 34=2 [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]", "toApp [Active open]"},
                    .outer = {},
                    .started = error::app_callback_threw,
                    .wire = {"35=j 34=2"},
                    .retained = {"35=A 34=1", "35=j 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects_LiveWrite) {
    run_nested_to_app_throws(
        StoreKind::memory, FromAppReturns::reject, TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]", "toApp [Active open]"},
                    .outer = error::dispatch_aborted,
                    .started = error::app_callback_threw,
                    .wire = {},
                    .retained = {"35=A 34=1", "35=j 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects_FileStore) {
    run_nested_to_app_throws(
        StoreKind::file, FromAppReturns::reject, TransportKind::sync_capture,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closed_drained]",
                              "transmit 35=j 34=2 [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]", "toApp [Active open]"},
                    .outer = {},
                    .started = error::app_callback_threw,
                    .wire = {"35=j 34=2"},
                    .retained = {"35=A 34=1", "35=j 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = 3,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects_FileStore_LiveWrite) {
    run_nested_to_app_throws(
        StoreKind::file, FromAppReturns::reject, TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]", "toApp [Active open]"},
                    .outer = error::dispatch_aborted,
                    .started = error::app_callback_threw,
                    .wire = {},
                    .retained = {"35=A 34=1", "35=j 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = 2,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects_NoStore) {
    run_nested_to_app_throws(
        StoreKind::none, FromAppReturns::reject, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "transmit 35=j 34=2 [Active open]",
                      "on_inbound_frame completes [Active open]", "onLogout [Disconnected closing]",
                      "send completes [Disconnected closed_drained]"},
            .callbacks = {"toApp throws [Active open]", "toApp [Active open]"},
            .outer = {},
            .started = error::app_callback_threw,
            .wire = {"35=j 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects_NoStore_LiveWrite) {
    run_nested_to_app_throws(
        StoreKind::none, FromAppReturns::reject, TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromApp returns [Active open]", "transmit 35=j 34=2 [Active open]",
                              "onLogout [Disconnected closing]",
                              "on_inbound_frame completes [Disconnected closing]",
                              "send completes [Disconnected closed_drained]"},
                    .callbacks = {"toApp throws [Active open]", "toApp [Active open]"},
                    .outer = {},
                    .started = error::app_callback_threw,
                    .wire = {"35=j 34=2"},
                    .retained = {},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = {},
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

enum class CloseFrom { from_app, from_admin };

// An inbound callback of an Active session starts Session::close on the
// session's executor, directly.
void run_callback_starts_close(CloseFrom from, close_mode mode, StoreKind store,
                               TransportKind transport, const CloseExpect& expect) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const char* callback_name = from == CloseFrom::from_app ? "fromApp" : "fromAdmin";
    CloseCell cell{f, sess};
    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };

    const auto start_close = [&] {
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] %s: starting Session::close\n", callback_name);
        asio::co_spawn(ex, sess.close(mode), cell.completes(cell.started, "close"));
        std::fprintf(stderr, "[ B41 ] %s: co_spawn returned\n", callback_name);
        cell.read_inside_callback();
        cell.note(std::string(callback_name) + " returns");
    };

    if (from == CloseFrom::from_app) {
        f.app->on_from_app = [&] {
            if (!cell.inside.read) start_close();
        };
    } else {
        f.app->on_from_admin = [&](std::string_view msg_type) {
            if (msg_type == "1" && !cell.inside.read) start_close();
        };
    }
    f.app->on_to_app = [&] { cell.callback("toApp"); };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    f.app->on_to_admin = [&](std::string_view msg_type) { cell.to_admin(msg_type); };

    const auto inbound = from == CloseFrom::from_app ? peer_frame("D", 2,
                                                                  "11=PEER-1\x01"
                                                                  "54=1\x01"
                                                                  "55=AAPL\x01")
                                                     : peer_frame("1", 2, "112=B41-TR\x01");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                   cell.completes(cell.outer, "on_inbound_frame"));
    // A graceful close then waits for a peer Logout; everything the arm expects
    // before the end of that wait has happened once on_inbound_frame completed
    // and onLogout ran.
    bool completed = true;
    if (mode == close_mode::graceful) {
        completed = f.end_logout_wait_when(
            [&] { return cell.outer.done && cell.on_logout_calls > 0; },
            [&] {
                return std::string(cell.outer.done ? "onLogout has not run"
                                                   : "on_inbound_frame has not completed");
            },
            std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
            "CallbackSend::callback_starts_close_logout_wait");
    }
    // Bounded: an operation that does not complete is reported below, not waited for.
    completed =
        completed &&
        fixpp::test_support::pump_until(
            f.ioc, [&] { return cell.outer.done && (cell.started.done || !cell.inside.read); },
            std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
            "CallbackSend::callback_starts_close");
    if (!completed) {
        cell.record_pending();
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::callback_starts_close");
    }

    const std::string context = std::string("Close mode: ") +
                                (mode == close_mode::terminal ? "terminal" : "graceful") + ".";
    check_close_arm(cell, expect, callback_name, completed, context);
}

TEST(CallbackSend, FromAppStartsClose_Terminal) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::terminal, StoreKind::memory, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_NoStore) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::terminal, StoreKind::none, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {},
            .retained = {},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_NoStore_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {},
            .retained = {},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_FileStore) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::terminal, StoreKind::file, TransportKind::sync_capture,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {},
                    .retained = {"35=A 34=1", "none", "none", "none"},
                    .next_out = 2,
                    .next_in = 3,
                    .store_next_out = 2,
                    .store_next_in = 3,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_FileStore_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {},
                    .retained = {"35=A 34=1", "none", "none", "none"},
                    .next_out = 2,
                    .next_in = 3,
                    .store_next_out = 2,
                    .store_next_in = 3,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Graceful) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::graceful, StoreKind::memory, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Graceful_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::graceful, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Graceful_FileStore) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::graceful, StoreKind::file, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromApp returns [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Graceful_FileStore_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::graceful, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"fromApp returns [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Graceful_NoStore) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::graceful, StoreKind::none, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Graceful_NoStore_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_app, close_mode::graceful, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAdminStartsClose_Terminal) {
    run_callback_starts_close(
        CloseFrom::from_admin, close_mode::terminal, StoreKind::memory, TransportKind::sync_capture,
        CloseExpect{.order = {"fromAdmin returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "transmit 35=0 34=2 [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toAdmin 35=0 [Active open]"},
                    .outer = {},
                    .started = {},
                    .wire = {"35=0 34=2"},
                    .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAdminStartsClose_Terminal_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_admin, close_mode::terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromAdmin returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toAdmin 35=0 [Active open]"},
                    .outer = error::dispatch_aborted,
                    .started = {},
                    .wire = {},
                    .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAdminStartsClose_Terminal_FileStore) {
    run_callback_starts_close(
        CloseFrom::from_admin, close_mode::terminal, StoreKind::file, TransportKind::sync_capture,
        CloseExpect{.order = {"fromAdmin returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "transmit 35=0 34=2 [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toAdmin 35=0 [Active open]"},
                    .outer = {},
                    .started = {},
                    .wire = {"35=0 34=2"},
                    .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = 3,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAdminStartsClose_Terminal_FileStore_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_admin, close_mode::terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromAdmin returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "on_inbound_frame completes [Disconnected closed_drained]"},
                    .callbacks = {"toAdmin 35=0 [Active open]"},
                    .outer = error::dispatch_aborted,
                    .started = {},
                    .wire = {},
                    .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = 3,
                    .store_next_in = 2,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, FromAdminStartsClose_Terminal_NoStore) {
    run_callback_starts_close(
        CloseFrom::from_admin, close_mode::terminal, StoreKind::none, TransportKind::sync_capture,
        CloseExpect{
            .order = {"fromAdmin returns [Active open]", "transmit 35=0 34=2 [Active open]",
                      "on_inbound_frame completes [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=0 [Active open]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAdminStartsClose_Terminal_NoStore_LiveWrite) {
    run_callback_starts_close(
        CloseFrom::from_admin, close_mode::terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"fromAdmin returns [Active open]", "transmit 35=0 34=2 [Active open]",
                              "onLogout [Disconnected closing]",
                              "on_inbound_frame completes [Disconnected closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {"toAdmin 35=0 [Active open]"},
                    .outer = {},
                    .started = {},
                    .wire = {"35=0 34=2"},
                    .retained = {},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = {},
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

// Whose outbound callback starts the second operation.
enum class OuterSite {
    app_send,         // the toApp of a Session::send
    heartbeat_reply,  // the toAdmin of the Heartbeat answering a TestRequest
    poss_dup_reject,  // the toAdmin of the Reject answering a PossDup without 122
};
enum class Starts { close_graceful, close_terminal, send };
enum class ResetOnDisconnect { no, yes };

// An outbound callback of an Active session starts a second operation on the
// session's executor, directly: a close or a send. An arm that starts a close
// asserts through check_close_arm against `expect`; an arm that starts a send
// does not read `expect`.
void run_outbound_callback_starts(OuterSite site, Starts starts, StoreKind store,
                                  TransportKind transport = TransportKind::sync_capture,
                                  const CloseExpect& expect = {},
                                  ResetOnDisconnect reset = ResetOnDisconnect::no) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    cfg.reset_on_disconnect = reset == ResetOnDisconnect::yes;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const std::size_t wire_before = f.wire.size();
    const auto outer_payload = bytes_of(
        "35=D\x01"
        "11=B41-OUTER\x01"
        "55=AAA\x01");
    const auto inner_payload = bytes_of(
        "35=D\x01"
        "11=B41-INNER\x01"
        "55=BBB\x01");
    const char* inner_name = starts == Starts::send ? "send" : "close";
    const char* outer_name = site == OuterSite::app_send ? "send" : "on_inbound_frame";

    bool started = false;
    bool in_callback = false;
    bool outer_done = false;
    bool inner_done = false;
    std::string outer_result = "not completed";
    std::string inner_result = "not completed";
    std::string seen_in_callback = "not recorded";
    std::string seen_when_outer_done = "not recorded";
    std::optional<bool> inner_to_app_in_callback;  // Starts::send only
    std::vector<std::string> events;               // from the second operation's start on, in order
    CloseCell cell{f, sess};

    const auto session_now = [&] {
        return "state=" + std::to_string(static_cast<int>(sess.state())) +
               " lifecycle=" + std::to_string(session_test_access::lifecycle_value(sess));
    };
    const auto where = [&] {
        return std::string(in_callback ? " (starting callback on the stack)"
                                       : " (starting callback returned)");
    };
    const auto result_text = [](std::exception_ptr ep, const expected_t<void>& r) {
        return ep  ? std::string("threw")
               : r ? std::string("ok")
                   : "error " + std::to_string(static_cast<int>(r.error()));
    };
    const auto start_inner = [&](const char* callback) {
        started = true;
        in_callback = true;
        const auto on_done = [&](std::exception_ptr ep, expected_t<void> r) {
            inner_done = true;
            inner_result = result_text(ep, r);
            events.push_back(std::string("the ") + inner_name + "'s completion handler" + where());
            record_into(cell.started)(ep, std::move(r));
            cell.note(std::string(inner_name) + " completes");
        };
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] %s: starting Session::%s\n", callback, inner_name);
        if (starts == Starts::send) {
            asio::co_spawn(ex, sess.send(inner_payload), on_done);
        } else {
            asio::co_spawn(ex,
                           sess.close(starts == Starts::close_terminal ? close_mode::terminal
                                                                       : close_mode::graceful),
                           on_done);
        }
        std::fprintf(stderr, "[ B41 ] %s: co_spawn returned\n", callback);
        seen_in_callback = session_now() + ", NextNumOut=" + std::to_string(next_out(sess)) + ", " +
                           inner_name + " " + inner_result;
        events.push_back(std::string(callback) + " returns");
        cell.read_inside_callback();
        cell.note(std::string(callback) + " returns");
        in_callback = false;
    };

    f.on_transmit = [&](const std::string& frame) {
        cell.transmitted(frame);
        if (started) {
            events.push_back("transmit " + describe(frame) + where() + " " + session_now());
        }
    };
    f.app->on_to_app = [&] {
        if (site == OuterSite::app_send && f.app->to_app_calls == 1) {
            start_inner("toApp");
            return;
        }
        if (!started) return;
        cell.callback("toApp");
        if (starts == Starts::send && !inner_to_app_in_callback.has_value()) {
            inner_to_app_in_callback = in_callback;
        }
        events.push_back("toApp, call " + std::to_string(f.app->to_app_calls) + where());
    };
    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (!started && site == OuterSite::heartbeat_reply && msg_type == "0") {
            start_inner("toAdmin(35=0)");
            return;
        }
        if (!started && site == OuterSite::poss_dup_reject && msg_type == "3") {
            start_inner("toAdmin(35=3)");
            return;
        }
        // Any other admin frame, the close's own Logout included, is only recorded.
        if (started) {
            events.push_back("toAdmin 35=" + std::string(msg_type) + where());
            cell.to_admin(msg_type);
        }
    };
    f.app->on_lifecycle = [&](const char* name) {
        cell.lifecycle_callback(name);
        if (started) events.push_back(std::string(name) + where() + " " + session_now());
    };

    const auto on_outer_done = [&](std::exception_ptr ep, expected_t<void> r) {
        outer_done = true;
        outer_result = result_text(ep, r);
        seen_when_outer_done = session_now();
        events.push_back(std::string("the outer ") + outer_name + "'s completion handler");
        record_into(cell.outer)(ep, std::move(r));
        cell.note(std::string(outer_name) + " completes");
    };
    const auto inbound = site == OuterSite::heartbeat_reply ? peer_frame("1", 2, "112=B41-TR\x01")
                                                            : peer_frame("D", 2,
                                                                         "43=Y\x01"
                                                                         "11=PEER-DUP\x01"
                                                                         "54=1\x01"
                                                                         "55=AAPL\x01");
    if (site == OuterSite::app_send) {
        asio::co_spawn(ex, sess.send(outer_payload), on_outer_done);
    } else {
        asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                       on_outer_done);
    }
    // A graceful close then waits for a peer Logout; everything the arm expects
    // before the end of that wait has happened once the outer operation completed
    // and onLogout ran.
    bool logout_wait_ended = true;
    if (starts == Starts::close_graceful) {
        logout_wait_ended = f.end_logout_wait_when(
            [&] { return outer_done && cell.on_logout_calls > 0; },
            [&] {
                return outer_done ? std::string("onLogout has not run")
                                  : std::string("the outer ") + outer_name + " has not completed";
            },
            std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
            "CallbackSend::outbound_callback_starts_logout_wait");
    }
    // Bounded: an operation that does not complete is reported below, not waited for.
    const bool completed =
        logout_wait_ended &&
        fixpp::test_support::pump_until(
            f.ioc, [&] { return outer_done && (inner_done || !started); }, std::chrono::seconds{5},
            fixpp::test_support::kPumpSlice, "CallbackSend::outbound_callback_starts");

    if (starts != Starts::send) {
        if (!completed) {
            cell.record_pending();
            fixpp::test_support::cancel_and_drain_or_report(
                f.ioc, *f.clock, "CallbackSend::outbound_callback_starts");
        }
        const char* starting_callback = site == OuterSite::app_send          ? "toApp"
                                        : site == OuterSite::heartbeat_reply ? "toAdmin(35=0)"
                                                                             : "toAdmin(35=3)";
        const std::string close_context =
            std::string("Outer: ") + outer_name +
            "; close mode: " + (starts == Starts::close_terminal ? "terminal" : "graceful") +
            (reset == ResetOnDisconnect::yes ? "; reset_on_disconnect set." : ".");
        ASSERT_TRUE(started) << "PATH NOT DRIVEN: the starting callback never ran. "
                             << close_context;
        check_close_arm(cell, expect, starting_callback, completed, close_context);
        return;
    }

    // Store reads: each bounded, each into storage that outlives a late completion.
    MessageStore* st = session_test_access::store(sess);
    const auto store_next_out = [&]() -> std::string {
        if (st == nullptr) return "no store";
        auto slot = std::make_shared<std::pair<bool, std::string>>(false, "read not completed");
        asio::co_spawn(f.exec, st->next_seqnum(direction_t::outbound, false),
                       [slot](std::exception_ptr ep, expected_t<seqnum_t> r) {
                           slot->first = true;
                           slot->second =
                               ep  ? "threw"
                               : r ? std::to_string(*r)
                                   : "error " + std::to_string(static_cast<int>(r.error()));
                       });
        (void)fixpp::test_support::pump_until(
            f.ioc, [&] { return slot->first; }, std::chrono::seconds{2},
            fixpp::test_support::kPumpSlice, "CallbackSend::outbound_callback_starts_store");
        return slot->second;
    };
    std::vector<std::shared_ptr<CollectVisitor>> visitors;
    // The frames the store returns for one outbound number, and the read's status.
    const auto store_holds = [&](seqnum_t seq) -> std::pair<std::string, std::vector<std::string>> {
        if (st == nullptr) return {"no store", {}};
        auto visitor = std::make_shared<CollectVisitor>();
        visitors.push_back(visitor);
        auto slot = std::make_shared<std::pair<bool, std::string>>(false, "read not completed");
        asio::co_spawn(f.exec, st->retrieve(seq, seq, direction_t::outbound, *visitor),
                       [slot](std::exception_ptr ep, expected_t<void> r) {
                           slot->first = true;
                           slot->second =
                               ep  ? "threw"
                               : r ? "ok"
                                   : "error " + std::to_string(static_cast<int>(r.error()));
                       });
        (void)fixpp::test_support::pump_until(
            f.ioc, [&] { return slot->first; }, std::chrono::seconds{2},
            fixpp::test_support::kPumpSlice, "CallbackSend::outbound_callback_starts_retrieve");
        return {slot->second, slot->first ? visitor->frames : std::vector<std::string>{}};
    };

    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    std::vector<seqnum_t> wire_seqs;
    // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
    for (const auto& fr : sent) wire_seqs.push_back(seq_of(fr));
    std::vector<seqnum_t> unique_seqs = wire_seqs;
    std::ranges::sort(unique_seqs);
    const bool distinct = std::ranges::adjacent_find(unique_seqs) == unique_seqs.end();
    const bool ascending =
        std::ranges::adjacent_find(wire_seqs, std::greater_equal<>{}) == wire_seqs.end();
    unique_seqs.erase(std::ranges::unique(unique_seqs).begin(), unique_seqs.end());

    const bool has_store = st != nullptr;
    bool all_retained = true;
    std::string store_text;
    for (const seqnum_t seq : unique_seqs) {
        const auto [status, frames] = store_holds(seq);
        store_text += " 34=" + std::to_string(seq) + ": " + describe(frames) + "(" + status + ")";
        for (const auto& fr : sent) {
            if (seq_of(fr) != seq) continue;
            if (frames.size() != 1 || frames.front() != fr) all_retained = false;
        }
    }
    const std::string manager_out = std::to_string(next_out(sess));
    const std::string store_out = store_next_out();
    const bool store_agrees = store_out == manager_out;
    const bool outcomes_ok = outer_result == "ok" && inner_result == "ok";
    const auto verdict = [](bool applies, bool holds) {
        return std::string(!applies ? "n/a (no store)" : holds ? "holds" : "FAILS");
    };

    const std::string classification = !inner_to_app_in_callback.has_value() ? "never ran"
                                       : *inner_to_app_in_callback           ? "nested"
                                                                             : "deferred";
    std::string event_text;
    for (const auto& e : events) event_text += " [" + e + "]";
    const std::string context =
        std::string("Outer: ") + outer_name + "; started inside its callback: " + inner_name +
        "; the inner send's toApp: " + classification +
        ". Transmitted, in wire order: " + describe(sent) + "; the store holds under" +
        (store_text.empty() ? " (nothing read)" : store_text) + "; outer " + outer_name + ": " +
        outer_result + "; " + inner_name + ": " + inner_result +
        "; NextNumOut: manager=" + manager_out + ", store=" + store_out +
        "; inside the starting callback after the co_spawn returned: " + seen_in_callback +
        "; when the outer operation completed: " + seen_when_outer_done +
        "; at the end: " + session_now() + "; from the second operation's start on, in order:" +
        (event_text.empty() ? " (none)" : event_text) +
        "; live writes started=" + std::to_string(f.live_writes.started) +
        ", most in flight at once=" + std::to_string(f.live_writes.max_in_flight) +
        ". (a) distinct MsgSeqNums: " + verdict(true, distinct) +
        "; (b) ascending wire order: " + verdict(true, ascending) +
        "; (c) each frame retained under its own number: " + verdict(has_store, all_retained) +
        "; (d) store and manager agree: " + verdict(has_store, store_agrees) +
        "; (e) both operations report success: " + verdict(true, outcomes_ok) + ".";
    std::printf("[ MEASURED ] %s\n", context.c_str());
    std::fflush(stdout);
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::outbound_callback_starts");
    }

    ASSERT_TRUE(started) << "PATH NOT DRIVEN: the starting callback never ran. " << context;
    ASSERT_NE(classification, "never ran")
        << "PATH NOT DRIVEN: the inner send's toApp never ran. " << context;
    EXPECT_EQ(classification, "deferred")
        << "a send started in toApp or toAdmin must be deferred. " << context;
    // The outer frame, then the send's, under consecutive numbers.
    ASSERT_EQ(sent.size(), 2U) << "exactly two frames must be transmitted. " << context;
    EXPECT_EQ(field(sent[1], 11), "B41-INNER")
        << "the frame of the send started in the callback must be transmitted second. " << context;
    EXPECT_EQ(wire_seqs, (std::vector<seqnum_t>{2U, 3U}))
        << "the two frames must carry MsgSeqNum(34) 2 then 3. " << context;
    EXPECT_TRUE(distinct) << "(a) " << context;
    EXPECT_TRUE(ascending) << "(b) " << context;
    if (has_store) {
        EXPECT_TRUE(all_retained) << "(c) " << context;
        EXPECT_TRUE(store_agrees) << "(d) " << context;
    }
    EXPECT_TRUE(outcomes_ok) << "(e) " << context;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << context;
    EXPECT_EQ(manager_out, "4") << "two frames must take two numbers. " << context;
}

TEST(CallbackSend, ToAppStartsClose_Graceful) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_graceful, StoreKind::memory, TransportKind::sync_capture,
        CloseExpect{
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send completes [Active closing]", "transmit 35=5 34=3 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Graceful_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_graceful, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send completes [Active closing]", "transmit 35=5 34=3 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Graceful_NoStore) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_graceful, StoreKind::none, TransportKind::sync_capture,
        CloseExpect{.order = {"toApp returns [Active open]",
                              "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                              "send completes [Active open]", "transmit 35=5 34=3 [Active closing]",
                              "onLogout [LogoutSent closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {"toAdmin 35=5 [Active closing]"},
                    .outer = {},
                    .started = {},
                    .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
                    .retained = {},
                    .next_out = 4,
                    .next_in = 2,
                    .store_next_out = {},
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Graceful_NoStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_graceful, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "send completes [Active closing]", "transmit 35=5 34=3 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Graceful_FileStore) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_graceful, StoreKind::file, TransportKind::sync_capture,
        CloseExpect{
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send completes [Active closing]", "transmit 35=5 34=3 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = 2,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Graceful_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_graceful, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send completes [Active closing]", "transmit 35=5 34=3 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = 2,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_graceful, StoreKind::memory,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=0 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_graceful, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=0 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_FileStore) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_graceful, StoreKind::file,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "on_inbound_frame completes [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=0 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_graceful, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "on_inbound_frame completes [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=0 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_NoStore) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_graceful, StoreKind::none,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active open]",
                      "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_NoStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_graceful, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_graceful, StoreKind::memory,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=3 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_graceful, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=3 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite_FileStore) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_graceful, StoreKind::file,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=3 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_graceful, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=3 34=2", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite_NoStore) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_graceful, StoreKind::none,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active open]",
                      "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite_NoStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_graceful, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::memory, TransportKind::sync_capture,
        CloseExpect{.order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "transmit 35=D 34=2 11=B41-OUTER [Disconnected closed_drained]",
                              "send completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {"35=D 34=2 11=B41-OUTER"},
                    .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
                    .next_out = 3,
                    .next_in = 2,
                    .store_next_out = 3,
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "send completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = error::dispatch_aborted,
                    .started = {},
                    .wire = {},
                    .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
                    .next_out = 3,
                    .next_in = 2,
                    .store_next_out = 3,
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal_FileStore) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::file, TransportKind::sync_capture,
        CloseExpect{.order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "transmit 35=D 34=2 11=B41-OUTER [Disconnected closed_drained]",
                              "send completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {"35=D 34=2 11=B41-OUTER"},
                    .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
                    .next_out = 3,
                    .next_in = 2,
                    .store_next_out = 3,
                    .store_next_in = 2,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]",
                              "send completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = error::dispatch_aborted,
                    .started = {},
                    .wire = {},
                    .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
                    .next_out = 3,
                    .next_in = 2,
                    .store_next_out = 3,
                    .store_next_in = 2,
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal_NoStore) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::none, TransportKind::sync_capture,
        CloseExpect{.order = {"toApp returns [Active open]",
                              "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                              "send completes [Active open]", "onLogout [Disconnected closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {"35=D 34=2 11=B41-OUTER"},
                    .retained = {},
                    .next_out = 3,
                    .next_in = 2,
                    .store_next_out = {},
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal_NoStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "onLogout [Disconnected closing]", "send completes [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=D 34=2 11=B41-OUTER"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsClose_Terminal_ResetOnDisconnect_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = error::dispatch_aborted,
                    .started = {},
                    .wire = {},
                    .retained = {"none", "none", "none", "none"},
                    .next_out = 1,
                    .next_in = 1,
                    .store_next_out = 1,
                    .store_next_in = 1,
                    .writes_after_close = 1,
                    .writes_refused = 1},
        ResetOnDisconnect::yes);
}

TEST(CallbackSend, ToAppStartsClose_Terminal_ResetOnDisconnect_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::app_send, Starts::close_terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                              "send completes [Disconnected closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = error::dispatch_aborted,
                    .started = {},
                    .wire = {},
                    .retained = {"none", "none", "none", "none"},
                    .next_out = 1,
                    .next_in = 1,
                    .store_next_out = 1,
                    .store_next_in = 1,
                    .writes_after_close = 1,
                    .writes_refused = 1},
        ResetOnDisconnect::yes);
}

// A terminal close started in toAdmin, at the site that calls toAdmin before it assigns
// the frame's MsgSeqNum: the Heartbeat that answers a TestRequest. The operation that
// called toAdmin is on_inbound_frame.
TEST(CallbackSend, ToAdminStartsClose_Terminal) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_terminal, StoreKind::memory,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "transmit 35=0 34=2 [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2"},
            .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = error::dispatch_aborted,
            .started = {},
            .wire = {},
            .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_FileStore) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_terminal, StoreKind::file,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "transmit 35=0 34=2 [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2"},
            .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = error::dispatch_aborted,
            .started = {},
            .wire = {},
            .retained = {"35=A 34=1", "35=0 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 2,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_NoStore) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_terminal, StoreKind::none,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=0) returns [Active open]", "transmit 35=0 34=2 [Active open]",
                      "on_inbound_frame completes [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=0 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_NoStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::heartbeat_reply, Starts::close_terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"toAdmin(35=0) returns [Active open]",
                              "transmit 35=0 34=2 [Active open]", "onLogout [Disconnected closing]",
                              "on_inbound_frame completes [Disconnected closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {"35=0 34=2"},
                    .retained = {},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = {},
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

// The same at the site that assigns the frame's MsgSeqNum before it calls toAdmin: the
// Reject that answers a PossDup without OrigSendingTime.
TEST(CallbackSend, ToAdminStartsClose_Terminal_AssignFirstSite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_terminal, StoreKind::memory,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "transmit 35=3 34=2 [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2"},
            .retained = {"35=A 34=1", "35=3 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_AssignFirstSite_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {},
            .retained = {"35=A 34=1", "35=3 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_AssignFirstSite_FileStore) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_terminal, StoreKind::file,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "transmit 35=3 34=2 [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2"},
            .retained = {"35=A 34=1", "35=3 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_AssignFirstSite_FileStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "on_inbound_frame completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {},
            .retained = {"35=A 34=1", "35=3 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_AssignFirstSite_NoStore) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_terminal, StoreKind::none,
        TransportKind::sync_capture,
        CloseExpect{
            .order = {"toAdmin(35=3) returns [Active open]", "transmit 35=3 34=2 [Active open]",
                      "on_inbound_frame completes [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = {},
            .started = {},
            .wire = {"35=3 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminStartsClose_Terminal_AssignFirstSite_NoStore_LiveWrite) {
    run_outbound_callback_starts(
        OuterSite::poss_dup_reject, Starts::close_terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseExpect{.order = {"toAdmin(35=3) returns [Active open]",
                              "transmit 35=3 34=2 [Active open]", "onLogout [Disconnected closing]",
                              "on_inbound_frame completes [Disconnected closing]",
                              "close completes [Disconnected closed_drained]"},
                    .callbacks = {},
                    .outer = {},
                    .started = {},
                    .wire = {"35=3 34=2"},
                    .retained = {},
                    .next_out = 3,
                    .next_in = 3,
                    .store_next_out = {},
                    .store_next_in = {},
                    .writes_after_close = 0,
                    .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsSend_NoStore) {
    run_outbound_callback_starts(OuterSite::app_send, Starts::send, StoreKind::none);
}

TEST(CallbackSend, ToAdminStartsSend_NoStore) {
    run_outbound_callback_starts(OuterSite::heartbeat_reply, Starts::send, StoreKind::none);
}

TEST(CallbackSend, ToAdminStartsSend_AssignFirstSite_NoStore) {
    run_outbound_callback_starts(OuterSite::poss_dup_reject, Starts::send, StoreKind::none);
}

TEST(CallbackSend, ToAppStartsSend_NoStore_LiveWrite) {
    run_outbound_callback_starts(OuterSite::app_send, Starts::send, StoreKind::none,
                                 TransportKind::live_suspending_write);
}

TEST(CallbackSend, ToAdminStartsSend_NoStore_LiveWrite) {
    run_outbound_callback_starts(OuterSite::heartbeat_reply, Starts::send, StoreKind::none,
                                 TransportKind::live_suspending_write);
}

TEST(CallbackSend, ToAdminStartsSend_AssignFirstSite_NoStore_LiveWrite) {
    run_outbound_callback_starts(OuterSite::poss_dup_reject, Starts::send, StoreKind::none,
                                 TransportKind::live_suspending_write);
}

namespace {

// How a cell classifies a send it started in a callback (the file header, "Witness").
std::string classify(const std::optional<bool>& to_app_ran_inside_the_callback) {
    return !to_app_ran_inside_the_callback.has_value() ? "never ran"
           : *to_app_ran_inside_the_callback           ? "nested"
                                                       : "deferred";
}

}  // namespace

namespace {

// ── A close and a send started in one callback ───────────────────────────────
//
// `.specify/563-564-callback-send.md` §3, "New cells", the row of a close() and a
// send started in one callback; §2.1 "The deferred path", step 5.

// A recorded result as an arm's values spell it.
std::string result_of(const SendOutcome& o) {
    if (!o.done) return "not completed";
    if (o.threw) return "threw";
    if (!o.err) return "ok";
    if (*o.err == error::session_invalid_state_for_send) return "session_invalid_state_for_send";
    if (*o.err == error::session_already_closed) return "session_already_closed";
    if (*o.err == error::dispatch_aborted) return "dispatch_aborted";
    return "error " + std::to_string(static_cast<int>(*o.err));
}

std::string in_quotes(const std::string& s) { return "\"" + s + "\""; }

// Which callback starts what, and in which order.
enum class OneCallbackStarts {
    // The toApp of a send starts a close and then a second send.
    terminal_close_then_send_in_to_app,
    graceful_close_then_send_in_to_app,
    // The toApp of a send starts a second send and then a close.
    send_then_graceful_close_in_to_app,
    // The test body starts a graceful close; the toAdmin of its Logout starts a send.
    send_in_the_closes_logout_to_admin,
    // The fromApp of an inbound 35=D starts a close and then a send.
    graceful_close_then_send_in_from_app,
    // The peer sends a Logout and no close() is started; the toAdmin of the Logout
    // that confirms it starts a send.
    send_in_the_confirming_logouts_to_admin,
};

// What one arm must show. Each TEST spells its arm's values out.
struct CloseAndSendExpect {
    // The session's FSM state and lifecycle inside the starting callback, after its
    // last co_spawn returned.
    std::string inside;
    // Where the toApp of the send the callback started ran (the file header,
    // "Witness").
    std::string send_to_app;
    // As CloseExpect::order, with the completion of each operation of the shape.
    std::vector<std::string> order;
    // As CloseExpect::callbacks.
    std::vector<std::string> callbacks;
    // The result of the operation that called the callback, of the close and of the
    // send the callback started, as result_of() spells it. "not started": the shape
    // has no such operation.
    std::string outer;
    std::string close;
    std::string send;
    // As the members of CloseExpect with these names.
    std::vector<std::string> wire;
    std::vector<std::string> retained;
    seqnum_t next_out = 0;
    seqnum_t next_in = 0;
    std::optional<seqnum_t> store_next_out;
    std::optional<seqnum_t> store_next_in;
    // The session's FSM state and lifecycle once every operation completed.
    std::string at_the_end;
    std::size_t writes_after_close = 0;
    std::size_t writes_refused = 0;
};

// One callback of an Active session starts a send, and in every shape but the last a
// close is started too: by the same callback, or by the test body where the callback
// is that close's own Logout's toAdmin. The send's payload is "B41-INNER"; where a
// send calls the callback, its payload is "B41-OUTER".
void run_close_and_send_in_one_callback(OneCallbackStarts shape, StoreKind store,
                                        TransportKind transport, const CloseAndSendExpect& expect) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const auto outer_payload = bytes_of(
        "35=D\x01"
        "11=B41-OUTER\x01"
        "55=AAA\x01");
    const auto inner_payload = bytes_of(
        "35=D\x01"
        "11=B41-INNER\x01"
        "55=BBB\x01");
    const bool in_to_app = shape == OneCallbackStarts::terminal_close_then_send_in_to_app ||
                           shape == OneCallbackStarts::graceful_close_then_send_in_to_app ||
                           shape == OneCallbackStarts::send_then_graceful_close_in_to_app;
    const bool in_logout_to_admin =
        shape == OneCallbackStarts::send_in_the_closes_logout_to_admin ||
        shape == OneCallbackStarts::send_in_the_confirming_logouts_to_admin;
    const bool in_from_app = shape == OneCallbackStarts::graceful_close_then_send_in_from_app;
    const bool has_close = shape != OneCallbackStarts::send_in_the_confirming_logouts_to_admin;
    const bool close_started_by_the_body =
        shape == OneCallbackStarts::send_in_the_closes_logout_to_admin;
    const bool has_outer = !close_started_by_the_body;
    const close_mode mode = shape == OneCallbackStarts::terminal_close_then_send_in_to_app
                                ? close_mode::terminal
                                : close_mode::graceful;
    const char* outer_name = in_to_app ? "send(OUTER)" : "on_inbound_frame";

    // cell.outer is the operation that called the callback, cell.started the send
    // the callback started.
    CloseCell cell{f, sess};
    SendOutcome close_op;
    bool starting = false;  // the starting callback is on the stack
    bool close_completed_inside = false;
    std::optional<bool> send_to_app_inside;

    const auto start_close = [&] {
        asio::co_spawn(ex, sess.close(mode), cell.completes(close_op, "close"));
    };
    const auto start_send = [&] {
        asio::co_spawn(ex, sess.send(inner_payload), cell.completes(cell.started, "send(INNER)"));
    };
    const auto starting_callback = [&](const char* name) {
        starting = true;
        // A build with asserts may stop inside a co_spawn; these two lines bracket
        // them.
        std::fprintf(stderr, "[ B41 ] %s: starting the operations\n", name);
        switch (shape) {
            case OneCallbackStarts::terminal_close_then_send_in_to_app:
            case OneCallbackStarts::graceful_close_then_send_in_to_app:
            case OneCallbackStarts::graceful_close_then_send_in_from_app:
                start_close();
                start_send();
                break;
            case OneCallbackStarts::send_then_graceful_close_in_to_app:
                start_send();
                start_close();
                break;
            case OneCallbackStarts::send_in_the_closes_logout_to_admin:
            case OneCallbackStarts::send_in_the_confirming_logouts_to_admin:
                start_send();
                break;
        }
        std::fprintf(stderr, "[ B41 ] %s: the co_spawns returned\n", name);
        cell.read_inside_callback();
        close_completed_inside = close_op.done;
        cell.note(std::string(name) + " returns");
        starting = false;
    };

    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    f.app->on_to_app = [&] {
        if (in_to_app && !cell.inside.read && !starting) {
            starting_callback("toApp");
            return;
        }
        if (!send_to_app_inside.has_value()) send_to_app_inside = starting;
        cell.callback("toApp");
    };
    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (in_logout_to_admin && msg_type == "5" && !cell.inside.read && !starting) {
            starting_callback("toAdmin(35=5)");
            return;
        }
        cell.to_admin(msg_type);
    };
    f.app->on_from_app = [&] {
        if (in_from_app && !cell.inside.read && !starting) starting_callback("fromApp");
    };

    const auto peer_order = peer_frame("D", 2,
                                       "11=PEER-1\x01"
                                       "54=1\x01"
                                       "55=AAPL\x01");
    const auto peer_logout = peer_frame("5", 2, "");
    if (in_to_app) {
        asio::co_spawn(ex, sess.send(outer_payload), cell.completes(cell.outer, "send(OUTER)"));
    } else if (close_started_by_the_body) {
        start_close();
    } else {
        const auto& inbound = in_from_app ? peer_order : peer_logout;
        asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                       cell.completes(cell.outer, "on_inbound_frame"));
    }

    const auto outer_done = [&] { return cell.outer.done || !has_outer; };
    const auto pending = [&] {
        return std::string(outer_done() ? "" : std::string(outer_name) + " had not completed; ") +
               (cell.started.done ? "" : "send(INNER) had not completed; ") +
               (close_op.done || !has_close ? "" : "the close had not completed; ");
    };
    // A graceful close waits for a peer Logout; everything the arm expects before
    // the end of that wait has happened once the other operations completed and
    // onLogout ran.
    bool completed = true;
    if (has_close && mode == close_mode::graceful) {
        completed = f.end_logout_wait_when(
            [&] { return outer_done() && cell.started.done && cell.on_logout_calls > 0; },
            [&] { return pending() + (cell.on_logout_calls > 0 ? "" : "onLogout had not run"); },
            std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
            "CallbackSend::close_and_send_logout_wait");
    }
    // Bounded: an operation that does not complete is reported below, not waited for.
    completed =
        completed &&
        fixpp::test_support::pump_until(
            f.ioc,
            [&] { return outer_done() && cell.started.done && (close_op.done || !has_close); },
            std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
            "CallbackSend::close_and_send");
    // Recorded before the drain, which completes what it cancels.
    const std::string pending_at_the_bound = completed ? std::string{} : pending();
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::close_and_send");
    }

    const bool has_store = session_test_access::store(sess) != nullptr;
    std::vector<std::string> retained_full;
    const std::vector<std::string> retained = retained_frames(f, sess, retained_full);
    const std::optional<seqnum_t> store_out = store_next(f, sess, direction_t::outbound);
    const std::optional<seqnum_t> store_in = store_next(f, sess, direction_t::inbound);
    // Read after the store reads, which run the io_context.
    const std::vector<std::string> wire_full(
        f.wire.begin() + static_cast<std::ptrdiff_t>(cell.wire_before), f.wire.end());
    std::vector<std::string> wire;
    // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
    for (const auto& frame : wire_full) wire.push_back(brief(frame));
    const std::string at_the_end = where_is(sess);
    const std::string inside = cell.inside.state + " " + cell.inside.lifecycle;
    const std::string send_to_app = classify(send_to_app_inside);
    const std::string outer_result = has_outer ? result_of(cell.outer) : "not started";
    const std::string close_result = has_close ? result_of(close_op) : "not started";
    const std::string send_result = result_of(cell.started);
    const seqnum_t manager_out = next_out(sess);
    const seqnum_t manager_in = next_in(sess);

    // The arm's values as read, in the form its TEST spells them out.
    const std::string reading =
        "CloseAndSendExpect{.inside = " + in_quotes(inside) +
        ", .send_to_app = " + in_quotes(send_to_app) + ", .order = " + literal(cell.order) +
        ", .callbacks = " + literal(cell.callbacks) + ", .outer = " + in_quotes(outer_result) +
        ", .close = " + in_quotes(close_result) + ", .send = " + in_quotes(send_result) +
        ", .wire = " + literal(wire) + ", .retained = " + literal(retained) +
        ", .next_out = " + std::to_string(manager_out) +
        ", .next_in = " + std::to_string(manager_in) + ", .store_next_out = " + literal(store_out) +
        ", .store_next_in = " + literal(store_in) + ", .at_the_end = " + in_quotes(at_the_end) +
        ", .writes_after_close = " + std::to_string(f.live_writes.begun_after_close) +
        ", .writes_refused = " + std::to_string(f.live_writes.refused_after_close) +
        "}; inside the starting callback: onLogout calls " +
        std::to_string(cell.inside.on_logout_calls) + ", Logout toAdmin calls " +
        std::to_string(cell.inside.logout_to_admin_calls) + ", the close " +
        (close_completed_inside ? "completed" : "not completed") + ", send(INNER) " +
        (cell.inside.started_completed ? "completed" : "not completed") +
        "; transport close() calls " + std::to_string(f.live_writes.closes) +
        ", live writes started " + std::to_string(f.live_writes.started) + ", most in flight " +
        std::to_string(f.live_writes.max_in_flight) + ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    // A session that no close() has closed is closed here, so that nothing of it is
    // left running when the cell returns.
    if (session_test_access::lifecycle_value(sess) == 1) {
        (void)f.run(sess.close(close_mode::terminal), "CallbackSend::close_and_send_teardown");
    }

    ASSERT_TRUE(cell.inside.read) << "PATH NOT DRIVEN: the starting callback never ran. "
                                  << reading;
    EXPECT_TRUE(completed) << "every operation must complete within the pump's bound; "
                           << pending_at_the_bound << reading;

    // Inside the starting callback, after its last co_spawn returned.
    if (has_close && !close_started_by_the_body) {
        EXPECT_EQ(cell.inside.lifecycle, "open")
            << "inside the callback after the start, the lifecycle is open. " << reading;
    }
    EXPECT_EQ(inside, expect.inside)
        << "inside the callback after the starts, the session's FSM state and lifecycle. "
        << reading;
    EXPECT_EQ(cell.inside.on_logout_calls, 0)
        << "inside the callback after the starts, onLogout has not run. " << reading;
    EXPECT_EQ(cell.inside.logout_to_admin_calls, 0)
        << "inside the callback after the starts, no Logout's toAdmin other than the starting "
           "callback has run. "
        << reading;
    EXPECT_FALSE(close_completed_inside)
        << "inside the callback after the starts, the close has not completed. " << reading;
    EXPECT_FALSE(cell.inside.started_completed)
        << "inside the callback after the starts, the send has not completed. " << reading;

    // What the shape's row states, spelled out apart from the arm's values.
    const bool inner_on_the_wire = frame_with_id(wire_full, "B41-INNER") != nullptr;
    const bool inner_retained = std::ranges::any_of(
        retained_full, [](const std::string& frame) { return field(frame, 11) == "B41-INNER"; });
    const bool logout_is_last = !wire_full.empty() && field(wire_full.back(), 35) == "5";
    switch (shape) {
        case OneCallbackStarts::terminal_close_then_send_in_to_app:
        case OneCallbackStarts::graceful_close_then_send_in_to_app:
        case OneCallbackStarts::send_in_the_closes_logout_to_admin: {
            EXPECT_EQ(cell.started.err, std::optional<error>{error::session_invalid_state_for_send})
                << "the send returns session_invalid_state_for_send. " << reading;
            EXPECT_FALSE(cell.started.threw) << reading;
            EXPECT_EQ(send_to_app, "never ran")
                << "a send that is refused does not reach its toApp. " << reading;
            EXPECT_FALSE(inner_on_the_wire)
                << "nothing of the refused send is transmitted. " << reading;
            EXPECT_FALSE(inner_retained) << "nothing of the refused send is stored. " << reading;
            // The numbers taken after the Logon's: the outer frame's where a send
            // calls the callback, and the Logout's where the close is graceful.
            const seqnum_t taken = (in_to_app ? 1U : 0U) + (mode == close_mode::graceful ? 1U : 0U);
            EXPECT_EQ(manager_out, 2U + taken)
                << "the refused send consumes no MsgSeqNum. " << reading;
            if (mode == close_mode::graceful) {
                EXPECT_TRUE(logout_is_last)
                    << "the Logout is the last frame transmitted. " << reading;
            }
            break;
        }
        case OneCallbackStarts::send_then_graceful_close_in_to_app: {
            EXPECT_EQ(send_to_app, "deferred") << reading;
            ASSERT_GE(wire_full.size(), 2U)
                << "the send's frame and then the Logout must be transmitted. " << reading;
            const std::string& before_last = wire_full[wire_full.size() - 2];
            EXPECT_EQ(field(before_last, 11), "B41-INNER")
                << "the send's frame is transmitted, then the Logout. " << reading;
            EXPECT_TRUE(logout_is_last)
                << "the send's frame is transmitted, then the Logout. " << reading;
            EXPECT_EQ(seq_of(wire_full.back()), seq_of(before_last) + 1)
                << "the Logout carries the MsgSeqNum after the send's. " << reading;
            break;
        }
        case OneCallbackStarts::graceful_close_then_send_in_from_app: {
            EXPECT_EQ(send_to_app, "nested")
                << "the close has not begun, so the send is admitted and its toApp runs inside "
                   "fromApp. "
                << reading;
            EXPECT_EQ(send_result, "ok") << "the send is admitted. " << reading;
            ASSERT_EQ(wire_full.size(), 2U)
                << "the send's frame and then the Logout must be transmitted. " << reading;
            EXPECT_EQ(field(wire_full[0], 11), "B41-INNER")
                << "the send's frame is transmitted, then the Logout. " << reading;
            EXPECT_TRUE(logout_is_last)
                << "the send's frame is transmitted, then the Logout. " << reading;
            EXPECT_EQ(close_result, "ok") << reading;
            break;
        }
        case OneCallbackStarts::send_in_the_confirming_logouts_to_admin: {
            if (!has_store) {
                // With no store the confirming Logout's emit does not suspend, so
                // the session has left Active before the send resumes.
                EXPECT_EQ(cell.started.err,
                          std::optional<error>{error::session_invalid_state_for_send})
                    << "with no store the send resumes on a session that left Active and is "
                       "refused. "
                    << reading;
                EXPECT_EQ(send_to_app, "never ran") << reading;
                EXPECT_FALSE(inner_on_the_wire)
                    << "nothing of the refused send is transmitted. " << reading;
                break;
            }
            EXPECT_EQ(send_to_app, "deferred") << reading;
            ASSERT_EQ(wire_full.size(), 2U)
                << "the confirming Logout and then the send's frame must be transmitted. "
                << reading;
            EXPECT_EQ(field(wire_full[0], 35), "5")
                << "the confirming Logout is transmitted first. " << reading;
            EXPECT_EQ(field(wire_full[1], 11), "B41-INNER")
                << "the send's frame is transmitted after the confirming Logout. " << reading;
            EXPECT_EQ(seq_of(wire_full[1]), seq_of(wire_full[0]) + 1)
                << "the send's frame carries the MsgSeqNum after the confirming Logout's. "
                << reading;
            break;
        }
    }

    // The arm's values.
    EXPECT_EQ(send_to_app, expect.send_to_app)
        << "where the toApp of the send the callback started ran. " << reading;
    EXPECT_EQ(cell.order, expect.order)
        << "the order of the starting callback's return, onLogout, each transmit and the "
           "completions, each with the session's FSM state and lifecycle at that moment. "
        << reading;
    EXPECT_EQ(cell.callbacks, expect.callbacks)
        << "the other toApp and toAdmin calls, each with the session's FSM state and lifecycle "
           "on its entry. "
        << reading;
    EXPECT_EQ(outer_result, expect.outer)
        << "the result of the operation that called the callback. " << reading;
    EXPECT_EQ(close_result, expect.close) << "the result of the close. " << reading;
    EXPECT_EQ(send_result, expect.send)
        << "the result of the send the callback started. " << reading;
    EXPECT_EQ(wire, expect.wire) << "what was transmitted. " << reading;
    EXPECT_EQ(has_store, !expect.retained.empty())
        << "the arm's values and the session disagree on whether there is a store. " << reading;
    EXPECT_EQ(retained, expect.retained) << "what the store retained. " << reading;
    if (has_store) {
        // A transmitted frame is retained byte for byte under its own number.
        for (const auto& frame : wire_full) {
            const seqnum_t seq = seq_of(frame);
            ASSERT_TRUE(seq >= 1 && seq <= kRetainedThrough)
                << "a transmitted frame carries a MsgSeqNum outside the range read. " << reading;
            EXPECT_EQ(retained_full[seq - 1], frame)
                << "the store's frame under outbound 34=" << seq
                << " is not the frame transmitted with that number. " << reading;
        }
        EXPECT_EQ(store_out, expect.store_next_out)
            << "the store's next outbound number. " << reading;
    }
    EXPECT_EQ(manager_out, expect.next_out) << "the manager's next outbound number. " << reading;
    EXPECT_EQ(manager_in, expect.next_in) << "the manager's next inbound number. " << reading;
    if (expect.store_next_in) {
        EXPECT_EQ(store_in, expect.store_next_in) << "the store's next inbound number. " << reading;
    }
    EXPECT_EQ(at_the_end, expect.at_the_end)
        << "the session's FSM state and lifecycle once every operation completed. " << reading;
    EXPECT_EQ(f.live_writes.begun_after_close, expect.writes_after_close)
        << "the writes begun after the transport's close(). " << reading;
    EXPECT_EQ(f.live_writes.refused_after_close, expect.writes_refused)
        << "the writes the transport refused. " << reading;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << reading;
}

}  // namespace

// toApp starts a terminal close and then a send. The close runs first and the send is
// refused.
TEST(CallbackSend, ToAppStartsCloseThenSend_Terminal) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::terminal_close_then_send_in_to_app, StoreKind::memory,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "send(INNER) completes [Disconnected closed_drained]",
                      "transmit 35=D 34=2 11=B41-OUTER [Disconnected closed_drained]",
                      "send(OUTER) completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Terminal_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::terminal_close_then_send_in_to_app, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "send(INNER) completes [Disconnected closed_drained]",
                      "send(OUTER) completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "dispatch_aborted",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Terminal_FileStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::terminal_close_then_send_in_to_app, StoreKind::file,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "send(INNER) completes [Disconnected closed_drained]",
                      "transmit 35=D 34=2 11=B41-OUTER [Disconnected closed_drained]",
                      "send(OUTER) completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Terminal_FileStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::terminal_close_then_send_in_to_app, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "send(INNER) completes [Disconnected closed_drained]",
                      "send(OUTER) completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "dispatch_aborted",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Terminal_NoStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::terminal_close_then_send_in_to_app, StoreKind::none,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "send(OUTER) completes [Active open]", "onLogout [Disconnected closing]",
                      "close completes [Disconnected closed_drained]",
                      "send(INNER) completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Terminal_NoStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::terminal_close_then_send_in_to_app, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{.inside = "Active open",
                           .send_to_app = "never ran",
                           .order = {"toApp returns [Active open]",
                                     "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                                     "onLogout [Disconnected closing]",
                                     "send(INNER) completes [Disconnected closing]",
                                     "send(OUTER) completes [Disconnected closing]",
                                     "close completes [Disconnected closed_drained]"},
                           .callbacks = {},
                           .outer = "ok",
                           .close = "ok",
                           .send = "session_invalid_state_for_send",
                           .wire = {"35=D 34=2 11=B41-OUTER"},
                           .retained = {},
                           .next_out = 3,
                           .next_in = 2,
                           .store_next_out = {},
                           .store_next_in = {},
                           .at_the_end = "Disconnected closed_drained",
                           .writes_after_close = 0,
                           .writes_refused = 0});
}

// toApp starts a graceful close and then a send. The send resumes while the close is
// emitting its Logout, with the session still Active, and is refused because a close has
// begun.
TEST(CallbackSend, ToAppStartsCloseThenSend_Graceful) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_to_app, StoreKind::memory,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "send(INNER) completes [Active closing]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Graceful_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_to_app, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "send(INNER) completes [Active closing]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Graceful_FileStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_to_app, StoreKind::file,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "send(INNER) completes [Active closing]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Graceful_FileStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_to_app, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]", "send(INNER) completes [Active closing]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = 4,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Graceful_NoStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_to_app, StoreKind::none,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "send(OUTER) completes [Active open]", "transmit 35=5 34=3 [Active closing]",
                      "onLogout [LogoutSent closing]", "send(INNER) completes [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsCloseThenSend_Graceful_NoStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_to_app, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "send(INNER) completes [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// toApp starts a send and then a graceful close. The send resumes first and is admitted.
TEST(CallbackSend, ToAppStartsSendThenClose_Graceful) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_then_graceful_close_in_to_app, StoreKind::memory,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=D 34=3 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=4 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER", "35=5 34=4"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER",
                         "35=5 34=4"},
            .next_out = 5,
            .next_in = 2,
            .store_next_out = 5,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsSendThenClose_Graceful_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_then_graceful_close_in_to_app, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=D 34=3 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=4 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER", "35=5 34=4"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER",
                         "35=5 34=4"},
            .next_out = 5,
            .next_in = 2,
            .store_next_out = 5,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsSendThenClose_Graceful_FileStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_then_graceful_close_in_to_app, StoreKind::file,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=D 34=3 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=4 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER", "35=5 34=4"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER",
                         "35=5 34=4"},
            .next_out = 5,
            .next_in = 2,
            .store_next_out = 5,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsSendThenClose_Graceful_FileStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_then_graceful_close_in_to_app, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active closing]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=D 34=3 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=4 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER", "35=5 34=4"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER",
                         "35=5 34=4"},
            .next_out = 5,
            .next_in = 2,
            .store_next_out = 5,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsSendThenClose_Graceful_NoStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_then_graceful_close_in_to_app, StoreKind::none,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "send(OUTER) completes [Active open]",
                      "transmit 35=D 34=3 11=B41-INNER [Active open]",
                      "send(INNER) completes [Active open]", "transmit 35=5 34=4 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER", "35=5 34=4"},
            .retained = {},
            .next_out = 5,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAppStartsSendThenClose_Graceful_NoStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_then_graceful_close_in_to_app, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-OUTER [Active open]",
                      "send(OUTER) completes [Active closing]",
                      "transmit 35=D 34=3 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=4 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-OUTER", "35=D 34=3 11=B41-INNER", "35=5 34=4"},
            .retained = {},
            .next_out = 5,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// The toAdmin of a graceful close's Logout starts a send. The close has begun, so the
// send is refused.
TEST(CallbackSend, LogoutToAdminStartsSend_GracefulClose) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_closes_logout_to_admin, StoreKind::memory,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active closing",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "not started",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_GracefulClose_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_closes_logout_to_admin, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active closing",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "not started",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_GracefulClose_FileStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_closes_logout_to_admin, StoreKind::file,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active closing",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "not started",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_GracefulClose_FileStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_closes_logout_to_admin, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active closing",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "not started",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_GracefulClose_NoStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_closes_logout_to_admin, StoreKind::none,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active closing",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "send(INNER) completes [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "not started",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_GracefulClose_NoStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_closes_logout_to_admin, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active closing",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]",
                      "send(INNER) completes [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .outer = "not started",
            .close = "ok",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// fromApp starts a graceful close and then a send. The close waits for fromApp to return
// and has not begun; a send started in fromApp does not wait, so it is admitted and its
// toApp runs inside fromApp.
TEST(CallbackSend, FromAppStartsCloseThenSend_Graceful) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_from_app, StoreKind::memory,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "nested",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=D 34=2 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-INNER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-INNER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseThenSend_Graceful_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_from_app, StoreKind::memory,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "nested",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=D 34=2 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-INNER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-INNER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseThenSend_Graceful_FileStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_from_app, StoreKind::file,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "nested",
            .order = {"fromApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-INNER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-INNER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseThenSend_Graceful_FileStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_from_app, StoreKind::file,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "nested",
            .order = {"fromApp returns [Active open]",
                      "transmit 35=D 34=2 11=B41-INNER [Active closing]",
                      "send(INNER) completes [Active closing]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-INNER", "35=5 34=3"},
            .retained = {"35=A 34=1", "35=D 34=2 11=B41-INNER", "35=5 34=3", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseThenSend_Graceful_NoStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_from_app, StoreKind::none,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "nested",
            .order = {"transmit 35=D 34=2 11=B41-INNER [Active open]",
                      "fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "send(INNER) completes [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-INNER", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseThenSend_Graceful_NoStore_LiveWrite) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::graceful_close_then_send_in_from_app, StoreKind::none,
        TransportKind::live_suspending_write,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "nested",
            .order = {"transmit 35=D 34=2 11=B41-INNER [Active open]",
                      "fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "send(INNER) completes [Active closing]",
                      "transmit 35=5 34=3 [Active closing]", "onLogout [LogoutSent closing]",
                      "close completes [Disconnected closed_drained]"},
            .callbacks = {"toApp [Active open]", "toAdmin 35=5 [Active closing]"},
            .outer = "ok",
            .close = "ok",
            .send = "ok",
            .wire = {"35=D 34=2 11=B41-INNER", "35=5 34=3"},
            .retained = {},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// The peer starts the logout; the toAdmin of the Logout that confirms it starts a send. No
// close() has begun, so nothing refuses the send, and its frame is numbered and transmitted
// after our Logout. The arms on a store pin a disclosed limitation
// (`.specify/563-564-callback-send.md` §7, the bullet on a logout the peer began), not a
// wanted behaviour. The arm with no store pins where that limitation stops: the Logout's
// emit does not suspend there, the session has left Active when the send resumes, and the
// send is refused. On the synchronous sink only: the row of this shape names it.
TEST(CallbackSend, LogoutToAdminStartsSend_PeerLogout) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_confirming_logouts_to_admin, StoreKind::memory,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toAdmin(35=5) returns [Active open]", "transmit 35=5 34=2 [Active open]",
                      "onLogout [Disconnected open]",
                      "on_inbound_frame completes [Disconnected open]",
                      "transmit 35=D 34=3 11=B41-INNER [Disconnected open]",
                      "send(INNER) completes [Disconnected open]"},
            .callbacks = {"toApp [Active open]"},
            .outer = "ok",
            .close = "not started",
            .send = "ok",
            .wire = {"35=5 34=2", "35=D 34=3 11=B41-INNER"},
            .retained = {"35=A 34=1", "35=5 34=2", "35=D 34=3 11=B41-INNER", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = {},
            .at_the_end = "Disconnected open",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_PeerLogout_FileStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_confirming_logouts_to_admin, StoreKind::file,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "deferred",
            .order = {"toAdmin(35=5) returns [Active open]", "transmit 35=5 34=2 [Active open]",
                      "transmit 35=D 34=3 11=B41-INNER [Active open]",
                      "send(INNER) completes [Active open]", "onLogout [Disconnected open]",
                      "on_inbound_frame completes [Disconnected open]"},
            .callbacks = {"toApp [Active open]"},
            .outer = "ok",
            .close = "not started",
            .send = "ok",
            .wire = {"35=5 34=2", "35=D 34=3 11=B41-INNER"},
            .retained = {"35=A 34=1", "35=5 34=2", "35=D 34=3 11=B41-INNER", "none"},
            .next_out = 4,
            .next_in = 3,
            .store_next_out = 4,
            .store_next_in = 3,
            .at_the_end = "Disconnected open",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, LogoutToAdminStartsSend_PeerLogout_NoStore) {
    run_close_and_send_in_one_callback(
        OneCallbackStarts::send_in_the_confirming_logouts_to_admin, StoreKind::none,
        TransportKind::sync_capture,
        CloseAndSendExpect{
            .inside = "Active open",
            .send_to_app = "never ran",
            .order = {"toAdmin(35=5) returns [Active open]", "transmit 35=5 34=2 [Active open]",
                      "onLogout [Disconnected open]",
                      "on_inbound_frame completes [Disconnected open]",
                      "send(INNER) completes [Disconnected open]"},
            .callbacks = {},
            .outer = "ok",
            .close = "not started",
            .send = "session_invalid_state_for_send",
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected open",
            .writes_after_close = 0,
            .writes_refused = 0});
}

namespace {

// ── A close that waits while another close runs first ────────────────────────
//
// `.specify/563-564-callback-send.md` §3, "New cells", the row of a close that waits
// while another close runs first; §2.1a "Where the wait sits".

// What one arm must show. Each TEST spells its arm's values out.
struct BehindAnotherCloseExpect {
    // In the order they happen, each followed by the session's FSM state and
    // lifecycle at that moment: fromApp's return, the start of the other close,
    // onLogout, every transmit, the completion of on_inbound_frame and the
    // completion of the close fromApp started. The completion of the other close is
    // not in it: co_spawn posts the completion handler of a coroutine that never
    // suspended, so where that handler runs says nothing about the close.
    std::vector<std::string> order;
    // As CloseExpect::callbacks.
    std::vector<std::string> callbacks;
    // The result of on_inbound_frame, of the close fromApp started and of the other
    // close, as result_of() spells it.
    std::string inbound;
    std::string callbacks_close;
    std::string other_close;
    // The session's FSM state and lifecycle when the co_spawn that started the other
    // close returned.
    std::string after_the_other_start;
    // How often the hook installed with set_close_flush_hook ran.
    int flush_hook_calls = 0;
    // As the members of CloseExpect with these names.
    std::vector<std::string> wire;
    std::vector<std::string> retained;
    seqnum_t next_out = 0;
    seqnum_t next_in = 0;
    std::optional<seqnum_t> store_next_out;
    std::optional<seqnum_t> store_next_in;
    std::size_t writes_after_close = 0;
    std::size_t writes_refused = 0;
};

// fromApp for an inbound 35=D starts a close. Before the io_context runs, the cell
// has queued the frame's delivery and, behind it, a handler that starts another close
// of the same mode. That handler is then ahead of the post the callback's close
// makes, so the other close runs first.
void run_close_behind_another_close(close_mode mode, StoreKind store, TransportKind transport,
                                    const BehindAnotherCloseExpect& expect) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    int flush_hook_calls = 0;
    sess.set_close_flush_hook([&] {
        ++flush_hook_calls;
        return expected_t<void>{};
    });

    // cell.outer is on_inbound_frame, cell.started the close fromApp starts.
    CloseCell cell{f, sess};
    SendOutcome other_close;
    bool other_started = false;
    bool other_started_before_the_callback = false;
    bool callbacks_close_done_at_the_other_start = false;
    std::string after_the_other_start = "not recorded";

    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    f.app->on_to_app = [&] { cell.callback("toApp"); };
    f.app->on_to_admin = [&](std::string_view msg_type) { cell.to_admin(msg_type); };
    f.app->on_from_app = [&] {
        if (cell.inside.read) return;
        other_started_before_the_callback = other_started;
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] fromApp: starting Session::close\n");
        asio::co_spawn(ex, sess.close(mode), cell.completes(cell.started, "the callback's close"));
        std::fprintf(stderr, "[ B41 ] fromApp: co_spawn returned\n");
        cell.read_inside_callback();
        cell.note("fromApp returns");
    };

    const auto inbound = peer_frame("D", 2,
                                    "11=PEER-1\x01"
                                    "54=1\x01"
                                    "55=AAPL\x01");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                   cell.completes(cell.outer, "on_inbound_frame"));
    asio::post(ex, [&] {
        other_started = true;
        callbacks_close_done_at_the_other_start = cell.started.done;
        cell.note("the other close starts");
        asio::co_spawn(ex, sess.close(mode), record_into(other_close));
        after_the_other_start = where_is(sess);
    });

    const auto pending = [&] {
        return std::string(cell.outer.done ? "" : "on_inbound_frame had not completed; ") +
               (other_started ? "" : "the other close had not started; ") +
               (other_close.done ? "" : "the other close had not completed; ") +
               (cell.started.done ? "" : "the callback's close had not completed; ");
    };
    // A graceful close waits for a peer Logout; everything the arm expects before
    // the end of that wait has happened once on_inbound_frame completed and
    // onLogout ran.
    bool completed = true;
    if (mode == close_mode::graceful) {
        completed = f.end_logout_wait_when(
            [&] { return cell.outer.done && other_started && cell.on_logout_calls > 0; },
            [&] { return pending() + (cell.on_logout_calls > 0 ? "" : "onLogout had not run"); },
            std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
            "CallbackSend::behind_another_close_logout_wait");
    }
    // Bounded: an operation that does not complete is reported below, not waited for.
    completed = completed && fixpp::test_support::pump_until(
                                 f.ioc,
                                 [&] {
                                     return cell.outer.done && other_close.done &&
                                            (cell.started.done || !cell.inside.read);
                                 },
                                 std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
                                 "CallbackSend::behind_another_close");
    // Recorded before the drain, which completes what it cancels.
    const std::string pending_at_the_bound = completed ? std::string{} : pending();
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::behind_another_close");
    }

    const bool has_store = session_test_access::store(sess) != nullptr;
    std::vector<std::string> retained_full;
    const std::vector<std::string> retained = retained_frames(f, sess, retained_full);
    const std::optional<seqnum_t> store_out = store_next(f, sess, direction_t::outbound);
    const std::optional<seqnum_t> store_in = store_next(f, sess, direction_t::inbound);
    // Read after the store reads, which run the io_context.
    const std::vector<std::string> wire_full(
        f.wire.begin() + static_cast<std::ptrdiff_t>(cell.wire_before), f.wire.end());
    std::vector<std::string> wire;
    // NOLINTNEXTLINE(performance-inefficient-vector-operation): test bookkeeping, built once
    for (const auto& frame : wire_full) wire.push_back(brief(frame));
    const std::string at_the_end = where_is(sess);
    const std::string inbound_result = result_of(cell.outer);
    const std::string callbacks_close_result = result_of(cell.started);
    const std::string other_close_result = result_of(other_close);
    const seqnum_t manager_out = next_out(sess);
    const seqnum_t manager_in = next_in(sess);

    // The arm's values as read, in the form its TEST spells them out.
    const std::string reading =
        "BehindAnotherCloseExpect{.order = " + literal(cell.order) +
        ", .callbacks = " + literal(cell.callbacks) + ", .inbound = " + in_quotes(inbound_result) +
        ", .callbacks_close = " + in_quotes(callbacks_close_result) +
        ", .other_close = " + in_quotes(other_close_result) +
        ", .after_the_other_start = " + in_quotes(after_the_other_start) +
        ", .flush_hook_calls = " + std::to_string(flush_hook_calls) + ", .wire = " + literal(wire) +
        ", .retained = " + literal(retained) + ", .next_out = " + std::to_string(manager_out) +
        ", .next_in = " + std::to_string(manager_in) + ", .store_next_out = " + literal(store_out) +
        ", .store_next_in = " + literal(store_in) +
        ", .writes_after_close = " + std::to_string(f.live_writes.begun_after_close) +
        ", .writes_refused = " + std::to_string(f.live_writes.refused_after_close) +
        "}; inside fromApp after the start: " + cell.inside.state + " " + cell.inside.lifecycle +
        ", onLogout calls " + std::to_string(cell.inside.on_logout_calls) +
        ", Logout toAdmin calls " + std::to_string(cell.inside.logout_to_admin_calls) +
        ", the callback's close " +
        (cell.inside.started_completed ? "completed" : "not completed") + "; onLogout calls " +
        std::to_string(cell.on_logout_calls) + "; at the end: " + at_the_end +
        "; transport close() calls " + std::to_string(f.live_writes.closes) +
        ", live writes started " + std::to_string(f.live_writes.started) + ", most in flight " +
        std::to_string(f.live_writes.max_in_flight) +
        ". Close mode of both closes: " + (mode == close_mode::terminal ? "terminal" : "graceful") +
        ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    // A session that no close() has closed is closed here, so that nothing of it is
    // left running when the cell returns.
    if (session_test_access::lifecycle_value(sess) == 1) {
        (void)f.run(sess.close(close_mode::terminal),
                    "CallbackSend::behind_another_close_teardown");
    }

    ASSERT_TRUE(cell.inside.read) << "PATH NOT DRIVEN: fromApp did not start its close. "
                                  << reading;
    ASSERT_FALSE(other_started_before_the_callback)
        << "PATH NOT DRIVEN: the other close had started before fromApp started its own. "
        << reading;
    ASSERT_TRUE(other_started) << "PATH NOT DRIVEN: the other close never started. " << reading;
    ASSERT_FALSE(callbacks_close_done_at_the_other_start)
        << "PATH NOT DRIVEN: the callback's close had completed before the other close started. "
        << reading;
    EXPECT_TRUE(completed) << "every operation must complete within the pump's bound; "
                           << pending_at_the_bound << reading;

    EXPECT_EQ(cell.inside.lifecycle, "open")
        << "inside the callback after the start, the lifecycle is open. " << reading;
    EXPECT_EQ(cell.inside.state, "Active")
        << "inside the callback after the start, the session is Active. " << reading;
    EXPECT_EQ(cell.inside.on_logout_calls, 0)
        << "inside the callback after the start, onLogout has not run. " << reading;
    EXPECT_EQ(cell.inside.logout_to_admin_calls, 0)
        << "inside the callback after the start, the Logout's toAdmin has not run. " << reading;
    EXPECT_FALSE(cell.inside.started_completed)
        << "inside the callback after the start, the close it started has not completed. "
        << reading;

    // What the row states, spelled out apart from the arm's values.
    EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;
    EXPECT_EQ(at_the_end, "Disconnected closed_drained")
        << "the lifecycle ends closed_drained. " << reading;
    EXPECT_FALSE(cell.started.threw) << reading;
    EXPECT_FALSE(other_close.threw) << reading;
    if (mode == close_mode::terminal) {
        ASSERT_EQ(after_the_other_start, "Disconnected closed_drained")
            << "PATH NOT DRIVEN: the other close must run to its end without suspending, so that "
               "it is finished when the callback's close resumes. "
            << reading;
        EXPECT_EQ(cell.started.err, std::optional<error>{error::session_already_closed})
            << "the callback's close returns exactly session_already_closed. " << reading;
        EXPECT_EQ(other_close.err, std::optional<error>{}) << reading;
        EXPECT_EQ(flush_hook_calls, 0) << "a terminal close does not call the hook. " << reading;
    } else {
        EXPECT_EQ(callbacks_close_result, other_close_result)
            << "the callback's close joins the other one: both return the same result. " << reading;
        EXPECT_EQ(std::ranges::count_if(
                      wire_full, [](const std::string& frame) { return field(frame, 35) == "5"; }),
                  1)
            << "the Logout is transmitted once. " << reading;
        EXPECT_EQ(flush_hook_calls, 1) << "the hook has run once. " << reading;
    }

    // The arm's values.
    EXPECT_EQ(cell.order, expect.order)
        << "the order of fromApp's return, the start of the other close, onLogout, each "
           "transmit and the completions, each with the session's FSM state and lifecycle at "
           "that moment. "
        << reading;
    EXPECT_EQ(cell.callbacks, expect.callbacks)
        << "the toApp and toAdmin calls, each with the session's FSM state and lifecycle on its "
           "entry. "
        << reading;
    EXPECT_EQ(inbound_result, expect.inbound) << "the result of on_inbound_frame. " << reading;
    EXPECT_EQ(callbacks_close_result, expect.callbacks_close)
        << "the result of the close fromApp started. " << reading;
    EXPECT_EQ(other_close_result, expect.other_close)
        << "the result of the other close. " << reading;
    EXPECT_EQ(after_the_other_start, expect.after_the_other_start)
        << "the session when the co_spawn that started the other close returned. " << reading;
    EXPECT_EQ(flush_hook_calls, expect.flush_hook_calls)
        << "how often the close's flush hook ran. " << reading;
    EXPECT_EQ(wire, expect.wire) << "what was transmitted. " << reading;
    EXPECT_EQ(has_store, !expect.retained.empty())
        << "the arm's values and the session disagree on whether there is a store. " << reading;
    EXPECT_EQ(retained, expect.retained) << "what the store retained. " << reading;
    if (has_store) {
        EXPECT_EQ(store_out, expect.store_next_out)
            << "the store's next outbound number. " << reading;
    }
    EXPECT_EQ(manager_out, expect.next_out) << "the manager's next outbound number. " << reading;
    EXPECT_EQ(manager_in, expect.next_in) << "the manager's next inbound number. " << reading;
    if (expect.store_next_in) {
        EXPECT_EQ(store_in, expect.store_next_in) << "the store's next inbound number. " << reading;
    }
    EXPECT_EQ(f.live_writes.begun_after_close, expect.writes_after_close)
        << "the writes begun after the transport's close(). " << reading;
    EXPECT_EQ(f.live_writes.refused_after_close, expect.writes_refused)
        << "the writes the transport refused. " << reading;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << reading;
}

}  // namespace

// Both closes terminal. The other close runs to its end without suspending, so the
// callback's close resumes on a closed session. One arm: the row fixes the synchronous
// sink, and a store whose calls suspend would let the other close suspend.
TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Terminal) {
    run_close_behind_another_close(
        close_mode::terminal, StoreKind::memory, TransportKind::sync_capture,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "the other close starts [Active open]", "onLogout [Disconnected closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .inbound = "ok",
            .callbacks_close = "session_already_closed",
            .other_close = "ok",
            .after_the_other_start = "Disconnected closed_drained",
            .flush_hook_calls = 0,
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

// Both closes graceful. The callback's close resumes while the other one is in flight and
// joins it; the hook installed with set_close_flush_hook counts the runs of a graceful
// close's body.
TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Graceful) {
    run_close_behind_another_close(
        close_mode::graceful, StoreKind::memory, TransportKind::sync_capture,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "the other close starts [Active open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .inbound = "ok",
            .callbacks_close = "ok",
            .other_close = "ok",
            .after_the_other_start = "Active closing",
            .flush_hook_calls = 1,
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Graceful_LiveWrite) {
    run_close_behind_another_close(
        close_mode::graceful, StoreKind::memory, TransportKind::live_suspending_write,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "the other close starts [Active open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .inbound = "ok",
            .callbacks_close = "ok",
            .other_close = "ok",
            .after_the_other_start = "Active closing",
            .flush_hook_calls = 1,
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Graceful_FileStore) {
    run_close_behind_another_close(
        close_mode::graceful, StoreKind::file, TransportKind::sync_capture,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "the other close starts [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .inbound = "ok",
            .callbacks_close = "ok",
            .other_close = "ok",
            .after_the_other_start = "Active closing",
            .flush_hook_calls = 1,
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Graceful_FileStore_LiveWrite) {
    run_close_behind_another_close(
        close_mode::graceful, StoreKind::file, TransportKind::live_suspending_write,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "the other close starts [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .inbound = "ok",
            .callbacks_close = "ok",
            .other_close = "ok",
            .after_the_other_start = "Active closing",
            .flush_hook_calls = 1,
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = 3,
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Graceful_NoStore) {
    run_close_behind_another_close(
        close_mode::graceful, StoreKind::none, TransportKind::sync_capture,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "the other close starts [Active open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .inbound = "ok",
            .callbacks_close = "ok",
            .other_close = "ok",
            .after_the_other_start = "LogoutSent closing",
            .flush_hook_calls = 1,
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsCloseBehindAnotherClose_Graceful_NoStore_LiveWrite) {
    run_close_behind_another_close(
        close_mode::graceful, StoreKind::none, TransportKind::live_suspending_write,
        BehindAnotherCloseExpect{
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "the other close starts [Active open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .inbound = "ok",
            .callbacks_close = "ok",
            .other_close = "ok",
            .after_the_other_start = "Active closing",
            .flush_hook_calls = 1,
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .writes_after_close = 0,
            .writes_refused = 0});
}

namespace {

// ── A close that begins late, a close cancelled while it waits, and a close started
//    in a lifecycle callback ────────────────────────────────────────────────────
//
// `.specify/563-564-callback-send.md` §3, "New cells": the rows of two application
// frames delivered by one coroutine, of a close that waits and is cancelled
// meanwhile, and of a close started in each lifecycle callback.

// What one arm of these cells must show. Each TEST spells its arm's values out.
struct ArmValues {
    // The session's FSM state and lifecycle on entry to the callback that starts
    // the close.
    std::string entered;
    // In the order they happen, each followed by the session's FSM state and
    // lifecycle at that moment: what the arm's hooks note, among them every
    // transmit, onLogout and the completion of each operation.
    std::vector<std::string> order;
    // As CloseExpect::callbacks.
    std::vector<std::string> callbacks;
    // The result of each operation of the arm, as "<operation>: <result>", with the
    // result as result_of() spells it.
    std::vector<std::string> results;
    // As the members of CloseExpect with these names, with the frames counted from
    // the construction of the arm's CloseCell.
    std::vector<std::string> wire;
    std::vector<std::string> retained;
    seqnum_t next_out = 0;
    seqnum_t next_in = 0;
    std::optional<seqnum_t> store_next_out;
    // Read on a FileStore only.
    std::optional<seqnum_t> store_next_in;
    // The session's FSM state and lifecycle when the arm's operations had completed.
    std::string at_the_end;
    std::size_t writes_after_close = 0;
    std::size_t writes_refused = 0;
};

std::string literal_of(const ArmValues& v) {
    return "ArmValues{.entered = " + in_quotes(v.entered) + ", .order = " + literal(v.order) +
           ", .callbacks = " + literal(v.callbacks) + ", .results = " + literal(v.results) +
           ", .wire = " + literal(v.wire) + ", .retained = " + literal(v.retained) +
           ", .next_out = " + std::to_string(v.next_out) +
           ", .next_in = " + std::to_string(v.next_in) +
           ", .store_next_out = " + literal(v.store_next_out) +
           ", .store_next_in = " + literal(v.store_next_in) +
           ", .at_the_end = " + in_quotes(v.at_the_end) +
           ", .writes_after_close = " + std::to_string(v.writes_after_close) +
           ", .writes_refused = " + std::to_string(v.writes_refused) + "}";
}

// Reads an arm's values once its operations have completed, or the pump's bound
// was reached. `full_wire` receives the transmitted frames' bytes.
ArmValues read_arm_values(Fixture& f, Session& sess, const CloseCell& cell, std::string entered,
                          std::vector<std::string> results, std::vector<std::string>& full_wire) {
    ArmValues got;
    got.entered = std::move(entered);
    got.results = std::move(results);
    // Read before the store reads, which run the io_context.
    got.at_the_end = where_is(sess);
    std::vector<std::string> retained_full;
    got.retained = retained_frames(f, sess, retained_full);
    got.store_next_out = store_next(f, sess, direction_t::outbound);
    if (f.store_kind == StoreKind::file) {
        got.store_next_in = store_next(f, sess, direction_t::inbound);
    }
    full_wire.assign(f.wire.begin() + static_cast<std::ptrdiff_t>(cell.wire_before), f.wire.end());
    for (const auto& frame : full_wire) got.wire.push_back(brief(frame));
    got.order = cell.order;
    got.callbacks = cell.callbacks;
    got.next_out = next_out(sess);
    got.next_in = next_in(sess);
    got.writes_after_close = f.live_writes.begun_after_close;
    got.writes_refused = f.live_writes.refused_after_close;
    return got;
}

// What the starting callback read after its co_spawn returned.
std::string inside_text(const CloseCell& cell, const char* callback) {
    if (!cell.inside.read) return std::string("; ") + callback + " started no close";
    return std::string("; inside ") + callback + " after the start: " + cell.inside.state + " " +
           cell.inside.lifecycle + ", onLogout calls " +
           std::to_string(cell.inside.on_logout_calls) + ", Logout toAdmin calls " +
           std::to_string(cell.inside.logout_to_admin_calls) + ", the close " +
           (cell.inside.started_completed ? "completed" : "not completed");
}

std::string transport_text(const Fixture& f) {
    return "; transport close() calls " + std::to_string(f.live_writes.closes) +
           ", live writes started " + std::to_string(f.live_writes.started) + ", most in flight " +
           std::to_string(f.live_writes.max_in_flight);
}

void expect_arm_values(const ArmValues& got, const ArmValues& want, const std::string& reading) {
    EXPECT_EQ(got.entered, want.entered)
        << "the session on entry to the callback that starts the close. " << reading;
    EXPECT_EQ(got.order, want.order)
        << "the order of what the arm notes, each with the session's FSM state and lifecycle at "
           "that moment. "
        << reading;
    EXPECT_EQ(got.callbacks, want.callbacks)
        << "the other callbacks, each with the session's FSM state and lifecycle on its entry. "
        << reading;
    EXPECT_EQ(got.results, want.results) << "the result of each operation. " << reading;
    EXPECT_EQ(got.wire, want.wire) << "what was transmitted. " << reading;
    EXPECT_EQ(got.retained, want.retained) << "what the store retained. " << reading;
    EXPECT_EQ(got.next_out, want.next_out) << "the manager's next outbound number. " << reading;
    EXPECT_EQ(got.next_in, want.next_in) << "the manager's next inbound number. " << reading;
    EXPECT_EQ(got.store_next_out, want.store_next_out)
        << "the store's next outbound number. " << reading;
    EXPECT_EQ(got.store_next_in, want.store_next_in)
        << "the store's next inbound number. " << reading;
    EXPECT_EQ(got.at_the_end, want.at_the_end)
        << "the session when the arm's operations had completed. " << reading;
    EXPECT_EQ(got.writes_after_close, want.writes_after_close)
        << "the writes begun after the transport's close(). " << reading;
    EXPECT_EQ(got.writes_refused, want.writes_refused)
        << "the writes the transport refused. " << reading;
}

// The index in `order` of the first entry that begins with `what`, or the size
// of `order` when there is none.
std::size_t position_of(const std::vector<std::string>& order, std::string_view what) {
    const auto it = std::ranges::find_if(
        order, [&](const std::string& entry) { return entry.starts_with(what); });
    return static_cast<std::size_t>(it - order.begin());
}

// ── Two application frames delivered by one coroutine ────────────────────────
//
// §3, "New cells", the row of two application frames delivered by one coroutine;
// §2.1a "The close can be late".

// The first fromApp starts a terminal close. The two frames are delivered as the
// read pump delivers the complete frames of one read (Fixture::deliver_in_one_read).
void run_two_frames_in_one_read(StoreKind store, TransportKind transport, const ArmValues& expect) {
    Fixture f{ExecKind::io_context, store};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    CloseCell cell{f, sess};
    std::string entered = "not recorded";
    int from_app_calls = 0;
    bool delivery_threw = false;
    std::optional<std::vector<expected_t<void>>> delivered;

    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    f.app->on_to_app = [&] { cell.callback("toApp"); };
    f.app->on_to_admin = [&](std::string_view msg_type) { cell.to_admin(msg_type); };
    f.app->on_from_app = [&] {
        if (++from_app_calls != 1) {
            cell.note("fromApp for the second frame");
            return;
        }
        entered = where_is(sess);
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] fromApp: starting Session::close\n");
        asio::co_spawn(ex, sess.close(close_mode::terminal),
                       cell.completes(cell.started, "the close"));
        std::fprintf(stderr, "[ B41 ] fromApp: co_spawn returned\n");
        cell.read_inside_callback();
        cell.note("fromApp for the first frame returns");
    };

    std::vector<std::vector<std::byte>> frames;
    frames.push_back(peer_frame("D", 2,
                                "11=PEER-1\x01"
                                "54=1\x01"
                                "55=AAPL\x01"));
    frames.push_back(peer_frame("D", 3,
                                "11=PEER-2\x01"
                                "54=1\x01"
                                "55=AAPL\x01"));
    asio::co_spawn(ex, Fixture::deliver_in_one_read(sess, std::move(frames)),
                   [&](std::exception_ptr ep, std::vector<expected_t<void>> results) {
                       delivery_threw = static_cast<bool>(ep);
                       delivered = std::move(results);
                       cell.note("the delivery completes");
                   });

    const auto pending = [&] {
        return std::string(delivered.has_value() ? "" : "the delivery had not completed; ") +
               (cell.started.done ? "" : "the close had not completed; ");
    };
    // Bounded: an operation that does not complete is reported below, not waited for.
    const bool completed = fixpp::test_support::pump_until(
        f.ioc, [&] { return delivered.has_value() && (cell.started.done || !cell.inside.read); },
        std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
        "CallbackSend::two_frames_in_one_read");
    // Recorded before the drain, which completes what it cancels.
    const std::string pending_at_the_bound = completed ? std::string{} : pending();
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::two_frames_in_one_read");
    }

    std::vector<std::string> results;
    if (!delivered.has_value()) {
        results.emplace_back("the delivery: not completed");
    } else if (delivery_threw) {
        results.emplace_back("the delivery: threw");
    } else {
        for (std::size_t i = 0; i < delivered->size(); ++i) {
            const expected_t<void>& r = (*delivered)[i];
            results.push_back(
                "frame " + std::to_string(i + 1) + ": " +
                (r ? std::string("ok") : "error " + std::to_string(static_cast<int>(r.error()))));
        }
    }
    results.push_back("the close: " + result_of(cell.started));
    std::vector<std::string> full_wire;
    const ArmValues got = read_arm_values(f, sess, cell, entered, results, full_wire);
    const std::string reading = literal_of(got) + inside_text(cell, "fromApp") +
                                "; fromApp calls " + std::to_string(from_app_calls) +
                                "; onLogout calls " + std::to_string(cell.on_logout_calls) +
                                transport_text(f) + ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    // A session that no close() has closed is closed here, so that nothing of it is
    // left running when the cell returns.
    if (session_test_access::lifecycle_value(sess) == 1) {
        (void)f.run(sess.close(close_mode::terminal),
                    "CallbackSend::two_frames_in_one_read_teardown");
    }

    ASSERT_TRUE(cell.inside.read) << "PATH NOT DRIVEN: fromApp did not start its close. "
                                  << reading;
    EXPECT_TRUE(completed) << "every operation must complete within the pump's bound; "
                           << pending_at_the_bound << reading;

    EXPECT_EQ(cell.inside.lifecycle, "open")
        << "inside the callback after the start, the lifecycle is open. " << reading;
    EXPECT_EQ(cell.inside.state, "Active")
        << "inside the callback after the start, the session is Active. " << reading;
    EXPECT_EQ(cell.inside.on_logout_calls, 0)
        << "inside the callback after the start, onLogout has not run. " << reading;
    EXPECT_FALSE(cell.inside.started_completed)
        << "inside the callback after the start, the close it started has not completed. "
        << reading;

    // What the row states, spelled out apart from the arm's values.
    EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;
    EXPECT_EQ(got.at_the_end, "Disconnected closed_drained")
        << "the lifecycle ends closed_drained. " << reading;
    EXPECT_EQ(result_of(cell.started), "ok") << "the close's result is ok. " << reading;
    ASSERT_TRUE(delivered.has_value() && !delivery_threw) << reading;
    const std::size_t second_from_app = position_of(got.order, "fromApp for the second frame");
    const std::size_t on_logout = position_of(got.order, "onLogout");
    if (store == StoreKind::file) {
        EXPECT_EQ(from_app_calls, 1)
            << "on a persistent store the second fromApp does not run. " << reading;
        // The ASSERT_TRUE on delivered.has_value() above returns when it is empty.
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        ASSERT_EQ(delivered->size(), 2U)
            << "the second frame's on_inbound_frame must have been awaited. " << reading;
        EXPECT_TRUE((*delivered)[1].has_value())
            << "the second frame's on_inbound_frame returns ok. " << reading;
        // NOLINTEND(bugprone-unchecked-optional-access)
        EXPECT_LT(on_logout, position_of(got.order, "the delivery completes"))
            << "the close has begun when the second frame's on_inbound_frame returns. " << reading;
    } else {
        EXPECT_EQ(from_app_calls, 2) << "on a volatile store the second fromApp runs. " << reading;
        EXPECT_LT(second_from_app, on_logout)
            << "the second fromApp runs before onLogout. " << reading;
    }

    // The arm's values.
    expect_arm_values(got, expect, reading);
}

}  // namespace

// The MemoryStore arms pin a disclosed limitation, not a wanted behaviour: on a volatile
// store the Active arm does not suspend after fromApp, so the second frame of the read is
// delivered, fromApp included, before the close that the first fromApp started begins
// (`.specify/563-564-callback-send.md` §2.1a, "The close can be late").
TEST(CallbackSend, FromAppStartsClose_Terminal_TwoFramesInOneRead) {
    run_two_frames_in_one_read(
        StoreKind::memory, TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp for the first frame returns [Active open]",
                      "fromApp for the second frame [Active open]",
                      "the delivery completes [Active open]", "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"frame 1: ok", "frame 2: ok", "the close: ok"},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 4,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_TwoFramesInOneRead_LiveWrite) {
    run_two_frames_in_one_read(
        StoreKind::memory, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp for the first frame returns [Active open]",
                      "fromApp for the second frame [Active open]",
                      "the delivery completes [Active open]", "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"frame 1: ok", "frame 2: ok", "the close: ok"},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 4,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// The FileStore arms pin where that limitation stops: the Active arm suspends in its persist
// of the first frame's inbound number, the close runs meanwhile, and the second frame reaches
// a session that is no longer Active, so its fromApp does not run.
TEST(CallbackSend, FromAppStartsClose_Terminal_TwoFramesInOneRead_FileStore) {
    run_two_frames_in_one_read(
        StoreKind::file, TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp for the first frame returns [Active open]",
                            "onLogout [Disconnected closing]",
                            "the close completes [Disconnected closed_drained]",
                            "the delivery completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"frame 1: ok", "frame 2: ok", "the close: ok"},
                  .wire = {},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 3,
                  .store_next_out = 2,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, FromAppStartsClose_Terminal_TwoFramesInOneRead_FileStore_LiveWrite) {
    run_two_frames_in_one_read(
        StoreKind::file, TransportKind::live_suspending_write,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp for the first frame returns [Active open]",
                            "onLogout [Disconnected closing]",
                            "the close completes [Disconnected closed_drained]",
                            "the delivery completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"frame 1: ok", "frame 2: ok", "the close: ok"},
                  .wire = {},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 3,
                  .store_next_out = 2,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

namespace {

// ── A close that waits, cancelled meanwhile ──────────────────────────────────
//
// §3, "New cells", the row of a close that waits, in each mode, cancelled
// meanwhile; §2.1a "Cancellation and the wait".

// fromApp for an inbound 35=D starts a close of mode `mode` by a co_spawn whose
// completion token is bound to a slot of the cell, and emits `emission` on that
// slot before it returns. The row names "a callback"; the cell takes fromApp.
void run_waiting_close_cancelled(close_mode mode, asio::cancellation_type emission, StoreKind store,
                                 TransportKind transport, const ArmValues& expect) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    CloseCell cell{f, sess};
    asio::cancellation_signal signal;
    std::string entered = "not recorded";
    std::string after_the_emission = "not recorded";
    bool emitted = false;
    bool slot_has_a_handler = false;
    // The completion token of the close the cell cancels. A closure call produces
    // it, so that the call is a direct argument of co_spawn: both co_spawn census
    // instruments (tools/audit_co_spawn_named_closure.py and
    // tools/reconcile_co_spawn_census.py) must list the site.
    const auto completes_with_slot = [&](SendOutcome& o) {
        return asio::bind_cancellation_slot(signal.slot(), cell.completes(o, "the close"));
    };

    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    f.app->on_to_app = [&] { cell.callback("toApp"); };
    f.app->on_to_admin = [&](std::string_view msg_type) { cell.to_admin(msg_type); };
    f.app->on_from_app = [&] {
        if (cell.inside.read) return;
        entered = where_is(sess);
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] fromApp: starting Session::close\n");
        asio::co_spawn(ex, sess.close(mode), completes_with_slot(cell.started));
        std::fprintf(stderr, "[ B41 ] fromApp: co_spawn returned\n");
        slot_has_a_handler = signal.slot().has_handler();
        cell.read_inside_callback();
        signal.emit(emission);
        emitted = true;
        after_the_emission = where_is(sess);
        cell.note("fromApp returns");
    };

    const auto inbound = peer_frame("D", 2,
                                    "11=PEER-1\x01"
                                    "54=1\x01"
                                    "55=AAPL\x01");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                   cell.completes(cell.outer, "on_inbound_frame"));

    const auto pending = [&] {
        return std::string(cell.outer.done ? "" : "on_inbound_frame had not completed; ") +
               (cell.started.done ? "" : "the close had not completed; ");
    };
    // A graceful close waits for a peer Logout; everything the arm expects before
    // the end of that wait has happened once on_inbound_frame completed and
    // onLogout ran. A close that has completed is in no wait.
    bool completed = true;
    if (mode == close_mode::graceful) {
        completed = f.end_logout_wait_when(
            [&] { return cell.started.done || (cell.outer.done && cell.on_logout_calls > 0); },
            [&] { return pending() + (cell.on_logout_calls > 0 ? "" : "onLogout had not run"); },
            std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
            "CallbackSend::waiting_close_cancelled_logout_wait");
    }
    // Bounded: an operation that does not complete is reported below, not waited for.
    completed =
        completed &&
        fixpp::test_support::pump_until(
            f.ioc, [&] { return cell.outer.done && (cell.started.done || !cell.inside.read); },
            std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
            "CallbackSend::waiting_close_cancelled");
    // Recorded before the drain, which completes what it cancels.
    const std::string pending_at_the_bound = completed ? std::string{} : pending();
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::waiting_close_cancelled");
    }

    std::vector<std::string> full_wire;
    const ArmValues got = read_arm_values(
        f, sess, cell, entered,
        {"on_inbound_frame: " + result_of(cell.outer), "the close: " + result_of(cell.started)},
        full_wire);
    const std::string reading =
        literal_of(got) + inside_text(cell, "fromApp") + ", the cell's slot " +
        (slot_has_a_handler ? "has a handler" : "has no handler") +
        "; after the emission: " + after_the_emission + "; onLogout calls " +
        std::to_string(cell.on_logout_calls) + transport_text(f) +
        ". Close mode: " + (mode == close_mode::terminal ? "terminal" : "graceful") +
        "; emission: " + (emission == asio::cancellation_type::terminal ? "terminal" : "total") +
        ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    // A session that no close() has closed is closed here, so that nothing of it is
    // left running when the cell returns.
    if (session_test_access::lifecycle_value(sess) == 1) {
        (void)f.run(sess.close(close_mode::terminal),
                    "CallbackSend::waiting_close_cancelled_teardown");
    }

    ASSERT_TRUE(cell.inside.read) << "PATH NOT DRIVEN: fromApp did not start its close. "
                                  << reading;
    ASSERT_TRUE(emitted) << "PATH NOT DRIVEN: fromApp made no emission. " << reading;
    EXPECT_EQ(cell.inside.lifecycle, "open")
        << "inside the callback after the start, the lifecycle is open. " << reading;
    EXPECT_EQ(cell.inside.state, "Active")
        << "inside the callback after the start, the session is Active. " << reading;
    EXPECT_EQ(cell.inside.on_logout_calls, 0)
        << "inside the callback after the start, onLogout has not run. " << reading;
    EXPECT_EQ(cell.inside.logout_to_admin_calls, 0)
        << "inside the callback after the start, the Logout's toAdmin has not run. " << reading;
    // Witness: the emission was made on a slot the close's chain listens on, while
    // the close had not completed.
    ASSERT_FALSE(cell.inside.started_completed)
        << "PATH NOT DRIVEN: the close had completed before the emission, so it was not waiting. "
        << reading;
    ASSERT_TRUE(slot_has_a_handler)
        << "PATH NOT DRIVEN: the co_spawn left no handler in the cell's slot, so the emission "
           "reached nothing. "
        << reading;
    EXPECT_EQ(after_the_emission, "Active open")
        << "the emission changes nothing inside the callback. " << reading;
    EXPECT_TRUE(completed) << "every operation must complete within the pump's bound; "
                           << pending_at_the_bound << reading;

    // What the row states, spelled out apart from the arm's values.
    EXPECT_EQ(got.at_the_end, "Disconnected closed_drained")
        << "the session reaches closed_drained. " << reading;
    EXPECT_FALSE(cell.started.threw) << reading;
    EXPECT_EQ(result_of(cell.started), "ok") << "the close's result is ok. " << reading;
    EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;

    // The arm's values.
    expect_arm_values(got, expect, reading);
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << reading;
}

}  // namespace

// A terminal and a graceful close, each with a `terminal` and with a `total` emission made
// while the close waits for fromApp to return.
//
// A terminal close; the emission is `terminal`. close() resets its cancellation state to
// one that records nothing before it waits, so the emission is not recorded and the close
// runs. A wait in front of that reset would record the emission.
TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTerminal) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::terminal, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTerminal_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTerminal_FileStore) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::terminal, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                            "the close completes [Disconnected closed_drained]",
                            "on_inbound_frame completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 3,
                  .store_next_out = 2,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTerminal_FileStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                            "the close completes [Disconnected closed_drained]",
                            "on_inbound_frame completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 3,
                  .store_next_out = 2,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTerminal_NoStore) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::terminal, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTerminal_NoStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// A terminal close; the emission is `total`.
TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTotal) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::total, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTotal_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::total, StoreKind::memory,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTotal_FileStore) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::total, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                            "the close completes [Disconnected closed_drained]",
                            "on_inbound_frame completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 3,
                  .store_next_out = 2,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTotal_FileStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::total, StoreKind::file,
        TransportKind::live_suspending_write,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]", "onLogout [Disconnected closing]",
                            "the close completes [Disconnected closed_drained]",
                            "on_inbound_frame completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 3,
                  .store_next_out = 2,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTotal_NoStore) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::total, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Terminal_EmitTotal_NoStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::terminal, asio::cancellation_type::total, StoreKind::none,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "onLogout [Disconnected closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {},
            .retained = {},
            .next_out = 2,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// A graceful close; the emission is `terminal`. The cell ends the close's wait for the
// peer's Logout by advancing the mock clock.
TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTerminal) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::terminal, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTerminal_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::terminal, StoreKind::memory,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTerminal_FileStore) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::terminal, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]",
                            "on_inbound_frame completes [Active closing]",
                            "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                            "the close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=5 [Active closing]"},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {"35=5 34=2"},
                  .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
                  .next_out = 3,
                  .next_in = 3,
                  .store_next_out = 3,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTerminal_FileStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::terminal, StoreKind::file,
        TransportKind::live_suspending_write,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]",
                            "on_inbound_frame completes [Active closing]",
                            "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                            "the close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=5 [Active closing]"},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {"35=5 34=2"},
                  .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
                  .next_out = 3,
                  .next_in = 3,
                  .store_next_out = 3,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTerminal_NoStore) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::terminal, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTerminal_NoStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::terminal, StoreKind::none,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// A graceful close; the emission is `total`.
TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTotal) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::total, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTotal_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::total, StoreKind::memory,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTotal_FileStore) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::total, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]",
                            "on_inbound_frame completes [Active closing]",
                            "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                            "the close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=5 [Active closing]"},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {"35=5 34=2"},
                  .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
                  .next_out = 3,
                  .next_in = 3,
                  .store_next_out = 3,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTotal_FileStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::total, StoreKind::file,
        TransportKind::live_suspending_write,
        ArmValues{.entered = "Active open",
                  .order = {"fromApp returns [Active open]",
                            "on_inbound_frame completes [Active closing]",
                            "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                            "the close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=5 [Active closing]"},
                  .results = {"on_inbound_frame: ok", "the close: ok"},
                  .wire = {"35=5 34=2"},
                  .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
                  .next_out = 3,
                  .next_in = 3,
                  .store_next_out = 3,
                  .store_next_in = 3,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTotal_NoStore) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::total, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, CloseCancelledWhileItWaits_Graceful_EmitTotal_NoStore_LiveWrite) {
    run_waiting_close_cancelled(
        close_mode::graceful, asio::cancellation_type::total, StoreKind::none,
        TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active open",
            .order = {"fromApp returns [Active open]", "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the close: ok"},
            .wire = {"35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 3,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

namespace {

// ── A close started in a lifecycle callback ──────────────────────────────────
//
// §3, "New cells", the row of a close started in each lifecycle callback; §2.1a
// "The lifecycle callbacks".

// Forwards to the fixture's mock clock and counts the sleeps in flight, in two
// classes: those whose deadline lies exactly `interval` ahead of the clock when
// the sleep begins, and all others. With `interval` the session's heartbeat
// interval and a clock that nobody has advanced since the session last sent and
// last received a frame, the first class is the liveness loop's sleep. The wait
// of a graceful close for the peer's Logout is in the second.
class SleepCountingClock final : public fixpp::core::Clock {
public:
    struct Counts {
        std::size_t interval_begun = 0;
        std::size_t interval_in_flight = 0;
        std::size_t other_in_flight = 0;
    };

    SleepCountingClock(std::shared_ptr<fixpp::core::mock_clock> inner,
                       std::chrono::milliseconds interval)
        : inner_{std::move(inner)}, interval_{interval} {}

    // Called when a sleep of the first class begins.
    std::function<void()> on_interval_sleep;

    [[nodiscard]] const Counts& counts() const noexcept { return *counts_; }

    [[nodiscard]] fixpp::core::utc_time_point now() const noexcept override {
        return inner_->now();
    }
    [[nodiscard]] fixpp::core::steady_time_point steady_now() const noexcept override {
        return inner_->steady_now();
    }
    [[nodiscard]] asio::awaitable<void> sleep_until(
        fixpp::core::steady_time_point deadline) override {
        // Counted in a coroutine of its own, so that a sleep that ends by an
        // exception, or whose frame is destroyed, is counted out too.
        struct InFlight {
            std::shared_ptr<Counts> counts;
            bool of_interval;
            InFlight(std::shared_ptr<Counts> c, bool i) : counts{std::move(c)}, of_interval{i} {
                ++(of_interval ? counts->interval_in_flight : counts->other_in_flight);
            }
            InFlight(const InFlight&) = delete;
            InFlight& operator=(const InFlight&) = delete;
            ~InFlight() { --(of_interval ? counts->interval_in_flight : counts->other_in_flight); }
        };
        const bool of_interval = deadline - inner_->steady_now() == interval_;
        const InFlight in_flight{counts_, of_interval};
        if (of_interval) {
            ++counts_->interval_begun;
            if (on_interval_sleep) on_interval_sleep();
        }
        co_await inner_->sleep_until(deadline);
    }
    void cancel_sleeps() noexcept override { inner_->cancel_sleeps(); }
    void forget_session(Session* s) noexcept override { inner_->forget_session(s); }

private:
    std::shared_ptr<fixpp::core::mock_clock> inner_;
    std::chrono::milliseconds interval_;
    std::shared_ptr<Counts> counts_ = std::make_shared<Counts>();
};

enum class LifecycleArm {
    // onLogon starts the close, inside the Logon arm.
    on_logon,
    // The setup of on_logon with no close started: the positive control of the
    // readings "no sleep of the heartbeat interval". `mode` is not read.
    on_logon_no_close,
    // The test body starts a graceful close; its onLogout starts a close.
    on_logout_of_a_graceful_close,
    // The test body starts a graceful close; the toAdmin of its Logout starts a
    // close. toAdmin is not a lifecycle callback: the arm is here because its
    // setup and its readings are those of the arm above.
    to_admin_of_a_graceful_closes_logout,
    // The test body delivers a frame under another BeginString, at which the
    // Active arm writes Disconnected without a close(); that write's onLogout
    // starts a close.
    on_logout_after_another_begin_string,
    // onCreate starts the close, inside open().
    on_create,
};

// The heartbeat interval of the arms that run the Logon arm. It is the 108= of
// the peer's Logon the fixture feeds.
constexpr std::chrono::seconds kLifecycleHeartbeat{30};

void run_lifecycle_callback_starts_close(LifecycleArm arm, session_role role, close_mode mode,
                                         StoreKind store, TransportKind transport,
                                         const ArmValues& expect) {
    const bool logon_arm = arm == LifecycleArm::on_logon || arm == LifecycleArm::on_logon_no_close;
    const bool starts_a_close = arm != LifecycleArm::on_logon_no_close;
    const bool to_admin_arm = arm == LifecycleArm::to_admin_of_a_graceful_closes_logout;
    const bool on_logout_arm = arm == LifecycleArm::on_logout_of_a_graceful_close ||
                               arm == LifecycleArm::on_logout_after_another_begin_string;
    // The toAdmin arm on a FileStore and a live transport. The completion of the
    // Logout's store call, which the file executor posts, and the end of the Logout step,
    // which the terminal close's escalation brings about by cancelling the grace timer,
    // race for the session's executor: the Logout is transmitted only where the
    // completion is first. The arm asserts what holds in both orders; fixpp#590 owns the
    // one ending. A MemoryStore, and a session with no store, run that path on the
    // session's executor alone.
    const bool logout_write_races = to_admin_arm && store == StoreKind::file &&
                                    transport == TransportKind::live_suspending_write;
    const char* starting = arm == LifecycleArm::on_create ? "onCreate"
                           : logon_arm                    ? "onLogon"
                           : to_admin_arm                 ? "toAdmin(Logout)"
                                                          : "onLogout";
    Fixture f{ExecKind::io_context, store};
    f.role = role;
    // Elsewhere the interval stays zero and no liveness loop runs.
    if (logon_arm) f.heartbeat_interval = kLifecycleHeartbeat;
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    const auto clock = std::make_shared<SleepCountingClock>(f.clock, kLifecycleHeartbeat);
    cfg.clock_override = clock;
    const bool validate_gate = cfg.validate_inbound_messages;
    Session sess{f.engine, cfg};
    // Before open(), so that the arm's wire holds an initiator's Logon too.
    CloseCell cell{f, sess};
    std::string entered = starts_a_close ? "not recorded" : "no close started";
    int from_app_calls = 0;

    const auto start_close = [&] {
        entered = where_is(sess);
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] %s: starting Session::close\n", starting);
        asio::co_spawn(sess.executor().underlying(), sess.close(mode),
                       cell.completes(cell.started, "the callback's close"));
        std::fprintf(stderr, "[ B41 ] %s: co_spawn returned\n", starting);
        cell.read_inside_callback();
        cell.note(std::string(starting) + " returns");
    };
    clock->on_interval_sleep = [&] { cell.note("a sleep of the heartbeat interval begins"); };
    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    f.app->on_to_app = [&] { cell.callback("toApp"); };
    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (to_admin_arm && msg_type == "5" && !cell.inside.read) {
            cell.note("toAdmin(Logout)");
            start_close();
            return;
        }
        cell.to_admin(msg_type);
    };
    f.app->on_from_app = [&] {
        ++from_app_calls;
        cell.callback("fromApp");
    };
    f.app->on_create = [&] {
        if (arm != LifecycleArm::on_create) return;
        cell.note("onCreate");
        start_close();
    };
    f.app->on_lifecycle = [&](const char* name) {
        if (std::string_view{name} == "onLogon") {
            if (!logon_arm) return;
            cell.note("onLogon");
            if (starts_a_close && !cell.inside.read) start_close();
            return;
        }
        cell.lifecycle_callback(name);
        if (on_logout_arm && !cell.inside.read) start_close();
    };

    const auto other_begin = peer_frame_with_begin("FIX.4.4", "D", 2,
                                                   "11=PEER-1\x01"
                                                   "54=1\x01"
                                                   "55=AAPL\x01");
    const char* outer_name = "on_inbound_frame";
    bool setup_ok = true;
    if (arm == LifecycleArm::on_create) {
        outer_name = "open";
        asio::co_spawn(f.exec, sess.open(), cell.completes(cell.outer, outer_name));
    } else if (logon_arm) {
        setup_ok = f.run(sess.open(), "CallbackSend::lifecycle_open").has_value();
        if (setup_ok) {
            f.feed_peer_logon_on_session_executor(sess, cell.completes(cell.outer, outer_name));
        }
    } else {
        setup_ok = f.open_to_active(sess);
        if (setup_ok && transport == TransportKind::live_suspending_write) {
            f.attach_live_transport(sess);
        }
        if (setup_ok && (arm == LifecycleArm::on_logout_of_a_graceful_close || to_admin_arm)) {
            outer_name = "the graceful close";
            asio::co_spawn(sess.executor().underlying(), sess.close(close_mode::graceful),
                           cell.completes(cell.outer, outer_name));
        } else if (setup_ok) {
            asio::co_spawn(sess.executor().underlying(),
                           sess.on_inbound_frame(std::span<const std::byte>{other_begin}),
                           cell.completes(cell.outer, outer_name));
        }
    }
    const fixpp::core::steady_time_point clock_before = f.clock->steady_now();
    // The frames on the arm's wire before the test body's operation began.
    const std::size_t wire_at_the_start = f.wire.size() - cell.wire_before;

    const auto pending = [&] {
        return std::string(cell.outer.done ? ""
                                           : std::string(outer_name) + " had not completed; ") +
               (cell.started.done || !starts_a_close ? ""
                                                     : "the callback's close had not completed; ");
    };
    bool completed = setup_ok;
    if (completed && !starts_a_close) {
        // The control: a started liveness loop has a sleep in flight while the
        // session is Active.
        completed = fixpp::test_support::pump_until(
            f.ioc, [&] { return cell.outer.done && clock->counts().interval_in_flight > 0; },
            std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
            "CallbackSend::lifecycle_control");
    } else if (completed) {
        // A graceful close the callback started may wait for a peer Logout; it is
        // in that wait once a sleep that is not the liveness loop's is in flight.
        // An arm whose test body starts the graceful close does not advance the
        // clock: there the close the callback starts must end the wait.
        if (mode == close_mode::graceful) {
            completed = f.end_logout_wait_when(
                [&] {
                    return cell.started.done ||
                           (cell.outer.done && clock->counts().other_in_flight > 0);
                },
                [&] {
                    return pending() + (clock->counts().other_in_flight > 0
                                            ? ""
                                            : "no close was in its wait for the peer's Logout");
                },
                std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
                "CallbackSend::lifecycle_logout_wait");
        }
        // Bounded: an operation that does not complete is reported below, not
        // waited for.
        completed =
            completed &&
            fixpp::test_support::pump_until(
                f.ioc, [&] { return cell.outer.done && (cell.started.done || !cell.inside.read); },
                std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
                "CallbackSend::lifecycle");
    }
    // Recorded before the drain, which completes what it cancels.
    const std::string pending_at_the_bound = completed ? std::string{} : pending();
    const SleepCountingClock::Counts sleeps = clock->counts();
    const bool clock_advanced = f.clock->steady_now() != clock_before;
    if (setup_ok && !completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock, "CallbackSend::lifecycle");
    }

    std::vector<std::string> results{std::string(outer_name) + ": " + result_of(cell.outer)};
    if (starts_a_close) results.push_back("the callback's close: " + result_of(cell.started));
    std::vector<std::string> full_wire;
    const ArmValues got = read_arm_values(f, sess, cell, entered, results, full_wire);
    const std::string reading =
        literal_of(got) + inside_text(cell, starting) + "; onLogout calls " +
        std::to_string(cell.on_logout_calls) + "; fromApp calls " + std::to_string(from_app_calls) +
        "; sleeps of the heartbeat interval: begun " + std::to_string(sleeps.interval_begun) +
        ", in flight when the arm's operations had completed " +
        std::to_string(sleeps.interval_in_flight) + "; the mock clock was " +
        (clock_advanced ? "advanced" : "not advanced") + transport_text(f) +
        ". Role: " + (role == session_role::initiator ? "initiator" : "acceptor") +
        "; the close's mode: " +
        (!starts_a_close                ? "none"
         : mode == close_mode::terminal ? "terminal"
                                        : "graceful") +
        ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    // A session that no close() has closed is closed here, and a liveness loop it
    // left asleep is waited for, so that nothing of the session is left running
    // when the cell returns.
    if (session_test_access::lifecycle_value(sess) == 1) {
        (void)f.run(sess.close(close_mode::terminal), "CallbackSend::lifecycle_teardown");
    }
    EXPECT_TRUE(fixpp::test_support::pump_until(
        f.ioc, [&] { return clock->counts().interval_in_flight == 0; }, std::chrono::seconds{5},
        fixpp::test_support::kPumpSlice, "CallbackSend::lifecycle_sleeps_ended"))
        << "a sleep of the heartbeat interval was still in flight after the session closed. "
        << reading;

    ASSERT_TRUE(setup_ok) << "setup: the session did not open or did not reach Active. " << reading;
    EXPECT_TRUE(completed) << "every operation must complete within the pump's bound; "
                           << pending_at_the_bound << reading;

    if (!starts_a_close) {
        // The control. Its count is the one the arms with a close are read against.
        EXPECT_EQ(result_of(cell.outer), "ok") << reading;
        EXPECT_EQ(got.at_the_end, "Active open")
            << "with no close started the session is Active. " << reading;
        EXPECT_GE(sleeps.interval_in_flight, 1U)
            << "with no close started, the liveness loop has a sleep of the heartbeat interval "
               "in flight while the session is Active. "
            << reading;
        expect_arm_values(got, expect, reading);
        return;
    }

    ASSERT_TRUE(cell.inside.read) << "PATH NOT DRIVEN: " << starting << " did not start its close. "
                                  << reading;
    EXPECT_EQ(cell.inside.state + " " + cell.inside.lifecycle, entered)
        << "inside the callback after the start, the session has the state and lifecycle the "
           "callback entered with. "
        << reading;
    EXPECT_FALSE(cell.inside.started_completed)
        << "inside the callback after the start, the close it started has not completed. "
        << reading;
    EXPECT_FALSE(cell.started.threw) << reading;
    EXPECT_EQ(got.at_the_end.substr(got.at_the_end.find(' ') + 1), "closed_drained")
        << "the session ends closed. " << reading;
    const auto logouts_sent = std::ranges::count_if(
        full_wire, [](const std::string& frame) { return field(frame, 35) == "5"; });

    // What the row states for each callback, spelled out apart from the arm's values.
    switch (arm) {
        case LifecycleArm::on_logon:
            EXPECT_EQ(cell.inside.lifecycle, "open")
                << "inside the callback after the start, the lifecycle is open. " << reading;
            EXPECT_EQ(cell.inside.state, "Active")
                << "inside the callback after the start, the session is Active. " << reading;
            EXPECT_EQ(cell.inside.on_logout_calls, 0)
                << "inside the callback after the start, onLogout has not run. " << reading;
            EXPECT_EQ(result_of(cell.started), "ok") << "the close's result is ok. " << reading;
            EXPECT_EQ(sleeps.interval_in_flight, 0U)
                << "no sleep of the heartbeat interval is in flight when the close has "
                   "completed. "
                << reading;
            if (store == StoreKind::file) {
                EXPECT_EQ(sleeps.interval_begun, 0U)
                    << "on a persistent store no liveness loop is started: no sleep of the "
                       "heartbeat interval begins. "
                    << reading;
                EXPECT_EQ(result_of(cell.outer), "ok")
                    << "on a persistent store on_inbound_frame returns ok. " << reading;
            } else {
                EXPECT_GE(sleeps.interval_begun, 1U)
                    << "on a volatile store the Logon arm finishes first, liveness loop "
                       "included: a sleep of the heartbeat interval begins. "
                    << reading;
                EXPECT_LT(position_of(got.order, "a sleep of the heartbeat interval begins"),
                          position_of(got.order, "onLogout"))
                    << "on a volatile store the liveness loop starts before the close begins. "
                    << reading;
                if (mode == close_mode::graceful) {
                    ASSERT_FALSE(full_wire.empty()) << reading;
                    EXPECT_EQ(field(full_wire.back(), 35), "5")
                        << "a graceful close sends its Logout after the Logon exchange. "
                        << reading;
                    EXPECT_EQ(logouts_sent, 1) << "the Logout is transmitted once. " << reading;
                }
            }
            break;
        case LifecycleArm::on_logout_of_a_graceful_close:
            EXPECT_EQ(cell.inside.lifecycle, "closing")
                << "onLogout runs inside the graceful close. " << reading;
            EXPECT_EQ(result_of(cell.started), "ok")
                << "the terminal close joins the graceful one and returns its result. " << reading;
            EXPECT_EQ(result_of(cell.outer), result_of(cell.started))
                << "the two closes return the same result. " << reading;
            EXPECT_FALSE(clock_advanced)
                << "the cell does not advance the mock clock: the graceful close's wait for the "
                   "peer's Logout ends because the terminal close joined it. "
                << reading;
            EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;
            EXPECT_EQ(logouts_sent, 1) << "the Logout is transmitted once. " << reading;
            break;
        case LifecycleArm::to_admin_of_a_graceful_closes_logout:
            EXPECT_EQ(cell.inside.lifecycle, "closing")
                << "the Logout's toAdmin runs inside the graceful close. " << reading;
            EXPECT_EQ(cell.inside.state, "Active")
                << "inside the callback after the start, the session is Active: the Logout has "
                   "not been sent. "
                << reading;
            EXPECT_EQ(cell.inside.on_logout_calls, 0)
                << "inside the callback after the start, onLogout has not run. " << reading;
            EXPECT_EQ(result_of(cell.started), "ok")
                << "the terminal close joins the graceful one and returns its result. " << reading;
            EXPECT_EQ(result_of(cell.outer), result_of(cell.started))
                << "the two closes return the same result. " << reading;
            // The reading "not advanced" has its positive control in the
            // ToAdminOfGracefulCloseStartsNoClose cells: with no close started in the
            // callback, the graceful close stays in its wait until the clock is advanced.
            EXPECT_FALSE(clock_advanced)
                << "the cell does not advance the mock clock: the graceful close's wait for the "
                   "peer's Logout ends because the terminal close joined it. "
                << reading;
            EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;
            if (logout_write_races) {
                EXPECT_LE(logouts_sent, 1) << "the Logout is transmitted at most once. " << reading;
                ASSERT_EQ(got.retained.size(), kRetainedThrough) << reading;
                EXPECT_EQ(got.retained[1], "35=5 34=2")
                    << "the Logout is retained under its number, transmitted or not. " << reading;
            } else {
                EXPECT_EQ(logouts_sent, 1) << "the Logout is transmitted once. " << reading;
                EXPECT_LT(position_of(got.order, "toAdmin(Logout) returns"),
                          position_of(got.order, "transmit 35=5"))
                    << "the Logout is transmitted after its toAdmin returned. " << reading;
            }
            break;
        case LifecycleArm::on_logout_after_another_begin_string:
            ASSERT_FALSE(validate_gate)
                << "PATH NOT DRIVEN: the validate gate is on, so the frame does not reach the "
                   "Active arm's BeginString test. "
                << reading;
            EXPECT_EQ(cell.inside.lifecycle, "open")
                << "inside the callback after the start, the lifecycle is open. " << reading;
            EXPECT_EQ(from_app_calls, 0) << "the frame is not delivered to fromApp. " << reading;
            EXPECT_EQ(entered, "Disconnected open")
                << "the write of Disconnected is outside close(): onLogout runs with the "
                   "lifecycle open, so no close is in flight for the callback's close to join. "
                << reading;
            EXPECT_EQ(full_wire.size(), wire_at_the_start)
                << "the route emits no frame, and neither does the callback's close. " << reading;
            EXPECT_NE(cell.started.err, std::optional<error>{error::session_already_closed})
                << "the callback's close does not return session_already_closed. " << reading;
            EXPECT_EQ(result_of(cell.started), "ok")
                << "the callback's close is the session's first and returns ok. " << reading;
            EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;
            break;
        case LifecycleArm::on_create:
            EXPECT_EQ(cell.inside.lifecycle, "open")
                << "inside the callback after the start, the lifecycle is open. " << reading;
            EXPECT_EQ(result_of(cell.outer), "ok") << "open() returns ok. " << reading;
            EXPECT_EQ(result_of(cell.started), "ok") << "the close's result is ok. " << reading;
            EXPECT_EQ(cell.on_logout_calls, 0) << "onLogout does not run. " << reading;
            EXPECT_EQ(logouts_sent, 0) << "no Logout is transmitted. " << reading;
            if (role == session_role::initiator) {
                EXPECT_LT(position_of(got.order, "transmit 35=A"),
                          position_of(got.order, "onCreate"))
                    << "an initiator's Logon is transmitted before onCreate runs. " << reading;
            } else {
                EXPECT_TRUE(full_wire.empty()) << "an acceptor transmits nothing. " << reading;
            }
            break;
        case LifecycleArm::on_logon_no_close:
            break;
    }

    // The arm's values. Where the Logout's write races, they are compared without what
    // the two orders differ in: the Logout's transmit, and the FSM state onLogout runs in.
    ArmValues compared = got;
    if (logout_write_races) {
        std::erase_if(compared.order,
                      [](const std::string& e) { return e.starts_with("transmit 35=5"); });
        for (auto& e : compared.order) {
            if (e.starts_with("onLogout [")) e = "onLogout";
        }
        std::erase_if(compared.wire, [](const std::string& w) { return w.starts_with("35=5"); });
    }
    expect_arm_values(compared, expect, reading);
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << reading;
}

}  // namespace

// onLogon starts a close. On a persistent store the Logon arm is suspended in its persist
// of the inbound number when the close runs, and stops when it resumes: it starts no
// liveness loop. On a volatile store the arm finishes first, liveness loop included. An
// initiator and an acceptor, a terminal and a graceful close. On the synchronous sink only:
// the live transport double attaches to a session that is already Active.
TEST(CallbackSend, OnLogonStartsClose_Initiator_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::initiator, close_mode::terminal, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogon [Active open]",
                      "onLogon returns [Active open]",
                      "a sleep of the heartbeat interval begins [Active open]",
                      "on_inbound_frame completes [Active open]", "onLogout [Disconnected closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"on_inbound_frame: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1"},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 2,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Initiator_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::initiator, close_mode::terminal, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogon [Active open]",
                            "onLogon returns [Active open]", "onLogout [Disconnected closing]",
                            "the callback's close completes [Disconnected closed_drained]",
                            "on_inbound_frame completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Initiator_Graceful) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::initiator, close_mode::graceful, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogon [Active open]",
                            "onLogon returns [Active open]",
                            "a sleep of the heartbeat interval begins [Active open]",
                            "on_inbound_frame completes [Active open]",
                            "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1", "35=5 34=2"},
                  .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
                  .next_out = 3,
                  .next_in = 2,
                  .store_next_out = 3,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Initiator_Graceful_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::initiator, close_mode::graceful, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogon [Active open]",
                            "onLogon returns [Active open]",
                            "on_inbound_frame completes [Active closing]",
                            "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1", "35=5 34=2"},
                  .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
                  .next_out = 3,
                  .next_in = 2,
                  .store_next_out = 3,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Acceptor_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::acceptor, close_mode::terminal, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"transmit 35=A 34=1 [LogonReceived open]", "onLogon [Active open]",
                      "onLogon returns [Active open]",
                      "a sleep of the heartbeat interval begins [Active open]",
                      "on_inbound_frame completes [Active open]", "onLogout [Disconnected closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonReceived open]"},
            .results = {"on_inbound_frame: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1"},
            .retained = {"35=A 34=1", "none", "none", "none"},
            .next_out = 2,
            .next_in = 2,
            .store_next_out = 2,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Acceptor_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::acceptor, close_mode::terminal, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "Active open",
                  .order = {"transmit 35=A 34=1 [LogonReceived open]", "onLogon [Active open]",
                            "onLogon returns [Active open]", "onLogout [Disconnected closing]",
                            "the callback's close completes [Disconnected closed_drained]",
                            "on_inbound_frame completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonReceived open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Acceptor_Graceful) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::acceptor, close_mode::graceful, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"transmit 35=A 34=1 [LogonReceived open]", "onLogon [Active open]",
                      "onLogon returns [Active open]",
                      "a sleep of the heartbeat interval begins [Active open]",
                      "on_inbound_frame completes [Active open]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonReceived open]", "toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsClose_Acceptor_Graceful_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon, session_role::acceptor, close_mode::graceful, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{
            .entered = "Active open",
            .order = {"transmit 35=A 34=1 [LogonReceived open]", "onLogon [Active open]",
                      "onLogon returns [Active open]",
                      "on_inbound_frame completes [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonReceived open]", "toAdmin 35=5 [Active closing]"},
            .results = {"on_inbound_frame: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// The positive control of the readings "no sleep of the heartbeat interval" above: the same
// setup on a persistent store, with no close started in onLogon.
TEST(CallbackSend, OnLogonStartsNoClose_Initiator_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon_no_close, session_role::initiator, close_mode::terminal,
        StoreKind::file, TransportKind::sync_capture,
        ArmValues{.entered = "no close started",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogon [Active open]",
                            "a sleep of the heartbeat interval begins [Active open]",
                            "on_inbound_frame completes [Active open]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Active open",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogonStartsNoClose_Acceptor_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logon_no_close, session_role::acceptor, close_mode::terminal,
        StoreKind::file, TransportKind::sync_capture,
        ArmValues{.entered = "no close started",
                  .order = {"transmit 35=A 34=1 [LogonReceived open]", "onLogon [Active open]",
                            "a sleep of the heartbeat interval begins [Active open]",
                            "on_inbound_frame completes [Active open]"},
                  .callbacks = {"toAdmin 35=A [LogonReceived open]"},
                  .results = {"on_inbound_frame: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Active open",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

// The test body starts a graceful close; onLogout, which that close fires, starts a terminal
// one. The terminal close joins the graceful one, and the wait for the peer's Logout ends
// without the cell advancing the clock.
TEST(CallbackSend, OnLogoutOfGracefulCloseStartsClose_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_of_a_graceful_close, session_role::initiator, close_mode::terminal,
        StoreKind::memory, TransportKind::sync_capture,
        ArmValues{
            .entered = "LogoutSent closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]", "onLogout returns [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutOfGracefulCloseStartsClose_Terminal_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_of_a_graceful_close, session_role::initiator, close_mode::terminal,
        StoreKind::memory, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "LogoutSent closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]", "onLogout returns [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutOfGracefulCloseStartsClose_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_of_a_graceful_close, session_role::initiator, close_mode::terminal,
        StoreKind::file, TransportKind::sync_capture,
        ArmValues{
            .entered = "LogoutSent closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]", "onLogout returns [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutOfGracefulCloseStartsClose_Terminal_FileStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_of_a_graceful_close, session_role::initiator, close_mode::terminal,
        StoreKind::file, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "LogoutSent closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]", "onLogout returns [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutOfGracefulCloseStartsClose_Terminal_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_of_a_graceful_close, session_role::initiator, close_mode::terminal,
        StoreKind::none, TransportKind::sync_capture,
        ArmValues{
            .entered = "LogoutSent closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]", "onLogout returns [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutOfGracefulCloseStartsClose_Terminal_NoStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_of_a_graceful_close, session_role::initiator, close_mode::terminal,
        StoreKind::none, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "LogoutSent closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "transmit 35=5 34=2 [Active closing]",
                      "onLogout [LogoutSent closing]", "onLogout returns [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]", "toAdmin 35=5 [Active closing]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

namespace {

// The positive control of the ToAdminOfGracefulCloseStartsClose cells' reading "the mock
// clock was not advanced": their setup with no close started in the Logout's toAdmin.
// The graceful close then stays in its wait for the peer's Logout, and ends when the
// cell advances the mock clock.
void run_graceful_close_with_no_close_started(StoreKind store, TransportKind transport) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    cfg.logout_disconnect_timeout_ms = kLogoutWaitMs;
    const auto clock = std::make_shared<SleepCountingClock>(f.clock, kLifecycleHeartbeat);
    cfg.clock_override = clock;
    Session sess{f.engine, cfg};
    // Before open(), as in the cells this one is the control of.
    CloseCell cell{f, sess};
    f.on_transmit = [&](const std::string& frame) { cell.transmitted(frame); };
    f.app->on_to_admin = [&](std::string_view msg_type) { cell.to_admin(msg_type); };
    f.app->on_lifecycle = [&](const char* name) { cell.lifecycle_callback(name); };
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);

    const fixpp::core::steady_time_point clock_before = f.clock->steady_now();
    asio::co_spawn(sess.executor().underlying(), sess.close(close_mode::graceful),
                   cell.completes(cell.outer, "the graceful close"));
    // The close is in its wait once onLogout has run and a sleep is in flight on the
    // session's clock. HeartBtInt is 0, so no liveness loop sleeps there.
    const auto in_its_wait = [&] {
        return cell.on_logout_calls > 0 && clock->counts().other_in_flight > 0;
    };
    const bool reached_the_wait = fixpp::test_support::pump_until(
        f.ioc, [&] { return cell.outer.done || in_its_wait(); }, std::chrono::seconds{5},
        fixpp::test_support::kPumpSlice, "CallbackSend::no_close_started_reaches_the_wait");
    // Read before the clock is advanced.
    const bool done_in_the_wait = cell.outer.done;
    const std::string where_in_the_wait = where_is(sess);
    const std::size_t noted_in_the_wait = cell.order.size();
    const bool advanced_in_the_wait = f.clock->steady_now() != clock_before;

    bool completed =
        reached_the_wait && !done_in_the_wait &&
        f.end_logout_wait_when(
            in_its_wait, [] { return std::string("the graceful close had left its wait"); },
            std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms},
            "CallbackSend::no_close_started_logout_wait");
    // Bounded: a close that does not complete is reported below, not waited for.
    completed = completed && fixpp::test_support::pump_until(
                                 f.ioc, [&] { return cell.outer.done; }, std::chrono::seconds{5},
                                 fixpp::test_support::kPumpSlice, "CallbackSend::no_close_started");
    const bool clock_advanced = f.clock->steady_now() != clock_before;
    if (!completed && !cell.outer.done) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::no_close_started");
    }

    const std::string at_the_end = where_is(sess);
    const std::vector<std::string> wire(
        f.wire.begin() + static_cast<std::ptrdiff_t>(cell.wire_before), f.wire.end());
    const auto logouts_sent = std::ranges::count_if(
        wire, [](const std::string& frame) { return field(frame, 35) == "5"; });
    const std::string reading =
        std::string("no close started in the Logout's toAdmin; in the wait: ") + where_in_the_wait +
        ", the graceful close " + (done_in_the_wait ? "completed" : "not completed") +
        ", the mock clock " + (advanced_in_the_wait ? "advanced" : "not advanced") +
        "; at the end: " + at_the_end + ", the mock clock " +
        (clock_advanced ? "advanced" : "not advanced") + "; .order = " + literal(cell.order) +
        "; entries of .order noted before the clock was advanced: " +
        std::to_string(noted_in_the_wait) + "; .callbacks = " + literal(cell.callbacks) +
        "; the graceful close: " + result_of(cell.outer) + "; onLogout calls " +
        std::to_string(cell.on_logout_calls) + transport_text(f) + ".";
    std::printf("[ MEASURED ] %s\n", reading.c_str());
    std::fflush(stdout);

    if (session_test_access::lifecycle_value(sess) == 1) {
        (void)f.run(sess.close(close_mode::terminal), "CallbackSend::no_close_started_teardown");
    }

    ASSERT_TRUE(reached_the_wait)
        << "PATH NOT DRIVEN: the graceful close neither completed nor reached its wait for the "
           "peer's Logout. "
        << reading;
    EXPECT_FALSE(done_in_the_wait)
        << "with no close started in the Logout's toAdmin, the graceful close does not complete "
           "while the mock clock stands. "
        << reading;
    EXPECT_EQ(where_in_the_wait, "LogoutSent closing")
        << "in its wait the graceful close has sent the Logout and has not finished. " << reading;
    EXPECT_TRUE(completed) << "the graceful close must complete once the mock clock has passed "
                              "logout_disconnect_timeout_ms. "
                           << reading;
    EXPECT_TRUE(clock_advanced) << reading;
    EXPECT_EQ(position_of(cell.order, "the graceful close completes"), noted_in_the_wait)
        << "the graceful close's completion is the first thing noted after the clock was "
           "advanced. "
        << reading;
    EXPECT_EQ(result_of(cell.outer), "ok") << reading;
    EXPECT_EQ(at_the_end, "Disconnected closed_drained") << reading;
    EXPECT_EQ(cell.on_logout_calls, 1) << "onLogout runs once. " << reading;
    EXPECT_EQ(logouts_sent, 1) << "the Logout is transmitted once. " << reading;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << reading;
}

}  // namespace

// The test body starts a graceful close; the toAdmin of that close's Logout starts a terminal
// one. The terminal close begins after toAdmin returns, joins the graceful one, and the wait
// for the peer's Logout ends without the cell advancing the clock (B&L B-563-2).
TEST(CallbackSend, ToAdminOfGracefulCloseStartsClose_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::to_admin_of_a_graceful_closes_logout, session_role::initiator,
        close_mode::terminal, StoreKind::memory, TransportKind::sync_capture,
        ArmValues{
            .entered = "Active closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "toAdmin(Logout) [Active closing]",
                      "toAdmin(Logout) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsClose_Terminal_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::to_admin_of_a_graceful_closes_logout, session_role::initiator,
        close_mode::terminal, StoreKind::memory, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "toAdmin(Logout) [Active closing]",
                      "toAdmin(Logout) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsClose_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::to_admin_of_a_graceful_closes_logout, session_role::initiator,
        close_mode::terminal, StoreKind::file, TransportKind::sync_capture,
        ArmValues{
            .entered = "Active closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "toAdmin(Logout) [Active closing]",
                      "toAdmin(Logout) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// The arm in which the Logout's write races the escalation (logout_write_races in the
// helper; fixpp#590). Its .order has no transmit of the Logout and no state on onLogout, and
// its .wire no Logout: the helper removes them from what it compares.
TEST(CallbackSend, ToAdminOfGracefulCloseStartsClose_Terminal_FileStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::to_admin_of_a_graceful_closes_logout, session_role::initiator,
        close_mode::terminal, StoreKind::file, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "toAdmin(Logout) [Active closing]",
                      "toAdmin(Logout) returns [Active closing]", "onLogout",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1"},
            .retained = {"35=A 34=1", "35=5 34=2", "none", "none"},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = 3,
            .store_next_in = 2,
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsClose_Terminal_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::to_admin_of_a_graceful_closes_logout, session_role::initiator,
        close_mode::terminal, StoreKind::none, TransportKind::sync_capture,
        ArmValues{
            .entered = "Active closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "toAdmin(Logout) [Active closing]",
                      "toAdmin(Logout) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsClose_Terminal_NoStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::to_admin_of_a_graceful_closes_logout, session_role::initiator,
        close_mode::terminal, StoreKind::none, TransportKind::live_suspending_write,
        ArmValues{
            .entered = "Active closing",
            .order = {"transmit 35=A 34=1 [LogonSent open]", "toAdmin(Logout) [Active closing]",
                      "toAdmin(Logout) returns [Active closing]",
                      "transmit 35=5 34=2 [Active closing]", "onLogout [LogoutSent closing]",
                      "the graceful close completes [Disconnected closed_drained]",
                      "the callback's close completes [Disconnected closed_drained]"},
            .callbacks = {"toAdmin 35=A [LogonSent open]"},
            .results = {"the graceful close: ok", "the callback's close: ok"},
            .wire = {"35=A 34=1", "35=5 34=2"},
            .retained = {},
            .next_out = 3,
            .next_in = 2,
            .store_next_out = {},
            .store_next_in = {},
            .at_the_end = "Disconnected closed_drained",
            .writes_after_close = 0,
            .writes_refused = 0});
}

// The control of the cells above: no close is started in the Logout's toAdmin.
TEST(CallbackSend, ToAdminOfGracefulCloseStartsNoClose) {
    run_graceful_close_with_no_close_started(StoreKind::memory, TransportKind::sync_capture);
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsNoClose_LiveWrite) {
    run_graceful_close_with_no_close_started(StoreKind::memory,
                                             TransportKind::live_suspending_write);
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsNoClose_FileStore) {
    run_graceful_close_with_no_close_started(StoreKind::file, TransportKind::sync_capture);
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsNoClose_FileStore_LiveWrite) {
    run_graceful_close_with_no_close_started(StoreKind::file, TransportKind::live_suspending_write);
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsNoClose_NoStore) {
    run_graceful_close_with_no_close_started(StoreKind::none, TransportKind::sync_capture);
}

TEST(CallbackSend, ToAdminOfGracefulCloseStartsNoClose_NoStore_LiveWrite) {
    run_graceful_close_with_no_close_started(StoreKind::none, TransportKind::live_suspending_write);
}

// A frame under another BeginString makes the Active arm write Disconnected with no close()
// and no emit; onLogout, which that write fires, starts a close. It is the session's first.
TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::terminal, StoreKind::memory, TransportKind::sync_capture,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Terminal_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::terminal, StoreKind::memory, TransportKind::live_suspending_write,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::terminal, StoreKind::file, TransportKind::sync_capture,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Terminal_FileStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::terminal, StoreKind::file, TransportKind::live_suspending_write,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Terminal_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::terminal, StoreKind::none, TransportKind::sync_capture,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Terminal_NoStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::terminal, StoreKind::none, TransportKind::live_suspending_write,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Graceful) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::graceful, StoreKind::memory, TransportKind::sync_capture,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Graceful_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::graceful, StoreKind::memory, TransportKind::live_suspending_write,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Graceful_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::graceful, StoreKind::file, TransportKind::sync_capture,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Graceful_FileStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::graceful, StoreKind::file, TransportKind::live_suspending_write,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = 2,
                  .store_next_in = 2,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Graceful_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::graceful, StoreKind::none, TransportKind::sync_capture,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnLogoutAfterAnotherBeginStringStartsClose_Graceful_NoStore_LiveWrite) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_logout_after_another_begin_string, session_role::initiator,
        close_mode::graceful, StoreKind::none, TransportKind::live_suspending_write,
        ArmValues{.entered = "Disconnected open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onLogout [Disconnected open]",
                            "onLogout returns [Disconnected open]",
                            "on_inbound_frame completes [Disconnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"on_inbound_frame: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {},
                  .next_out = 2,
                  .next_in = 2,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

// onCreate starts a close, inside open(). The session never reached Active: open() returns
// ok and the session ends closed with no onLogout and no Logout. An initiator has
// transmitted its Logon before onCreate runs. On the synchronous sink only, as the onLogon
// arms.
TEST(CallbackSend, OnCreateStartsClose_Initiator_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::initiator, close_mode::terminal, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{.entered = "LogonSent open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onCreate [LogonSent open]",
                            "onCreate returns [LogonSent open]", "open completes [LogonSent open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 1,
                  .store_next_out = 2,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Initiator_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::initiator, close_mode::terminal, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "LogonSent open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onCreate [LogonSent open]",
                            "onCreate returns [LogonSent open]", "open completes [LogonSent open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 1,
                  .store_next_out = 2,
                  .store_next_in = 1,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Initiator_Terminal_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::initiator, close_mode::terminal, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{.entered = "LogonSent open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onCreate [LogonSent open]",
                            "onCreate returns [LogonSent open]", "open completes [LogonSent open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {},
                  .next_out = 2,
                  .next_in = 1,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Initiator_Graceful) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::initiator, close_mode::graceful, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{.entered = "LogonSent open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onCreate [LogonSent open]",
                            "onCreate returns [LogonSent open]", "open completes [LogonSent open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 1,
                  .store_next_out = 2,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Initiator_Graceful_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::initiator, close_mode::graceful, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "LogonSent open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onCreate [LogonSent open]",
                            "onCreate returns [LogonSent open]", "open completes [LogonSent open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {"35=A 34=1", "none", "none", "none"},
                  .next_out = 2,
                  .next_in = 1,
                  .store_next_out = 2,
                  .store_next_in = 1,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Initiator_Graceful_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::initiator, close_mode::graceful, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{.entered = "LogonSent open",
                  .order = {"transmit 35=A 34=1 [LogonSent open]", "onCreate [LogonSent open]",
                            "onCreate returns [LogonSent open]", "open completes [LogonSent open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {"toAdmin 35=A [LogonSent open]"},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {"35=A 34=1"},
                  .retained = {},
                  .next_out = 2,
                  .next_in = 1,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Acceptor_Terminal) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::acceptor, close_mode::terminal, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{.entered = "NotConnected open",
                  .order = {"onCreate [NotConnected open]", "onCreate returns [NotConnected open]",
                            "open completes [NotConnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {},
                  .retained = {"none", "none", "none", "none"},
                  .next_out = 1,
                  .next_in = 1,
                  .store_next_out = 1,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Acceptor_Terminal_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::acceptor, close_mode::terminal, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "NotConnected open",
                  .order = {"onCreate [NotConnected open]", "onCreate returns [NotConnected open]",
                            "open completes [NotConnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {},
                  .retained = {"none", "none", "none", "none"},
                  .next_out = 1,
                  .next_in = 1,
                  .store_next_out = 1,
                  .store_next_in = 1,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Acceptor_Terminal_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::acceptor, close_mode::terminal, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{.entered = "NotConnected open",
                  .order = {"onCreate [NotConnected open]", "onCreate returns [NotConnected open]",
                            "open completes [NotConnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {},
                  .retained = {},
                  .next_out = 1,
                  .next_in = 1,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Acceptor_Graceful) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::acceptor, close_mode::graceful, StoreKind::memory,
        TransportKind::sync_capture,
        ArmValues{.entered = "NotConnected open",
                  .order = {"onCreate [NotConnected open]", "onCreate returns [NotConnected open]",
                            "open completes [NotConnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {},
                  .retained = {"none", "none", "none", "none"},
                  .next_out = 1,
                  .next_in = 1,
                  .store_next_out = 1,
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Acceptor_Graceful_FileStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::acceptor, close_mode::graceful, StoreKind::file,
        TransportKind::sync_capture,
        ArmValues{.entered = "NotConnected open",
                  .order = {"onCreate [NotConnected open]", "onCreate returns [NotConnected open]",
                            "open completes [NotConnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {},
                  .retained = {"none", "none", "none", "none"},
                  .next_out = 1,
                  .next_in = 1,
                  .store_next_out = 1,
                  .store_next_in = 1,
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

TEST(CallbackSend, OnCreateStartsClose_Acceptor_Graceful_NoStore) {
    run_lifecycle_callback_starts_close(
        LifecycleArm::on_create, session_role::acceptor, close_mode::graceful, StoreKind::none,
        TransportKind::sync_capture,
        ArmValues{.entered = "NotConnected open",
                  .order = {"onCreate [NotConnected open]", "onCreate returns [NotConnected open]",
                            "open completes [NotConnected open]",
                            "the callback's close completes [Disconnected closed_drained]"},
                  .callbacks = {},
                  .results = {"open: ok", "the callback's close: ok"},
                  .wire = {},
                  .retained = {},
                  .next_out = 1,
                  .next_in = 1,
                  .store_next_out = {},
                  .store_next_in = {},
                  .at_the_end = "Disconnected closed_drained",
                  .writes_after_close = 0,
                  .writes_refused = 0});
}

// Two sessions, A and B, on one io_context; each has its own strand, Application
// and capture. A's toApp starts a send on B by a co_spawn onto B's executor, which
// is idle, so B's send begins inline. The depth that Session::send reads belongs to
// the session, so B's send does not wait for A's toApp to return.
TEST(CallbackSend, ToAppStartsSendOnAnotherSession) {
    Fixture f{ExecKind::io_context};
    const auto app_b = std::make_shared<CallbackApp>();
    fixpp::core::EngineConfig engine_b;
    engine_b.clock = f.clock;
    engine_b.executor = f.exec;
    engine_b.application = app_b;
    std::vector<std::string> wire_b;
    auto cfg_b = f.cfg();
    cfg_b.transport_send = [&wire_b](std::span<const std::byte> fr) {
        wire_b.push_back(text_of(fr));
    };
    Session a{f.engine, f.cfg()};
    Session b{engine_b, cfg_b};
    ASSERT_TRUE(f.open_to_active(a)) << "setup: session A did not reach Active";
    ASSERT_TRUE(f.open_to_active(b)) << "setup: session B did not reach Active";
    ASSERT_EQ(next_out(a), 2U) << "setup: A's Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_out(b), 2U) << "setup: B's Logon must have consumed outbound MsgSeqNum 1";

    const auto ex_a = a.executor().underlying();
    const auto ex_b = b.executor().underlying();
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);
    const std::size_t wire_a_before = f.wire.size();
    const std::size_t wire_b_before = wire_b.size();

    SendOutcome on_a;
    SendOutcome on_b;
    bool a_to_app_live = false;
    std::optional<bool> b_to_app_saw_a_live;
    f.app->on_to_app = [&] {
        a_to_app_live = true;
        asio::co_spawn(ex_b, b.send(two), record_into(on_b));
        a_to_app_live = false;
    };
    app_b->on_to_app = [&] { b_to_app_saw_a_live = a_to_app_live; };
    asio::co_spawn(ex_a, a.send(one), record_into(on_a));
    if (!f.pump_done([&] { return on_a.done && on_b.done; }, "CallbackSend::another_session")) {
        return;
    }

    const std::vector<std::string> sent_a(
        f.wire.begin() + static_cast<std::ptrdiff_t>(wire_a_before), f.wire.end());
    const std::vector<std::string> sent_b(
        wire_b.begin() + static_cast<std::ptrdiff_t>(wire_b_before), wire_b.end());
    const std::string context =
        "B's send, relative to A's toApp: " + classify(b_to_app_saw_a_live) +
        ". A transmitted: " + describe(sent_a) + "; B transmitted: " + describe(sent_b) +
        "; A's send: " + describe(on_a) + "; B's send: " + describe(on_b) +
        "; NextNumOut: A=" + std::to_string(next_out(a)) + ", B=" + std::to_string(next_out(b)) +
        ".";

    ASSERT_EQ(f.app->to_app_calls, 1) << "PATH NOT DRIVEN: A's toApp must run once. " << context;
    ASSERT_TRUE(b_to_app_saw_a_live.has_value())
        << "PATH NOT DRIVEN: B's toApp never ran. " << context;
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the ASSERT_TRUE above requires it
    EXPECT_TRUE(*b_to_app_saw_a_live)
        << "a send on session B started in session A's toApp must not be deferred: B's toApp "
           "must run before A's toApp returns. "
        << context;

    EXPECT_EQ(describe(on_a), "ok") << context;
    EXPECT_EQ(describe(on_b), "ok") << context;
    ASSERT_EQ(sent_a.size(), 1U) << context;
    ASSERT_EQ(sent_b.size(), 1U) << context;
    EXPECT_EQ(field(sent_a.front(), 11), kIdOne) << context;
    EXPECT_EQ(seq_of(sent_a.front()), 2U) << context;
    EXPECT_EQ(field(sent_b.front(), 11), kIdTwo) << context;
    EXPECT_EQ(seq_of(sent_b.front()), 2U) << context;
    EXPECT_EQ(a.state(), fsm_state::Active) << context;
    EXPECT_EQ(b.state(), fsm_state::Active) << context;
}

namespace {

enum class Emission { terminal, total, terminal_with_throw_if_cancelled_off };

// Session::send awaited from a coroutine that first turns throw_if_cancelled off.
asio::awaitable<expected_t<void>> send_with_throw_if_cancelled_off(
    Session& s, std::span<const std::byte> payload) {
    co_await asio::this_coro::throw_if_cancelled(false);
    co_return co_await s.send(payload);
}

// toApp for ONE starts send(TWO) by a co_spawn whose completion token is bound to
// `signal`'s slot, and emits on that signal before it returns. A send started in
// toApp waits for the callback to return before it starts, so the emission reaches
// it while it waits. A detached co_spawn has no slot, and an emission would then
// reach nothing.
void run_deferred_send_cancelled(Emission emission) {
    Fixture f{ExecKind::io_context};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";

    const auto ex = sess.executor().underlying();
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);
    const std::size_t wire_before = f.wire.size();

    asio::cancellation_signal signal;
    SendOutcome first;
    SendOutcome second;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    // Read in toApp(ONE), between the co_spawn's return and the emission.
    bool emitted = false;
    bool slot_has_a_handler = false;
    bool second_pending_at_emission = false;
    int to_app_calls_at_emission = 0;
    f.app->on_to_app = [&] {
        if (f.app->to_app_calls != 1) {
            if (!inner_saw_outer_live.has_value()) inner_saw_outer_live = outer_live;
            return;
        }
        outer_live = true;
        const auto token = asio::bind_cancellation_slot(signal.slot(), record_into(second));
        if (emission == Emission::terminal_with_throw_if_cancelled_off) {
            asio::co_spawn(ex, send_with_throw_if_cancelled_off(sess, two), token);
        } else {
            asio::co_spawn(ex, sess.send(two), token);
        }
        slot_has_a_handler = signal.slot().has_handler();
        second_pending_at_emission = !second.done;
        to_app_calls_at_emission = f.app->to_app_calls;
        signal.emit(emission == Emission::total ? asio::cancellation_type::total
                                                : asio::cancellation_type::terminal);
        emitted = true;
        outer_live = false;
    };
    asio::co_spawn(ex, sess.send(one), record_into(first));
    if (!f.pump_done([&] { return first.done && second.done; }, "CallbackSend::cancelled")) {
        return;
    }

    const std::string classification = classify(inner_saw_outer_live);
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string store_out = f.store_next_out(sess);
    const Retained under_three = f.retained(sess, 3);
    const std::string context =
        "send(TWO)'s toApp: " + classification + ". Transmitted, in wire order: " + describe(sent) +
        "; send(ONE): " + describe(first) + "; send(TWO): " + describe(second) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) + ", store=" + store_out +
        "; the store holds under outbound 34=3: " + describe_retained(under_three) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) +
        "; toApp calls: " + std::to_string(f.app->to_app_calls) + ".";

    // Witness: the emission was made on a slot the chain listens on, while the send
    // had neither completed nor reached its own toApp.
    ASSERT_TRUE(emitted) << "PATH NOT DRIVEN: toApp(ONE) never ran. " << context;
    ASSERT_TRUE(slot_has_a_handler)
        << "PATH NOT DRIVEN: the co_spawn installed no handler in the cell's slot. " << context;
    ASSERT_TRUE(second_pending_at_emission)
        << "PATH NOT DRIVEN: send(TWO) had completed before the emission. " << context;
    ASSERT_EQ(to_app_calls_at_emission, 1)
        << "PATH NOT DRIVEN: send(TWO) had reached its own toApp before the emission, so it was "
           "not waiting to start. "
        << context;

    EXPECT_EQ(describe(first), "ok") << context;
    if (emission == Emission::terminal) {
        // The send is refused before it starts: it has consumed nothing.
        EXPECT_FALSE(second.threw) << context;
        EXPECT_EQ(second.err, std::optional<error>{error::dispatch_aborted}) << context;
        EXPECT_EQ(classification, "never ran")
            << "a send cancelled before it starts must not reach its toApp. " << context;
        EXPECT_EQ(sess.state(), fsm_state::Active) << "the session must stay Active. " << context;
        ASSERT_EQ(sent.size(), 1U) << "nothing of send(TWO) may be transmitted. " << context;
        EXPECT_EQ(field(sent.front(), 11), kIdOne) << context;
        EXPECT_EQ(seq_of(sent.front()), 2U) << context;
        EXPECT_EQ(next_out(sess), 3U) << "send(TWO) must consume no MsgSeqNum. " << context;
        EXPECT_EQ(store_out, "3") << "send(TWO) must consume no MsgSeqNum. " << context;
        EXPECT_TRUE(under_three.frames.empty())
            << "nothing of send(TWO) may be stored. " << context;
        return;
    }
    // The send is not refused before it starts: its own toApp runs, after toApp(ONE)
    // returned. With a total emission the chain's terminal-only filter dropped it.
    // With throw_if_cancelled off the cancellation is recorded, and the test the
    // deferred path applies before it starts the send does not hold.
    EXPECT_EQ(classification, "deferred") << context;
    // What the send does from there on. For the throw_if_cancelled arm the design
    // note does not say what the send returns after its toApp; these lines pin it.
    EXPECT_EQ(describe(second), "ok") << context;
    expect_one_then_two(sent, context);
    EXPECT_EQ(next_out(sess), 4U) << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

}  // namespace

TEST(CallbackSend, DeferredSendCancelled_Terminal) {
    run_deferred_send_cancelled(Emission::terminal);
}

TEST(CallbackSend, DeferredSendCancelled_Total) { run_deferred_send_cancelled(Emission::total); }

TEST(CallbackSend, DeferredSendCancelled_Terminal_ThrowIfCancelledOff) {
    run_deferred_send_cancelled(Emission::terminal_with_throw_if_cancelled_off);
}

namespace {

enum class SecondSend { overlapping, started_in_to_app };

// A store that breaks the ordering requirement on MessageStore::store: its first
// store() call after the session is Active completes in the store and then does not
// return to the session until message TWO has been transmitted. The session
// transmits a frame when that frame's store() call returns, so TWO goes out before
// ONE although ONE was started, and numbered, first. This is what such a store
// costs; it is not a wanted behaviour.
void run_store_completing_out_of_order(SecondSend shape) {
    Fixture f{ExecKind::io_context, StoreKind::none};
    bool armed = false;
    int stores_while_armed = 0;
    const auto factory = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    factory->hooks.on_outbound_store = [&] { return armed && ++stores_while_armed == 1; };
    factory->hooks.release_when = [&] { return frame_with_id(f.wire, kIdTwo) != nullptr; };
    auto cfg = f.cfg();
    cfg.store_factory = factory;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    armed = true;

    const auto ex = sess.executor().underlying();
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);
    const std::size_t wire_before = f.wire.size();

    SendOutcome first;
    SendOutcome second;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    if (shape == SecondSend::started_in_to_app) {
        f.app->on_to_app = [&] {
            if (f.app->to_app_calls == 1) {
                outer_live = true;
                asio::co_spawn(ex, sess.send(two), record_into(second));
                outer_live = false;
            } else if (f.app->to_app_calls == 2) {
                inner_saw_outer_live = outer_live;
            }
        };
        asio::co_spawn(ex, sess.send(one), record_into(first));
    } else {
        asio::post(ex, [&] {
            asio::co_spawn(ex, sess.send(one), record_into(first));
            asio::co_spawn(ex, sess.send(two), record_into(second));
        });
    }
    if (!f.pump_done([&] { return first.done && second.done; }, "CallbackSend::out_of_order")) {
        return;
    }

    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        "Transmitted, in wire order: " + describe(sent) + "; send(ONE): " + describe(first) +
        "; send(TWO): " + describe(second) +
        "; outbound store() calls after Active: " + std::to_string(stores_while_armed) +
        "; the held call was released by its bound: " +
        (factory->log->hold_timed_out ? "yes" : "no") +
        "; NextNumOut=" + std::to_string(next_out(sess)) +
        "; state=" + std::to_string(static_cast<int>(sess.state())) + ".";

    // Witness: the first store() call was held and released by TWO's transmit, and
    // in the second shape TWO was started in ONE's toApp and deferred.
    ASSERT_EQ(stores_while_armed, 2)
        << "PATH NOT DRIVEN: each send must make one store() call. " << context;
    ASSERT_FALSE(factory->log->hold_timed_out)
        << "PATH NOT DRIVEN: the held store() call was released by its bound, not by the "
           "transmit of message TWO. "
        << context;
    if (shape == SecondSend::started_in_to_app) {
        ASSERT_EQ(classify(inner_saw_outer_live), "deferred")
            << "PATH NOT DRIVEN: send(TWO) was not deferred out of toApp(ONE). " << context;
    }

    EXPECT_EQ(describe(first), "ok") << context;
    EXPECT_EQ(describe(second), "ok") << context;
    ASSERT_EQ(sent.size(), 2U) << "exactly two frames must be transmitted. " << context;
    EXPECT_EQ(field(sent[0], 11), kIdTwo)
        << "message TWO, whose store() call returned first, is transmitted first. " << context;
    EXPECT_EQ(seq_of(sent[0]), 3U) << "message TWO was numbered second. " << context;
    EXPECT_EQ(field(sent[1], 11), kIdOne) << context;
    EXPECT_EQ(seq_of(sent[1]), 2U) << "message ONE was numbered first. " << context;
    EXPECT_EQ(next_out(sess), 4U) << context;
}

}  // namespace

TEST(CallbackSend, StoreCompletingOutOfOrder_OverlappingSends) {
    run_store_completing_out_of_order(SecondSend::overlapping);
}

TEST(CallbackSend, StoreCompletingOutOfOrder_SendStartedInToApp) {
    run_store_completing_out_of_order(SecondSend::started_in_to_app);
}

// toApp for ONE starts a detached send over a heap buffer and frees the buffer
// before it returns. A send started in toApp builds its frame after toApp returned,
// so it must have taken its own copy of the payload first. The buffer is on the
// heap because a memory checker sees a read of a freed heap block without further
// options.
TEST(CallbackSend, ToAppStartsSend_OverABufferItFrees) {
    Fixture f{ExecKind::io_context};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";

    const auto ex = sess.executor().underlying();
    const auto one = bytes_of(kPayloadOne);
    constexpr std::string_view kHeapId = "B41-HEAP";
    constexpr std::string_view kHeapPayload =
        "35=D\x01"
        "11=B41-HEAP\x01"
        "55=HHH\x01";
    const std::size_t wire_before = f.wire.size();

    SendOutcome first;
    bool outer_live = false;
    std::optional<bool> inner_saw_outer_live;
    f.app->on_to_app = [&] {
        if (f.app->to_app_calls != 1) {
            if (!inner_saw_outer_live.has_value()) inner_saw_outer_live = outer_live;
            return;
        }
        outer_live = true;
        auto heap = std::make_unique<std::vector<std::byte>>(bytes_of(kHeapPayload));
        asio::co_spawn(ex, sess.send(std::span<const std::byte>{*heap}), asio::detached);
        heap.reset();
        outer_live = false;
    };
    asio::co_spawn(ex, sess.send(one), record_into(first));
    // The detached send has no completion to wait for: its frame on the wire ends
    // the wait.
    if (!f.pump_done([&] { return first.done && frame_with_id(f.wire, kHeapId) != nullptr; },
                     "CallbackSend::freed_buffer")) {
        return;
    }

    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context = "The detached send's toApp: " + classify(inner_saw_outer_live) +
                                ". Transmitted, in wire order: " + describe(sent) +
                                "; send(ONE): " + describe(first) +
                                "; NextNumOut=" + std::to_string(next_out(sess)) + ".";

    // Witness: the send built its frame after toApp(ONE) had freed the buffer.
    ASSERT_EQ(classify(inner_saw_outer_live), "deferred")
        << "PATH NOT DRIVEN: the send did not start after toApp(ONE) returned, so it read the "
           "buffer before the buffer was freed. "
        << context;

    const std::string* heap_frame = frame_with_id(sent, kHeapId);
    ASSERT_NE(heap_frame, nullptr) << context;
    EXPECT_EQ(field(*heap_frame, 35), "D") << context;
    // The payload's fields after MsgType, contiguous and in order.
    EXPECT_NE(heap_frame->find("\x01"
                               "11=B41-HEAP\x01"
                               "55=HHH\x01"),
              std::string::npos)
        << "the transmitted frame must carry the payload's bytes. " << context;
    EXPECT_EQ(seq_of(*heap_frame), 3U) << context;
    EXPECT_EQ(describe(first), "ok") << context;
}

namespace {

// The most handlers the callback's loop below runs. A loop that ends because no
// handler is ready ends below it. A deferred send that posted itself again each
// time it resumed would keep one handler ready for ever, and the loop would end
// here instead.
constexpr std::size_t kPollBound = 64;

struct PollInsideToApp {
    bool entered = false;
    // The handler the callback posts onto the bare io_context: whether it ran while
    // the callback was on the stack. It is the control of the callback's loop.
    std::optional<bool> control_ran_inside;
    std::size_t polled = 0;
    bool bound_hit = false;
    std::optional<bool> inner_saw_outer_live;
    SendOutcome first;
    SendOutcome second;
    // Called by the callback around its loop.
    std::function<void()> before_poll;
    std::function<void()> after_poll;
};

// send(ONE) on the session's executor. Its toApp starts send(TWO) there, posts a
// handler onto the bare io_context, and then runs the io_context one handler at a
// time until none is ready or kPollBound have run. The second send's own toApp
// starts nothing and runs nothing. Returns once both sends completed or the pump's
// budget ran out.
[[nodiscard]] bool start_a_send_in_to_app_then_run_the_io_context(Fixture& f, Session& sess,
                                                                  PollInsideToApp& r) {
    const auto ex = sess.executor().underlying();
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);
    bool outer_live = false;
    f.app->on_to_app = [&] {
        if (f.app->to_app_calls != 1) {
            if (!r.inner_saw_outer_live.has_value()) r.inner_saw_outer_live = outer_live;
            return;
        }
        r.entered = true;
        outer_live = true;
        asio::co_spawn(ex, sess.send(two), record_into(r.second));
        asio::post(f.ioc.get_executor(), [&] { r.control_ran_inside = outer_live; });
        if (r.before_poll) r.before_poll();
        while (r.polled < kPollBound && f.ioc.poll_one() != 0) ++r.polled;
        r.bound_hit = r.polled >= kPollBound;
        if (r.after_poll) r.after_poll();
        outer_live = false;
    };
    asio::co_spawn(ex, sess.send(one), record_into(r.first));
    return f.pump_done([&] { return r.first.done && r.second.done; },
                       "CallbackSend::poll_in_to_app");
}

std::string describe(const PollInsideToApp& r) {
    return "send(TWO)'s toApp: " + classify(r.inner_saw_outer_live) +
           "; the handler posted onto the bare io_context: " + classify(r.control_ran_inside) +
           "; the callback's loop ran " + std::to_string(r.polled) + " handler(s), bound " +
           std::to_string(kPollBound) + "; send(ONE): " + describe(r.first) +
           "; send(TWO): " + describe(r.second) + ".";
}

}  // namespace

// direct_executor, attested, over the bare io_context executor: a handler posted to
// the session's executor is one the callback's own run of the io_context can run. So
// the send started in toApp resumes inside toApp and, with the assertions compiled
// out, runs nested. This cell pins behaviour outside the contract (the file header
// has the pointer). It is there only so that a deferred send which defers again each
// time it resumes fails a cell: the callback's loop then ends at its bound.
TEST(CallbackSend, ToAppRunsTheIoContext_DirectExecutor_SendNestsAndThePollEnds) {
#ifndef NDEBUG
    GTEST_SKIP() << "on a build with assertions this setup aborts: "
                    "CallbackGuard.DeferredSendResumedInsideToAppDies";
#else
    Fixture f{ExecKind::io_context};
    f.direct_executor = true;
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    PollInsideToApp r;
    if (!start_a_send_in_to_app_then_run_the_io_context(f, sess, r)) return;
    const std::string context = describe(r);

    ASSERT_TRUE(r.entered) << "PATH NOT DRIVEN: toApp(ONE) never ran. " << context;
    ASSERT_EQ(classify(r.control_ran_inside), "nested")
        << "PATH NOT DRIVEN: the callback's loop did not run a handler posted onto the "
           "io_context. "
        << context;
    EXPECT_FALSE(r.bound_hit) << "the callback's loop must end below its bound. " << context;
    EXPECT_EQ(classify(r.inner_saw_outer_live), "nested") << context;
#endif
}

// The same callback on a session in the default mode, with a strand as the
// fixture's executor: the session's strand is a second strand, made over that one.
// A post onto the session's strand is not a handler the callback's run of the
// io_context can run while the strand's own handler holds the callback.
TEST(CallbackSend, ToAppRunsTheIoContext_OverAStrand_SendIsDeferred) {
    Fixture f{ExecKind::strand};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    const std::size_t wire_before = f.wire.size();
    PollInsideToApp r;
    if (!start_a_send_in_to_app_then_run_the_io_context(f, sess, r)) return;
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    const std::string context =
        describe(r) + " Transmitted, in wire order: " + describe(sent) + ".";

    ASSERT_TRUE(r.entered) << "PATH NOT DRIVEN: toApp(ONE) never ran. " << context;
    ASSERT_EQ(classify(r.control_ran_inside), "nested")
        << "PATH NOT DRIVEN: the callback's loop did not run a handler posted onto the "
           "io_context, so it cannot show that it does not run the send. "
        << context;
    EXPECT_FALSE(r.bound_hit) << context;
    EXPECT_EQ(classify(r.inner_saw_outer_live), "deferred")
        << "the send must start after toApp(ONE) returned. " << context;
    EXPECT_EQ(describe(r.first), "ok") << context;
    EXPECT_EQ(describe(r.second), "ok") << context;
    expect_one_then_two(sent, context);
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

// ── Suite CallbackGuard: the debug guard (`.specify/563-564-callback-send.md` §2.2) ──
//
// Builds with asserts only. A death cell runs its statement in a child process and
// requires an abort whose output holds a piece of ONE assertion's text. The pieces
// are spelled out here and not taken from session.hpp or session.cpp: an expectation
// read from its subject cannot fail. Each is a substring of a single string literal
// of the assertion, because the C library prints adjacent literals unjoined.
//
// A statement that reaches its end without an abort leaves through std::exit, with
// the exit codes its comment gives, so that a missing death is a failed assertion
// of the cell and not a hang or a teardown over a detached coroutine.

#ifdef __has_feature
#if __has_feature(thread_sanitizer)
#define B41_BUILT_WITH_TSAN 1
#endif
#endif
#ifdef __SANITIZE_THREAD__
#define B41_BUILT_WITH_TSAN 1
#endif

#ifndef NDEBUG
namespace {

// The owner token's assertion (callback_owner_hold / callback_dispatch_scope).
constexpr const char* kOwnerTokenText = "concurrent session callback entry";
// The assertion of a function that parses over the session's inbound parse buffer.
constexpr const char* kParseSiteText = "inbound parse begun inside a callback";
// The assertion of the parse_and_dispatch_ overload that calls toApp and toAdmin.
constexpr const char* kOutboundEntryText = "toApp or toAdmin entered inside toApp or toAdmin";
// The assertion at the onLogon and onLogout sites.
constexpr const char* kLifecycleEntryText = "onLogon or onLogout entered inside toApp or toAdmin";
// The assertion of the deferred path of Session::send, where it resumes after its post.
constexpr const char* kDeferredSendText = "deferred send resumed inside toApp or toAdmin";
// The assertion of Session::close, where it resumes after its wait.
constexpr const char* kWaitedCloseText = "close resumed inside a callback";

// Lines a statement writes to stderr around the step that is expected to abort.
constexpr const char* kLeftNestedScopeLine = "[ B41 ] the first thread has left its nested scope";
constexpr const char* kStepBeginsLine = "[ B41 ] the callback begins the step";
constexpr const char* kStepReturnedLine = "[ B41 ] the callback's step returned";

void mark(const char* line) {
    std::fprintf(stderr, "%s\n", line);
    std::fflush(stderr);
}

// The abort came after the callback began its step and before the step returned,
// that is, with the callback on the stack; and it carries `text`.
auto died_inside_the_step_at(const char* text) {
    return ::testing::AllOf(::testing::HasSubstr(kStepBeginsLine), ::testing::HasSubstr(text),
                            ::testing::Not(::testing::HasSubstr(kStepReturnedLine)));
}

// ── NC-9: the owner token, through session_test_access::enter_callback_scope ──
//
// The statements enter and leave scopes on a session that was never opened, so the
// guard's state is the only session state their threads share. A statement that
// starts threads starts them inside the death statement.

// A first thread enters a scope and stays inside; a second thread enters after the
// first has told it so through a promise. With `first_nests_and_leaves` the first
// thread has by then entered a nested scope, left it, and printed
// kLeftNestedScopeLine. Returns only if the second thread's entry did not abort.
void enter_from_a_second_thread_while_a_first_is_inside(bool first_nests_and_leaves) {
    fixpp::core::EngineConfig engine;
    Session sess{engine, SessionConfig{}};
    std::promise<void> first_is_inside;
    std::promise<void> release_first;
    std::thread first{[&] {
        [[maybe_unused]] const auto outer = session_test_access::enter_callback_scope(sess);
        if (first_nests_and_leaves) {
            {
                [[maybe_unused]] const auto nested =
                    session_test_access::enter_callback_scope(sess);
            }
            mark(kLeftNestedScopeLine);
        }
        first_is_inside.set_value();
        release_first.get_future().wait();
    }};
    first_is_inside.get_future().wait();
    std::thread second{[&] {
        [[maybe_unused]] const auto scope = session_test_access::enter_callback_scope(sess);
    }};
    second.join();
    release_first.set_value();
    first.join();
}

// One thread enters a scope, enters a nested one, leaves both, and enters again.
// Exit code 0: no assertion fired.
[[noreturn]] void enter_nested_leave_both_and_enter_again() {
    fixpp::core::EngineConfig engine;
    Session sess{engine, SessionConfig{}};
    {
        [[maybe_unused]] const auto outer = session_test_access::enter_callback_scope(sess);
        {
            [[maybe_unused]] const auto nested = session_test_access::enter_callback_scope(sess);
        }
    }
    {
        [[maybe_unused]] const auto again = session_test_access::enter_callback_scope(sess);
    }
    std::exit(0);  // NOLINT(concurrency-mt-unsafe)
}

#ifdef B41_BUILT_WITH_TSAN
// As the promise statement, but the only ordering between the two threads is a flag
// that the first stores and the second loads with memory_order_relaxed, which orders
// nothing for ThreadSanitizer. The first thread stays inside its scope until the
// second has either died or passed. The main thread starts both and then joins
// them, so the death statement does not return first; a join orders nothing between
// the two threads. Returns only if the second thread's entry did not abort.
void enter_from_a_second_thread_ordered_by_a_relaxed_flag_alone() {
    fixpp::core::EngineConfig engine;
    Session sess{engine, SessionConfig{}};
    std::atomic<bool> first_is_inside{false};
    std::atomic<bool> second_passed{false};
    std::thread first{[&] {
        [[maybe_unused]] const auto scope = session_test_access::enter_callback_scope(sess);
        first_is_inside.store(true, std::memory_order_relaxed);
        while (!second_passed.load(std::memory_order_relaxed)) std::this_thread::yield();
    }};
    std::thread second{[&] {
        while (!first_is_inside.load(std::memory_order_relaxed)) std::this_thread::yield();
        {
            [[maybe_unused]] const auto scope = session_test_access::enter_callback_scope(sess);
        }
        second_passed.store(true, std::memory_order_relaxed);
    }};
    first.join();
    second.join();
}
#endif

// ── NC-19: a frame fed from inside a callback ─────────────────────────────────

enum class FedFrom { to_app, to_admin, on_logon };
enum class FedFrame {
    // An application frame under the session's BeginString, with the inbound number
    // the session expects when the callback feeds it: its arm reaches a parse.
    next_expected_application,
    // The same frame under another BeginString: the Active arm disconnects the
    // session at its BeginString test, with no parse over the buffer and no emit.
    other_begin_string,
};

// A callback of the session feeds one frame to on_inbound_frame, by a co_spawn on
// the session's executor, which begins inline. The callback is the toApp of a send
// on an Active session, the toAdmin of the Heartbeat that answers the peer's
// TestRequest, or the onLogon that the peer's Logon fires.
// Exit codes: 2 = setup failed; 3 = the feeding callback never ran; 0 = the feed
// returned without an abort.
[[noreturn]] void feed_a_frame_from_inside_a_callback(FedFrom from, FedFrame what) {
    Fixture f{ExecKind::io_context};
    Session sess{f.engine, f.cfg()};
    // std::exit reports this death-test child's verdict as its exit code.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    const bool up = from == FedFrom::on_logon
                        ? f.run(sess.open(), "CallbackGuard::open").has_value()
                        : f.open_to_active(sess);
    if (!up) std::exit(2);
    const auto ex = sess.executor().underlying();
    std::vector<std::byte> fed_frame;
    bool fed = false;
    const auto feed = [&] {
        fed = true;
        fed_frame = peer_frame_with_begin(
            what == FedFrame::other_begin_string ? std::string_view{"FIX.4.4"} : kBegin, "D",
            static_cast<std::uint32_t>(next_in(sess)),
            "11=PEER-FED\x01"
            "54=1\x01"
            "55=AAPL\x01");
        mark(kStepBeginsLine);
        asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{fed_frame}),
                       asio::detached);
        mark(kStepReturnedLine);
    };
    f.app->on_to_app = [&] {
        if (from == FedFrom::to_app && !fed) feed();
    };
    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (from == FedFrom::to_admin && !fed && msg_type == "0") feed();
    };
    f.app->on_lifecycle = [&](const char* name) {
        if (from == FedFrom::on_logon && !fed && std::string_view{name} == "onLogon") feed();
    };

    const auto payload = bytes_of(kPayloadOne);
    const auto test_request = peer_frame("1", 2, "112=B41-TR\x01");
    const auto logon = peer_frame("A", 1,
                                  "98=0\x01"
                                  "108=30\x01");
    SendOutcome outer;
    if (from == FedFrom::to_app) {
        asio::co_spawn(ex, sess.send(payload), record_into(outer));
    } else {
        const auto& frame = from == FedFrom::to_admin ? test_request : logon;
        asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{frame}),
                       record_into(outer));
    }
    (void)fixpp::test_support::pump_until(
        f.ioc, [&] { return outer.done; }, std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
        "CallbackGuard::feed_from_inside_a_callback");
    std::exit(fed ? 0 : 3);
    // NOLINTEND(concurrency-mt-unsafe)
}

// ── NC-20: an outbound callback inside an outbound callback ───────────────────

// direct_executor, attested, over the bare io_context executor, with a heartbeat
// interval. A send from the statement; its toApp advances the mock clock by the
// interval and then runs the io_context, so the liveness loop wakes and begins its
// emit, whose toAdmin is entered with toApp on the stack.
// Exit codes: 2 = setup failed; 3 = toApp never ran; 0 = the callback's poll()
// returned without an abort.
[[noreturn]] void run_the_io_context_inside_to_app_after_the_heartbeat_interval() {
    constexpr std::chrono::seconds kInterval{30};
    Fixture f{ExecKind::io_context};
    f.direct_executor = true;
    f.heartbeat_interval = kInterval;
    Session sess{f.engine, f.cfg()};
    // std::exit reports this death-test child's verdict as its exit code.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (!f.open_to_active(sess)) std::exit(2);
    const auto ex = sess.executor().underlying();
    bool entered = false;
    f.app->on_to_app = [&] {
        if (entered) return;
        entered = true;
        f.clock->advance(kInterval);
        mark(kStepBeginsLine);
        (void)f.ioc.poll();
        mark(kStepReturnedLine);
    };
    const auto payload = bytes_of(kPayloadOne);
    SendOutcome outer;
    asio::co_spawn(ex, sess.send(payload), record_into(outer));
    (void)fixpp::test_support::pump_until(
        f.ioc, [&] { return outer.done; }, std::chrono::seconds{5}, fixpp::test_support::kPumpSlice,
        "CallbackGuard::poll_inside_to_app");
    std::exit(entered ? 0 : 3);
    // NOLINTEND(concurrency-mt-unsafe)
}

// ── NC-16: a deferred send that resumes inside the callback that started it ────

// direct_executor, attested, over the bare io_context executor. A send from the
// statement; its toApp starts a second send and then runs the io_context, so the
// second send resumes after its post with toApp on the stack.
// Exit codes: 2 = setup failed; 3 = toApp never ran; 0 = the callback's loop
// returned without an abort.
[[noreturn]] void run_the_io_context_inside_to_app_after_starting_a_send() {
    Fixture f{ExecKind::io_context};
    f.direct_executor = true;
    Session sess{f.engine, f.cfg()};
    // std::exit reports this death-test child's verdict as its exit code.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (!f.open_to_active(sess)) std::exit(2);
    PollInsideToApp r;
    r.before_poll = [] { mark(kStepBeginsLine); };
    r.after_poll = [] { mark(kStepReturnedLine); };
    (void)start_a_send_in_to_app_then_run_the_io_context(f, sess, r);
    std::exit(r.entered ? 0 : 3);
    // NOLINTEND(concurrency-mt-unsafe)
}

// ── NC-16, second arm: a waiting close that resumes inside the callback that started it ──

// direct_executor, attested, over the bare io_context executor. The peer's 35=D is
// delivered from the statement; its fromApp starts a terminal close and then runs the
// io_context, so the close resumes after its wait with fromApp on the stack.
// Exit codes: 2 = setup failed; 3 = fromApp never ran; 0 = the callback's loop
// returned without an abort.
[[noreturn]] void run_the_io_context_inside_from_app_after_starting_a_close() {
    Fixture f{ExecKind::io_context};
    f.direct_executor = true;
    Session sess{f.engine, f.cfg()};
    // std::exit reports this death-test child's verdict as its exit code.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (!f.open_to_active(sess)) std::exit(2);
    const auto ex = sess.executor().underlying();
    bool entered = false;
    SendOutcome inbound;
    SendOutcome close_op;
    f.app->on_from_app = [&] {
        if (entered) return;
        entered = true;
        asio::co_spawn(ex, sess.close(close_mode::terminal), record_into(close_op));
        mark(kStepBeginsLine);
        std::size_t polled = 0;
        while (polled < kPollBound && f.ioc.poll_one() != 0) ++polled;
        mark(kStepReturnedLine);
    };
    const auto order = peer_frame("D", 2,
                                  "11=PEER-1\x01"
                                  "54=1\x01"
                                  "55=AAPL\x01");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{order}),
                   record_into(inbound));
    (void)fixpp::test_support::pump_until(
        f.ioc, [&] { return inbound.done && close_op.done; }, std::chrono::seconds{5},
        fixpp::test_support::kPumpSlice, "CallbackGuard::poll_inside_from_app");
    std::exit(entered ? 0 : 3);
    // NOLINTEND(concurrency-mt-unsafe)
}

}  // namespace
#endif  // !NDEBUG

// NC-9 (a). A first thread is inside; a second enters after a handshake. This shows
// that the second thread's entry is detected. It does not show that the detection is
// free of a data race: the handshake orders the first thread's write before the
// second thread's read, for ThreadSanitizer as for the hardware, so a token built as
// a plain member passes this cell on every preset. The relaxed-flag cell below,
// which runs under ThreadSanitizer only, is the witness of the token's atomicity.
TEST(CallbackGuard, ASecondThreadsEntryDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the owner token's assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(enter_from_a_second_thread_while_a_first_is_inside(false),
                 ::testing::HasSubstr(kOwnerTokenText));
#endif
}

// NC-9 (b). The first thread has entered a nested scope and left it; its outer scope
// is still live when the second thread enters. The line the first thread prints
// after its nested scope ended is part of the match, so a death at the nested entry
// itself does not pass.
TEST(CallbackGuard, ASecondThreadsEntryDiesAfterANestedScopeEnded) {
#ifdef NDEBUG
    GTEST_SKIP() << "the owner token's assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(enter_from_a_second_thread_while_a_first_is_inside(true),
                 ::testing::AllOf(::testing::HasSubstr(kLeftNestedScopeLine),
                                  ::testing::HasSubstr(kOwnerTokenText)));
#endif
}

// NC-9 (c). Control: one thread enters a nested scope, leaves both, and enters
// again, and no assertion fires.
TEST(CallbackGuard, NestedEntryOnOneThreadPasses) {
#ifdef NDEBUG
    GTEST_SKIP() << "the guard's assertions are compiled out under NDEBUG";
#else
    EXPECT_EXIT(enter_nested_leave_both_and_enter_again(), ::testing::ExitedWithCode(0), "");
#endif
}

// NC-9 (d). The second thread's entry dies at the owner token's assertion, and the
// detection itself draws no ThreadSanitizer report. The child is a fresh process
// (the threadsafe style), so no thread of this binary's other cells is in it.
TEST(CallbackGuard, ASecondThreadsEntryDiesWithNoDataRaceOnTheToken) {
#if defined(B41_BUILT_WITH_TSAN) && !defined(NDEBUG)
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(enter_from_a_second_thread_ordered_by_a_relaxed_flag_alone(),
                 ::testing::AllOf(::testing::HasSubstr(kOwnerTokenText),
                                  ::testing::Not(::testing::HasSubstr("ThreadSanitizer"))));
#else
    GTEST_SKIP() << "needs ThreadSanitizer and a build with assertions";
#endif
}

// NC-19, the dispatch arms. The fed frame is in sequence, so its arm reaches the
// parse over the session's buffer, with the feeding callback on the stack.
TEST(CallbackGuard, AFrameFedFromToAppDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the parse-site assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(
        feed_a_frame_from_inside_a_callback(FedFrom::to_app, FedFrame::next_expected_application),
        died_inside_the_step_at(kParseSiteText));
#endif
}

TEST(CallbackGuard, AFrameFedFromToAdminDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the parse-site assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(
        feed_a_frame_from_inside_a_callback(FedFrom::to_admin, FedFrame::next_expected_application),
        died_inside_the_step_at(kParseSiteText));
#endif
}

TEST(CallbackGuard, AFrameFedFromOnLogonDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the parse-site assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(
        feed_a_frame_from_inside_a_callback(FedFrom::on_logon, FedFrame::next_expected_application),
        died_inside_the_step_at(kParseSiteText));
#endif
}

// NC-19, the lifecycle arm. The fed frame disconnects the session with no parse over
// the buffer and no emit, so onLogout is entered inside toApp.
TEST(CallbackGuard, OnLogoutEnteredInsideToAppDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the lifecycle sites' assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(feed_a_frame_from_inside_a_callback(FedFrom::to_app, FedFrame::other_begin_string),
                 died_inside_the_step_at(kLifecycleEntryText));
#endif
}

// NC-20. The liveness loop's toAdmin is entered inside toApp.
TEST(CallbackGuard, ToAdminEnteredInsideToAppDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the outbound-entry assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(run_the_io_context_inside_to_app_after_the_heartbeat_interval(),
                 died_inside_the_step_at(kOutboundEntryText));
#endif
}

// NC-16, first arm. The abort must come from the deferred path's own assertion,
// inside the callback's loop. If the send went on from there to its own toApp, the
// process would abort with the outbound-entry text, which this cell does not accept.
TEST(CallbackGuard, DeferredSendResumedInsideToAppDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the deferred path's assertion is compiled out under NDEBUG";
#else
    EXPECT_DEATH(run_the_io_context_inside_to_app_after_starting_a_send(),
                 died_inside_the_step_at(kDeferredSendText));
#endif
}

// NC-16, second arm. The abort must come from the assertion of the close's wait,
// inside the callback's loop. A run in which that assertion does not fire leaves
// through std::exit, which this cell does not accept.
TEST(CallbackGuard, WaitedCloseResumedInsideFromAppDies) {
#ifdef NDEBUG
    GTEST_SKIP() << "the assertion of the close's wait is compiled out under NDEBUG";
#else
    EXPECT_DEATH(run_the_io_context_inside_from_app_after_starting_a_close(),
                 died_inside_the_step_at(kWaitedCloseText));
#endif
}

#ifdef __has_feature
#if __has_feature(address_sanitizer)
#define B41_BUILT_WITH_ASAN 1
#endif
#endif
#ifdef __SANITIZE_ADDRESS__
#define B41_BUILT_WITH_ASAN 1
#endif

// Not a measurement of fixpp: see the file header.
TEST(CallbackSend, DISABLED_MemoryCheckerPositiveControl_UseAfterFree) {
#ifdef B41_BUILT_WITH_ASAN
    const bool run = true;
#else
    const char* opt_in =
        std::getenv("B41_RUN_UNDER_MEMORY_CHECKER");  // NOLINT(concurrency-mt-unsafe)
    const bool run = opt_in != nullptr && std::string_view{opt_in} == "1";
#endif
    if (!run) GTEST_SKIP() << "not built with AddressSanitizer, and not opted in for a checker";
    // The read of the deleted block is the fault this control plants for the memory checker.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory,clang-analyzer-cplusplus.NewDelete)
    auto* block = new int[4]{1, 2, 3, 4};
    int* volatile dangling = block;
    delete[] block;
    std::printf("[ B41 ] reading a deleted heap block: %d\n", dangling[1]);
    // NOLINTEND(cppcoreguidelines-owning-memory,clang-analyzer-cplusplus.NewDelete)
    std::fflush(stdout);
#ifdef B41_BUILT_WITH_ASAN
    FAIL() << "AddressSanitizer did not stop the read of a deleted heap block.";
#endif
}

// NOLINTEND(modernize-use-std-print)

}  // namespace fixpp::session::test
