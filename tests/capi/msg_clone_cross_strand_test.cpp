// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/msg_clone_cross_strand_test.cpp — SC-006: fixpp_msg_clone cross-strand seam.
//
// [2i §9] seam #13: clone an inbound fixpp_msg_t on strand A's dispatch window, then
// read the clone on strand B AFTER the source dispatch window has closed.
//
// Contract under test (contracts/message-write.md):
//   - fixpp_msg_clone creates an independent owner-controlled arena copy.
//   - The clone is NOT session-tombstoned (session close / engine destroy does not
//     invalidate the clone).
//   - Clone reads (fixpp_msg_get_*) are THREAD_SAFE.
//   - Clone owns its data: valid after the inbound dispatch window closes.
//
// Witness shape (FR-009a seam #13):
//   1. A loopback engine pair; initiator sends a Heartbeat with tag 112=CLONE_TEST.
//   2. The acceptor recv callback (on the session strand = dispatch window) calls
//      fixpp_msg_clone and stores the clone handle in a shared slot.
//   3. The callback signals a drain thread; the dispatch window CLOSES.
//   4. The drain thread (strand B, AFTER window closed) reads the clone via
//      fixpp_msg_get_string(tag 35) and fixpp_msg_get_string(tag 112).
//   5. Asserts byte-identity with the expected values; clone NOT tombstoned by session
//      close/engine destroy.
//
// Anchors: contracts/message-write.md section clone; spec.md SC-006; research D-9/E-1/E-9.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"
#include "capi_loopback_support.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

namespace {

// A shared slot where the acceptor recv callback deposits the clone handle and
// signals the drain thread.
struct CloneSlot {
    std::mutex mu;
    std::condition_variable cv;
    fixpp_msg_t* clone = nullptr;  // deposited by the recv callback, read by drain thread
    bool ready = false;

    // Called by the acceptor recv callback (on the session strand).
    void deposit(fixpp_msg_t* c) {
        std::unique_lock<std::mutex> lk(mu);
        clone = c;
        ready = true;
        cv.notify_one();
    }

    // Called by the drain thread: waits for the clone to be deposited, then reads it.
    bool wait_ready(std::chrono::milliseconds deadline = 4000ms) {
        std::unique_lock<std::mutex> lk(mu);
        return cv.wait_for(lk, deadline, [this] { return ready; });
    }
};

// The recv callback: clones the inbound message and deposits the clone.
// Runs on the session strand (dispatch window).
static void clone_recv_cb(const fixpp_msg_t* msg, void* userdata) {
    auto* slot = static_cast<CloneSlot*>(userdata);
    fixpp_msg_t* clone = nullptr;
    fixpp_error_t rc = fixpp_msg_clone(msg, &clone);
    if (rc == FIXPP_ERR_OK && clone != nullptr) {
        slot->deposit(clone);
    } else {
        slot->deposit(nullptr);  // signal failure
    }
    // The dispatch window closes when this callback returns.
}

}  // namespace

// SC-006 seam #13: clone on strand A (dispatch window) → read on strand B after window closes.
TEST(MsgCloneCrossStrand, CloneOnDispatchWindowReadOnDrainThread) {
    // Setup loopback pair
    fixpp_engine_t* init_eng = nullptr;
    fixpp_engine_t* acc_eng = nullptr;
    fixpp_session_t* init_sess = nullptr;
    fixpp_session_t* acc_sess = nullptr;

    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 4, &init_eng), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 4, &acc_eng), FIXPP_ERR_OK);

    // Acceptor session
    {
        fixpp_session_config_t* sc = make_session_cfg("FIXSRV", "FIXCLI", FIXPP_ROLE_ACCEPTOR);
        set_loopback_endpoint(sc, "127.0.0.1", 0);
        ASSERT_EQ(fixpp_session_open(acc_eng, sc, &acc_sess), FIXPP_ERR_OK);
    }

    // Install clone recv callback on acceptor
    CloneSlot slot;
    ASSERT_EQ(fixpp_session_register_callback(acc_sess, clone_recv_cb, &slot), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(acc_eng), FIXPP_ERR_OK);

    // Read the acceptor's bound port
    fixpp::session::SessionId acc_id{};
    {
        auto* ae = reinterpret_cast<fixpp_engine*>(acc_eng);
        ASSERT_TRUE(ae->state_ && ae->state_->engine_.has_value());
        ASSERT_FALSE(ae->sessions_.empty());
        acc_id = ae->sessions_[0]->id;
    }
    uint16_t port = wait_for_bound_port(acc_eng, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind a port";

    // Initiator session
    {
        fixpp_session_config_t* sc = make_session_cfg("FIXCLI", "FIXSRV", FIXPP_ROLE_INITIATOR);
        set_loopback_endpoint(sc, "127.0.0.1", port);
        ASSERT_EQ(fixpp_session_open(init_eng, sc, &init_sess), FIXPP_ERR_OK);
    }
    ASSERT_EQ(fixpp_engine_start(init_eng), FIXPP_ERR_OK);

    ASSERT_TRUE(wait_for_established(init_sess)) << "initiator did not establish";
    ASSERT_TRUE(wait_for_established(acc_sess)) << "acceptor did not establish";

    // Send a NewOrderSingle ("D") from initiator — an APP message so it routes
    // through fromApp on the acceptor and triggers the clone recv callback.
    // (Heartbeat "0" is ADMIN and does not trigger fromApp → recv_cb.)
    // tag 112 (TestReqID) is declared in the minimal dict for Heartbeat; as a
    // raw application payload, the session does not validate outbound app fields.
    const std::string payload = std::string("35=D\x01") + "112=CLONE_TEST\x01";
    const auto* pb = reinterpret_cast<const uint8_t*>(payload.data());
    ASSERT_EQ(fixpp_session_send(init_sess, pb, payload.size()), FIXPP_ERR_OK);

    // Wait for the acceptor recv callback to deposit the clone
    ASSERT_TRUE(slot.wait_ready()) << "acceptor recv callback did not fire within deadline";

    fixpp_msg_t* clone = slot.clone;
    ASSERT_NE(clone, nullptr) << "fixpp_msg_clone returned null or failed inside callback";

    // Strand B: read the clone AFTER the dispatch window has closed.
    // The dispatch window closed when the recv callback returned.
    // We are now on the test thread (strand B).

    // Read MsgType (tag 35) — sent as "D" (NewOrderSingle, an APP message)
    const char* mt = nullptr;
    size_t mt_len = 0;
    EXPECT_EQ(fixpp_msg_get_msg_type(clone, &mt, &mt_len), FIXPP_ERR_OK);
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(std::string_view(mt, mt_len), "D") << "clone MsgType must be 'D' (NewOrderSingle)";

    // Read tag 112 (TestReqID)
    const char* treq = nullptr;
    size_t treq_len = 0;
    EXPECT_EQ(fixpp_msg_get_string(clone, 112, &treq, &treq_len), FIXPP_ERR_OK);
    ASSERT_NE(treq, nullptr);
    EXPECT_EQ(std::string_view(treq, treq_len), "CLONE_TEST")
        << "clone tag 112 must be 'CLONE_TEST' (byte-identity with original)";

    // Clone is NOT tombstoned by session close
    fixpp_session_close(init_sess);
    fixpp_session_close(acc_sess);
    init_sess = nullptr;
    acc_sess = nullptr;

    // After session close, clone reads still work (clone is session-independent)
    mt = nullptr;
    mt_len = 0;
    EXPECT_EQ(fixpp_msg_get_msg_type(clone, &mt, &mt_len), FIXPP_ERR_OK)
        << "clone must still be readable after session close";
    EXPECT_NE(mt, nullptr);
    EXPECT_EQ(std::string_view(mt, mt_len), "D");

    // Clone is NOT tombstoned by engine destroy
    fixpp_engine_destroy(init_eng);
    fixpp_engine_destroy(acc_eng);
    init_eng = nullptr;
    acc_eng = nullptr;

    // After engine destroy, clone reads still work (clone owns its own arena)
    mt = nullptr;
    mt_len = 0;
    EXPECT_EQ(fixpp_msg_get_msg_type(clone, &mt, &mt_len), FIXPP_ERR_OK)
        << "clone must still be readable after engine destroy";
    EXPECT_NE(mt, nullptr);
    EXPECT_EQ(std::string_view(mt, mt_len), "D");

    treq = nullptr;
    treq_len = 0;
    EXPECT_EQ(fixpp_msg_get_string(clone, 112, &treq, &treq_len), FIXPP_ERR_OK)
        << "clone tag 112 must be readable after engine destroy";
    EXPECT_NE(treq, nullptr);
    EXPECT_EQ(std::string_view(treq, treq_len), "CLONE_TEST");

    // Destroy the clone (single destroy — double-destroy is UB per B-051-2).
    EXPECT_EQ(fixpp_msg_destroy(clone), FIXPP_ERR_OK);
    clone = nullptr;
}

// Clone NULL/dead-handle guards
TEST(MsgCloneCrossStrand, CloneNullAndDeadHandleGuards) {
    fixpp_msg_t* clone = nullptr;

    // NULL src
    EXPECT_EQ(fixpp_msg_clone(nullptr, &clone), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(clone, nullptr);

    // NULL clone_out
    fixpp_msg inbound_shell{};
    inbound_shell.tag_ = FIXPP_HANDLE_TAG_MSG;
    inbound_shell.flavour = FixppMsgFlavour::inbound;
    inbound_shell.view = nullptr;
    EXPECT_EQ(
        fixpp_msg_clone(reinterpret_cast<const fixpp_msg_t*>(&inbound_shell), nullptr),
        FIXPP_ERR_NULL_HANDLE);

    // Dead handle (tag_ = FIXPP_HANDLE_TAG_DEAD) → INVALID_HANDLE
    fixpp_msg dead_shell{};
    dead_shell.tag_ = FIXPP_HANDLE_TAG_DEAD;
    EXPECT_EQ(
        fixpp_msg_clone(reinterpret_cast<const fixpp_msg_t*>(&dead_shell), &clone),
        FIXPP_ERR_INVALID_HANDLE);
    EXPECT_EQ(clone, nullptr);
}
