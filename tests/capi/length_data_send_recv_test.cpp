// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/length_data_send_recv_test.cpp — fixpp#426 / fixpp#428 conversational witness
//
// Two C-ABI engines over plaintext loopback (the send_recv_test.cpp topology). The
// initiator builds NewOrderSingle through the C ABI with EncodedText(355) holding
// bytes a scanner that ignores Length+Data would misread: an embedded SOH followed
// by a MsgSeqNum(34) and a Text(58), plus a high-bit byte. It commits and sends.
// The acceptor's receive callback must read EncodedText back byte-for-byte, see no
// Text(58), and see its own MsgSeqNum rather than the forged one.
//
// This crosses every layer #426/#428 changed on the path: fixpp_msg_set_data, the
// commit-time pair check, Session::send_impl's payload walk, the inbound Index
// parse, and the C-ABI read.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>

#include "length_data_capi_support.hpp"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

namespace {

// Pieces a SOH-splitting scanner would misread: a MsgSeqNum, a Text, and a
// CheckSum-shaped `<SOH>10=` (which a forward search for the trailer, as clone's
// frame bounds used to do, takes for the real CheckSum), plus a high-bit byte.
constexpr std::string_view kEncoded{
    "x\x01"
    "34=99\x01"
    "58=F\x01"
    "10=1\xff",
    18};

struct Received {
    std::mutex m;
    std::string encoded;
    std::string cloned;
    fixpp_error_t clone_rc = FIXPP_ERR_UNKNOWN;
    bool text_present = true;
    int64_t seq = 0;
    std::atomic<bool> done{false};
};

}  // namespace

TEST(CapiLengthDataSendRecv, EncodedTextWithEmbeddedSohArrivesByteExact) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                  FIXPP_C_ABI_VERSION_MINOR, &B),
              FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), FIXPP_C_ABI_VERSION_MAJOR,
                                  FIXPP_C_ABI_VERSION_MINOR, &A),
              FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_length_data_session_cfg("ACC-LD", "INI-LD", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);

    Received got;
    auto on_msg = [](const fixpp_msg_t* inbound, void* ud) {
        auto* r = static_cast<Received*>(ud);
        const uint8_t* bytes = nullptr;
        size_t len = 0;
        bool has_text = true;
        int64_t seq = 0;
        fixpp_error_t const rc_b = fixpp_msg_get_bytes(inbound, 355, &bytes, &len);
        fixpp_error_t const rc_t = fixpp_msg_has_tag(inbound, 58, &has_text);
        fixpp_error_t const rc_s = fixpp_msg_get_int(inbound, 34, &seq);
        // The clone re-frames its own copy of the bytes (design §4 row 14).
        fixpp_msg_t* clone = nullptr;
        fixpp_error_t const rc_c = fixpp_msg_clone(inbound, &clone);
        const uint8_t* cbytes = nullptr;
        size_t clen = 0;
        fixpp_error_t const rc_cb =
            rc_c == FIXPP_ERR_OK ? fixpp_msg_get_bytes(clone, 355, &cbytes, &clen) : rc_c;
        std::scoped_lock lk(r->m);
        if (rc_b == FIXPP_ERR_OK) r->encoded.assign(as_sv(bytes, len));
        if (rc_t == FIXPP_ERR_OK) r->text_present = has_text;
        if (rc_s == FIXPP_ERR_OK) r->seq = seq;
        r->clone_rc = rc_cb;
        if (rc_cb == FIXPP_ERR_OK) r->cloned.assign(as_sv(cbytes, clen));
        if (clone != nullptr) fixpp_msg_destroy(clone);
        r->done.store(true, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, on_msg, &got), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);
    std::uint16_t const port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0U);

    fixpp_session_config_t* ini =
        make_length_data_session_cfg("INI-LD", "ACC-LD", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h));
    ASSERT_TRUE(wait_for_established(acc_h));

    fixpp_msg_t* m = nullptr;
    ASSERT_EQ(fixpp_msg_create_outbound(ini_h, "D", 1, &m), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_string(m, 11, "ORD1", 4), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_string(m, 55, "SYM", 3), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_set_data(m, 355, as_u8(kEncoded), kEncoded.size()), FIXPP_ERR_OK);
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
    ASSERT_EQ(fixpp_msg_commit(m, &payload, &payload_len), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_send(ini_h, payload, payload_len), FIXPP_ERR_OK);
    fixpp_msg_destroy(m);

    ASSERT_TRUE(fixpp::test_support::wait_until_observed(
        [&] { return got.done.load(std::memory_order_acquire); }, 5s))
        << "the acceptor never received the order";
    {
        std::scoped_lock lk(got.m);
        EXPECT_EQ(got.encoded, kEncoded);
        EXPECT_FALSE(got.text_present) << "58=F inside EncodedText was read as a field";
        EXPECT_NE(got.seq, 99) << "34=99 inside EncodedText was read as MsgSeqNum";
        ASSERT_EQ(got.clone_rc, FIXPP_ERR_OK) << "clone or its EncodedText read failed";
        EXPECT_EQ(got.cloned, kEncoded) << "the clone must frame past the <SOH>10= in the value";
    }

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}
