// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_544_send_slot_cells.cpp
//
// fixpp#544 (B35 Phase 5; `.specify/544-hot-path-zero-alloc.md` §2.5, §3 "Behaviour"; owner
// rulings R-9, R-10). `Session::send` builds its frame into one session-owned slot, held by a
// strand-local flag from the build until `store_then_emit` returns; a send that finds the slot
// held takes a fallback leaf that owns its own buffer. No allocation gate can see a broken
// flag: two sends sharing the slot corrupt bytes silently. These cells assert the bytes.
//
// Part 1 is required to hold at base too, where every send owns its buffer. It is the
// differential half:
//   - Interleaved: two sends spawned back to back from the session strand; the second runs
//     while the first is suspended in its store. Each send's stored and transmitted bytes
//     must equal a frame built here, by hand, from the same payload, seq and mock-clock time.
//   - Nested: a send started from inside the first send's toApp runs inline, because a
//     co_spawn onto a strand already running on this thread dispatches inline. The bytes toApp's
//     view spans must not change across that send. Neither result is asserted: both sends can carry
//     one MsgSeqNum (fixpp#563, out of scope).
//   - Cancelled inside toApp, on the primary path: the cancellation check after toApp stays
//     where it is today.
// Part 2 needs the slot flag (tests/support/session_test_access.hpp), so it has no base form:
//   - a veto and a throwing toApp each leave the flag clear;
//   - a send with the slot held takes the leaf, and its bytes are the same;
//   - cancelled inside toApp on the leaf path: the leaf boundary restores the chain's
//     throw_if_cancelled before send_impl runs.
//
// To run part 1 at base, copy this file into a worktree at the base commit, delete everything
// from the part-2 banner to the end of the anonymous namespace, and register it as below.
//
// Mutants, each run against the whole binary; each named cell must then fail:
//   - Session::send's flag test deleted, so every send builds into the slot: Interleaved and
//     Nested;
//   - the holder released before send_impl's store_then_emit returns: Interleaved;
//   - the holder replaced by a clear on success only: the veto and throw cells;
//   - the leaf's FIXPP_INBOUND_SPLIT_ENTRY deleted: the leaf-path cancellation cell;
//   - the b3 sequence applied to Session::send -> send_impl: the primary-path cancellation cell.

#include <gtest/gtest.h>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

namespace fixpp::session::test {
namespace {

// The session's own identity, and SendingTime(52) at the mock clock's seed with the default
// millisecond precision.
constexpr std::string_view kSender = "ISLD";
constexpr std::string_view kTarget = "TW";
constexpr std::string_view kBegin = "FIX.4.2";
constexpr std::string_view kStamp52 = "20240101-00:00:00.000";

// Two payloads of different lengths, so a frame built over the other one differs in its
// BodyLength, its checksum and its length.
constexpr std::string_view kPayloadOne =
    "35=D\x01"
    "11=SLOT-ONE\x01"
    "55=AAA\x01";
constexpr std::string_view kPayloadTwo =
    "35=D\x01"
    "11=SLOT-TWO-LONGER\x01"
    "38=7\x01"
    "55=BBBB\x01";

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

std::string text_of(std::span<const std::byte> b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

std::string checksummed(std::string full) {
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    return full;
}

// The frame Session::send must produce for `payload` at `seq`, built by hand: MsgType first,
// then 34, 49, 52, 56, then the payload's remaining fields (none of which is a header tag).
std::string expected_frame(seqnum_t seq, std::string_view payload) {
    const auto soh = payload.find('\x01');
    std::string body(payload.substr(0, soh + 1));
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(kSender) + "\x01";
    body += "52=" + std::string(kStamp52) + "\x01";
    body += "56=" + std::string(kTarget) + "\x01";
    body += payload.substr(soh + 1);
    std::string full = "8=" + std::string(kBegin) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    return checksummed(std::move(full));
}

// The peer's Logon, inbound to the acceptor.
std::vector<std::byte> peer_logon() {
    std::string body = "35=A\x01";
    body += "34=1\x01";
    body += "49=" + std::string(kTarget) + "\x01";
    body += "52=" + std::string(kStamp52) + "\x01";
    body += "56=" + std::string(kSender) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    std::string full = "8=" + std::string(kBegin) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    return bytes_of(checksummed(std::move(full)));
}

class SlotApplication final : public Application {
public:
    int to_app_calls = 0;
    bool veto = false;
    bool throw_in_to_app = false;
    // Runs inside toApp, with the view toApp receives, before the veto or the throw.
    std::function<void(const MessageView<access_mode::Index>&)> on_to_app;

    expected_t<void> toApp(const MessageView<access_mode::Index>& mv,
                           const SessionId& /*id*/) override {
        ++to_app_calls;
        if (on_to_app) on_to_app(mv);
        if (throw_in_to_app) throw std::runtime_error("toApp throws");
        if (veto) return std::unexpected(error::app_do_not_send);
        return {};
    }
};

// Collects the outbound frames a retrieve() walk visits.
class CollectingVisitor final : public retrieve_visitor {
public:
    std::map<seqnum_t, std::string> frames;

    asio::awaitable<expected_t<visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte> frame) noexcept override {
        frames[seq] = text_of(frame);
        co_return visit_result::cont;
    }
};

// A send's completion, recorded by a plain completion handler.
struct SendOutcome {
    bool done = false;
    bool threw = false;
    std::optional<error> err;  // empty when the send returned a value
};

auto record_into(SendOutcome& o) {
    return [&o](std::exception_ptr ep, expected_t<void> r) {
        o.done = true;
        o.threw = static_cast<bool>(ep);
        if (!ep && !r) o.err = r.error();
    };
}

struct SlotFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<SlotApplication> app = std::make_shared<SlotApplication>();
    fixpp::core::EngineConfig engine;
    std::vector<std::string> wire;  // every frame transport_send received

    SlotFixture() {
        clock = std::make_shared<fixpp::core::mock_clock>(
            std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200},
            fixpp::core::steady_time_point{}, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;
    }

    SessionConfig cfg() {
        SessionConfig c;
        c.sender_comp_id = std::string(kSender);
        c.target_comp_id = std::string(kTarget);
        c.begin_string = std::string(kBegin);
        c.heartbeat_interval = 0s;
        c.security_profile = fixpp::test_support::make_minimal_security_profile();
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.executor_override = ioc.get_executor();
        c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        c.transport_send = [this](std::span<const std::byte> f) { wire.push_back(text_of(f)); };
        MemoryStore::Config sc;
        sc.inbound_capacity = 16;
        sc.outbound_capacity = 16;
        sc.max_frame_bytes = 4096;
        c.store_factory = std::make_shared<MemoryStoreFactory>(sc);
        return c;
    }

    // Runs the io_context until `pred` holds. Nothing in these cells waits on a timer.
    template <class Pred>
    bool pump_until(Pred pred) {
        for (int i = 0; i < 10000; ++i) {
            if (pred()) return true;
            ioc.restart();
            ioc.poll();
        }
        return pred();
    }

    template <class Awaitable>
    expected_t<void> run(Awaitable a, char const* what) {
        auto fut = asio::co_spawn(ioc, std::move(a), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 500ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, what);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << what;
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // Acceptor: open(), then the peer's Logon; the reply Logon is outbound seq 1.
    bool open_to_active(Session& s) {
        if (!run(s.open(), "SlotFixture::open")) return false;
        const auto logon = peer_logon();
        if (!run(s.on_inbound_frame(logon), "SlotFixture::logon")) return false;
        return s.state() == fsm_state::Active;
    }

    // The outbound frames the store holds at seqs [begin, end].
    std::map<seqnum_t, std::string> stored(Session& s, seqnum_t begin, seqnum_t end) {
        CollectingVisitor v;
        MessageStore* store = session_test_access::store(s);
        if (store == nullptr) {
            ADD_FAILURE() << "the session has no store";
            return {};
        }
        auto r =
            run(store->retrieve(begin, end, direction_t::outbound, v), "SlotFixture::retrieve");
        EXPECT_TRUE(r.has_value()) << "retrieve failed";
        return v.frames;
    }
};

seqnum_t next_outbound(Session& s) { return session_test_access::seqnum_mgr(s).peek_outbound(); }

// ── Part 1: holds at base and after the change ───────────────────────────────

TEST(B35SendSlot, Interleaved_EachSendStoresAndTransmitsItsOwnFrame) {
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    const seqnum_t seq0 = next_outbound(sess);
    const std::size_t wire_before = f.wire.size();
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);

    SendOutcome r1;
    SendOutcome r2;
    bool first_pending_when_second_started = false;
    int to_app_after_first = 0;
    int to_app_after_second = 0;
    const auto ex = sess.executor().underlying();
    // A spawn from the session strand runs the send inline until its first suspension.
    auto driver = [&]() -> asio::awaitable<void> {
        asio::co_spawn(ex, sess.send(one), record_into(r1));
        to_app_after_first = f.app->to_app_calls;
        first_pending_when_second_started = !r1.done;
        asio::co_spawn(ex, sess.send(two), record_into(r2));
        to_app_after_second = f.app->to_app_calls;
        co_return;
    };
    asio::co_spawn(ex, driver(), asio::detached);
    // `driver` is a named closure, so it must be driven inside its own scope by a call that
    // tools/audit_co_spawn_named_closure.py counts as a drive. The free pump is one; the
    // fixture's member pump is not.
    ASSERT_TRUE(fixpp::test_support::pump_until(f.ioc, [&] { return r1.done && r2.done; }))
        << "a send did not complete";

    // Positive controls: the two sends overlapped, and each built its frame and reached toApp
    // while the first was suspended.
    EXPECT_TRUE(first_pending_when_second_started)
        << "the first send completed before the second started: the sends did not overlap";
    EXPECT_EQ(to_app_after_first, 1);
    EXPECT_EQ(to_app_after_second, 2);
    ASSERT_FALSE(r1.threw);
    ASSERT_FALSE(r2.threw);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the stream runs only if the optional is set
    EXPECT_FALSE(r1.err.has_value()) << "first send failed: " << static_cast<int>(*r1.err);
    EXPECT_FALSE(r2.err.has_value()) << "second send failed: " << static_cast<int>(*r2.err);
    // NOLINTEND(bugprone-unchecked-optional-access)

    const std::string e1 = expected_frame(seq0, kPayloadOne);
    const std::string e2 = expected_frame(seq0 + 1, kPayloadTwo);
    const auto st = f.stored(sess, seq0, seq0 + 1);
    ASSERT_EQ(st.size(), 2U) << "the store does not hold both frames";
    EXPECT_EQ(st.at(seq0), e1) << "the first send stored bytes it did not build";
    EXPECT_EQ(st.at(seq0 + 1), e2) << "the second send stored bytes it did not build";
    ASSERT_EQ(f.wire.size(), wire_before + 2) << "not exactly two frames transmitted";
    EXPECT_EQ(f.wire[wire_before], e1) << "the first transmitted frame is not the first send's";
    EXPECT_EQ(f.wire[wire_before + 1], e2)
        << "the second transmitted frame is not the second send's";
}

// True in a build without NDEBUG. Read from a volatile so that no compiler folds the skip
// below: folded either way, one branch is unreachable code, which MSVC reports as C4702
// under /W4 (the precedent is tests/support/msvc_debug_arena_skip.hpp).
bool asserts_enabled() noexcept {
#ifndef NDEBUG
    static volatile bool on = true;
#else
    static volatile bool on = false;
#endif
    return on;
}

TEST(B35SendSlot, Nested_ASendInsideToAppLeavesTheOuterFrameIntact) {
    // The nested send's own toApp enters callback_dispatch_scope while the outer toApp's is
    // live, and that scope asserts no nested callback entry in builds without NDEBUG. That
    // assert is fixpp#564; this cell runs where NDEBUG is defined until #564 is resolved.
    if (asserts_enabled()) {
        GTEST_SKIP() << "fixpp#564: a send nested in toApp trips callback_dispatch_scope's "
                        "assert without NDEBUG";
    }
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    const auto one = bytes_of(kPayloadOne);
    const auto two = bytes_of(kPayloadTwo);
    const auto ex = sess.executor().underlying();

    SendOutcome outer;
    SendOutcome inner;
    std::string snapshot;
    std::string after;
    std::string inner_view;
    bool inner_ran_inline = false;
    f.app->on_to_app = [&](const MessageView<access_mode::Index>& mv) {
        if (f.app->to_app_calls == 1) {
            snapshot = text_of(mv.bytes());
            asio::co_spawn(ex, sess.send(two), record_into(inner));
            inner_ran_inline = !inner_view.empty();
            after = text_of(mv.bytes());
        } else if (f.app->to_app_calls == 2) {
            inner_view = text_of(mv.bytes());
        }
    };
    asio::co_spawn(ex, sess.send(one), record_into(outer));
    ASSERT_TRUE(f.pump_until([&] { return outer.done && inner.done; }))
        << "a send did not complete";

    // Positive controls: the nested send ran inline, inside the outer toApp, over a frame
    // different from the outer one.
    ASSERT_TRUE(inner_ran_inline) << "the nested send did not reach its toApp inline";
    ASSERT_FALSE(snapshot.empty());
    EXPECT_NE(inner_view, snapshot) << "the nested send's frame equals the outer frame";
    EXPECT_NE(inner_view.find("11=SLOT-TWO-LONGER\x01"), std::string::npos);
    EXPECT_EQ(after, snapshot) << "the nested send changed the bytes the outer toApp's view spans";
}

// Cancellation emitted from inside toApp, on a chain that accepts terminal cancellation. The
// first check after toApp is assign_outbound's co_await, as today: the send is not stored or
// transmitted, and Session::send reports dispatch_aborted and records Disconnected.
struct CancelOutcome {
    SendOutcome send;
    int to_app_calls = 0;
    std::size_t wire_after = 0;
    seqnum_t next_out_after = 0;
    fsm_state state_after = fsm_state::NotConnected;
};

CancelOutcome send_cancelled_in_to_app(SlotFixture& f, Session& sess,
                                       std::function<void()> before_send = {}) {
    CancelOutcome o;
    const auto one = bytes_of(kPayloadOne);
    asio::cancellation_signal sig;
    f.app->on_to_app = [&](const MessageView<access_mode::Index>&) {
        sig.emit(asio::cancellation_type::terminal);
    };
    if (before_send) before_send();
    const auto ex = sess.executor().underlying();
    asio::co_spawn(ex, sess.send(one),
                   asio::bind_cancellation_slot(sig.slot(), record_into(o.send)));
    EXPECT_TRUE(f.pump_until([&] { return o.send.done; })) << "the send did not complete";
    o.to_app_calls = f.app->to_app_calls;
    o.wire_after = f.wire.size();
    o.next_out_after = next_outbound(sess);
    o.state_after = sess.state();
    f.app->on_to_app = nullptr;
    return o;
}

void expect_cancelled_after_to_app(const CancelOutcome& o, std::size_t wire_before, seqnum_t seq0) {
    EXPECT_EQ(o.to_app_calls, 1) << "toApp did not run exactly once";
    EXPECT_FALSE(o.send.threw);
    ASSERT_TRUE(o.send.err.has_value()) << "the cancelled send returned a value";
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): ASSERT_TRUE above
    EXPECT_EQ(*o.send.err, error::dispatch_aborted);
    EXPECT_EQ(o.wire_after, wire_before) << "the cancelled send was transmitted";
    EXPECT_EQ(o.next_out_after, seq0) << "the cancelled send consumed a seqnum";
    EXPECT_EQ(o.state_after, fsm_state::Disconnected);
}

TEST(B35SendSlot, CancelledInsideToApp_PrimaryPath_ThrowsAtAssignOutbound) {
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    const seqnum_t seq0 = next_outbound(sess);
    const std::size_t wire_before = f.wire.size();
    expect_cancelled_after_to_app(send_cancelled_in_to_app(f, sess), wire_before, seq0);
}

// ── Part 2: needs the slot flag; no base form ────────────────────────────────

TEST(B35SendSlot, VetoedSend_ReleasesTheSlot) {
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    bool held_in_to_app = false;
    f.app->veto = true;
    f.app->on_to_app = [&](const MessageView<access_mode::Index>&) {
        held_in_to_app = session_test_access::send_slot_in_use(sess);
    };
    const auto one = bytes_of(kPayloadOne);
    auto r = f.run(sess.send(one), "VetoedSend_ReleasesTheSlot");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::app_do_not_send);
    EXPECT_TRUE(held_in_to_app) << "the send did not hold the slot while toApp ran";
    EXPECT_FALSE(session_test_access::send_slot_in_use(sess)) << "a vetoed send left the slot held";
    EXPECT_EQ(sess.state(), fsm_state::Active);
}

TEST(B35SendSlot, ThrowingToApp_ReleasesTheSlot) {
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    bool held_in_to_app = false;
    f.app->throw_in_to_app = true;
    f.app->on_to_app = [&](const MessageView<access_mode::Index>&) {
        held_in_to_app = session_test_access::send_slot_in_use(sess);
    };
    const auto one = bytes_of(kPayloadOne);
    auto r = f.run(sess.send(one), "ThrowingToApp_ReleasesTheSlot");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::app_callback_threw);
    EXPECT_TRUE(held_in_to_app) << "the send did not hold the slot while toApp ran";
    EXPECT_FALSE(session_test_access::send_slot_in_use(sess))
        << "a send whose toApp threw left the slot held";
}

TEST(B35SendSlot, SlotHeld_SendTakesTheLeafWithTheSameBytes) {
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    const seqnum_t seq0 = next_outbound(sess);
    const std::size_t wire_before = f.wire.size();
    session_test_access::send_slot_in_use(sess) = true;
    const auto one = bytes_of(kPayloadOne);
    auto r = f.run(sess.send(one), "SlotHeld_SendTakesTheLeafWithTheSameBytes");
    ASSERT_TRUE(r.has_value()) << "the leaf send failed: " << static_cast<int>(r.error());
    // The leaf never writes the flag, so the one the test set is still set.
    EXPECT_TRUE(session_test_access::send_slot_in_use(sess));
    const std::string e1 = expected_frame(seq0, kPayloadOne);
    const auto st = f.stored(sess, seq0, seq0);
    ASSERT_EQ(st.size(), 1U);
    EXPECT_EQ(st.at(seq0), e1);
    ASSERT_EQ(f.wire.size(), wire_before + 1);
    EXPECT_EQ(f.wire[wire_before], e1);
    session_test_access::send_slot_in_use(sess) = false;
}

TEST(B35SendSlot, CancelledInsideToApp_LeafPath_ThrowsAtAssignOutbound) {
    SlotFixture f;
    Session sess(f.engine, f.cfg());
    ASSERT_TRUE(f.open_to_active(sess));
    const seqnum_t seq0 = next_outbound(sess);
    const std::size_t wire_before = f.wire.size();
    const auto o = send_cancelled_in_to_app(
        f, sess, [&] { session_test_access::send_slot_in_use(sess) = true; });
    expect_cancelled_after_to_app(o, wire_before, seq0);
    session_test_access::send_slot_in_use(sess) = false;
}

}  // namespace
}  // namespace fixpp::session::test
