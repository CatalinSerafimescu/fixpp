// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_563_564_callback_send.cpp
//
// fixpp#563 and fixpp#564 (batch B28) — reproduction only, no fix.
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
// #564's claim: on a build without NDEBUG the nested send's own toApp
// constructs a callback_dispatch_scope while the outer callback's scope is
// live, and that scope's assert aborts the process. So every cell marked
// "nested" below is expected to abort there on such a build, and #563 is only
// observable where the assert is compiled out. No cell depends on another: run
// each alone with --gtest_filter.
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
//                             35=D starts a send of a reply. nested.
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
// live transport; tests/session/test_application_engine_send.cpp holds the
// cells that drive it.
//
// mock_clock: nothing here depends on elapsed time. HeartBtInt is 0, so no
// liveness loop runs.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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

// An inbound frame from the peer: MsgType, 34, 49, 52, 56, then `rest`.
std::vector<std::byte> peer_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view rest) {
    std::string body = "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(kTarget) + "\x01";
    body += "52=" + std::string(kStamp52) + "\x01";
    body += "56=" + std::string(kSender) + "\x01";
    body += rest;
    std::string full = "8=" + std::string(kBegin) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    return bytes_of(full);
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

    expected_t<void> fromApp(const MessageView<access_mode::Index>& /*mv*/,
                             const SessionId& /*id*/) override {
        ++from_app_calls;
        if (on_from_app) on_from_app();
        return {};
    }
    expected_t<void> toApp(const MessageView<access_mode::Index>& /*mv*/,
                           const SessionId& /*id*/) override {
        ++to_app_calls;
        if (on_to_app) on_to_app();
        return {};
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
enum class StoreKind { memory, file };

struct Fixture {
    asio::io_context ioc;
    // The FileStore cell's file-I/O executor. Declared after ioc, so it is
    // joined before ioc is destroyed.
    asio::thread_pool file_pool{1};
    std::filesystem::path file_dir;
    StoreKind store_kind;
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
        c.heartbeat_interval = 0s;
        c.security_profile = fixpp::test_support::make_minimal_security_profile();
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.executor_override = exec;
        c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        c.transport_send = [this](std::span<const std::byte> f) { wire.push_back(text_of(f)); };
        if (store_kind == StoreKind::file) {
            file_dir = fixpp::test_support::unique_temp_dir("b28_563");
            FileStore::Config fc;
            fc.directory = file_dir;
            fc.max_frame_bytes = 4096;
            fc.file_io_executor = file_pool.get_executor();
            c.store_factory = std::make_shared<FileStoreFactory>(fc);
        } else {
            MemoryStore::Config sc;
            sc.inbound_capacity = 16;
            sc.outbound_capacity = 16;
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

    // Initiator: open() emits the Logon, then the peer's Logon acknowledges it.
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

    // Witness.
    ASSERT_EQ(f.app->from_app_calls, 1)
        << "PATH NOT DRIVEN: the inbound 35=D did not reach fromApp. " << context;
    ASSERT_TRUE(inner_saw_outer_live.has_value())
        << "PATH NOT DRIVEN: the nested send's toApp never ran. " << context;
    ASSERT_TRUE(*inner_saw_outer_live)
        << "PATH NOT DRIVEN (the send was deferred): the nested send's toApp ran only after "
           "fromApp had returned. "
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

}  // namespace fixpp::session::test
