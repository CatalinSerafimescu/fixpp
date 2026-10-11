// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_563_564_callback_send.cpp
//
// fixpp#563 and fixpp#564 (batch B28).
//
// Subject: a Session::send started from inside an Application callback, on the
// session's own executor. co_spawn's first step is a dispatch, so when the
// spawning thread is already running that executor the nested send runs inline
// until its first suspension.
//
// #563's claim: send_impl peeks the outbound MsgSeqNum before toApp, stamps the
// frame with the peeked value, and advances the counter after toApp. A send
// started inside toApp peeks the same value, so two frames carry one
// MsgSeqNum(34) while the counter moves by two.
//
// #564: on a build without NDEBUG the session's debug guard aborts the process
// where a callback is entered inside another callback of the same session in a
// way the contract does not allow, at the assertion of the site where that
// nesting begins (`.specify/563-564-callback-send.md` §2.2). A nesting the
// contract allows passes. A cell whose own process drives a nesting of the
// first kind therefore ends in that abort on such a build, and what the cell
// measures is observable only where the assertions are compiled out. No cell
// depends on another: run each alone with --gtest_filter.
//
// Cells:
//   ToAppSendsNested          #563 claim. toApp for message ONE starts a send
//                             of message TWO. nested.
//   ToAppSendsNestedOnStrand  the same cell with a strand as the session's
//                             executor, which is what an Engine gives a
//                             session. nested.
//   ToAppSendsNested_FileStore
//                             the claim cell on a persistent FileStore, where
//                             a refused store() is session-fatal. nested.
//   SequentialSends           control. The same two messages, the second
//                             started after the first completed.
//   SequentialSends_FileStore control for the FileStore cell.
//   ToAppSendsNested_NextSendIsRetained
//                             characterisation of what follows the nested
//                             pair: a third message, sent on its own after both
//                             completed, must be transmitted and retained under
//                             its MsgSeqNum. nested.
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
//                             outer one. This cell classifies the send as
//                             nested, deferred or never ran, prints that, and
//                             fails only on "never ran"; the sequence-number
//                             assertions run either way.
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
//                             in ascending MsgSeqNum order. Classified and
//                             printed like ToAdminStartsSend.
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
//                             batch B41, characterisation. fromApp starts a
//                             send whose toApp throws. Prints the session's
//                             state and lifecycle inside fromApp and after the
//                             inbound call, both results, the callbacks that
//                             ran from the throw on, the frames transmitted and
//                             the sequence numbers. Asserts only that the path
//                             was driven and that on_inbound_frame completed.
//   FromAppSendsReply_NestedToAppThrows_FileStore
//                             the same on a FileStore, where the inbound
//                             number's persistence can be read from the store.
//   FromAppSendsReply_NestedToAppThrows_FromAppRejects
//                             the same, with fromApp returning an error after
//                             it started the send, which asks the session for a
//                             BusinessMessageReject.
//   FromAppStartsClose_Terminal
//                             batch B41, characterisation; no send is involved.
//                             fromApp starts Session::close(terminal) with a
//                             co_spawn on the session's executor. Prints the
//                             state and lifecycle inside fromApp, when
//                             on_inbound_frame completed and at the end; both
//                             results; the callbacks and the two completion
//                             handlers in order; the frames transmitted; the
//                             sequence numbers. Asserts only that the path was
//                             driven and that on_inbound_frame completed.
//   FromAppStartsClose_Graceful
//                             the same with close(graceful), whose Logout goes
//                             through toAdmin. No peer Logout is fed, so the
//                             close ends on its own wait, which the cell
//                             shortens through logout_disconnect_timeout_ms.
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
//                             (counter advanced first). A graceful close emits
//                             a Logout; whether that Logout's emit starts
//                             inside the first frame's emit is what each cell
//                             records. The close is started once; the Logout's
//                             own toAdmin is only recorded. Each cell
//                             prints the frames in wire order, what the store
//                             holds under each frame's number, both outcomes,
//                             state and lifecycle at three points and the
//                             callbacks in order, then checks (a) distinct
//                             MsgSeqNums, (b) ascending wire order, (c) each
//                             frame retained under its own number, (d) store
//                             and manager agree, (e) both operations report
//                             success. The final state is not judged.
//   ToAppSendsNested_NoStore, ToAdminStartsSend_NoStore,
//   ToAdminStartsSend_AssignFirstSite_NoStore, OverlappingSends_NoStore
//                             batch B41. The nested-send and overlapping-send
//                             shapes on a session with no MessageStore
//                             (SessionConfig::store_factory left null), where
//                             store_then_emit goes straight to the transport.
//                             This fixture's capture is a synchronous function,
//                             so the write is not a suspension point here; the
//                             cells record whether the nested send transmits
//                             before the starting callback returns. (c) and (d)
//                             do not apply.
//   the same four with _LiveWrite
//                             the same with a live transport attached, whose
//                             async_write records the frame and then suspends
//                             once, so the send path goes through the write
//                             gate and yields there.
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
//
// Running the callback-starts-close cells under a memory checker: this fixture
// keeps the Session on the test's stack for the whole cell, so no cell frees the
// Session while one of its arms is running or suspended. What a sanitizer can
// see here is a read or write of something close() itself released.
//
// Witness, asserted first in every nested cell: the nested send's toApp ran
// while the outer callback was still on the stack. Without it a pass of the
// sequence-number assertions would say nothing about the nested path, so a
// miss is reported as PATH NOT DRIVEN, with one message for "never ran" and
// another for "ran after the outer callback returned" (the send was deferred).
// A change that defers nested sends moves these cells to the second message;
// the witness then has to be re-decided together with the contract.
//
// Store: a volatile bounded MemoryStore, except in the cells named _FileStore.
// The interleaving of the two sends depends on where a send first suspends,
// and that is inside the store (MemoryStore::store starts with a post). A
// session with no store has no such suspension and is not covered. A
// persistent store turns a refused store() into a session-fatal error, so the
// _FileStore claim cell is expected to fail differently from the volatile one.
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
// which sets an interval and advances the mock clock itself.

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
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
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
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
    return std::string(frame.substr(pos, end == std::string_view::npos ? frame.npos : end - pos));
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
    // close() calls on the transport, and the writes begun after the first one.
    std::size_t closes = 0;
    std::size_t begun_after_close = 0;
    // Set it before the transport is attached. True: a write begun after close()
    // returns transport_write_error, is not counted in `started` and hands
    // nothing to `on_write`, as a closed socket does. False: such a write is
    // handed to `on_write` like any other, so a cell whose subject is a write
    // that can follow a close sets this.
    bool refuse_after_close = false;
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
            if (stats_.refuse_after_close) co_return std::unexpected{error::transport_write_error};
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
    // the loop then runs on `exec` and not on the session's strand; a cell that
    // needs it on the session's strand feeds the Logon itself, on
    // sess.executor().underlying().
    std::chrono::seconds heartbeat_interval{0};
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
        const auto logon = peer_frame("A", 1,
                                      "98=0\x01"
                                      "108=30\x01");
        if (!run(s.on_inbound_frame(std::span<const std::byte>{logon}), "CallbackSend::logon")) {
            return false;
        }
        return s.state() == fsm_state::Active;
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

enum class Shape { nested_in_to_app, sequential };

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
    // The session's NextNumOut as the nested path sees it: inside the nested
    // send's toApp, and in the outer toApp once the nested co_spawn returned.
    // Printed with every failure, not asserted.
    std::string seen_in_nested_to_app = "not recorded";
    std::string seen_after_spawn = "not recorded";

    if (shape == Shape::nested_in_to_app) {
        f.app->on_to_app = [&] {
            if (f.app->to_app_calls == 1) {
                outer_live = true;
                asio::co_spawn(ex, sess.send(two), record_into(second));
                outer_live = false;
                seen_after_spawn =
                    std::to_string(next_out(sess)) +
                    (second.done ? " (nested send completed)" : " (nested send pending)");
            } else if (f.app->to_app_calls == 2) {
                inner_saw_outer_live = outer_live;
                seen_in_nested_to_app = std::to_string(next_out(sess));
            }
        };
        asio::co_spawn(ex, sess.send(one), record_into(first));
        if (!f.pump_done([&] { return first.done && second.done; }, "CallbackSend::nested")) {
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
    const std::string context = "Transmitted, in wire order: " + describe(sent) +
                                "; send(ONE): " + describe(first) +
                                "; send(TWO): " + describe(second) +
                                "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
                                ", store=" + f.store_next_out(sess) +
                                "; state=" + std::to_string(static_cast<int>(sess.state())) +
                                "; NextNumOut in the nested toApp=" + seen_in_nested_to_app +
                                ", after the nested co_spawn returned=" + seen_after_spawn + ".";

    // (a) Witness.
    if (shape == Shape::nested_in_to_app) {
        ASSERT_TRUE(inner_saw_outer_live.has_value())
            << "PATH NOT DRIVEN: the nested send's toApp never ran. " << context;
        ASSERT_TRUE(*inner_saw_outer_live)
            << "PATH NOT DRIVEN (the send was deferred): the nested send's toApp ran only after "
               "the outer toApp had returned. "
            << context;
    }
    EXPECT_EQ(f.app->to_app_calls, 2) << "each send must reach toApp once. " << context;

    if (then == Then::third_send) {
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

    // (c) The store retains each frame under the MsgSeqNum it was sent with.
    expect_retained_as_sent(f, sess, *sent_one, context);
    expect_retained_as_sent(f, sess, *sent_two, context);

    // (d) Two sends consume two sequence numbers.
    EXPECT_EQ(next_out(sess), 4U) << "(d) NextNumOut must have advanced by two. " << context;
    EXPECT_EQ(sess.state(), fsm_state::Active) << context;
}

}  // namespace

TEST(CallbackSend, ToAppSendsNested) {
    run_two_sends(Shape::nested_in_to_app, ExecKind::io_context);
}

TEST(CallbackSend, ToAppSendsNestedOnStrand) {
    run_two_sends(Shape::nested_in_to_app, ExecKind::strand);
}

TEST(CallbackSend, ToAppSendsNested_FileStore) {
    run_two_sends(Shape::nested_in_to_app, ExecKind::io_context, Then::check_pair, StoreKind::file);
}

TEST(CallbackSend, SequentialSends) { run_two_sends(Shape::sequential, ExecKind::io_context); }

TEST(CallbackSend, SequentialSends_FileStore) {
    run_two_sends(Shape::sequential, ExecKind::io_context, Then::check_pair, StoreKind::file);
}

TEST(CallbackSend, ToAppSendsNested_NextSendIsRetained) {
    run_two_sends(Shape::nested_in_to_app, ExecKind::io_context, Then::third_send);
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
    f.memory_outbound_capacity = 2 * kPairs + 16;
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
        const auto want_a = static_cast<seqnum_t>(2 + 2 * i);
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
            first_anomaly = "pair " + std::to_string(i) + ": transmitted " + describe(sent) +
                            "; send(A): " + describe(a) + "; send(B): " + describe(b) +
                            "; NextNumOut after A's co_spawn returned=" + out_after_a +
                            ", after B's=" + out_after_b;
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
        const auto last = static_cast<seqnum_t>(1 + 2 * pairs_run);
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
    EXPECT_EQ(next_out(sess), 2U + 2U * kPairs) << "every send must take one number. " << context;
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

enum class FromAppReturns { ok, reject };

// fromApp for an inbound 35=D starts a send whose toApp throws. Everything is
// recorded and printed; little is asserted.
void run_nested_to_app_throws(StoreKind store, FromAppReturns from_app_returns) {
    Fixture f{ExecKind::io_context, store};
    Session sess{f.engine, f.cfg()};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const auto reply = bytes_of(kPayloadOne);
    const std::size_t wire_before = f.wire.size();

    SendOutcome nested;
    bool in_from_app = false;
    bool thrown = false;
    std::optional<bool> throwing_to_app_inside_from_app;
    std::string seen_in_from_app = "not recorded";
    std::vector<std::string> events;  // callbacks from the throw on, in order

    const auto session_now = [&] {
        return "state=" + std::to_string(static_cast<int>(sess.state())) +
               " lifecycle=" + std::to_string(session_test_access::lifecycle_value(sess));
    };
    const auto where = [&] {
        return std::string(in_from_app ? " (fromApp on the stack)" : " (fromApp returned)");
    };

    f.app->on_from_app = [&] {
        in_from_app = true;
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] fromApp: starting Session::send\n");
        asio::co_spawn(ex, sess.send(reply), record_into(nested));
        std::fprintf(stderr, "[ B41 ] fromApp: co_spawn returned\n");
        seen_in_from_app = session_now() + ", send " + describe(nested);
        if (thrown) events.push_back("fromApp returns");
        in_from_app = false;
    };
    f.app->on_to_app = [&] {
        if (thrown) {
            events.push_back("toApp, call " + std::to_string(f.app->to_app_calls) + where());
            return;
        }
        thrown = true;
        throwing_to_app_inside_from_app = in_from_app;
        events.push_back("toApp throws" + where());
        throw std::runtime_error{"B41: toApp throws"};
    };
    f.app->on_lifecycle = [&](const char* name) {
        events.push_back(std::string(name) + where() + " " + session_now());
    };
    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (thrown) events.push_back("toAdmin 35=" + std::string(msg_type) + where());
    };
    if (from_app_returns == FromAppReturns::reject) {
        f.app->from_app_result = std::unexpected(error::session_invalid_argument);
    }

    const auto order = peer_frame("D", 2,
                                  "11=PEER-1\x01"
                                  "54=1\x01"
                                  "55=AAPL\x01");
    bool inbound_done = false;
    std::string inbound_result = "not completed";
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{order}),
                   [&](std::exception_ptr ep, expected_t<void> r) {
                       inbound_done = true;
                       inbound_result =
                           ep  ? "threw"
                           : r ? "ok"
                               : "error " + std::to_string(static_cast<int>(r.error()));
                   });
    // Bounded: a call that does not complete is reported below, not waited for.
    const auto all_done = [&] {
        return inbound_done && (nested.done || f.app->from_app_calls == 0);
    };
    const bool completed = fixpp::test_support::pump_until(f.ioc, all_done, std::chrono::seconds{5},
                                                           fixpp::test_support::kPumpSlice,
                                                           "CallbackSend::nested_to_app_throws");

    // Store reads, each bounded and reported as text.
    const auto store_next = [&](direction_t dir) -> std::string {
        MessageStore* st = session_test_access::store(sess);
        if (st == nullptr) return "no store";
        std::string out = "read not completed";
        bool done = false;
        asio::co_spawn(f.exec, st->next_seqnum(dir, false),
                       [&](std::exception_ptr ep, expected_t<seqnum_t> r) {
                           done = true;
                           out = ep  ? "threw"
                                 : r ? std::to_string(*r)
                                     : "error " + std::to_string(static_cast<int>(r.error()));
                       });
        (void)fixpp::test_support::pump_until(
            f.ioc, [&] { return done; }, std::chrono::seconds{2}, fixpp::test_support::kPumpSlice,
            "CallbackSend::nested_to_app_throws_store");
        return out;
    };

    const std::string classification = !throwing_to_app_inside_from_app.has_value() ? "never ran"
                                       : *throwing_to_app_inside_from_app           ? "nested"
                                                                                    : "deferred";
    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    std::string event_text;
    for (const auto& e : events) event_text += " [" + e + "]";
    const std::string context =
        "Classification: " + classification +
        ". Inside fromApp after the co_spawn returned: " + seen_in_from_app +
        "; after on_inbound_frame: " + session_now() + "; on_inbound_frame: " + inbound_result +
        "; send: " + describe(nested) +
        "; callbacks from the throw on, in order:" + (event_text.empty() ? " (none)" : event_text) +
        "; frames transmitted after the inbound frame was fed: " + describe(sent) +
        "; NextNumIn: manager=" + std::to_string(next_in(sess)) +
        ", store=" + store_next(direction_t::inbound) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + store_next(direction_t::outbound) + "; fromApp returned " +
        (from_app_returns == FromAppReturns::reject ? "an error" : "success") + ".";
    std::printf("[ MEASURED ] %s\n", context.c_str());
    std::fflush(stdout);
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::nested_to_app_throws");
    }

    ASSERT_EQ(f.app->from_app_calls, 1)
        << "PATH NOT DRIVEN: the inbound 35=D did not reach fromApp. " << context;
    ASSERT_TRUE(thrown) << "PATH NOT DRIVEN: the send's toApp never ran. " << context;
    EXPECT_TRUE(inbound_done) << "on_inbound_frame must complete. " << context;
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows) {
    run_nested_to_app_throws(StoreKind::memory, FromAppReturns::ok);
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FileStore) {
    run_nested_to_app_throws(StoreKind::file, FromAppReturns::ok);
}

TEST(CallbackSend, FromAppSendsReply_NestedToAppThrows_FromAppRejects) {
    run_nested_to_app_throws(StoreKind::memory, FromAppReturns::reject);
}

enum class CloseFrom { from_app, from_admin };

// A callback of an Active session starts Session::close on the session's
// executor, directly. Everything is recorded and printed; little is asserted.
void run_callback_starts_close(CloseFrom from, close_mode mode, StoreKind store,
                               TransportKind transport = TransportKind::sync_capture) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    // close(graceful) waits this long for a peer Logout that this cell never feeds.
    cfg.logout_disconnect_timeout_ms = 300;
    Session sess{f.engine, cfg};
    ASSERT_TRUE(f.open_to_active(sess)) << "setup: session did not reach Active";
    if (transport == TransportKind::live_suspending_write) f.attach_live_transport(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active) << "setup: session left Active";
    ASSERT_EQ(next_out(sess), 2U) << "setup: the Logon must have consumed outbound MsgSeqNum 1";
    ASSERT_EQ(next_in(sess), 2U) << "setup: the peer's Logon must have been consumed";

    const auto ex = sess.executor().underlying();
    const std::size_t wire_before = f.wire.size();
    const char* callback_name = from == CloseFrom::from_app ? "fromApp" : "fromAdmin";

    bool started = false;
    bool in_callback = false;
    bool close_done = false;
    bool inbound_done = false;
    std::string close_result = "not completed";
    std::string inbound_result = "not completed";
    std::string seen_in_callback = "not recorded";
    std::string seen_when_inbound_done = "not recorded";
    std::vector<std::string> events;  // from the close's start on, in order

    const auto session_now = [&] {
        return "state=" + std::to_string(static_cast<int>(sess.state())) +
               " lifecycle=" + std::to_string(session_test_access::lifecycle_value(sess));
    };
    const auto where = [&] {
        return std::string(in_callback ? " (callback on the stack)" : " (callback returned)");
    };
    const auto result_text = [](std::exception_ptr ep, const expected_t<void>& r) {
        return ep  ? std::string("threw")
               : r ? std::string("ok")
                   : "error " + std::to_string(static_cast<int>(r.error()));
    };
    const auto start_close = [&] {
        started = true;
        in_callback = true;
        // A build with asserts may stop inside the co_spawn; these two lines
        // bracket it.
        std::fprintf(stderr, "[ B41 ] %s: starting Session::close\n", callback_name);
        asio::co_spawn(ex, sess.close(mode), [&](std::exception_ptr ep, expected_t<void> r) {
            close_done = true;
            close_result = result_text(ep, r);
            events.push_back("close's completion handler" + where() +
                             (inbound_done ? ", on_inbound_frame completed"
                                           : ", on_inbound_frame not completed"));
        });
        std::fprintf(stderr, "[ B41 ] %s: co_spawn returned\n", callback_name);
        seen_in_callback = session_now() + ", close " + close_result;
        events.push_back(std::string(callback_name) + " returns");
        in_callback = false;
    };

    if (from == CloseFrom::from_app) {
        f.app->on_from_app = [&] {
            if (!started) start_close();
        };
    } else {
        f.app->on_from_admin = [&](std::string_view msg_type) {
            if (msg_type == "1" && !started) start_close();
        };
    }
    f.app->on_to_app = [&] {
        if (started) events.push_back("toApp" + where());
    };
    f.app->on_lifecycle = [&](const char* name) {
        if (started) events.push_back(std::string(name) + where() + " " + session_now());
    };
    f.app->on_to_admin = [&](std::string_view msg_type) {
        if (started) events.push_back("toAdmin 35=" + std::string(msg_type) + where());
    };

    const auto inbound = from == CloseFrom::from_app ? peer_frame("D", 2,
                                                                  "11=PEER-1\x01"
                                                                  "54=1\x01"
                                                                  "55=AAPL\x01")
                                                     : peer_frame("1", 2, "112=B41-TR\x01");
    asio::co_spawn(ex, sess.on_inbound_frame(std::span<const std::byte>{inbound}),
                   [&](std::exception_ptr ep, expected_t<void> r) {
                       inbound_done = true;
                       inbound_result = result_text(ep, r);
                       seen_when_inbound_done = session_now();
                       events.push_back("on_inbound_frame's completion handler");
                   });
    // Bounded: an operation that does not complete is reported below, not waited for.
    const bool completed = fixpp::test_support::pump_until(
        f.ioc, [&] { return inbound_done && (close_done || !started); }, std::chrono::seconds{5},
        fixpp::test_support::kPumpSlice, "CallbackSend::callback_starts_close");

    // Store reads, each bounded and reported as text.
    const auto store_next = [&](direction_t dir) -> std::string {
        MessageStore* st = session_test_access::store(sess);
        if (st == nullptr) return "no store";
        std::string out = "read not completed";
        bool done = false;
        asio::co_spawn(f.exec, st->next_seqnum(dir, false),
                       [&](std::exception_ptr ep, expected_t<seqnum_t> r) {
                           done = true;
                           out = ep  ? "threw"
                                 : r ? std::to_string(*r)
                                     : "error " + std::to_string(static_cast<int>(r.error()));
                       });
        (void)fixpp::test_support::pump_until(
            f.ioc, [&] { return done; }, std::chrono::seconds{2}, fixpp::test_support::kPumpSlice,
            "CallbackSend::callback_starts_close_store");
        return out;
    };

    const std::vector<std::string> sent(f.wire.begin() + static_cast<std::ptrdiff_t>(wire_before),
                                        f.wire.end());
    std::string event_text;
    for (const auto& e : events) event_text += " [" + e + "]";
    const std::string context =
        std::string("Callback: ") + callback_name +
        "; close mode: " + (mode == close_mode::terminal ? "terminal" : "graceful") +
        ". Inside the callback after the co_spawn returned: " + seen_in_callback +
        "; when on_inbound_frame completed: " + seen_when_inbound_done +
        "; at the end: " + session_now() + "; on_inbound_frame: " + inbound_result +
        "; close: " + close_result +
        "; callbacks and completion handlers from the close's start on, in order:" +
        (event_text.empty() ? " (none)" : event_text) +
        "; frames transmitted after the inbound frame was fed: " + describe(sent) +
        "; NextNumIn: manager=" + std::to_string(next_in(sess)) +
        ", store=" + store_next(direction_t::inbound) +
        "; NextNumOut: manager=" + std::to_string(next_out(sess)) +
        ", store=" + store_next(direction_t::outbound) + ".";
    std::printf("[ MEASURED ] %s\n", context.c_str());
    std::fflush(stdout);
    if (!completed) {
        fixpp::test_support::cancel_and_drain_or_report(f.ioc, *f.clock,
                                                        "CallbackSend::callback_starts_close");
    }

    ASSERT_TRUE(started) << "PATH NOT DRIVEN: the inbound frame did not reach " << callback_name
                         << ". " << context;
    EXPECT_TRUE(inbound_done) << "on_inbound_frame must complete. " << context;
}

TEST(CallbackSend, FromAppStartsClose_Terminal) {
    run_callback_starts_close(CloseFrom::from_app, close_mode::terminal, StoreKind::memory);
}

TEST(CallbackSend, FromAppStartsClose_Graceful) {
    run_callback_starts_close(CloseFrom::from_app, close_mode::graceful, StoreKind::memory);
}

TEST(CallbackSend, FromAppStartsClose_Terminal_FileStore) {
    run_callback_starts_close(CloseFrom::from_app, close_mode::terminal, StoreKind::file);
}

TEST(CallbackSend, FromAdminStartsClose_Terminal) {
    run_callback_starts_close(CloseFrom::from_admin, close_mode::terminal, StoreKind::memory);
}

// Whose outbound callback starts the second operation.
enum class OuterSite {
    app_send,         // the toApp of a Session::send
    heartbeat_reply,  // the toAdmin of the Heartbeat answering a TestRequest
    poss_dup_reject,  // the toAdmin of the Reject answering a PossDup without 122
};
enum class Starts { close_graceful, close_terminal, send };

// An outbound callback of an Active session starts a second operation on the
// session's executor, directly: a close or a send.
void run_outbound_callback_starts(OuterSite site, Starts starts, StoreKind store,
                                  TransportKind transport = TransportKind::sync_capture) {
    Fixture f{ExecKind::io_context, store};
    auto cfg = f.cfg();
    // close(graceful) waits this long for a peer Logout that this cell never feeds.
    cfg.logout_disconnect_timeout_ms = 300;
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
        in_callback = false;
    };

    f.on_transmit = [&](const std::string& frame) {
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
        if (started) events.push_back("toAdmin 35=" + std::string(msg_type) + where());
    };
    f.app->on_lifecycle = [&](const char* name) {
        if (started) events.push_back(std::string(name) + where() + " " + session_now());
    };

    const auto on_outer_done = [&](std::exception_ptr ep, expected_t<void> r) {
        outer_done = true;
        outer_result = result_text(ep, r);
        seen_when_outer_done = session_now();
        events.push_back(std::string("the outer ") + outer_name + "'s completion handler");
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
    // Bounded: an operation that does not complete is reported below, not waited
    // for. The bound is well above the graceful close's shortened wait.
    const bool completed = fixpp::test_support::pump_until(
        f.ioc, [&] { return outer_done && (inner_done || !started); }, std::chrono::seconds{5},
        fixpp::test_support::kPumpSlice, "CallbackSend::outbound_callback_starts");

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
    for (const auto& fr : sent) wire_seqs.push_back(seq_of(fr));
    std::vector<seqnum_t> unique_seqs = wire_seqs;
    std::ranges::sort(unique_seqs);
    const bool distinct = std::ranges::adjacent_find(unique_seqs) == unique_seqs.end();
    const bool ascending =
        std::ranges::adjacent_find(wire_seqs, std::greater_equal<>{}) == wire_seqs.end();
    unique_seqs.erase(std::unique(unique_seqs.begin(), unique_seqs.end()), unique_seqs.end());

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

    const std::string classification = starts != Starts::send ? std::string("n/a (a close)")
                                       : !inner_to_app_in_callback.has_value()
                                           ? std::string("never ran")
                                       : *inner_to_app_in_callback ? std::string("nested")
                                                                   : std::string("deferred");
    std::string event_text;
    for (const auto& e : events) event_text += " [" + e + "]";
    const std::string context =
        std::string("Outer: ") + outer_name + "; started inside its callback: " + inner_name +
        (starts == Starts::close_graceful   ? " (graceful)"
         : starts == Starts::close_terminal ? " (terminal)"
                                            : "") +
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
    if (starts == Starts::send) {
        ASSERT_NE(classification, "never ran")
            << "PATH NOT DRIVEN: the inner send's toApp never ran. " << context;
    }
    EXPECT_TRUE(distinct) << "(a) " << context;
    EXPECT_TRUE(ascending) << "(b) " << context;
    if (has_store) {
        EXPECT_TRUE(all_retained) << "(c) " << context;
        EXPECT_TRUE(store_agrees) << "(d) " << context;
    }
    EXPECT_TRUE(outcomes_ok) << "(e) " << context;
    EXPECT_LE(f.live_writes.max_in_flight, 1) << "one live write in flight at a time. " << context;
    if (starts == Starts::send) {
        EXPECT_EQ(manager_out, "4") << "two frames must take two numbers. " << context;
    }
}

TEST(CallbackSend, ToAppStartsClose_Graceful) {
    run_outbound_callback_starts(OuterSite::app_send, Starts::close_graceful, StoreKind::memory);
}

TEST(CallbackSend, ToAppStartsClose_Terminal) {
    run_outbound_callback_starts(OuterSite::app_send, Starts::close_terminal, StoreKind::memory);
}

TEST(CallbackSend, ToAppStartsClose_Graceful_FileStore) {
    run_outbound_callback_starts(OuterSite::app_send, Starts::close_graceful, StoreKind::file);
}

TEST(CallbackSend, ToAdminStartsClose_Graceful) {
    run_outbound_callback_starts(OuterSite::heartbeat_reply, Starts::close_graceful,
                                 StoreKind::memory);
}

TEST(CallbackSend, ToAdminStartsClose_Graceful_AssignFirstSite) {
    run_outbound_callback_starts(OuterSite::poss_dup_reject, Starts::close_graceful,
                                 StoreKind::memory);
}

TEST(CallbackSend, ToAppSendsNested_NoStore) {
    run_outbound_callback_starts(OuterSite::app_send, Starts::send, StoreKind::none);
}

TEST(CallbackSend, ToAdminStartsSend_NoStore) {
    run_outbound_callback_starts(OuterSite::heartbeat_reply, Starts::send, StoreKind::none);
}

TEST(CallbackSend, ToAdminStartsSend_AssignFirstSite_NoStore) {
    run_outbound_callback_starts(OuterSite::poss_dup_reject, Starts::send, StoreKind::none);
}

TEST(CallbackSend, ToAppSendsNested_NoStore_LiveWrite) {
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

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define B41_BUILT_WITH_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
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

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define B41_BUILT_WITH_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
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
    auto* block = new int[4]{1, 2, 3, 4};
    int* volatile dangling = block;
    delete[] block;
    std::printf("[ B41 ] reading a deleted heap block: %d\n", dangling[1]);
    std::fflush(stdout);
#ifdef B41_BUILT_WITH_ASAN
    FAIL() << "AddressSanitizer did not stop the read of a deleted heap block.";
#endif
}

}  // namespace fixpp::session::test
