// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/length_data_logon_refusal_test.cpp — C-ABI observers of a refused Logon:
// 091 (fixpp#418) FR-020 and 092 (fixpp#507) D-1/D-2.
//
// CapiLogonMalformedCount (091): a C-ABI acceptor engine faces a raw TCP peer that
// writes a hand-built Logon and then one application message (35=D, 34=2) in a
// single write. The Logon carries RawDataLength(95) and RawData(96) and then
// EncryptMethod(98)=0, so the count is the only thing that can refuse it: 95=2
// makes the counted value end on a byte that is not SOH (FR-020), 95=1 is the
// well-formed twin.
//
// CapiLogonMalformedTag (092): the same shape, with a field whose tag is not a
// well-formed tag number (9x9=1) where RawDataLength and RawData sit, and 909=1
// as the twin that differs in that one byte. It runs in both roles:
//   - acceptor: the raw peer is the initiator, as above;
//   - initiator: the raw peer is the acceptor. It listens before the C-ABI
//     engine starts, reads the engine's Logon, and answers with the Logon reply
//     (141=Y, since the initiator resets on Logon) and the application message
//     in a single write.
//
// The C-ABI effects (include/fix/c_api/session.h, the clause on each declaration)
// are asserted in two phases, because after the drain the session is not Active
// on either code path (where it was Active, onLogout has cleared `established`),
// so is_established, send and the toApp count no longer discriminate there:
//   1. once the Logon arm has settled, while the peer is still connected:
//      fixpp_session_is_established, fixpp_session_send and the toApp callback
//      count;
//   2. after the peer disconnects and the session has drained: the receive
//      callback count and fixpp_session_close.
//
// The Logon arm settles in Active or Disconnected; the barrier reads the
// session's state on its own strand (an off-strand read is a data race, see
// Engine::send) and waits for one of those two.
//
// Anchors: specs/091-data-field-bytes/spec.md FR-020 (C-ABI effect);
//          data-model.md Appendix A; spec/behaviors-and-limitations.md B-091-4;
//          specs/092-garbled-frame-reject/research.md R-8 (C-ABI 1.10) and
//          contracts D-1/D-2.

#include <gtest/gtest.h>

#include <array>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "capi_drain_support.hpp"
#include "capi_internal.hpp"
#include "capi_loopback_support.hpp"
#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"
#include "fixpp/core/fix_time.hpp"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

namespace {

constexpr char const* kAcceptorCompId = "ACC-LR";
constexpr char const* kPeerCompId = "INI-LR";

// The HeartBtInt configured on the acceptor and carried in the peer's 108=. A
// heartbeat-driven path is the earliest that can move an Active session off
// Active, so the whole observation window must stay below one interval.
constexpr int kHeartBtIntSeconds = 30;
constexpr std::chrono::milliseconds kHeartBtInt{kHeartBtIntSeconds * 1000};

// The accept loop's bound on reading the first frame (src/session/engine.cpp,
// kFirstFrameDeadline), spelled here independently. A frame that is never routed
// leaves the session NotConnected, so this deadline cannot settle the barrier;
// past it the frame has either been routed or dropped.
constexpr std::chrono::milliseconds kFirstFrameDeadline{5000};

constexpr std::chrono::milliseconds kSettleBudget = kFirstFrameDeadline;
constexpr std::chrono::milliseconds kDrainBudget = kHeartBtInt / 4;
constexpr std::chrono::milliseconds kBoundPortBudget = kFirstFrameDeadline;
static_assert(kSettleBudget + kDrainBudget < kHeartBtInt,
              "the observation window must end before a heartbeat-driven path can fire");

// The initiator cells' bounds. No timer moves a session out of LogonSent (the
// liveness loop is spawned on the transition to Active; confirm with
// `grep -n run_liveness_loop src/session/session.cpp`), so the settle bound
// only limits how long a reply that is never processed is waited for: it cannot
// settle the barrier itself.
constexpr std::chrono::milliseconds kPeerAcceptBudget = kFirstFrameDeadline;
constexpr std::chrono::milliseconds kPeerReadBudget = kFirstFrameDeadline;
constexpr std::chrono::milliseconds kInitiatorSettleBudget = kFirstFrameDeadline;
static_assert(kPeerAcceptBudget + kPeerReadBudget + kInitiatorSettleBudget + kDrainBudget <
                  kHeartBtInt,
              "the initiator's observation window must end before a heartbeat-driven path "
              "can fire");

// SendingTime(52) from the real clock: the C-ABI engine runs a real-time clock and
// the acceptor checks SendingTime against it.
std::string utc_now_sending_time() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    if (!r) {
        ADD_FAILURE() << "utc_time_to_fix_string failed";
        return {};
    }
    return std::string{r->data(), r->size()};
}

std::string frame_fix42(std::string const& body) {
    std::string full =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    return full + "10=" + csbuf + "\x01";
}

// `tag=value<SOH>` for each pair, in order.
std::string fix_fields(std::initializer_list<std::pair<int, std::string>> fields) {
    std::string out;
    for (auto const& [tag, value] : fields) {
        out += std::to_string(tag) + "=" + value + "\x01";
    }
    return out;
}

// The Logon from `sender` to `target` then one NewOrderSingle, back to back.
// `extra_logon_fields` is spliced into the Logon verbatim ahead of
// EncryptMethod(98), so it can carry a field whose tag is not a well-formed number.
std::string logon_then_order(std::string_view sender, std::string_view target,
                             std::string_view extra_logon_fields, bool reset_seq_num_flag) {
    std::string const ts = utc_now_sending_time();
    std::string logon_body = fix_fields({{35, "A"},
                                         {34, "1"},
                                         {49, std::string{sender}},
                                         {52, ts},
                                         {56, std::string{target}},
                                         {108, std::to_string(kHeartBtIntSeconds)}});
    if (reset_seq_num_flag) {
        logon_body += fix_fields({{141, "Y"}});
    }
    logon_body += extra_logon_fields;
    logon_body += fix_fields({{98, "0"}});
    std::string const logon = frame_fix42(logon_body);
    std::string const order = frame_fix42(fix_fields({{35, "D"},
                                                      {34, "2"},
                                                      {49, std::string{sender}},
                                                      {52, ts},
                                                      {56, std::string{target}},
                                                      {11, "ORD1"},
                                                      {55, "TESTSYM"},
                                                      {54, "1"},
                                                      {38, "100"},
                                                      {40, "1"}}));
    return logon + order;
}

// RawDataLength(95) and RawData(96); only the count differs between 091's refused
// cell and its twin.
std::string raw_data_fields(std::string_view raw_data_length) {
    return fix_fields({{95, std::string{raw_data_length}}, {96, "x"}});
}

// Reads the session's FSM state on its strand until it is Active or Disconnected,
// the two states the acceptor's Logon arm leaves it in. Returns the last state
// read, or NotConnected if the session was never found. Each read is waited on no
// later than `budget` and holds only a weak_ptr, so a read the strand never runs
// is abandoned without keeping the Session alive.
fixpp::session::fsm_state settled_logon_state(fixpp_engine_t* engine,
                                              fixpp::session::SessionId const& id,
                                              std::chrono::milliseconds budget) {
    using fixpp::session::fsm_state;
    auto* e = reinterpret_cast<fixpp_engine*>(engine);
    auto const until = std::chrono::steady_clock::now() + budget;
    fsm_state last = fsm_state::NotConnected;
    for (;;) {
        if (e->state_ != nullptr && e->state_->engine_.has_value()) {
            std::shared_ptr<fixpp::session::Session> sess = e->state_->engine_->lookup(id);
            if (sess != nullptr) {
                std::weak_ptr<fixpp::session::Session> const weak = sess;
                auto fut = asio::co_spawn(
                    sess->executor().underlying(),
                    [weak]() -> asio::awaitable<fsm_state> {
                        if (auto s = weak.lock()) co_return s->state();
                        co_return fsm_state::NotConnected;
                    },
                    asio::use_future);
                if (fut.wait_until(until) != std::future_status::ready) return last;
                last = fut.get();
                if (last == fsm_state::Active || last == fsm_state::Disconnected) return last;
            }
        }
        if (std::chrono::steady_clock::now() >= until) return last;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

struct Counts {
    std::atomic<int> received{0};
    std::atomic<int> to_app{0};
};

void on_receive(const fixpp_msg_t* /*inbound*/, void* ud) {
    static_cast<Counts*>(ud)->received.fetch_add(1);
}

fixpp_toapp_verdict on_to_app(const fixpp_msg_t* /*outbound*/, void* ud) {
    static_cast<Counts*>(ud)->to_app.fetch_add(1);
    return FIXPP_TOAPP_SEND;
}

// One acceptor engine and its session, started, with a connected raw peer that
// has written its Logon, carrying `extra_logon_fields`, and one application message.
struct AcceptorCell {
    fixpp_engine_t* engine = nullptr;
    fixpp_session_t* session = nullptr;
    fixpp::session::SessionId id{};
    Counts counts;
    asio::io_context peer_ioc;
    asio::ip::tcp::socket peer{peer_ioc};
    bool ready = false;

    explicit AcceptorCell(std::string_view extra_logon_fields) {
        // Consumer minor 9: the 1.9 codes are returned untranslated.
        if (fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR, 9, &engine) !=
            FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_engine_create failed";
            return;
        }
        fixpp_session_config_t* cfg =
            make_session_cfg(kAcceptorCompId, kPeerCompId, FIXPP_ROLE_ACCEPTOR);
        EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(cfg, kHeartBtIntSeconds),
                  FIXPP_ERR_OK);
        set_loopback_endpoint(cfg, "127.0.0.1", 0);
        id = session_id_of(cfg);
        if (fixpp_session_open(engine, cfg, &session) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_session_open failed";
            return;
        }
        EXPECT_EQ(fixpp_session_register_callback(session, on_receive, &counts), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_register_send_callback(session, on_to_app, &counts), FIXPP_ERR_OK);
        if (fixpp_engine_start(engine) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_engine_start failed";
            return;
        }
        std::uint16_t port = 0;
        if (!fixpp::test_support::wait_until_observed(
                [&] {
                    return fixpp_session_acceptor_bound_endpoint(session, &port) == FIXPP_ERR_OK &&
                           port != 0;
                },
                kBoundPortBudget)) {
            ADD_FAILURE() << "the acceptor never bound a port";
            return;
        }
        std::error_code ec;
        peer.connect({asio::ip::make_address("127.0.0.1"), port}, ec);
        if (ec) {
            ADD_FAILURE() << "peer connect: " << ec.message();
            return;
        }
        std::string const bytes =
            logon_then_order(kPeerCompId, kAcceptorCompId, extra_logon_fields, false);
        asio::write(peer, asio::buffer(bytes), ec);
        if (ec) {
            ADD_FAILURE() << "peer write: " << ec.message();
            return;
        }
        ready = true;
    }

    void disconnect_peer() {
        std::error_code ec;
        peer.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        peer.close(ec);
    }

    ~AcceptorCell() {
        disconnect_peer();
        if (engine != nullptr) fixpp_engine_destroy(engine);
    }

    AcceptorCell(AcceptorCell const&) = delete;
    AcceptorCell& operator=(AcceptorCell const&) = delete;
};

// True once `bytes` holds a whole frame: a CheckSum(10) field with its three
// digits and the closing SOH.
bool holds_whole_frame(std::string_view bytes) {
    constexpr std::string_view kTrailer{
        "\x01"
        "10="};
    auto const at = bytes.find(kTrailer);
    return at != std::string_view::npos && bytes.size() >= at + kTrailer.size() + 4 &&
           bytes[at + kTrailer.size() + 3] == '\x01';
}

// One initiator engine and its session, started, facing a raw acceptor peer that
// has read the engine's Logon and answered with its Logon reply, carrying
// `extra_logon_fields`, and one application message. Every peer operation runs
// on `peer_ioc` under a bound; on a timeout the pending operation is cancelled by
// closing its socket and `peer_ioc` runs until the handler has retired, before
// the locals the handler references go out of scope.
struct InitiatorCell {
    fixpp_engine_t* engine = nullptr;
    fixpp_session_t* session = nullptr;
    fixpp::session::SessionId id{};
    Counts counts;
    asio::io_context peer_ioc;
    asio::ip::tcp::acceptor listener{peer_ioc};
    asio::ip::tcp::socket peer{peer_ioc};
    bool ready = false;

    explicit InitiatorCell(std::string_view extra_logon_fields) {
        std::error_code ec;
        asio::ip::tcp::endpoint const any_port{asio::ip::make_address("127.0.0.1"), 0};
        listener.open(any_port.protocol(), ec);
        if (!ec) listener.bind(any_port, ec);
        // Listening before the engine starts: the initiator connects once, so a
        // refused connect would leave the session without a transport.
        if (!ec) listener.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) {
            ADD_FAILURE() << "peer listen: " << ec.message();
            return;
        }
        std::uint16_t const port = listener.local_endpoint().port();

        // Consumer minor 9: the 1.9 codes are returned untranslated.
        if (fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR, 9, &engine) !=
            FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_engine_create failed";
            return;
        }
        fixpp_session_config_t* cfg =
            make_session_cfg(kPeerCompId, kAcceptorCompId, FIXPP_ROLE_INITIATOR);
        EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(cfg, kHeartBtIntSeconds),
                  FIXPP_ERR_OK);
        set_loopback_endpoint(cfg, "127.0.0.1", port);
        id = session_id_of(cfg);
        if (fixpp_session_open(engine, cfg, &session) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_session_open failed";
            return;
        }
        EXPECT_EQ(fixpp_session_register_callback(session, on_receive, &counts), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_register_send_callback(session, on_to_app, &counts), FIXPP_ERR_OK);
        if (fixpp_engine_start(engine) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_engine_start failed";
            return;
        }

        bool accepted = false;
        std::error_code accept_ec;
        listener.async_accept(peer, [&](std::error_code e) {
            accepted = true;
            accept_ec = e;
        });
        peer_ioc.run_for(kPeerAcceptBudget);
        if (!accepted) {
            listener.close(ec);
            peer_ioc.restart();
            peer_ioc.run();
            ADD_FAILURE() << "the initiator never connected to the peer";
            return;
        }
        if (accept_ec) {
            ADD_FAILURE() << "peer accept: " << accept_ec.message();
            return;
        }

        std::string inbound;
        std::array<char, 512> chunk{};
        auto const read_until = std::chrono::steady_clock::now() + kPeerReadBudget;
        while (!holds_whole_frame(inbound)) {
            bool read_done = false;
            std::error_code read_ec;
            std::size_t n = 0;
            peer.async_read_some(asio::buffer(chunk), [&](std::error_code e, std::size_t k) {
                read_done = true;
                read_ec = e;
                n = k;
            });
            peer_ioc.restart();
            peer_ioc.run_until(read_until);
            if (!read_done) {
                peer.close(ec);
                peer_ioc.restart();
                peer_ioc.run();
                ADD_FAILURE() << "the initiator's Logon did not arrive whole";
                return;
            }
            if (read_ec) {
                ADD_FAILURE() << "peer read: " << read_ec.message();
                return;
            }
            inbound.append(chunk.data(), n);
        }
        if (inbound.find(std::string_view{"\x01"
                                          "35=A\x01"}) == std::string::npos) {
            ADD_FAILURE() << "the initiator's first frame is not a Logon";
            return;
        }

        std::string const bytes =
            logon_then_order(kAcceptorCompId, kPeerCompId, extra_logon_fields, true);
        asio::write(peer, asio::buffer(bytes), ec);
        if (ec) {
            ADD_FAILURE() << "peer write: " << ec.message();
            return;
        }
        ready = true;
    }

    void disconnect_peer() {
        std::error_code ec;
        peer.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        peer.close(ec);
        listener.close(ec);
    }

    ~InitiatorCell() {
        disconnect_peer();
        if (engine != nullptr) fixpp_engine_destroy(engine);
    }

    InitiatorCell(InitiatorCell const&) = delete;
    InitiatorCell& operator=(InitiatorCell const&) = delete;
};

// 092's refused cell, in either role: the Logon arm has settled and every
// observer reads the session as never established (research R-8, D-1/D-2).
template <class Cell>
void expect_refused_on_every_observer(Cell& cell, std::chrono::milliseconds settle_budget) {
    auto const settled = settled_logon_state(cell.engine, cell.id, settle_budget);
    ASSERT_TRUE(settled == fixpp::session::fsm_state::Active ||
                settled == fixpp::session::fsm_state::Disconnected)
        << "the Logon arm did not settle within the budget";

    // Phase 1: the peer is still connected.
    bool established = true;
    EXPECT_EQ(fixpp_session_is_established(cell.session, &established), FIXPP_ERR_OK);
    EXPECT_FALSE(established) << "the session established on a Logon with a malformed tag";
    auto const payload = make_app_payload("OUT1");
    EXPECT_EQ(fixpp_session_send(cell.session, payload.data(), payload.size()),
              FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(cell.counts.to_app.load(), 0) << "toApp fired on a refused session";

    // Phase 2: the peer disconnects and the session drains.
    cell.disconnect_peer();
    EXPECT_TRUE(wait_for_acceptor_drained(cell.engine, cell.id, kDrainBudget))
        << "the session did not drain after the peer disconnected";
    EXPECT_EQ(cell.counts.received.load(), 0)
        << "the peer's application message was delivered on a refused session";
    EXPECT_EQ(fixpp_session_close(cell.session), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
}

// The well-formed twin, in either role: the Logon establishes on every observer.
template <class Cell>
void expect_established_on_every_observer(Cell& cell, std::chrono::milliseconds settle_budget) {
    EXPECT_EQ(settled_logon_state(cell.engine, cell.id, settle_budget),
              fixpp::session::fsm_state::Active);

    bool established = false;
    EXPECT_EQ(fixpp_session_is_established(cell.session, &established), FIXPP_ERR_OK);
    EXPECT_TRUE(established);
    auto const payload = make_app_payload("OUT1");
    EXPECT_EQ(fixpp_session_send(cell.session, payload.data(), payload.size()), FIXPP_ERR_OK);
    EXPECT_EQ(cell.counts.to_app.load(), 1);

    cell.disconnect_peer();
    EXPECT_TRUE(wait_for_acceptor_drained(cell.engine, cell.id, kDrainBudget))
        << "the session did not drain after the peer disconnected";
    EXPECT_EQ(cell.counts.received.load(), 1);
    EXPECT_EQ(fixpp_session_close(cell.session), FIXPP_ERR_OK);
}

// A field whose tag is not a well-formed tag number, and its twin that differs in
// that one byte.
constexpr std::string_view kMalformedTagField{"9x9=1\x01"};
constexpr std::string_view kWellFormedTagField{"909=1\x01"};

}  // namespace

TEST(CapiLogonMalformedCount, AcceptorMalformedCountIsRefusedOnEveryObserver) {
    AcceptorCell cell{raw_data_fields("2")};
    ASSERT_TRUE(cell.ready);

    auto const settled = settled_logon_state(cell.engine, cell.id, kSettleBudget);
    ASSERT_TRUE(settled == fixpp::session::fsm_state::Active ||
                settled == fixpp::session::fsm_state::Disconnected)
        << "the Logon arm did not settle within the budget";

    // Phase 1: the peer is still connected.
    bool established = true;
    EXPECT_EQ(fixpp_session_is_established(cell.session, &established), FIXPP_ERR_OK);
    EXPECT_FALSE(established) << "the session established on a Logon with a malformed 95 count";
    auto const payload = make_app_payload("OUT1");
    EXPECT_EQ(fixpp_session_send(cell.session, payload.data(), payload.size()),
              FIXPP_ERR_SESSION_INVALID_STATE);
    EXPECT_EQ(cell.counts.to_app.load(), 0) << "toApp fired on a refused session";

    // Phase 2: the peer disconnects and the session drains.
    cell.disconnect_peer();
    EXPECT_TRUE(wait_for_acceptor_drained(cell.engine, cell.id, kDrainBudget))
        << "the acceptor did not drain after the peer disconnected";
    EXPECT_EQ(cell.counts.received.load(), 0)
        << "the peer's application message was delivered on a refused session";
    EXPECT_EQ(fixpp_session_close(cell.session), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
}

TEST(CapiLogonMalformedCount, AcceptorWellFormedCountEstablishesOnEveryObserver) {
    AcceptorCell cell{raw_data_fields("1")};
    ASSERT_TRUE(cell.ready);

    EXPECT_EQ(settled_logon_state(cell.engine, cell.id, kSettleBudget),
              fixpp::session::fsm_state::Active);

    bool established = false;
    EXPECT_EQ(fixpp_session_is_established(cell.session, &established), FIXPP_ERR_OK);
    EXPECT_TRUE(established);
    auto const payload = make_app_payload("OUT1");
    EXPECT_EQ(fixpp_session_send(cell.session, payload.data(), payload.size()), FIXPP_ERR_OK);
    EXPECT_EQ(cell.counts.to_app.load(), 1);

    cell.disconnect_peer();
    EXPECT_TRUE(wait_for_acceptor_drained(cell.engine, cell.id, kDrainBudget))
        << "the acceptor did not drain after the peer disconnected";
    EXPECT_EQ(cell.counts.received.load(), 1);
    EXPECT_EQ(fixpp_session_close(cell.session), FIXPP_ERR_OK);
}

TEST(CapiLogonMalformedTag, AcceptorMalformedTagIsRefusedOnEveryObserver) {
    AcceptorCell cell{kMalformedTagField};
    ASSERT_TRUE(cell.ready);
    expect_refused_on_every_observer(cell, kSettleBudget);
}

TEST(CapiLogonMalformedTag, AcceptorWellFormedTagEstablishesOnEveryObserver) {
    AcceptorCell cell{kWellFormedTagField};
    ASSERT_TRUE(cell.ready);
    expect_established_on_every_observer(cell, kSettleBudget);
}

TEST(CapiLogonMalformedTag, InitiatorMalformedTagReplyIsRefusedOnEveryObserver) {
    InitiatorCell cell{kMalformedTagField};
    ASSERT_TRUE(cell.ready);
    expect_refused_on_every_observer(cell, kInitiatorSettleBudget);
}

TEST(CapiLogonMalformedTag, InitiatorWellFormedTagReplyEstablishesOnEveryObserver) {
    InitiatorCell cell{kWellFormedTagField};
    ASSERT_TRUE(cell.ready);
    expect_established_on_every_observer(cell, kInitiatorSettleBudget);
}
