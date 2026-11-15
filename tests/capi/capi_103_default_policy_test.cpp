// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_103_default_policy_test.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#682, tasks T024: the C ABI's
// default reset policy (contracts/c-abi-1.17.md, "Witnesses": "a C-ABI cell asserting the
// default pair's Logon (no 141, 34 = stored)"). A session config made by
// fixpp_session_config_create on which fixpp_session_config_set_reset_seqnum_policy is never
// called, and no reset setting is turned on, faces a raw TCP peer
// (inbound_frame_dispositions_capi_support.hpp's RawAcceptor), which reads the engine's Logon
// bytes or sends its own Logon:
//   - an initiator's Logon carries no ResetSeqNumFlag(141) and 34=1 (the C ABI's store is
//     the in-memory one, so the stored number is 1);
//   - an acceptor accepts a Logon without 141 (fixpp_session_is_established true), and
//     fixpp_session_close on that session returns FIXPP_ERR_OK, where a session that never
//     established returns FIXPP_ERR_THREAD_SESSION_LIFECYCLE.
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
    EXPECT_FALSE(field(*logon, "141").has_value()) << *logon;
    EXPECT_EQ(field(*logon, "34"), std::optional<std::string>{"1"}) << *logon;
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

}  // namespace
