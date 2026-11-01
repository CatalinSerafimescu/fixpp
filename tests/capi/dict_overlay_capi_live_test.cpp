// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict_overlay_capi_live_test.cpp — W-9's live arm of 098-dialect-overlay
// (contracts/witnesses.md "C ABI and Python", the live arm; spec US4).
//
// Two C-ABI engines over plaintext loopback (capi_dict066_loopback_support.hpp's
// mechanics, as dict066_group_membership_red_test.cpp uses them). Each session's
// dictionary is the handle `fixpp_dict_load_from_xml_with_overlay(FIX44.xml, the W-2
// FIX44 overlay)` returns, set with `fixpp_session_config_set_dictionary`.
//
// The initiator sends a raw application payload written here, not built with
// `fixpp_msg_set_data`: on a base-only handle that call refuses 5011, and the arm
// would fail at the sender instead of on the value. The payload carries the
// overlay's custom pair 5010 (LENGTH) / 5011 (DATA), whose Data value holds an SOH
// followed by text shaped as a field (58 is not otherwise in the frame). The
// acceptor's callback reads 5011 with `fixpp_msg_get_bytes`; the arm asserts it is
// the full counted value. Pairing follows the session's dictionary, so on a
// base-only handle 5011 reads only the bytes before its SOH.
//
// After the first `fixpp_engine_start` every check is non-fatal, so a failing check
// still reaches the closes and destroys and the binary exits with a test failure.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "capi_dict066_loopback_support.hpp"
#include "capi_loopback_support.hpp"
#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

namespace {

constexpr std::uint16_t kLengthTag = 5010;
constexpr std::uint16_t kDataTag = 5011;
// The Data value: an SOH, then text shaped as a field.
constexpr std::string_view kDataValue{
    "AB\x01"
    "58=CD",
    8};

std::filesystem::path base_path() {
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
}
std::filesystem::path overlay_path() {
    return std::filesystem::path{FIXPP_098_FIXTURE_DIR} / "real" / "FIX44_overlay.xml";
}

// A NewOrderSingle application payload carrying the custom pair, with no session
// framing tag.
std::vector<std::uint8_t> payload() {
    std::string p = "35=D\x01";
    p += "11=ORD098\x01";
    p += "54=1\x01";
    p += "60=20240101-00:00:00.000\x01";
    p += "40=1\x01";
    p += std::to_string(kLengthTag) + "=" + std::to_string(kDataValue.size()) + "\x01";
    p += std::to_string(kDataTag) + "=";
    p.append(kDataValue.data(), kDataValue.size());
    p += "\x01";
    return {p.begin(), p.end()};
}

// make_session_cfg_fix44's settings, with `dict` as the dictionary.
fixpp_session_config_t* make_session_cfg(const char* sender, const char* target,
                                         fixpp_session_role role, fixpp_dict_t* dict) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.4"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, role), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, dict), FIXPP_ERR_OK);
    return sc;
}

TEST(DictOverlayCapiW9Live, CustomDataValueReadsIntact) {
    ASSERT_TRUE(std::filesystem::exists(base_path())) << base_path();
    ASSERT_TRUE(std::filesystem::exists(overlay_path())) << overlay_path();
    fixpp_dict_t* dict = nullptr;
    ASSERT_EQ(
        fixpp_dict_load_from_xml_with_overlay(base_path().c_str(), overlay_path().c_str(), &dict),
        FIXPP_ERR_OK);
    ASSERT_NE(dict, nullptr);

    fixpp_engine_t* acceptor_engine = nullptr;
    fixpp_engine_t* initiator_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &acceptor_engine), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &initiator_engine), FIXPP_ERR_OK);

    fixpp_session_config_t* acc_cfg =
        make_session_cfg("ACC-098", "INI-098", FIXPP_ROLE_ACCEPTOR, dict);
    set_loopback_endpoint(acc_cfg, "127.0.0.1", 0);
    auto const acc_id = session_id_of(acc_cfg);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(acceptor_engine, acc_cfg, &acc_h), FIXPP_ERR_OK);

    struct CbCtx {
        std::atomic<bool> fired{false};
        std::atomic<int> calls{0};
        fixpp_error_t rc = FIXPP_ERR_UNKNOWN;
        std::string value;  // copied out: the inbound view ends with the callback
    } ctx;
    auto cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<CbCtx*>(ud);
        if (c->calls.fetch_add(1) == 0 && inbound != nullptr) {
            const std::uint8_t* bytes = nullptr;
            std::size_t len = 0;
            c->rc = fixpp_msg_get_bytes(inbound, kDataTag, &bytes, &len);
            if (c->rc == FIXPP_ERR_OK) {
                c->value.assign(reinterpret_cast<const char*>(bytes), len);
            }
            c->fired.store(true, std::memory_order_release);
        }
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, cb, &ctx), FIXPP_ERR_OK);

    fixpp_session_config_t* ini_cfg =
        make_session_cfg("INI-098", "ACC-098", FIXPP_ROLE_INITIATOR, dict);
    fixpp_dict_destroy(dict);  // both setters copied it

    // From here on, only non-fatal checks until every handle is released.
    fixpp_session_t* ini_h = nullptr;
    EXPECT_EQ(fixpp_engine_start(acceptor_engine), FIXPP_ERR_OK);
    std::uint16_t const port = wait_for_bound_port(acceptor_engine, acc_id);
    EXPECT_NE(port, 0U) << "the acceptor did not bind";
    if (port != 0U) {
        set_loopback_endpoint(ini_cfg, "127.0.0.1", port);
        EXPECT_EQ(fixpp_session_open(initiator_engine, ini_cfg, &ini_h), FIXPP_ERR_OK);
    }
    if (ini_h == nullptr) {
        fixpp_session_config_destroy(ini_cfg);
    } else {
        EXPECT_EQ(fixpp_engine_start(initiator_engine), FIXPP_ERR_OK);
        bool const established = wait_for_established(ini_h) && wait_for_established(acc_h);
        EXPECT_TRUE(established) << "the pair never established";
        if (established) {
            auto const p = payload();
            EXPECT_EQ(fixpp_session_send(ini_h, p.data(), p.size()), FIXPP_ERR_OK)
                << "the sender refused the payload";
            EXPECT_TRUE(fixpp::test_support::wait_for_flag(ctx.fired, 5s))
                << "the message did not reach the acceptor's callback";
        }
    }

    if (ctx.fired.load(std::memory_order_acquire)) {
        EXPECT_EQ(ctx.rc, FIXPP_ERR_OK) << "fixpp_msg_get_bytes(5011)";
        EXPECT_EQ(ctx.value, kDataValue) << "5011 must read the full counted value";
    }

    if (ini_h != nullptr) {
        EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    }
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(initiator_engine);
    fixpp_engine_destroy(acceptor_engine);
}

}  // namespace
