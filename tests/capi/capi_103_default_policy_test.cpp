// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_103_default_policy_test.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#682, tasks T024: the C ABI's
// default reset policy (contracts/c-abi-1.17.md, "Witnesses"). A session config made by
// fixpp_session_config_create on which fixpp_session_config_set_reset_seqnum_policy is never
// called, and no reset setting is turned on, faces a raw TCP peer
// (inbound_frame_dispositions_capi_support.hpp's RawAcceptor), which reads the engine's Logon
// bytes or sends its own Logon:
//   - an initiator's Logon carries no ResetSeqNumFlag(141): the discriminator, since a strict
//     default over the fresh store's {1,1} would send 141=Y. 34=1 is asserted as the stated
//     precondition (a fresh in-memory store), not as a witness that a stored number is kept:
//     that is unreachable through the C ABI while it has no store setter and the Engine sends
//     one Logon per Session, and test_103_default_reset_policy.cpp's FileStore restart cell
//     carries it in C++;
//   - an acceptor accepts a Logon without 141 (fixpp_session_is_established true), and
//     fixpp_session_close on that session returns FIXPP_ERR_OK, where a session that never
//     established returns FIXPP_ERR_THREAD_SESSION_LIFECYCLE.
//
// fixpp#577, tasks T077: fixpp_session_send while a too-high Logout is pending is refused.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "inbound_frame_dispositions_capi_support.hpp"

namespace {

namespace ifd = fixpp::capi_test::ifd;

constexpr std::uint32_t kHeartBtIntSeconds = 30;

// One C-ABI engine over dictionaries/FIX44.xml with the default session config: the comp IDs,
// BeginString, role, HeartBtInt, plain TCP, the dictionary and the endpoint are set; the reset
// policy setter and every reset setting are not.
struct DefaultEngine {
    fixpp_dict_t* dict = nullptr;
    fixpp_engine_t* engine = nullptr;
    fixpp_session_t* session = nullptr;
    fixpp::session::SessionId id{};
    bool opened = false;

    DefaultEngine(fixpp_session_role const role, std::uint16_t const port) {
        if (fixpp_dict_load_from_xml(FIXPP_DICT_DIR "/FIX44.xml", &dict) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_dict_load_from_xml(FIX44.xml) failed";
            return;
        }
        fixpp_engine_config_t* ec = nullptr;
        if (fixpp_engine_config_create(&ec) != FIXPP_ERR_OK ||
            fixpp_engine_config_set_realtime_clock(ec) != FIXPP_ERR_OK ||
            fixpp_engine_create(ec, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR,
                                &engine) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "engine config or fixpp_engine_create failed";
            fixpp_engine_config_destroy(ec);
            return;
        }
        fixpp_session_config_t* sc = nullptr;
        if (fixpp_session_config_create(&sc) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "fixpp_session_config_create failed";
            return;
        }
        bool const ok =
            fixpp_session_config_set_comp_ids(sc, ifd::kEngineCompId, ifd::kPeerCompId) ==
                FIXPP_ERR_OK &&
            fixpp_session_config_set_begin_string(sc, "FIX.4.4") == FIXPP_ERR_OK &&
            fixpp_session_config_set_role(sc, role) == FIXPP_ERR_OK &&
            fixpp_session_config_set_heartbeat_seconds(sc, kHeartBtIntSeconds) == FIXPP_ERR_OK &&
            fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                              nullptr) == FIXPP_ERR_OK &&
            fixpp_session_config_set_dictionary(sc, dict) == FIXPP_ERR_OK &&
            fixpp_session_config_set_tcp_endpoint(sc, "127.0.0.1", port) == FIXPP_ERR_OK;
        id = fixpp::capi_test::session_id_of(sc);
        if (!ok || fixpp_session_open(engine, sc, &session) != FIXPP_ERR_OK) {
            ADD_FAILURE() << "session config or fixpp_session_open failed";
            fixpp_session_config_destroy(sc);
            return;
        }
        opened = true;
    }

    DefaultEngine(DefaultEngine const&) = delete;
    DefaultEngine& operator=(DefaultEngine const&) = delete;

    ~DefaultEngine() {
        if (engine != nullptr) fixpp_engine_destroy(engine);
        if (dict != nullptr) fixpp_dict_destroy(dict);
    }

    // These act on the handles, so they are non-const although they write no member.
    // NOLINTBEGIN(readability-make-member-function-const)
    [[nodiscard]] bool start() { return opened && fixpp_engine_start(engine) == FIXPP_ERR_OK; }

    [[nodiscard]] std::uint16_t bound_port() {
        auto const until = std::chrono::steady_clock::now() + ifd::kStepBudget;
        for (;;) {
            std::uint16_t p = 0;
            if (fixpp_session_acceptor_bound_endpoint(session, &p) == FIXPP_ERR_OK && p != 0) {
                return p;
            }
            if (std::chrono::steady_clock::now() >= until) return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    [[nodiscard]] bool wait_established() {
        auto const until = std::chrono::steady_clock::now() + ifd::kStepBudget;
        for (;;) {
            bool est = false;
            if (fixpp_session_is_established(session, &est) == FIXPP_ERR_OK && est) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    // Waits until the session has drained (bounded), then closes it.
    fixpp_error_t drain_then_close() {
        EXPECT_TRUE(fixpp::capi_test::wait_for_acceptor_drained(engine, id))
            << "the session never drained";
        return fixpp_session_close(session);
    }
    // NOLINTEND(readability-make-member-function-const)
};

// The value of the first `<SOH>tag=` field of `frame`, or nullopt.
std::optional<std::string> field(std::string_view const frame, std::string_view const tag) {
    std::string const needle = "\x01" + std::string{tag} + "=";
    auto const at = frame.find(needle);
    if (at == std::string_view::npos) return std::nullopt;
    auto const v = at + needle.size();
    auto const end = frame.find('\x01', v);
    return std::string{frame.substr(v, end == std::string_view::npos ? frame.size() - v : end - v)};
}

TEST(CapiDefaultResetPolicy103, DefaultInitiator_LogonCarriesNo141And34Eq1) {
    ifd::RawAcceptor peer;
    ASSERT_TRUE(peer.listening());
    DefaultEngine c{FIXPP_ROLE_INITIATOR, peer.port()};
    ASSERT_TRUE(c.start()) << "setup";
    ASSERT_TRUE(peer.accept()) << "the engine did not connect";
    auto const logon = peer.read_until(
        [](std::string const& f) { return ifd::RawAcceptor::has_field(f, "35=A"); });
    ASSERT_TRUE(logon.has_value()) << "the engine wrote no Logon";
    // The precondition: a fresh in-memory store numbers the Logon 1.
    EXPECT_EQ(field(*logon, "34"), std::optional<std::string>{"1"}) << *logon;
    // The discriminator: no ResetSeqNumFlag(141) under the default.
    EXPECT_FALSE(field(*logon, "141").has_value()) << *logon;
    peer.disconnect();
    (void)c.drain_then_close();
}

TEST(CapiDefaultResetPolicy103, DefaultAcceptor_AcceptsALogonWithout141_CloseReturnsOk) {
    DefaultEngine a{FIXPP_ROLE_ACCEPTOR, 0};
    ASSERT_TRUE(a.start()) << "setup";
    std::uint16_t const port = a.bound_port();
    ASSERT_NE(port, 0U) << "setup";
    ifd::RawAcceptor peer;
    ASSERT_TRUE(peer.connect(port)) << "setup";
    ASSERT_TRUE(peer.write(
        ifd::frame44("35=A\x01" + ifd::peer_header(1) +
                     ifd::fix_fields({{98, "0"}, {108, std::to_string(kHeartBtIntSeconds)}}))));
    bool const established = a.wait_established();
    EXPECT_TRUE(established) << "the default acceptor did not accept a Logon without 141";
    peer.disconnect();
    EXPECT_EQ(a.drain_then_close(), FIXPP_ERR_OK);
}

// tasks T077 (contracts/c-abi-1.17.md, "Witnesses"; FR-011, research D4.7): fixpp_session_send
// while a too-high Logout waits for its acknowledgement returns the invalid-state code. The
// Logout is at 34=4 with NextNumIn 2; the engine's ResendRequest shows it was processed.
// The session's bound T is SessionConfig's default (the C ABI sets none), on the real clock:
// after T the session acknowledges and is Disconnected, where a send is refused for another
// reason, so the send must be made before T has run from the Logout's write.
// The peer's Logon carries ResetSeqNumFlag(141)=Y, which the base's strict default and
// 103's lenient default both accept, so the cell runs the same path on both.
constexpr std::chrono::milliseconds kDefaultLogoutBound{2000};

TEST(CapiTooHighLogout103, SendWhileATooHighLogoutIsPending_RefusedWithTheInvalidStateCode) {
    DefaultEngine a{FIXPP_ROLE_ACCEPTOR, 0};
    ASSERT_TRUE(a.start()) << "setup";
    std::uint16_t const port = a.bound_port();
    ASSERT_NE(port, 0U) << "setup";
    ifd::RawAcceptor peer;
    ASSERT_TRUE(peer.connect(port)) << "setup";
    ASSERT_TRUE(peer.write(ifd::frame44(
        "35=A\x01" + ifd::peer_header(1) +
        ifd::fix_fields({{98, "0"}, {108, std::to_string(kHeartBtIntSeconds)}, {141, "Y"}}))));
    ASSERT_TRUE(a.wait_established()) << "setup";
    ASSERT_TRUE(peer.read_logon()) << "setup: the engine wrote no Logon reply";

    auto const logout_written = std::chrono::steady_clock::now();
    ASSERT_TRUE(peer.write(ifd::frame44("35=5\x01" + ifd::peer_header(4))));
    auto const rr =
        peer.read_until([](std::string const& f) { return ifd::RawAcceptor::has_field(f, "35=2"); },
                        kDefaultLogoutBound / 2);
    ASSERT_TRUE(rr.has_value()) << "the engine wrote no ResendRequest after the too-high Logout";

    std::string const payload =
        "35=D\x01"
        "11=ORD-577-C\x01"
        "55=TESTSYM\x01"
        "54=1\x01"
        "38=100\x01"
        "40=1\x01";
    auto const rc = fixpp_session_send(
        a.session, reinterpret_cast<std::uint8_t const*>(payload.data()), payload.size());
    auto const elapsed = std::chrono::steady_clock::now() - logout_written;
    ASSERT_LT(elapsed, kDefaultLogoutBound)
        << "the path was not exercised: the send ran after the bound T";
    EXPECT_EQ(rc, FIXPP_ERR_SESSION_INVALID_STATE)
        << "fixpp_session_send returned " << static_cast<int>(rc);
    peer.disconnect();
    (void)a.drain_then_close();
}

}  // namespace
