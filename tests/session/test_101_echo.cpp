// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_echo.cpp
//
// 101-inbound-reject-fidelity, tasks.md T013 (W101-40 to W101-44, W101-47, W101-48; spec US4
// scenarios 1 to 7, FR-007): a Reject or BusinessMessageReject echoes the inbound MsgType in 372
// verbatim whenever it fits the engine's outbound ceiling, Session::kSendFrameBytes. Above it, a
// Reject is sent without 372 and a BusinessMessageReject is not sent, with one rate-bounded
// session log record (research D7, contracts/reject-frames.md "RefMsgType echo").
//
// Thresholds are derived at test time, never written as a MsgType length (research D7, D9):
// each cell builds the site's message with the values the frame under test will carry (the
// session's CompIDs and SendingTime, the 34 it will stamp, the 45 it will echo, the gate's 58)
// into a buffer of kSendFrameBytes, read through session_test_access, and the longest MsgType for
// which that build succeeds is the boundary. Only the ceiling is used here: a MsgType that long
// overflows every site's own buffer, which is smaller. The site-buffer boundary controls need
// each site's size by name and are tasks.md T044's.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <fixpp/log/logger.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"
#include "support/log_capture.hpp"
#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

using fixpp::test_support::LogCapture;

// The values a Reject the session builds next takes from it, for a frame at MsgSeqNum `ref_seq`.
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

// A frame's fields other than BodyLength, CheckSum, SendingTime and 372.
std::vector<std::string> fields_but_372(const Decoded& d) {
    std::vector<std::string> v = field_set(d);
    std::erase_if(v, [](const std::string& f) { return f.starts_with("372="); });
    return v;
}

// ── The Reject sites ────────────────────────────────────────────────────────────────────

struct RejectSite {
    const char* name;
    Spec spec;
    int reason;
    int ref_tag;
    std::string_view text;
    std::string before_34;  // what puts the frame on the site's path
    bool callback;          // the site fires toAdmin (an Application is registered)
};

RejectSite gate_site() { return {"gate", spec(Kind::fix44), 11, 0, kTextInvalidMsgType, {}, true}; }
// PossDup Arm C on validation off: PossDupFlag(43)=Y without OrigSendingTime(122).
RejectSite leaf_site() {
    return {"inbound_reject_leaf_", spec(Kind::fix44).off(), 1, 122, {}, "43=Y\x01", true};
}
// The zero-argument emit_session_reject_: an application MsgType with no Application.
RejectSite no_app_site() {
    return {
        "emit_session_reject_", spec(Kind::fix44).off().without_application(), 3, 0, {}, {}, false};
}

// The site's Reject for a MsgType of `n` bytes, on a fresh session in Active.
struct SiteRun {
    std::unique_ptr<Rig101> rig;
    Ending101 e;
    std::string msg_type;
};

SiteRun run_site(const RejectSite& s, const std::function<std::size_t(std::size_t)>& pick) {
    SiteRun run;
    run.rig = started(s.spec, Arm::Active);
    if (!run.rig) {
        return run;
    }
    const auto max = longest_reject_msg_type(inputs_of(*run.rig, 2, s.reason, s.ref_tag, s.text));
    if (!max) {
        ADD_FAILURE() << "no MsgType fits the ceiling";
        run.rig.reset();
        return run;
    }
    run.msg_type = long_msg_type(pick(*max));
    run.e = run.rig->deliver(run.rig->frame(run.msg_type, 2, {}, s.before_34));
    return run;
}

// The site's one Reject with 372 = the inbound MsgType byte for byte, every other field as for
// a short MsgType's Reject, one outbound number and one callback (W101-44).
void expect_reject_with_372(const RejectSite& s, const SiteRun& run) {
    ASSERT_TRUE(run.rig);
    SCOPED_TRACE(std::string(s.name) + " n=" + std::to_string(run.msg_type.size()));
    const Ending101& e = run.e;
    ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
    const Decoded& rj = *e.of_type("3").front();
    EXPECT_EQ(rj.get(372), run.msg_type) << "RefMsgType(372) byte-equal";
    const Ending101 short_twin = ending_of(
        s.spec, Arm::Active, [&s](const Rig101& r) { return r.frame("U1", 2, {}, s.before_34); });
    ASSERT_EQ(short_twin.of_type("3").size(), 1U) << describe(short_twin);
    EXPECT_EQ(fields_but_372(rj), fields_but_372(*short_twin.of_type("3").front()));
    EXPECT_EQ(rj.get(34), std::to_string(e.next_out_before)) << "the peeked number";
    EXPECT_EQ(e.next_out_after, e.next_out_before + 1) << "one outbound number";
    if (s.callback) {
        EXPECT_EQ(e.to_admin, 1) << "one toAdmin";
        ASSERT_FALSE(run.rig->app->to_admin_372.empty());
        EXPECT_EQ(run.rig->app->to_admin_372.back(), run.msg_type) << "toAdmin sees 372";
    }
    EXPECT_EQ(e.after, fsm_state::Active);
}

// Above the ceiling: the Reject sent without 372, every other field as for a short MsgType's.
void expect_reject_without_372(const RejectSite& s, const SiteRun& run) {
    ASSERT_TRUE(run.rig);
    SCOPED_TRACE(std::string(s.name) + " n=" + std::to_string(run.msg_type.size()));
    const Ending101& e = run.e;
    ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
    const Decoded& rj = *e.of_type("3").front();
    EXPECT_FALSE(rj.has(372)) << "RefMsgType(372) omitted";
    const Ending101 short_twin = ending_of(
        s.spec, Arm::Active, [&s](const Rig101& r) { return r.frame("U1", 2, {}, s.before_34); });
    ASSERT_EQ(short_twin.of_type("3").size(), 1U) << describe(short_twin);
    EXPECT_EQ(fields_but_372(rj), fields_but_372(*short_twin.of_type("3").front()));
    EXPECT_EQ(e.next_out_after, e.next_out_before + 1) << "one outbound number";
    if (s.callback) {
        EXPECT_EQ(e.to_admin, 1) << "one toAdmin";
    }
    EXPECT_EQ(e.after, fsm_state::Active);
}

const auto kHalf = [](std::size_t max) { return max / 2; };
const auto kAtCeiling = [](std::size_t max) { return max; };
const auto kOverCeiling = [](std::size_t max) { return max + 1; };

// W101-40 (i): overflows the site's buffer, fits the ceiling. (iii): one byte under and over
// the ceiling. (ii): over the ceiling, sent without 372. W101-42: at the ceiling, toAdmin sees
// the 372 the wire carries.
TEST(Echo101, W101_40_Gate_FitsCeiling_372) {
    expect_reject_with_372(gate_site(), run_site(gate_site(), kHalf));
}
TEST(Echo101, W101_40_Gate_AtCeiling_372_ToAdmin) {
    expect_reject_with_372(gate_site(), run_site(gate_site(), kAtCeiling));
}
TEST(Echo101, W101_40_Gate_OverCeiling_No372) {
    expect_reject_without_372(gate_site(), run_site(gate_site(), kOverCeiling));
}
TEST(Echo101, W101_40_RejectLeaf_FitsCeiling_372) {
    expect_reject_with_372(leaf_site(), run_site(leaf_site(), kHalf));
}
TEST(Echo101, W101_40_RejectLeaf_AtCeiling_372_ToAdmin) {
    expect_reject_with_372(leaf_site(), run_site(leaf_site(), kAtCeiling));
}
TEST(Echo101, W101_40_RejectLeaf_OverCeiling_No372) {
    expect_reject_without_372(leaf_site(), run_site(leaf_site(), kOverCeiling));
}
TEST(Echo101, W101_40_NoApplication_FitsCeiling_372) {
    expect_reject_with_372(no_app_site(), run_site(no_app_site(), kHalf));
}
TEST(Echo101, W101_40_NoApplication_AtCeiling_372) {
    expect_reject_with_372(no_app_site(), run_site(no_app_site(), kAtCeiling));
}
TEST(Echo101, W101_40_NoApplication_OverCeiling_No372) {
    expect_reject_without_372(no_app_site(), run_site(no_app_site(), kOverCeiling));
}

// The record's rate window is max(HeartBtInt, 1 s) on the session's clock, HeartBtInt being the
// configured value; the record cells configure 1 s. Moves the mock clock past it, runs what that
// posted, and returns whether the session is still in the state it was in.
bool past_record_window(Rig101& rig) {
    const fsm_state before = rig.session().state();
    rig.r->clock->advance(std::chrono::seconds{2});
    for (int i = 0; i < 4; ++i) {
        rig.r->ioc.restart();
        rig.r->ioc.poll();
    }
    return rig.session().state() == before;
}

// ── The BusinessMessageReject leaf (validation-off acceptor, fromApp rejects) ────────────

struct BmrRun {
    std::unique_ptr<Rig101> rig;
    std::shared_ptr<LogCapture> log;
    std::size_t max = 0;
};

BmrRun bmr_session() {
    BmrRun b;
    b.log = std::make_shared<LogCapture>();
    auto logger = b.log->logger;
    b.rig = started(spec(Kind::fix44).off().with([logger](SessionConfig& c) {
        c.logger_override = logger;
        c.heartbeat_interval = std::chrono::seconds{1};
        with_memory_store(c);
    }),
                    Arm::Active);
    if (!b.rig) {
        return b;
    }
    b.rig->app->reject_from_app = true;
    RejectInputs in = inputs_of(*b.rig, 2, 0, 0, {});
    const auto max = longest_bmr_msg_type(in);
    if (!max) {
        ADD_FAILURE() << "no MsgType fits the ceiling";
        b.rig.reset();
        return b;
    }
    b.max = *max;
    return b;
}

// One BMR with 372 byte-equal, toApp seeing it, one number; then a ResendRequest covering it
// resends it with 372, not a GapFill, and the session stays Active.
void expect_bmr_with_372_and_resent(std::size_t n_of_max(std::size_t)) {
    BmrRun b = bmr_session();
    ASSERT_TRUE(b.rig);
    const std::string mt = long_msg_type(n_of_max(b.max));
    SCOPED_TRACE("n=" + std::to_string(mt.size()));
    const Ending101 e = b.rig->deliver(b.rig->frame(mt, 2));
    ASSERT_EQ(e.of_type("j").size(), 1U) << describe(e);
    const Decoded& bmr = *e.of_type("j").front();
    EXPECT_EQ(bmr.get(372), mt) << "RefMsgType(372) byte-equal";
    EXPECT_EQ(bmr.get(45), "2");
    EXPECT_EQ(e.to_app, 1) << "one toApp";
    ASSERT_FALSE(b.rig->app->to_app_372.empty());
    EXPECT_EQ(b.rig->app->to_app_372.back(), mt) << "toApp sees 372";
    EXPECT_EQ(e.next_out_after, e.next_out_before + 1) << "one outbound number";
    const std::string bmr_seq = bmr.get(34);
    const Ending101 rs = b.rig->deliver(b.rig->frame("2", 3,
                                                     "7=" + bmr_seq +
                                                         "\x01"
                                                         "16=" +
                                                         bmr_seq + "\x01"));
    EXPECT_TRUE(rs.of_type("4").empty()) << "not a GapFill: " << describe(rs);
    ASSERT_EQ(rs.of_type("j").size(), 1U) << describe(rs);
    EXPECT_EQ(rs.of_type("j").front()->get(372), mt) << "resent with 372";
    EXPECT_EQ(rs.after, fsm_state::Active);
}

// W101-41: US4 scenarios 3 and 4; W101-42: at the ceiling, toApp sees the 372.
TEST(Echo101, W101_41_Bmr_FitsCeiling_372_Resent) {
    expect_bmr_with_372_and_resent([](std::size_t max) { return max / 2; });
}
TEST(Echo101, W101_41_Bmr_AtCeiling_372_ToApp_Resent) {
    expect_bmr_with_372_and_resent([](std::size_t max) { return max; });
}

// Over the ceiling: no BMR, no toApp, no outbound number, the inbound consumed, Active, and one
// record; a second inside the rate window writes none, and the next record carries the count.
TEST(Echo101, W101_41_Bmr_OverCeiling_NotSent_OneRecord_RateBounded) {
    BmrRun b = bmr_session();
    ASSERT_TRUE(b.rig);
    const std::string mt = long_msg_type(b.max + 1);
    const Ending101 first = b.rig->deliver(b.rig->frame(mt, 2));
    EXPECT_TRUE(first.of_type("j").empty()) << describe(first);
    EXPECT_EQ(first.to_app, 0);
    EXPECT_EQ(first.next_out_after, first.next_out_before) << "no outbound number";
    EXPECT_EQ(first.next_in_after, first.next_in_before + 1) << "consumed";
    EXPECT_EQ(first.after, fsm_state::Active);
    const Ending101 second = b.rig->deliver(b.rig->frame(mt, 3));
    EXPECT_TRUE(second.of_type("j").empty()) << describe(second);
    ASSERT_TRUE(past_record_window(*b.rig)) << "the clock's move changed the session's state";
    const Ending101 third = b.rig->deliver(b.rig->frame(mt, 4));
    EXPECT_TRUE(third.of_type("j").empty()) << describe(third);
    const auto recs = ceiling_records(*b.log, "j");
    ASSERT_EQ(recs.size(), 2U) << "one record per window";
    EXPECT_EQ(recs[0].ref_msg_type_len, mt.size());
    EXPECT_EQ(recs[0].suppressed, 0U);
    EXPECT_EQ(recs[1].ref_msg_type_len, mt.size());
    EXPECT_EQ(recs[1].suppressed, 1U) << "the next record carries the count";
}

// ── W101-43 and W101-40 (iv): long CompIDs, in the NotConnected gate arm ────────────────
//
// The acceptor sends nothing before the peer's Logon, so a gate Reject is the first frame its
// CompIDs size. The peer's Logon omits SendingTime(52): the gate's required-field Reject.

std::string logon_without_52(const Rig101& rig) {
    const t99::Ctx c = rig.ctx(1);
    return t99::wire(rig.begin(),
                     "35=A\x01"
                     "34=1\x01"
                     "49=" +
                         c.snd +
                         "\x01"
                         "56=" +
                         c.tgt + "\x01" + t99::kLogonBody());
}

// The CompID length (both CompIDs equal) whose gate Reject, with 372 or without it, is the
// longest that builds into the ceiling.
std::optional<std::size_t> longest_compid(bool with_372) {
    RejectInputs in;
    in.begin = "FIX.4.4";
    in.sending_time = t99::kFresh;
    in.seq = 1;
    in.ref_seq = 1;
    in.ref_tag = 52;
    in.reason = 1;
    in.text = std::string(kTextRequiredTagMissing);
    return longest_fitting(
        [&](std::size_t n) {
            in.sender = std::string(n == 0 ? 1 : n, 'S');
            in.target = std::string(n == 0 ? 1 : n, 'T');
            return reject_fits(in, with_372 ? "A" : "", session_test_access::send_frame_bytes());
        },
        session_test_access::send_frame_bytes());
}

Spec compid_spec(std::size_t n, const std::shared_ptr<fixpp::log::Logger>& logger) {
    return spec(Kind::fix44).with([n, logger](SessionConfig& c) {
        c.sender_comp_id = std::string(n, 'S');
        c.target_comp_id = std::string(n, 'T');
        if (logger) {
            c.logger_override = logger;
            c.heartbeat_interval = std::chrono::seconds{1};
        }
    });
}

TEST(Echo101, W101_43_LongCompIds_GateReject_Sent_With372) {
    const auto n = longest_compid(/*with_372=*/true);
    ASSERT_TRUE(n);
    auto rig = started(compid_spec(*n, nullptr), Arm::NotConnected);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(logon_without_52(*rig));
    ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
    const Decoded& rj = *e.of_type("3").front();
    EXPECT_EQ(rj.get(372), "A");
    EXPECT_EQ(rj.get(373), "1");
    EXPECT_EQ(rj.get(371), "52");
    EXPECT_EQ(rj.get(58), kTextRequiredTagMissing);
    EXPECT_EQ(rj.get(49), std::string(*n, 'S'));
    EXPECT_EQ(e.to_admin, 1);
    EXPECT_EQ(e.next_out_after, e.next_out_before + 1);
}

// W101-40 (iv), US4 scenario 7: the Reject exceeds the ceiling even without 372. Nothing sent,
// no number, no toAdmin, the session left as the arm leaves it when its Reject is sent, and one
// record naming the Reject; a second inside the window writes none, the next carries the count.
TEST(Echo101, W101_40iv_RejectOverCeilingWithout372_NotSent_OneRecord) {
    const auto fit = longest_compid(/*with_372=*/false);
    ASSERT_TRUE(fit);
    const std::size_t n = *fit + 1;
    // The state the arm leaves when its Reject is sent: the same refusal with short CompIDs.
    const Ending101 sent = ending_of(spec(Kind::fix44), Arm::NotConnected, logon_without_52);
    ASSERT_EQ(sent.of_type("3").size(), 1U) << describe(sent);

    auto log = std::make_shared<LogCapture>();
    auto rig = started(compid_spec(n, log->logger), Arm::NotConnected);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(logon_without_52(*rig));
    EXPECT_TRUE(e.out.empty()) << describe(e);
    EXPECT_EQ(e.next_out_after, e.next_out_before) << "no outbound number";
    EXPECT_EQ(e.to_admin, 0) << "no toAdmin";
    EXPECT_EQ(e.after, sent.after) << "the arm's own ending";
    std::size_t expected_records = 1;
    if (e.after == fsm_state::NotConnected) {
        (void)rig->deliver(logon_without_52(*rig));
        ASSERT_TRUE(past_record_window(*rig)) << "the clock's move changed the session's state";
        (void)rig->deliver(logon_without_52(*rig));
        expected_records = 2;
    }
    const auto recs = ceiling_records(*log, "3");
    ASSERT_EQ(recs.size(), expected_records);
    EXPECT_EQ(recs[0].ref_msg_type_len, 1U) << "the inbound MsgType's length";
    EXPECT_EQ(recs[0].suppressed, 0U);
    if (expected_records == 2) {
        EXPECT_EQ(recs[1].suppressed, 1U) << "the next record carries the count";
    }
}

// ── W101-47: 092's disposer with a fitting 3-byte MsgType ───────────────────────────────

TEST(Echo101, W101_47_Disposer_ThreeByteMsgType_372Present) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(rig->frame("ABC", 2, "3a5=x\x01"));
    ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
    const Decoded& rj = *e.of_type("3").front();
    EXPECT_EQ(rj.get(373), "0") << "the disposer's malformed-tag Reject";
    EXPECT_EQ(rj.get(372), "ABC");
}

// ── W101-48: `35=D=X` is echoed `372=D=X` and parses back as one field ─────────────────

TEST(Echo101, W101_48_EqualsSignEchoedVerbatim_ParsesBackAsOneField) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(rig->frame("D=X", 2));
    ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
    const std::string& raw = e.of_type("3").front()->raw;
    const auto bytes = std::as_bytes(std::span{raw.data(), raw.size()});
    auto fv = fixpp::wire::test::make_frame_view(bytes);
    ASSERT_TRUE(fv);
    std::array<std::byte, 8192> buf{};
    std::pmr::monotonic_buffer_resource arena{buf.data(), buf.size(),
                                              std::pmr::null_memory_resource()};
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv);
    const auto f = mv->get(372);
    ASSERT_TRUE(f);
    const auto v = f->bytes();
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(v.data()), v.size()), "D=X");
}

}  // namespace
}  // namespace fixpp::session::test101
