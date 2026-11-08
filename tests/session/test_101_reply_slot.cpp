// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_reply_slot.cpp
//
// 101-inbound-reject-fidelity, tasks.md T044 and T047 (research D7, FR-007, FR-008):
//   ReplySlotRelease101 (W101-46b): the inbound reply slot is released on every exit of the
//   site's tail (a veto, a throw, a failed assign, a close during the store), so a next
//   overflowed reply is sent.
//   EchoSiteBoundary101 (W101-49): at each reply site, the boundary between the site's own buffer
//   and the slot, derived by building the site's message into a buffer of the site's size (read
//   by name through session_test_access), with controls one byte under and over it: under, the
//   frame is built in the site's buffer and the slot is untouched; over, it is rebuilt in the
//   slot; both carry 372 byte-equal.
//   BestEffortLeaf101 (M101-34d's unit cell): inbound_reject_best_effort_leaf_, called through
//   session_test_access, with a MsgType that overflows its buffer and fits the ceiling.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/fix_time.hpp>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "support/hooked_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

RejectInputs inputs_of(const Rig101& rig, unsigned ref_seq, int reason, int ref_tag,
                       std::string_view text) {
    return RejectInputs{.sender = rig.r->cfg.sender_comp_id,
                        .target = rig.r->cfg.target_comp_id,
                        .begin = rig.begin(),
                        .sending_time = rig.stamped_sending_time(),
                        .seq = rig.next_out(),
                        .ref_seq = ref_seq,
                        .ref_tag = ref_tag,
                        .reason = reason,
                        .text = std::string(text)};
}

void clear_slot(Rig101& rig) {
    auto slot = session_test_access::inbound_reply_slot(rig.session());
    std::ranges::fill(slot, std::byte{0});
}

bool slot_used(Rig101& rig) {
    auto slot = session_test_access::inbound_reply_slot(rig.session());
    return std::ranges::any_of(slot, [](std::byte b) { return b != std::byte{0}; });
}

constexpr auto kSites = session_test_access::reply_site_buffer_bytes();

// ── ReplySlotRelease101 (W101-46b) ──────────────────────────────────────────────────────

// A validation-off acceptor in Active whose fromApp rejects: each application frame draws a
// BusinessMessageReject. `mt` overflows the BMR leaf's buffer and fits the ceiling.
struct BmrRig {
    std::unique_ptr<Rig101> rig;
    std::string mt;
};

BmrRig bmr_rig() {
    BmrRig b;
    b.rig = started(spec(Kind::fix44).off(), Arm::Active);
    if (!b.rig) {
        return b;
    }
    b.rig->app->reject_from_app = true;
    const RejectInputs in = inputs_of(*b.rig, 2, 0, 0, {});
    const auto site = longest_fitting(
        [&](std::size_t n) {
            return bmr_fits(in, long_msg_type(n == 0 ? 1 : n), kSites.business_reject_leaf);
        },
        session_test_access::send_frame_bytes());
    const auto ceiling = longest_bmr_msg_type(in);
    if (!site || !ceiling || *ceiling <= *site + 1) {
        ADD_FAILURE() << "no MsgType overflows the BMR leaf's buffer and fits the ceiling";
        b.rig.reset();
        return b;
    }
    b.mt = long_msg_type(*site + 1);
    return b;
}

TEST(ReplySlotRelease101, BmrToAppVeto_ReleasesTheSlot_NextOverflowedBmrSent) {
    BmrRig b = bmr_rig();
    ASSERT_TRUE(b.rig);
    b.rig->app->veto_to_app = true;
    const Ending101 vetoed = b.rig->deliver(b.rig->frame(b.mt, 2));
    EXPECT_TRUE(vetoed.of_type("j").empty()) << describe(vetoed);
    EXPECT_EQ(vetoed.to_app, 1) << "the rebuilt BMR reached toApp";
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(b.rig->session()));
    EXPECT_EQ(vetoed.after, fsm_state::Active);
    b.rig->app->veto_to_app = false;
    const Ending101 next = b.rig->deliver(b.rig->frame(b.mt, 3));
    ASSERT_EQ(next.of_type("j").size(), 1U) << describe(next);
    EXPECT_EQ(next.of_type("j").front()->get(372), b.mt);
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(b.rig->session()));
}

TEST(ReplySlotRelease101, BmrToAppThrows_ReleasesTheSlot) {
    BmrRig b = bmr_rig();
    ASSERT_TRUE(b.rig);
    b.rig->app->throw_to_app = true;
    const Ending101 e = b.rig->deliver(b.rig->frame(b.mt, 2));
    EXPECT_TRUE(e.of_type("j").empty()) << describe(e);
    EXPECT_EQ(e.to_app, 1);
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(b.rig->session()));
}

// A failed assign_outbound: the outbound counter at its maximum, so the next assign fails.
TEST(ReplySlotRelease101, RejectAssignFails_ReleasesTheSlot) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    auto& mgr = session_test_access::seqnum_mgr(rig->session());
    seqnum_manager_test_access::set_counters(mgr, rig->next_in(),
                                             std::numeric_limits<seqnum_t>::max());
    const RejectInputs in = inputs_of(*rig, 2, 11, 0, kTextInvalidMsgType);
    const auto site = longest_fitting(
        [&](std::size_t n) {
            return reject_fits(in, long_msg_type(n == 0 ? 1 : n), kSites.emit_reject_text);
        },
        session_test_access::send_frame_bytes());
    ASSERT_TRUE(site);
    clear_slot(*rig);
    const Ending101 e = rig->deliver(rig->frame(long_msg_type(*site + 1), 2));
    EXPECT_TRUE(e.of_type("3").empty()) << describe(e);
    EXPECT_TRUE(slot_used(*rig)) << "the Reject was rebuilt in the slot before the assign";
    EXPECT_EQ(e.after, fsm_state::Disconnected) << "the failed assign's ending";
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(rig->session()));
}

// A close while an overflowed reply's store is suspended (research D7, "Release before the next
// frame"): the reply's coroutine holds the slot across the store; the close ends it, and the
// holder releases the slot, so the next overflowed reply on the same Session takes it.
TEST(ReplySlotRelease101, CloseWhileReplyStoreSuspended_ReleasesTheSlot_NextReplyTakesIt) {
    bool armed = false;
    auto factory = std::make_shared<fixpp::test_support::HookedStoreFactory>();
    factory->hooks.on_outbound_store = [&armed] { return armed; };
    factory->hooks.release_when = [] { return false; };
    auto rig = std::make_unique<Rig101>(
        spec(Kind::fix44).off().with([factory](SessionConfig& c) { c.store_factory = factory; }));
    ASSERT_FALSE(rig->start(Arm::Active).has_value());
    rig->note_sending_time();
    rig->app->reject_from_app = true;
    const RejectInputs in = inputs_of(*rig, 2, 0, 0, {});
    const auto site = longest_fitting(
        [&](std::size_t n) {
            return bmr_fits(in, long_msg_type(n == 0 ? 1 : n), kSites.business_reject_leaf);
        },
        session_test_access::send_frame_bytes());
    ASSERT_TRUE(site);
    const std::string mt = long_msg_type(*site + 1);
    auto& ioc = rig->r->ioc;
    Session& sess = rig->session();

    armed = true;
    const std::string f1 = rig->frame(mt, 2);
    auto reply =
        asio::co_spawn(ioc, sess.on_inbound_frame(std::as_bytes(std::span{f1})), asio::use_future);
    ioc.restart();
    ioc.run_for(std::chrono::milliseconds{100});
    ASSERT_TRUE(session_test_access::inbound_reply_slot_held(sess))
        << "precondition: the reply is suspended in its store, holding the slot";
    armed = false;
    auto closed = asio::co_spawn(ioc, sess.close(close_mode::terminal), asio::use_future);
    ASSERT_TRUE(t99::pump_ready(ioc, closed, std::chrono::seconds{30}));
    ASSERT_TRUE(t99::pump_ready(ioc, reply, std::chrono::seconds{30}));
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(sess)) << "released by the close";

    clear_slot(*rig);
    // A closed Session does not open again (open() answers session_already_open), so the next
    // overflowed reply is drawn through a reply site directly: it takes the slot, which asserts,
    // in builds with assertions, that no holder kept it.
    const RejectInputs rin = inputs_of(*rig, 3, 3, 0, {});
    const auto rsite = longest_fitting(
        [&](std::size_t n) {
            return reject_fits(rin, long_msg_type(n == 0 ? 1 : n), kSites.reject_best_effort);
        },
        session_test_access::send_frame_bytes());
    ASSERT_TRUE(rsite);
    const std::string rmt = long_msg_type(*rsite + 1);
    const std::string st = rig->stamped_sending_time();
    auto again =
        asio::co_spawn(ioc, session_test_access::inbound_reject_best_effort_leaf(sess, 3, rmt, st),
                       asio::use_future);
    ASSERT_TRUE(t99::pump_ready(ioc, again, std::chrono::seconds{30}));
    (void)again.get();
    EXPECT_TRUE(slot_used(*rig)) << "the next reply was rebuilt in the slot";
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(sess));
}

// ── EchoSiteBoundary101 (W101-49) ───────────────────────────────────────────────────────

struct BoundarySite {
    const char* name;
    Spec spec;
    int reason;
    int ref_tag;
    std::string_view text;
    std::string before_34;
    std::size_t site_bytes;
    bool bmr = false;
};

std::vector<BoundarySite> boundary_sites() {
    return {
        {"gate", spec(Kind::fix44), 11, 0, kTextInvalidMsgType, {}, kSites.emit_reject_text},
        {"inbound_reject_leaf_",
         spec(Kind::fix44).off(),
         1,
         122,
         {},
         "43=Y\x01",
         kSites.reject_leaf},
        {"emit_session_reject_",
         spec(Kind::fix44).off().without_application(),
         3,
         0,
         {},
         {},
         kSites.emit_reject},
        {"inbound_business_reject_leaf_",
         spec(Kind::fix44).off(),
         0,
         0,
         {},
         {},
         kSites.business_reject_leaf,
         true},
    };
}

void PrintTo(const BoundarySite& s, std::ostream* os) { *os << s.name; }

class EchoSiteBoundary101 : public testing::TestWithParam<BoundarySite> {};

// The longest MsgType the site's own buffer holds, on a session started as the cell's.
std::optional<std::size_t> site_boundary(const BoundarySite& s, const Rig101& rig) {
    const RejectInputs in = inputs_of(rig, 2, s.reason, s.ref_tag, s.text);
    return longest_fitting(
        [&](std::size_t n) {
            const std::string mt = long_msg_type(n == 0 ? 1 : n);
            return s.bmr ? bmr_fits(in, mt, s.site_bytes) : reject_fits(in, mt, s.site_bytes);
        },
        session_test_access::send_frame_bytes());
}

void expect_reply(const BoundarySite& s, std::ptrdiff_t delta, bool in_slot) {
    auto rig = started(s.spec, Arm::Active);
    ASSERT_TRUE(rig);
    if (s.bmr) {
        rig->app->reject_from_app = true;
    }
    const auto boundary = site_boundary(s, *rig);
    ASSERT_TRUE(boundary);
    const std::string mt =
        long_msg_type(static_cast<std::size_t>(static_cast<std::ptrdiff_t>(*boundary) + delta));
    SCOPED_TRACE(std::string(s.name) + " n=" + std::to_string(mt.size()));
    clear_slot(*rig);
    const Ending101 e = rig->deliver(rig->frame(mt, 2, {}, s.before_34));
    const char* type = s.bmr ? "j" : "3";
    ASSERT_EQ(e.of_type(type).size(), 1U) << describe(e);
    EXPECT_EQ(e.of_type(type).front()->get(372), mt) << "RefMsgType(372) byte-equal";
    EXPECT_EQ(slot_used(*rig), in_slot) << (in_slot ? "rebuilt in the slot" : "built in place");
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(rig->session()));
}

TEST_P(EchoSiteBoundary101, AtTheSiteBuffer_BuiltInPlace) { expect_reply(GetParam(), 0, false); }
TEST_P(EchoSiteBoundary101, OneByteOver_RebuiltInTheSlot) { expect_reply(GetParam(), 1, true); }

INSTANTIATE_TEST_SUITE_P(W101_49, EchoSiteBoundary101, testing::ValuesIn(boundary_sites()),
                         [](const testing::TestParamInfo<BoundarySite>& i) {
                             return t99::name_token(i.param.name);
                         });

// ── BestEffortLeaf101 (M101-34d's unit cell) ────────────────────────────────────────────

TEST(BestEffortLeaf101, OverflowingMsgTypeFitsTheCeiling_SentWith372) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const RejectInputs in = inputs_of(*rig, 2, 3, 0, {});
    const auto site = longest_fitting(
        [&](std::size_t n) {
            return reject_fits(in, long_msg_type(n == 0 ? 1 : n), kSites.reject_best_effort);
        },
        session_test_access::send_frame_bytes());
    ASSERT_TRUE(site);
    const std::string mt = long_msg_type(*site + 1);
    rig->r->out.clear();
    const std::string st = rig->stamped_sending_time();
    auto fut = asio::co_spawn(
        rig->r->ioc,
        session_test_access::inbound_reject_best_effort_leaf(rig->session(), 2, mt, st),
        asio::use_future);
    ASSERT_TRUE(t99::pump_ready(rig->r->ioc, fut, std::chrono::seconds{30}));
    EXPECT_TRUE(fut.get().has_value());
    ASSERT_EQ(rig->r->out.size(), 1U);
    const Decoded rj = decode(rig->r->out.front());
    EXPECT_EQ(rj.get(35), "3");
    EXPECT_EQ(rj.get(372), mt) << "RefMsgType(372) byte-equal";
    EXPECT_FALSE(session_test_access::inbound_reply_slot_held(rig->session()));
}

// ── ReplayGrowth101 (orchestrator ruling R8) ────────────────────────────────────────────
//
// replay_outbound_range_ asserts that the replay buffer's headroom holds a replay's growth,
// bounding each timestamp by the formatter's widest form, spelled there as the nanos grammar. This
// cell checks that bound against the formatter itself: no precision writes a longer UTCTimestamp.
TEST(ReplayGrowth101, NoPrecisionWritesALongerTimestampThanTheNanosForm) {
    using fixpp::core::fix_time_precision;
    const std::string_view widest = "YYYYMMDD-HH:MM:SS.sssssssss";
    const fixpp::core::utc_time_point tp{std::chrono::seconds{4102444799} +
                                         std::chrono::nanoseconds{999999999}};
    std::size_t longest = 0;
    for (auto p : {fix_time_precision::seconds, fix_time_precision::millis,
                   fix_time_precision::micros, fix_time_precision::nanos}) {
        std::array<char, 64> buf{};
        auto r = fixpp::core::utc_time_to_fix_string(tp, p, std::span<char>{buf});
        ASSERT_TRUE(r.has_value());
        longest = std::max(longest, r->size());
    }
    EXPECT_EQ(longest, widest.size());
}

}  // namespace
}  // namespace fixpp::session::test101
