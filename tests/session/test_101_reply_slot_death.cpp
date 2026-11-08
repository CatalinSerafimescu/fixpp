// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_reply_slot_death.cpp
//
// 101-inbound-reject-fidelity, tasks.md T045 (W101-46a; research D7 "The serialized-caller
// precondition", FR-008): two overlapping on_inbound_frame calls on one Session, each drawing a
// Reject that overflows its site's buffer, are outside on_inbound_frame's contract, and a build
// with assertions asserts on it where the second reply takes the inbound reply slot. The first
// call's outbound store is held by a HookedStore, so its reply holds the slot while the second
// call runs. Built with assertions only (the condition is NDEBUG's, as the assertion's is); its
// own executable because the cell aborts.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <string>

#include "support/hooked_store.hpp"
#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

#if !defined(NDEBUG)

// A validating FIX44 acceptor in Active whose outbound stores hold once `armed` is set; the
// MsgType overflows the validation gate's Reject buffer and fits the ceiling.
void overlap_two_overflowed_replies() {
    bool armed = false;
    auto factory = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    factory->hooks.on_outbound_store = [&armed] { return armed; };
    factory->hooks.release_when = [] { return false; };
    auto rig = std::make_unique<Rig101>(
        spec(Kind::fix44).with([factory](SessionConfig& c) { c.store_factory = factory; }));
    if (rig->start(Arm::Active)) {
        return;  // no death: the cell fails on the missing assertion text
    }
    const auto site = session_test_access::reply_site_buffer_bytes().emit_reject_text;
    const std::string mt = long_msg_type(site);
    armed = true;
    auto& ioc = rig->r->ioc;
    const std::string f1 = rig->frame(mt, 2);
    const std::string f2 = rig->frame(mt, 3);
    auto b1 = std::as_bytes(std::span{f1.data(), f1.size()});
    auto b2 = std::as_bytes(std::span{f2.data(), f2.size()});
    // Both calls are spawned before the context runs: the store's hold yields by posting, so a
    // context run interleaves the second call with the first's held store.
    auto first = asio::co_spawn(ioc, rig->session().on_inbound_frame(b1), asio::use_future);
    auto second = asio::co_spawn(ioc, rig->session().on_inbound_frame(b2), asio::use_future);
    ioc.restart();
    ioc.run_for(std::chrono::seconds{2});
}

TEST(ReplySlotDeath101, OverlappingCallsBothOverflowing_Assert) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(overlap_two_overflowed_replies(),
                 "inbound reply slot taken while another inbound reply holds it");
}

#endif

}  // namespace
}  // namespace fixpp::session::test101
