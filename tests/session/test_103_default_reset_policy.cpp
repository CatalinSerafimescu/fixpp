// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_default_reset_policy.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#682: a restarted initiator logs
// on with the default configuration. research.md "RED cells", "#682 — the default policy and
// strict's meaning"; tasks T022 (AS1 to AS4, the controls, and SC-002's in-process FileStore
// restart-and-resume cell in both roles).
//
// AS1 to AS4 and the controls run on mock_reconnect_factory.hpp's reconnect rig (a directly
// driven Session over a persistent FaultStore seeded at {in=37, out=42}, no clock). AS2 reads
// the store's calls through tests/support/recording_store.hpp's wrapper. The restart cell runs
// two Session lifetimes over one FileStore directory; the test is the peer: it answers a
// Logon as a QuickFIX acceptor does (ResetSeqNumFlag(141)=Y mirrored, otherwise its own next
// number) and opens a Logon to an acceptor the same way.
//
// A cell that reads "the default" names no reset policy: the config's member initializer
// decides it.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/test_103_support.hpp"
#include "support/recording_store.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

namespace {

namespace rr = fixpp::test_support::reconnect_rig;
namespace t103 = fixpp::test_support::t103;
using fixpp::session::fsm_state;
using fixpp::session::reset_seqnum_policy;
using fixpp::session::seqnum_t;
using fixpp::test_support::RecordingStoreFactory;
using fixpp::test_support::StoreCallLog;

// The Text(58) of a strict acceptor's refusal of a Logon without ResetSeqNumFlag(141)=Y
// (research D2.4, kStrictResetText), spelled here from the research, not read from the source.
constexpr std::string_view kStrictResetText = "ResetSeqNumFlag(141)=Y required";

// A frame from the peer, FIX.4.4: 35, 34, 49, 52, 56, then `extra`.
std::vector<std::byte> peer_frame(std::string_view const sender, std::string_view const target,
                                  std::string_view const msg_type, seqnum_t const seq,
                                  std::string_view const extra = {}) {
    std::string body = "35=" + std::string{msg_type} + "\x01" + "34=" + std::to_string(seq) +
                       "\x01" + "49=" + std::string{sender} + "\x01" +
                       "52=20240101-00:00:00.000\x01" + "56=" + std::string{target} + "\x01" +
                       std::string{extra};
    std::string msg =
        "8=FIX.4.4\x01" + std::string{"9="} + std::to_string(body.size()) + "\x01" + body;
    unsigned cs = 0;
    for (unsigned char const c : msg) cs += c;
    std::array<char, 8> buf{};
    std::snprintf(buf.data(), buf.size(), "%03u", cs & 0xFFU);
    msg += "10=" + std::string{buf.data()} + "\x01";
    std::vector<std::byte> out(msg.size());
    for (std::size_t i = 0; i < msg.size(); ++i) out[i] = static_cast<std::byte>(msg[i]);
    return out;
}

// A Logon from the peer; with ResetSeqNumFlag(141)=Y when `reset`.
std::vector<std::byte> peer_logon(std::string_view const sender, std::string_view const target,
                                  seqnum_t const seq, bool const reset) {
    std::string extra =
        "98=0\x01"
        "108=30\x01";
    if (reset) extra += "141=Y\x01";
    return peer_frame(sender, target, "A", seq, extra);
}

// The initiator rig's peer is SRV and the session CLI; the acceptor rig's the reverse.
std::vector<std::byte> to_initiator_logon(seqnum_t const seq, bool const reset) {
    return peer_logon("SRV", "CLI", seq, reset);
}
std::vector<std::byte> to_acceptor_logon(seqnum_t const seq, bool const reset) {
    return peer_logon("CLI", "SRV", seq, reset);
}

std::string as_string(std::vector<std::byte> const& f) {
    return {reinterpret_cast<char const*>(f.data()), f.size()};
}

// The frames the session wrote, as strings, in order.
std::vector<std::string> written(rr::Fixture const& f) {
    std::vector<std::string> out;
    for (auto const& fr : f.capture.frames) out.push_back(as_string(fr));
    return out;
}

// "35=<t>/34=<n>/141=<y>/58=<text>" of each frame, for a failure message.
std::string describe(std::vector<std::string> const& frames) {
    std::string out;
    for (auto const& f : frames) {
        auto const d = t103::decode(f);
        out += (out.empty() ? "" : " | ") + ("35=" + d.msg_type.value_or("?")) +
               "/34=" + d.seq.value_or("?");
        if (d.reset_seq_num) out += "/141=" + *d.reset_seq_num;
        if (d.text) out += "/58=" + *d.text;
    }
    return out.empty() ? std::string{"nothing"} : out;
}

std::vector<t103::frame_fields> of_type(std::vector<std::string> const& frames,
                                        std::string_view const type) {
    std::vector<t103::frame_fields> out;
    for (auto const& f : frames) {
        auto d = t103::decode(f);
        if (d.msg_type == type) out.push_back(std::move(d));
    }
    return out;
}

// Hands one frame to on_inbound_frame and returns its result. The frame is the caller's and
// outlives the pump.
fixpp::core::expected_t<void> feed(rr::Fixture& f, std::vector<std::byte> const& frame) {
    auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*f.session, f.ioc),
                              f.session->on_inbound_frame(frame), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, std::chrono::milliseconds{500},
                                                    "t103_682/feed")) {
        fixpp::test_support::drain_or_report(f.ioc, "t103_682/feed");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "t103_682/feed";
        return std::unexpected(fixpp::test_support::kWindowMissSentinel);
    }
    return fut.get();
}

// Session::send of one NewOrderSingle body; returns its result.
fixpp::core::expected_t<void> send_order(rr::Fixture& f, std::string const& payload) {
    auto fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*f.session, f.ioc),
                       f.session->send(std::span<const std::byte>{
                           reinterpret_cast<std::byte const*>(payload.data()), payload.size()}),
                       asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, std::chrono::milliseconds{500},
                                                    "t103_682/send")) {
        fixpp::test_support::drain_or_report(f.ioc, "t103_682/send");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "t103_682/send";
        return std::unexpected(fixpp::test_support::kWindowMissSentinel);
    }
    return fut.get();
}

seqnum_t next_in(rr::Fixture& f) {
    return fixpp::session::session_test_access::seqnum_mgr(*f.session).next_inbound_unsafe();
}

// make_reconnect_initiator over {37, 42}, refresh_on_logon off, with `policy` (nullopt: the
// default) and an optional config tweak.
rr::ReconnectInitiatorFixture initiator(
    std::optional<reset_seqnum_policy> const policy, seqnum_t const in = 37,
    seqnum_t const out = 42,
    std::function<std::shared_ptr<fixpp::session::MessageStoreFactory>(
        std::shared_ptr<rr::FaultStoreFactory>)>
        wrap = nullptr,
    std::function<void(fixpp::session::SessionConfig&)> const& tweak = nullptr) {
    return rr::make_reconnect_initiator(in, out, /*refresh_on_logon=*/false, /*persistent=*/true,
                                        policy, /*fail_on_nth_call=*/0, std::move(wrap), tweak);
}

// ── AS1: default, cold open over {37, 42} ───────────────────────────────────────────────────
TEST(DefaultResetPolicy103, AS1_DefaultInitiator_LogonAtStoredNumberNo141_AckAt37Active) {
    auto r = initiator(std::nullopt);
    ASSERT_NE(r.store, nullptr);
    auto& f = *r.fix;
    auto const logons = of_type(written(f), "A");
    ASSERT_EQ(logons.size(), 1U) << describe(written(f));
    EXPECT_EQ(logons.front().seq, std::optional<std::string>{"42"}) << describe(written(f));
    EXPECT_FALSE(logons.front().reset_seq_num.has_value()) << describe(written(f));

    auto const ack = to_initiator_logon(37, false);
    (void)feed(f, ack);
    EXPECT_EQ(f.session->state(), fsm_state::Active) << describe(written(f));
}

// ── AS2: strict, same store: a durable reset before the Logon ───────────────────────────────
TEST(DefaultResetPolicy103, AS2_StrictInitiator_ResetsBeforeTheLogon_141At1_NextAppAt2) {
    std::shared_ptr<StoreCallLog> log;
    auto r = initiator(reset_seqnum_policy::bilateral_strict, 37, 42,
                       [&log](std::shared_ptr<rr::FaultStoreFactory> inner) {
                           auto rec = std::make_shared<RecordingStoreFactory>(std::move(inner));
                           rec->log->arm();
                           log = rec->log;
                           return std::shared_ptr<fixpp::session::MessageStoreFactory>{rec};
                       });
    ASSERT_NE(r.store, nullptr);
    ASSERT_NE(log, nullptr);
    auto& f = *r.fix;

    // The store log: a reset (reset or reset_to) is recorded before store(1, outbound).
    std::optional<std::size_t> reset_at;
    std::optional<std::size_t> logon_store_at;
    for (auto const& c : log->calls()) {
        if (!reset_at && (c.op == fixpp::test_support::store_op::reset ||
                          c.op == fixpp::test_support::store_op::reset_to)) {
            reset_at = c.called_at;
        }
        if (!logon_store_at && c.op == fixpp::test_support::store_op::store && c.first == 1U &&
            c.dir == fixpp::session::direction_t::outbound) {
            logon_store_at = c.called_at;
        }
    }
    EXPECT_TRUE(reset_at.has_value()) << "no store reset was recorded before the Logon";
    EXPECT_TRUE(logon_store_at.has_value()) << "no store(1, outbound) was recorded";
    if (reset_at && logon_store_at) {
        EXPECT_LT(*reset_at, *logon_store_at);
    }

    auto const logons = of_type(written(f), "A");
    ASSERT_EQ(logons.size(), 1U) << describe(written(f));
    EXPECT_EQ(logons.front().seq, std::optional<std::string>{"1"}) << describe(written(f));
    EXPECT_EQ(logons.front().reset_seq_num, std::optional<std::string>{"Y"})
        << describe(written(f));

    auto const ack = to_initiator_logon(1, true);
    (void)feed(f, ack);
    ASSERT_EQ(f.session->state(), fsm_state::Active) << describe(written(f));

    std::size_t const before = f.capture.frames.size();
    auto const sr = send_order(f,
                               "35=D\x01"
                               "11=ORD-AS2\x01");
    ASSERT_TRUE(sr.has_value()) << "send returned " << static_cast<int>(sr.error());
    std::vector<std::string> const all = written(f);
    std::vector<std::string> const after(all.begin() + static_cast<std::ptrdiff_t>(before),
                                         all.end());
    auto const apps = of_type(after, "D");
    ASSERT_EQ(apps.size(), 1U) << describe(after);
    EXPECT_EQ(apps.front().seq, std::optional<std::string>{"2"}) << describe(after);
}

// ── AS3: strict acceptor, a Logon without 141, in and out of sequence ───────────────────────
void run_as3(seqnum_t const logon_seq) {
    auto r = rr::make_acceptor_notconnected(std::make_shared<rr::FaultStoreFactory>(37, 42),
                                            /*refresh_on_logon=*/false,
                                            reset_seqnum_policy::bilateral_strict);
    ASSERT_NE(r.store, nullptr);
    auto& f = *r.fix;
    auto const logon = to_acceptor_logon(logon_seq, false);
    auto const res = feed(f, logon);

    auto const logouts = of_type(written(f), "5");
    ASSERT_EQ(logouts.size(), 1U) << describe(written(f));
    EXPECT_EQ(logouts.front().text, std::optional<std::string>{std::string{kStrictResetText}})
        << describe(written(f));
    ASSERT_FALSE(res.has_value()) << "the refusal returned success";
    EXPECT_EQ(res.error(), fixpp::core::error::session_seqnum_reset_mismatch)
        << static_cast<int>(res.error());
    EXPECT_EQ(next_in(f), seqnum_t{37}) << "NextNumIn moved on a refused Logon";
    EXPECT_EQ(f.session->state(), fsm_state::Disconnected);
}

TEST(DefaultResetPolicy103, AS3_StrictAcceptor_InSequenceLogonWithout141_LogoutWithText) {
    run_as3(37);
}

TEST(DefaultResetPolicy103, AS3_StrictAcceptor_OutOfSequenceLogonWithout141_LogoutWithText) {
    run_as3(40);
}

// AS3's error ending (research D2.4; FR-004): the refusal Logout cannot take a number, the
// store's next outbound number being at its maximum. The arm returns that error, not the
// mismatch; no Logout is written and the session is Disconnected.
TEST(DefaultResetPolicy103, AS3_StrictAcceptor_RefusalLogoutCannotBeNumbered_ReturnsItsError) {
    auto r = rr::make_acceptor_notconnected(
        std::make_shared<rr::FaultStoreFactory>(37, fixpp::session::seqnum_max),
        /*refresh_on_logon=*/false, reset_seqnum_policy::bilateral_strict);
    ASSERT_NE(r.store, nullptr);
    auto& f = *r.fix;
    auto const logon = to_acceptor_logon(37, false);
    auto const res = feed(f, logon);
    ASSERT_FALSE(res.has_value()) << "the refusal returned success";
    EXPECT_EQ(res.error(), fixpp::core::error::store_seqnum_overflow)
        << static_cast<int>(res.error());
    EXPECT_TRUE(of_type(written(f), "5").empty()) << describe(written(f));
    EXPECT_EQ(f.session->state(), fsm_state::Disconnected);
}

// AS3 superseded (research D2.4; D-CAN-5): Engine::stop()'s first step runs on the session's
// strand while the refusal Logout is in its store call. The arm returns success, as a
// superseded Logon arm does, not the mismatch.
TEST(DefaultResetPolicy103, AS3_StrictAcceptor_EngineStopDuringTheRefusalsStore_ReturnsSuccess) {
    auto const rec =
        std::make_shared<RecordingStoreFactory>(std::make_shared<rr::FaultStoreFactory>(37, 42));
    auto const log = rec->log;
    auto r = rr::make_acceptor_notconnected(rec, /*refresh_on_logon=*/false,
                                            reset_seqnum_policy::bilateral_strict);
    ASSERT_NE(r.fix, nullptr);
    auto& f = *r.fix;
    log->arm();
    log->hold_in = [](fixpp::test_support::StoreCall const& c) {
        return c.op == fixpp::test_support::store_op::store &&
               c.dir == fixpp::session::direction_t::outbound && c.first == 42U;
    };
    auto const logon = to_acceptor_logon(37, false);
    auto feed_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*f.session, f.ioc),
                                   f.session->on_inbound_frame(logon), asio::use_future);
    ASSERT_TRUE(fixpp::test_support::pump_until(f.ioc, [&] { return log->holding(); }))
        << "setup: the refusal Logout's store was not held";
    // Engine::stop() calls it on the session's strand.
    asio::post(f.session->executor().underlying(),
               [&f] { fixpp::session::session_test_access::note_engine_stop(*f.session); });
    bool stop_noted = false;
    asio::post(f.session->executor().underlying(), [&stop_noted] { stop_noted = true; });
    ASSERT_TRUE(fixpp::test_support::pump_until(f.ioc, [&] { return stop_noted; }))
        << "setup: the stop's step did not run";
    ASSERT_TRUE(log->holding()) << "setup: the hold ended before the stop's step";
    log->release_hold();
    ASSERT_TRUE(fixpp::test_support::pump_until(f.ioc, [&] {
        return feed_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    })) << "the Logon did not complete";
    auto const res = feed_fut.get();
    EXPECT_TRUE(res.has_value()) << "the superseded refusal returned "
                                 << static_cast<int>(res.error());
}

// ── AS4: default acceptor, a Logon without 141 ──────────────────────────────────────────────
TEST(DefaultResetPolicy103, AS4_DefaultAcceptor_LogonWithout141_Active) {
    auto r = rr::make_acceptor_notconnected(std::make_shared<rr::FaultStoreFactory>(37, 42),
                                            /*refresh_on_logon=*/false, std::nullopt);
    ASSERT_NE(r.store, nullptr);
    auto& f = *r.fix;
    auto const logon = to_acceptor_logon(37, false);
    (void)feed(f, logon);
    EXPECT_EQ(f.session->state(), fsm_state::Active) << describe(written(f));
}

// ── Controls ────────────────────────────────────────────────────────────────────────────────
// Strict over {1, 1}: the acknowledgement leg AS2 needs can pass.
TEST(DefaultResetPolicy103, Control_StrictInitiatorOverFreshStore_141AckReachesActive) {
    auto r = initiator(reset_seqnum_policy::bilateral_strict, 1, 1);
    ASSERT_NE(r.store, nullptr);
    auto& f = *r.fix;
    auto const logons = of_type(written(f), "A");
    ASSERT_EQ(logons.size(), 1U) << describe(written(f));
    EXPECT_EQ(logons.front().seq, std::optional<std::string>{"1"});
    auto const ack = to_initiator_logon(1, true);
    (void)feed(f, ack);
    EXPECT_EQ(f.session->state(), fsm_state::Active) << describe(written(f));
}

// Lenient with reset_on_logon over {37, 42}: the reset route strict now takes.
TEST(DefaultResetPolicy103, Control_LenientResetOnLogon_141At1_AckReachesActive) {
    auto r = initiator(reset_seqnum_policy::bilateral_lenient, 37, 42, nullptr,
                       [](fixpp::session::SessionConfig& c) { c.reset_on_logon = true; });
    ASSERT_NE(r.store, nullptr);
    auto& f = *r.fix;
    auto const logons = of_type(written(f), "A");
    ASSERT_EQ(logons.size(), 1U) << describe(written(f));
    EXPECT_EQ(logons.front().seq, std::optional<std::string>{"1"}) << describe(written(f));
    EXPECT_EQ(logons.front().reset_seq_num, std::optional<std::string>{"Y"});
    auto const ack = to_initiator_logon(1, true);
    (void)feed(f, ack);
    EXPECT_EQ(f.session->state(), fsm_state::Active) << describe(written(f));
}

// ── SC-002's in-process half: a FileStore restart-and-resume, both roles ────────────────────
//
// Two Session lifetimes over one FileStore directory, each with the default configuration.
// The first logs on over a fresh store, exchanges two application messages out and two
// Heartbeats in, and closes; the second opens over what the first stored.
class RestartRig {
public:
    explicit RestartRig(fixpp::session::session_role const role) : role_{role} {}
    RestartRig(RestartRig const&) = delete;
    RestartRig& operator=(RestartRig const&) = delete;
    ~RestartRig() { (void)fixpp::test_support::try_remove_temp_dir(dir_); }

    [[nodiscard]] std::string_view self() const {
        return role_ == fixpp::session::session_role::initiator ? "CLI" : "SRV";
    }
    [[nodiscard]] std::string_view peer() const {
        return role_ == fixpp::session::session_role::initiator ? "SRV" : "CLI";
    }

    // A fresh Fixture whose Session runs over the rig's directory, opened.
    [[nodiscard]] std::unique_ptr<rr::Fixture> open() {
        auto f = std::make_unique<rr::Fixture>();
        f->cfg.role = role_;
        f->cfg.sender_comp_id = std::string{self()};
        f->cfg.target_comp_id = std::string{peer()};
        f->cfg.begin_string = "FIX.4.4";
        f->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        f->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        f->cfg.heartbeat_interval = std::chrono::seconds{0};
        f->cfg.executor_override = f->ioc.get_executor();
        fixpp::session::FileStore::Config fc;
        fc.directory = dir_;
        fc.sender_comp_id = f->cfg.sender_comp_id;
        fc.target_comp_id = f->cfg.target_comp_id;
        fc.file_io_executor = f->ioc.get_executor();
        f->cfg.store_factory = std::make_shared<fixpp::session::FileStoreFactory>(fc);
        auto* raw = f.get();
        f->cfg.transport_send = [raw](std::span<const std::byte> d) { raw->capture(d); };
        f->session = std::make_unique<fixpp::session::Session>(f->eng, f->cfg);
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*f->session, f->ioc),
                                  f->session->open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(f->ioc, fut, std::chrono::seconds{2},
                                                        "t103_682/restart/open")) {
            fixpp::test_support::drain_or_report(f->ioc, "t103_682/restart/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "t103_682/restart/open";
            return f;
        }
        auto const r = fut.get();
        EXPECT_TRUE(r.has_value()) << "open returned " << static_cast<int>(r.error());
        return f;
    }

    // Closes the session terminally and drains, then destroys it (releasing the store).
    static void close(std::unique_ptr<rr::Fixture>& f) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*f->session, f->ioc),
                                  f->session->close(fixpp::session::close_mode::terminal),
                                  asio::use_future);
        if (fixpp::test_support::run_window_then_ready(f->ioc, fut, std::chrono::seconds{2},
                                                       "t103_682/restart/close")) {
            (void)fut.get();
        } else {
            fixpp::test_support::drain_or_report(f->ioc, "t103_682/restart/close");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "t103_682/restart/close";
        }
        f->session.reset();
        f.reset();
    }

    // The peer's Logon to this session: mirrors a 141=Y the session's own Logon carried (an
    // initiator's), else carries none.
    [[nodiscard]] std::vector<std::byte> peer_logon_frame(seqnum_t const seq,
                                                          bool const reset) const {
        return peer_logon(peer(), self(), seq, reset);
    }
    [[nodiscard]] std::vector<std::byte> peer_heartbeat(seqnum_t const seq) const {
        return peer_frame(peer(), self(), "0", seq);
    }

private:
    fixpp::session::session_role role_;
    std::filesystem::path dir_ = fixpp::test_support::unique_temp_dir("t103_restart");
};

// The first lifetime: log on over the fresh store, two application messages out, two
// Heartbeats in. Returns false on a setup failure.
[[nodiscard]] bool first_lifetime(RestartRig& rig) {
    auto f = rig.open();
    if (f->session == nullptr) return false;
    bool const initiator = f->cfg.role == fixpp::session::session_role::initiator;
    if (initiator) {
        auto const logons = of_type(written(*f), "A");
        if (logons.size() != 1U) {
            ADD_FAILURE() << "setup: the first Logon was not written: " << describe(written(*f));
            return false;
        }
        auto const ack = rig.peer_logon_frame(1, logons.front().reset_seq_num == "Y");
        (void)feed(*f, ack);
    } else {
        // Accepted under every policy: a 141=Y Logon at 34=1 over a fresh store.
        auto const logon = rig.peer_logon_frame(1, true);
        (void)feed(*f, logon);
    }
    if (f->session->state() != fsm_state::Active) {
        ADD_FAILURE() << "setup: the first lifetime did not reach Active: "
                      << describe(written(*f));
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (!send_order(*f,
                        "35=D\x01"
                        "11=ORD-R" +
                            std::to_string(i) + "\x01")
                 .has_value()) {
            ADD_FAILURE() << "setup: a first-lifetime send failed";
            return false;
        }
    }
    for (seqnum_t s = 2; s <= 3; ++s) {
        auto const hb = rig.peer_heartbeat(s);
        (void)feed(*f, hb);
    }
    if (next_in(*f) != 4U) {
        ADD_FAILURE() << "setup: NextNumIn is " << next_in(*f) << " after the first lifetime";
        return false;
    }
    RestartRig::close(f);
    return true;
}

TEST(DefaultResetPolicy103, RestartResume_Initiator_DefaultLogsOnAtStoredNumbers) {
    RestartRig rig{fixpp::session::session_role::initiator};
    ASSERT_TRUE(first_lifetime(rig));

    auto f = rig.open();
    ASSERT_NE(f->session, nullptr);
    auto const logons = of_type(written(*f), "A");
    ASSERT_EQ(logons.size(), 1U) << describe(written(*f));
    EXPECT_EQ(logons.front().seq, std::optional<std::string>{"4"}) << describe(written(*f));
    EXPECT_FALSE(logons.front().reset_seq_num.has_value()) << describe(written(*f));

    auto const ack = rig.peer_logon_frame(4, false);
    (void)feed(*f, ack);
    ASSERT_EQ(f->session->state(), fsm_state::Active) << describe(written(*f));
    EXPECT_EQ(next_in(*f), seqnum_t{5});
    RestartRig::close(f);
}

TEST(DefaultResetPolicy103, RestartResume_Acceptor_DefaultAcceptsTheResumingLogon) {
    RestartRig rig{fixpp::session::session_role::acceptor};
    ASSERT_TRUE(first_lifetime(rig));

    auto f = rig.open();
    ASSERT_NE(f->session, nullptr);
    auto const logon = rig.peer_logon_frame(4, false);
    (void)feed(*f, logon);
    ASSERT_EQ(f->session->state(), fsm_state::Active) << describe(written(*f));
    auto const replies = of_type(written(*f), "A");
    ASSERT_EQ(replies.size(), 1U) << describe(written(*f));
    EXPECT_EQ(replies.front().seq, std::optional<std::string>{"4"}) << describe(written(*f));
    EXPECT_FALSE(replies.front().reset_seq_num.has_value()) << describe(written(*f));
    EXPECT_EQ(next_in(*f), seqnum_t{5});
    RestartRig::close(f);
}

}  // namespace
