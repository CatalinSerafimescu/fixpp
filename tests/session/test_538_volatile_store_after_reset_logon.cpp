// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_538_volatile_store_after_reset_logon.cpp
//
// fixpp#538 (.specify/536-537-538-recovery-liveness.md §2.2, D-538-1; §5, the table
// "#538", "The lifted cells" and "Guard, session level").
//
// After a Logon exchange in which both sides carry ResetSeqNumFlag(141)=Y, the
// initiator's store is reset so that its first outbound number is 2. The application
// messages the session then sends must be replayed when the peer asks for them with a
// ResendRequest, on a volatile MemoryStore and on a FileStore.
//
// Expected behaviour: FIX Session Layer (Nov 2020 errata), §4.8.5 "Gap fill
// process": "The peer responding to a ResendRequest(35=2) message shall
// retransmit messages requested by the peer in message sequence number order,
// with the original sequence numbers and PossDupFlag(43) set to "Y"." The same
// section lets a sender choose not to retransmit an application message and
// gap fill over it. The session exposes no such choice, so a GapFill over the
// messages is a loss of the frames, not a decision.
//
// Shape. The store is a bounded MemoryStore from MemoryStoreFactory, except in
// the cells named FileStore_:
//   1. Logon exchange, both Logons at 34=1.
//   2. Two Session::send calls, 35=D, transmitted at 34=2 and 34=3.
//   3. Peer sends ResendRequest BeginSeqNo(7)=2 EndSeqNo(16)=0.
//   4. Assert the answer is the two messages with 43=Y under 34=2 and 34=3,
//      and holds no SequenceReset.
//
// Session cells (suite StoreAfterResetLogon):
//   ResetLogon_ResendReplays              the reset shape. Initiator with
//                                         reset_on_logon, so its Logon carries
//                                         141=Y; the peer's Logon carries 141=Y.
//   NoResetLogon_ResendReplays            control. No 141=Y on either side.
//   OwnResetOnly_ResendReplays            control. reset_on_logon, but the peer's
//                                         Logon carries no 141=Y, so the reset
//                                         before the Logon runs and the 141=Y
//                                         reset unit does not.
//   Acceptor_PeerResetLogon_ResendReplays control. Acceptor answering a Logon
//                                         that carries 141=Y.
//   ResetLogon_StoreHoldsBothSends        attribution. The reset shape up to
//                                         step 2, then the session's store is
//                                         read directly.
//   NoResetLogon_StoreHoldsBothSends      its control.
//   FileStore_ResetLogon_ResendReplays    the reset shape on a FileStore.
//   FileStore_NoResetLogon_ResendReplays  its control.
//   FileStore_ResetLogon_StoreHoldsBothSends
//                                         the attribution cell on a FileStore.
//   Guard_ResetLogon_ResendRequestBeforeAnySend_OneGapFill, and the same named
//   FileStore_                            the empty direction: step 1, then a
//                                         ResendRequest 7=1 16=0 with nothing
//                                         sent. The walk asks the store for
//                                         number 1 of a direction that holds no
//                                         frame; the answer is one GapFill.
//
// Store cells, no session (suite MemoryStoreAfterResetTo). They replay on a
// bare MemoryStore the calls the initiator makes on its store in the reset
// shape. Re-derive that sequence by reading, in src/session/session.cpp,
// emit_initiator_logon_ (reset_seqnums_to_one_durable, then store_then_emit of
// the Logon), the peer_ack_sent_reset_flag arm of the LogonSent handler (the
// targets it passes to run_reset_unit_), run_reset_unit_ (the store's
// reset_to), and send_impl (store_then_emit of each message):
//   FirstRetainedIsTwo   reset(), store(1), reset_to(2, 2), store(2), store(3).
//   FirstRetainedIsOne   control: the same with reset_to(1, 1) and three stores
//                        from 1, so the only difference is the sequence number
//                        of the first frame retained after the reset.
//
// The attribution and store cells tell two failures apart:
//   (i)  the store's outbound counter is left behind the session's, so every
//        store() is refused as out of order, and a volatile store's refusal is
//        not surfaced. Then store() fails, and the store's NextNumOut stays
//        below the session's.
//   (ii) store() succeeds, but the store cannot find a frame it holds when the
//        first frame retained after the reset is not number 1. Then store()
//        succeeds, the store's NextNumOut equals the session's, and retrieve
//        reports a gap.
// Each assertion below names the failure it selects.
//
// mock_clock: nothing here depends on elapsed time. HeartBtInt is long so the
// liveness loop stays parked for the whole cell.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
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
#include <memory>
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

namespace fixpp::session::test {

namespace {

constexpr std::string_view kSt52 = "20240101-00:00:00.000";

constexpr std::string_view kIdA = "B28-A";
constexpr std::string_view kIdB = "B28-B";
constexpr std::string_view kPayloadA =
    "35=D\x01"
    "11=B28-A\x01"
    "55=AAA\x01";
constexpr std::string_view kPayloadB =
    "35=D\x01"
    "11=B28-B\x01"
    "55=BBB\x01";

std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

std::string text_of(std::span<const std::byte> b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

// A frame from the peer (49=TW, 56=ISLD).
std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view extra = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=" + std::string(kSt52) + "\x01";
    body += "56=ISLD\x01";
    body += std::string(extra);
    std::string full =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
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

std::string describe(std::string_view frame) {
    return "[35=" + field(frame, 35) + " 34=" + field(frame, 34) + " 43=" + field(frame, 43) +
           " 36=" + field(frame, 36) + " 11=" + field(frame, 11) + "]";
}

std::string describe(const std::vector<std::string>& frames) {
    std::string out;
    for (const auto& f : frames) out += describe(f) + " ";
    return out.empty() ? "(none)" : out;
}

std::string describe(const expected_t<void>& r) {
    return r ? "ok" : "error " + std::to_string(static_cast<int>(r.error()));
}

std::string describe(const expected_t<seqnum_t>& r) {
    return r ? std::to_string(*r) : "error " + std::to_string(static_cast<int>(r.error()));
}

struct Capture {
    std::vector<std::string> frames;
    [[nodiscard]] std::vector<std::string> of_type(std::string_view msg_type) const {
        std::vector<std::string> out;
        for (const auto& f : frames) {
            if (field(f, 35) == msg_type) out.push_back(f);
        }
        return out;
    }
};

class CollectVisitor final : public retrieve_visitor {
public:
    std::vector<std::string> frames;

    asio::awaitable<expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        frames.push_back(text_of(frame));
        co_return visit_result::cont;
    }
};

// One retrieve call over the outbound direction: its status and what it visited.
struct Retrieved {
    expected_t<void> status;
    std::vector<std::string> frames;
};

std::string describe(const Retrieved& r) {
    return describe(r.status) + ", visited " + describe(r.frames);
}

enum class StoreKind { memory, file };

struct LogonShape {
    session_role role = session_role::initiator;
    bool own_reset_on_logon = false;  // SessionConfig::reset_on_logon
    bool peer_logon_has_141 = false;  // the peer's Logon carries 141=Y
    StoreKind store = StoreKind::memory;
};

// The store's own account of the outbound direction after the two sends.
struct StoreReading {
    expected_t<seqnum_t> next_out{0};
    Retrieved slot2;
    Retrieved slot3;
    Retrieved range;

    [[nodiscard]] std::string text() const {
        return "store NextNumOut=" + describe(next_out) + "; retrieve(2,2): " + describe(slot2) +
               "; retrieve(3,3): " + describe(slot3) + "; retrieve(2,3): " + describe(range) + ".";
    }
};

class StoreCellBase : public ::testing::Test {
protected:
    asio::io_context ioc;
    // The FileStore cells' file-I/O executor. Declared after ioc, so it is
    // joined before ioc is destroyed.
    asio::thread_pool file_pool{1};
    std::filesystem::path file_dir;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    void TearDown() override {
        if (!file_dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(file_dir, ec);
        }
    }

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    static MemoryStore::Config store_cfg() {
        MemoryStore::Config sc;
        sc.inbound_capacity = 100;
        sc.outbound_capacity = 100;
        sc.max_frame_bytes = 4096;
        return sc;
    }

    template <class T>
    expected_t<T> run_sync(asio::awaitable<expected_t<T>> coro, const char* site) {
        auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    Retrieved retrieve(MessageStore& store, seqnum_t begin, seqnum_t end) {
        Retrieved out;
        CollectVisitor v;
        out.status = run_sync(store.retrieve(begin, end, direction_t::outbound, v),
                              "VolatileStore::retrieve");
        out.frames = std::move(v.frames);
        return out;
    }

    StoreReading read_store(MessageStore& store) {
        StoreReading r;
        r.next_out =
            run_sync(store.next_seqnum(direction_t::outbound, false), "VolatileStore::next");
        r.slot2 = retrieve(store, 2, 2);
        r.slot3 = retrieve(store, 3, 3);
        r.range = retrieve(store, 2, 3);
        return r;
    }

    // What a store that holds `a` under 2 and `b` under 3, with NextNumOut 4,
    // reports. `context` is appended to every failure.
    static void expect_holds_two_and_three(const StoreReading& got, const std::string& a,
                                           const std::string& b, const std::string& context) {
        ASSERT_TRUE(got.next_out.has_value()) << context;
        EXPECT_EQ(*got.next_out, 4U)
            << "the store's NextNumOut must follow the two stores at 2 and 3. A value below 4 "
               "selects mechanism (i): store() was refused. "
            << context;
        const std::vector<std::string> only_a{a};
        const std::vector<std::string> only_b{b};
        const std::vector<std::string> both{a, b};
        EXPECT_TRUE(got.slot2.status.has_value() && got.slot2.frames == only_a)
            << "retrieve(2,2) must visit the frame stored under 2. With NextNumOut at 4, a gap "
               "here selects mechanism (ii). "
            << context;
        EXPECT_TRUE(got.slot3.status.has_value() && got.slot3.frames == only_b)
            << "retrieve(3,3) must visit the frame stored under 3. With NextNumOut at 4, a gap "
               "here selects mechanism (ii). "
            << context;
        EXPECT_TRUE(got.range.status.has_value() && got.range.frames == both)
            << "retrieve(2,3) must visit both stored frames in order. " << context;
    }
};

class StoreAfterResetLogon : public StoreCellBase {
protected:
    SessionConfig make_cfg(Capture& cap, const LogonShape& shape) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = shape.role;
        cfg.reset_on_logon = shape.own_reset_on_logon;
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [&cap](std::span<const std::byte> f) {
            cap.frames.push_back(text_of(f));
        };
        if (shape.store == StoreKind::file) {
            file_dir = fixpp::test_support::unique_temp_dir("b28_538");
            FileStore::Config fc;
            fc.directory = file_dir;
            fc.max_frame_bytes = 4096;
            fc.file_io_executor = file_pool.get_executor();
            cfg.store_factory = std::make_shared<FileStoreFactory>(fc);
        } else {
            cfg.store_factory = std::make_shared<MemoryStoreFactory>(store_cfg());
        }
        return cfg;
    }

    expected_t<void> feed(Session& s, const std::vector<std::byte>& f) {
        return run_sync(s.on_inbound_frame(std::span<const std::byte>{f}), "VolatileStore::feed");
    }

    // Step 1. Every ASSERT here and in establish_and_send_two is a setup
    // precondition: a failure means the cell did not drive the shape, not a verdict.
    void establish(Session& s, Capture& cap, const LogonShape& shape) {
        ASSERT_TRUE(run_sync(s.open(), "VolatileStore::open").has_value()) << "setup: open()";
        (void)feed(s, make_frame("A", 1,
                                 shape.peer_logon_has_141 ? "98=0\x01"
                                                            "108=30\x01"
                                                            "141=Y\x01"
                                                          : "98=0\x01"
                                                            "108=30\x01"));
        ASSERT_EQ(s.state(), fsm_state::Active) << "setup: session did not reach Active";

        const auto own_logon = cap.of_type("A");
        ASSERT_EQ(own_logon.size(), 1U) << "setup: the session must have sent one Logon";
        ASSERT_EQ(field(own_logon.front(), 34), "1") << "setup: the session's Logon is 34=1";
        // The initiator's Logon carries 141=Y when reset_on_logon is set; the
        // acceptor's reply mirrors the peer's flag under bilateral_lenient.
        const bool own_141_expected = shape.role == session_role::initiator
                                          ? shape.own_reset_on_logon
                                          : shape.peer_logon_has_141;
        ASSERT_EQ(field(own_logon.front(), 141), own_141_expected ? "Y" : "")
            << "setup: 141 on the session's own Logon";
        auto& mgr = session_test_access::seqnum_mgr(s);
        ASSERT_EQ(mgr.next_outbound_unsafe(), 2U) << "setup: NextNumOut after the Logon exchange";
        ASSERT_EQ(mgr.next_inbound_unsafe(), 2U) << "setup: NextNumIn after the Logon exchange";
        cap.frames.clear();
    }

    // Steps 1 and 2. On return `sent_a` and `sent_b` are the two application
    // frames as transmitted.
    void establish_and_send_two(Session& s, Capture& cap, const LogonShape& shape,
                                std::string& sent_a, std::string& sent_b) {
        ASSERT_NO_FATAL_FAILURE(establish(s, cap, shape));
        auto& mgr = session_test_access::seqnum_mgr(s);
        const auto a = bytes_of(kPayloadA);
        const auto b = bytes_of(kPayloadB);
        ASSERT_TRUE(run_sync(s.send(a), "VolatileStore::send_a").has_value()) << "setup: send A";
        ASSERT_TRUE(run_sync(s.send(b), "VolatileStore::send_b").has_value()) << "setup: send B";
        ASSERT_EQ(cap.frames.size(), 2U)
            << "setup: two frames transmitted: " << describe(cap.frames);
        sent_a = cap.frames[0];
        sent_b = cap.frames[1];
        ASSERT_EQ(describe(sent_a), "[35=D 34=2 43= 36= 11=" + std::string(kIdA) + "]");
        ASSERT_EQ(describe(sent_b), "[35=D 34=3 43= 36= 11=" + std::string(kIdB) + "]");
        ASSERT_EQ(mgr.next_outbound_unsafe(), 4U) << "setup: NextNumOut after the two sends";
        cap.frames.clear();
    }

    void run_resend_cell(const LogonShape& shape) {
        Capture cap;
        auto cfg = make_cfg(cap, shape);
        Session s{engine, cfg};
        std::string sent_a;
        std::string sent_b;
        ASSERT_NO_FATAL_FAILURE(establish_and_send_two(s, cap, shape, sent_a, sent_b));

        // Step 3: ResendRequest for 2 through the current end.
        const auto rr_r = feed(s, make_frame("2", 2,
                                             "7=2\x01"
                                             "16=0\x01"));
        const std::string context =
            "Frames sent in answer to ResendRequest 7=2 16=0, in order: " + describe(cap.frames) +
            "; on_inbound_frame: " + describe(rr_r) + ".";

        // Step 4.
        EXPECT_TRUE(cap.of_type("4").empty())
            << "the two application messages were answered with a SequenceReset instead of "
               "being resent. "
            << context;
        const auto replayed = cap.of_type("D");
        ASSERT_EQ(replayed.size(), 2U) << "both application messages must be resent. " << context;
        EXPECT_EQ(describe(replayed[0]), "[35=D 34=2 43=Y 36= 11=" + std::string(kIdA) + "]")
            << context;
        EXPECT_EQ(describe(replayed[1]), "[35=D 34=3 43=Y 36= 11=" + std::string(kIdB) + "]")
            << context;
        EXPECT_EQ(s.state(), fsm_state::Active) << context;
    }

    void run_store_cell(const LogonShape& shape) {
        Capture cap;
        auto cfg = make_cfg(cap, shape);
        Session s{engine, cfg};
        std::string sent_a;
        std::string sent_b;
        ASSERT_NO_FATAL_FAILURE(establish_and_send_two(s, cap, shape, sent_a, sent_b));

        MessageStore* store = session_test_access::store(s);
        ASSERT_NE(store, nullptr) << "setup: the session must hold a store";
        const StoreReading got = read_store(*store);
        const std::string context =
            "Session NextNumOut=" +
            std::to_string(session_test_access::seqnum_mgr(s).next_outbound_unsafe()) + "; " +
            got.text();
        std::printf("#538 observation (session store): %s\n", context.c_str());
        expect_holds_two_and_three(got, sent_a, sent_b, context);
    }

    // The empty direction at session level: the 141=Y exchange, then the peer's
    // ResendRequest 7=1 16=0 before the session has sent anything. The store's outbound
    // direction holds no frame and its counter is 2, so the walk asks for number 1.
    void run_resend_before_any_send_cell(StoreKind store) {
        const LogonShape shape{.role = session_role::initiator,
                               .own_reset_on_logon = true,
                               .peer_logon_has_141 = true,
                               .store = store};
        Capture cap;
        auto cfg = make_cfg(cap, shape);
        Session s{engine, cfg};
        ASSERT_NO_FATAL_FAILURE(establish(s, cap, shape));
        MessageStore* store_ptr = session_test_access::store(s);
        ASSERT_NE(store_ptr, nullptr) << "setup: the session must hold a store";
        const auto next_out =
            run_sync(store_ptr->next_seqnum(direction_t::outbound, false), "VolatileStore::next");
        ASSERT_TRUE(next_out.has_value() && *next_out == 2U)
            << "setup: the store's NextNumOut after the 141=Y exchange: " << describe(next_out);

        const auto rr_r = feed(s, make_frame("2", 2,
                                             "7=1\x01"
                                             "16=0\x01"));
        const std::string context =
            "Frames sent in answer to ResendRequest 7=1 16=0, in order: " + describe(cap.frames) +
            "; on_inbound_frame: " + describe(rr_r) + ".";

        EXPECT_TRUE(rr_r.has_value()) << context;
        ASSERT_EQ(cap.frames.size(), 1U) << "the answer must be one frame. " << context;
        EXPECT_EQ(field(cap.frames[0], 35), "4") << context;
        EXPECT_EQ(field(cap.frames[0], 123), "Y") << "the frame must be a GapFill. " << context;
        EXPECT_EQ(field(cap.frames[0], 34), "1") << context;
        EXPECT_EQ(field(cap.frames[0], 36), "2") << context;
        EXPECT_EQ(s.state(), fsm_state::Active) << context;
    }
};

class MemoryStoreAfterResetTo : public StoreCellBase {
protected:
    // reset(), store(1), reset_to(first, first), then stores from `first`
    // through 3. `first` is 1 or 2.
    void run_unit_cell(seqnum_t first) {
        MemoryStore store{store_cfg()};
        // The store does not parse a frame; these only need to differ and to
        // print legibly through describe().
        const auto logon = bytes_of(
            "8=FIX.4.2\x01"
            "35=A\x01"
            "34=1\x01");
        const auto f1 = bytes_of(
            "8=FIX.4.2\x01"
            "35=D\x01"
            "34=1\x01"
            "11=UNIT-1\x01");
        const auto f2 = bytes_of(
            "8=FIX.4.2\x01"
            "35=D\x01"
            "34=2\x01"
            "11=UNIT-2\x01");
        const auto f3 = bytes_of(
            "8=FIX.4.2\x01"
            "35=D\x01"
            "34=3\x01"
            "11=UNIT-3\x01");
        const auto out = direction_t::outbound;

        ASSERT_TRUE(run_sync(store.reset(), "MemoryStoreUnit::reset").has_value());
        ASSERT_TRUE(run_sync(store.store(1, logon, out), "MemoryStoreUnit::logon").has_value());
        ASSERT_TRUE(run_sync(store.reset_to(first, first), "MemoryStoreUnit::reset_to").has_value())
            << "reset_to(" << first << ", " << first << ")";

        std::string stores;
        if (first == 1) {
            const auto r1 = run_sync(store.store(1, f1, out), "MemoryStoreUnit::store1");
            stores += "store(1): " + describe(r1) + "; ";
            EXPECT_TRUE(r1.has_value());
        }
        const auto r2 = run_sync(store.store(2, f2, out), "MemoryStoreUnit::store2");
        const auto r3 = run_sync(store.store(3, f3, out), "MemoryStoreUnit::store3");
        stores += "store(2): " + describe(r2) + "; store(3): " + describe(r3) + "; ";

        const StoreReading got = read_store(store);
        const std::string context = "After reset_to(" + std::to_string(first) + ", " +
                                    std::to_string(first) + "): " + stores + got.text();
        std::printf("#538 observation (bare MemoryStore): %s\n", context.c_str());

        EXPECT_TRUE(r2.has_value())
            << "store(2) must be accepted. A refusal selects mechanism (i). " << context;
        EXPECT_TRUE(r3.has_value())
            << "store(3) must be accepted. A refusal selects mechanism (i). " << context;
        expect_holds_two_and_three(got, text_of(f2), text_of(f3), context);
    }
};

}  // namespace

TEST_F(StoreAfterResetLogon, ResetLogon_ResendReplays) {
    run_resend_cell(
        {.role = session_role::initiator, .own_reset_on_logon = true, .peer_logon_has_141 = true});
}

TEST_F(StoreAfterResetLogon, NoResetLogon_ResendReplays) {
    run_resend_cell({.role = session_role::initiator,
                     .own_reset_on_logon = false,
                     .peer_logon_has_141 = false});
}

TEST_F(StoreAfterResetLogon, OwnResetOnly_ResendReplays) {
    run_resend_cell(
        {.role = session_role::initiator, .own_reset_on_logon = true, .peer_logon_has_141 = false});
}

TEST_F(StoreAfterResetLogon, Acceptor_PeerResetLogon_ResendReplays) {
    run_resend_cell(
        {.role = session_role::acceptor, .own_reset_on_logon = false, .peer_logon_has_141 = true});
}

TEST_F(StoreAfterResetLogon, ResetLogon_StoreHoldsBothSends) {
    run_store_cell(
        {.role = session_role::initiator, .own_reset_on_logon = true, .peer_logon_has_141 = true});
}

TEST_F(StoreAfterResetLogon, NoResetLogon_StoreHoldsBothSends) {
    run_store_cell({.role = session_role::initiator,
                    .own_reset_on_logon = false,
                    .peer_logon_has_141 = false});
}

TEST_F(StoreAfterResetLogon, FileStore_ResetLogon_ResendReplays) {
    run_resend_cell({.role = session_role::initiator,
                     .own_reset_on_logon = true,
                     .peer_logon_has_141 = true,
                     .store = StoreKind::file});
}

TEST_F(StoreAfterResetLogon, FileStore_NoResetLogon_ResendReplays) {
    run_resend_cell({.role = session_role::initiator,
                     .own_reset_on_logon = false,
                     .peer_logon_has_141 = false,
                     .store = StoreKind::file});
}

TEST_F(StoreAfterResetLogon, FileStore_ResetLogon_StoreHoldsBothSends) {
    run_store_cell({.role = session_role::initiator,
                    .own_reset_on_logon = true,
                    .peer_logon_has_141 = true,
                    .store = StoreKind::file});
}

TEST_F(StoreAfterResetLogon, Guard_ResetLogon_ResendRequestBeforeAnySend_OneGapFill) {
    run_resend_before_any_send_cell(StoreKind::memory);
}

TEST_F(StoreAfterResetLogon, Guard_FileStore_ResetLogon_ResendRequestBeforeAnySend_OneGapFill) {
    run_resend_before_any_send_cell(StoreKind::file);
}

TEST_F(MemoryStoreAfterResetTo, FirstRetainedIsTwo) { run_unit_cell(2); }

TEST_F(MemoryStoreAfterResetTo, FirstRetainedIsOne) { run_unit_cell(1); }

}  // namespace fixpp::session::test
