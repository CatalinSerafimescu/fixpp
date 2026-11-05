// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_session_overlay.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-3's Session half (tasks.md T010): a
// session's own dictionary and overlay survive routing, (a) to (h). Overlay fixtures are built from
// strings here. Comparands: research R-6's table for (c) and (d); for (e) and (f) the Data length
// against its count and 34 against the frame builder's; (g) and (h) their literals, (h) from
// data-model E-3. (e) and (f) also read resolved_application_version() against the frame's slot.
//
// (b)'s mutant is M-91. The evidence file's T020 record says which cells moved at `H0`.

#include <gtest/gtest.h>

#include <fixpp/dict/reify.hpp>
#include <memory_resource>
#include <string>

#include "capi/capi_internal.hpp"
#include "fix/c_api/message.h"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

constexpr const char* kDelivered = "- | stays | c | app=1 adm=0";

const char* const kFixtHead = "<fix type='FIXT' major='1' minor='1' servicepack='0'>";
const char* const k50Head = "<fix type='FIX' major='5' minor='0' servicepack='2'>";

// Header tags an overlay adds to S: scalar 5020, closed enum 5021 {A, B}, int 5022, research R-6's.
const std::string& kHeaderMembers() {
    static const std::string value =
        "<field name='VenueHdrScalar' required='N' /><field name='VenueHdrEnum' required='N' />"
        "<field name='VenueHdrInt' required='N' />";
    return value;
}
const std::string& kHeaderDefs() {
    static const std::string value =
        "<field number='5020' name='VenueHdrScalar' type='STRING' />"
        "<field number='5021' name='VenueHdrEnum' type='CHAR'><value enum='A' description='ALPHA'/>"
        "<value enum='B' description='BETA'/></field>"
        "<field number='5022' name='VenueHdrInt' type='INT' />";
    return value;
}

// (a): merged FIX50SP2 S: a body field and a Required promotion, on NewOrderSingle.
const DictPtr& fix50sp2_with_field_and_promotion() {
    static const DictPtr d = overlay_on(
        "FIX50SP2.xml", std::string(k50Head) +
                            "<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'>"
                            "<field name='VenueBody' required='N' />"
                            "<field name='Account' required='Y' />"
                            "</message></messages><fields>"
                            "<field number='5002' name='VenueBody' type='STRING' />"
                            "</fields></fix>");
    return d;
}

Fixture overlay_a_fixture() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2(), fix44()});
    f.session_dict = fix50sp2_with_field_and_promotion();
    return f;
}

TEST(W3a, OwnVersion_FieldAccepted) {
    const CellRun run =
        run_own_cell("W-3a accepted", overlay_a_fixture(), true, Arm::Active, [](const Rig& r) {
            return frame_35("D", r.peer_ctx(2),
                            kNos() +
                                "1=ACC\x01"
                                "5002=v\x01");
        });
    expect_ending(run, kDelivered);
}

TEST(W3a, OwnVersion_RequiredPromotionEnforced) {
    const CellRun run = run_own_cell(
        "W-3a promotion", overlay_a_fixture(), true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos() + "5002=v\x01"); });
    expect_ending(run, "R 1/1 D 45=2 | stays | c | app=0 adm=0");
}

TEST(W3a, AnotherVersion_FieldRejected) {
    const CellRun run = run_own_cell("W-3a other version", overlay_a_fixture(), true, Arm::Active,
                                     [](const Rig& r) {
                                         return frame_35("D", r.peer_ctx(2),
                                                         kNos() +
                                                             "1=ACC\x01"
                                                             "5002=v\x01",
                                                         "1128=6\x01");
                                     });
    expect_ending(run, "R 2/5002 D 45=2 | stays | c | app=0 adm=0");
}

// (b): merged FIX Latest S, plain FIX Latest registered, no FIX50SP2: resolved to FIX50SP2, S
// answers.
const DictPtr& latest_with_body_field() {
    static const DictPtr d = orchestra_overlay_on_latest(
        "<fixr:repository xmlns:fixr=\"http://fixprotocol.io/2020/orchestra/repository\" "
        "name=\"FIX.Latest\" version=\"FIX.Latest_EP303\"><fixr:fields>"
        "<fixr:field id=\"5002\" name=\"VenueBody\" type=\"String\" /></fixr:fields><fixr:messages>"
        "<fixr:message id=\"14\" name=\"NewOrderSingle\" msgType=\"D\"><fixr:structure>"
        "<fixr:fieldRef id=\"5002\" /></fixr:structure></fixr:message>"
        "</fixr:messages></fixr:repository>");
    return d;
}

TEST(W3b, OverlayFieldAcceptedOnTheFallbackVersion) {
    ASSERT_NE(latest_with_body_field().get(), latest().get());
    Fixture f;
    f.registry = make_registry({fixt11(), latest()});
    f.session_dict = latest_with_body_field();
    const CellRun run = run_own_cell("W-3b", std::move(f), true, Arm::Active, [](const Rig& r) {
        return frame_35("D", r.peer_ctx(2), kNos() + "5002=v\x01");
    });
    expect_ending(run, kDelivered);
}

// (c): T, an overlay's header tags on an application message routed to another table.
const DictPtr& fixt11_with_header_tags() {
    static const DictPtr d =
        overlay_on("FIXT11.xml", std::string(kFixtHead) + "<header>" + kHeaderMembers() +
                                     "</header><fields>" + kHeaderDefs() + "</fields></fix>");
    return d;
}

Fixture t_overlay_fixture() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2(), latest(), fix44()});
    f.session_dict = fixt11_with_header_tags();
    return f;
}

struct HeaderCase {
    const char* name;
    const char* field;
    const char* ending;
};

const HeaderCase kHeaderCases[] = {
    {.name = "Scalar", .field = "5020=v\x01", .ending = kDelivered},
    {.name = "EnumValid", .field = "5021=A\x01", .ending = kDelivered},
    {.name = "EnumInvalid",
     .field = "5021=Z\x01",
     .ending = "R 5/5021 D 45=2 | stays | c | app=0 adm=0"},
    {.name = "IntValid", .field = "5022=7\x01", .ending = kDelivered},
    {.name = "IntInvalid",
     .field = "5022=abc\x01",
     .ending = "R 5/5022 D 45=2 | stays | c | app=0 adm=0"},
};

class W3cHeader : public testing::TestWithParam<HeaderCase> {};

TEST_P(W3cHeader, RoutedToAnotherTable) {
    const HeaderCase& c = GetParam();
    const CellRun run =
        run_own_cell(std::string("W-3c ") + c.name, t_overlay_fixture(), true, Arm::Active,
                     [&](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos(), c.field); });
    expect_ending(run, c.ending);
}

INSTANTIATE_TEST_SUITE_P(W3, W3cHeader, testing::ValuesIn(kHeaderCases),
                         [](const testing::TestParamInfo<HeaderCase>& i) {
                             return std::string{i.param.name};
                         });

// Past the body: a tag S declares, after a body field, passes.
TEST(W3c, PastTheBody_Passes) {
    const CellRun run = run_own_cell(
        "W-3c past the body", t_overlay_fixture(), true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos() + "5020=v\x01"); });
    expect_ending(run, kDelivered);
}

// Collision: S's header tag 5040 closed at {X}, the routed table's NewOrderSingle closes it at {Y}.
// 5041 the same, with the routed table declaring it on ExecutionReport only: the control.
const DictPtr& fixt11_with_collision_tags() {
    static const DictPtr d =
        overlay_on("FIXT11.xml", std::string(kFixtHead) +
                                     "<header><field name='VenueCollide' required='N' /><field "
                                     "name='VenueOther' required='N' />"
                                     "</header><fields>"
                                     "<field number='5040' name='VenueCollide' "
                                     "type='STRING'><value enum='X' description='EX'/></field>"
                                     "<field number='5041' name='VenueOther' type='STRING'><value "
                                     "enum='X' description='EX'/></field>"
                                     "</fields></fix>");
    return d;
}

const DictPtr& fix50sp2_with_collision_tags() {
    static const DictPtr d = overlay_on(
        "FIX50SP2.xml", std::string(k50Head) +
                            "<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'>"
                            "<field name='VenueCollide' required='N' /></message>"
                            "<message name='ExecutionReport' msgtype='8' msgcat='app'>"
                            "<field name='VenueOther' required='N' /></message></messages><fields>"
                            "<field number='5040' name='VenueCollide' type='STRING'><value "
                            "enum='Y' description='WHY'/></field>"
                            "<field number='5041' name='VenueOther' type='STRING'><value enum='Y' "
                            "description='WHY'/></field>"
                            "</fields></fix>");
    return d;
}

Fixture collision_fixture() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2_with_collision_tags()});
    f.session_dict = fixt11_with_collision_tags();
    return f;
}

TEST(W3cCollision, SValuePasses) {
    expect_ending(run_own_cell("W-3c collision X", collision_fixture(), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "5040=X\x01");
                               }),
                  kDelivered);
}

TEST(W3cCollision, RoutedValueRefused) {
    expect_ending(run_own_cell("W-3c collision Y", collision_fixture(), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "5040=Y\x01");
                               }),
                  "R 5/5040 D 45=2 | stays | c | app=0 adm=0");
}

TEST(W3cCollision, TagOnAnotherMsgTypeOnly_Control) {
    expect_ending(run_own_cell("W-3c collision control", collision_fixture(), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "5041=X\x01");
                               }),
                  kDelivered);
}

// Standard tag: T, an overlay closing MessageEncoding(347) to {UTF-8}.
const DictPtr& fixt11_closing_347() {
    static const DictPtr d = overlay_on(
        "FIXT11.xml", std::string(kFixtHead) +
                          "<fields><field number='347' name='MessageEncoding' type='STRING'>"
                          "<value enum='UTF-8' description='UTF8'/></field></fields></fix>");
    return d;
}

Fixture t_closing_347() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2()});
    f.session_dict = fixt11_closing_347();
    return f;
}

TEST(W3cStandardTag, RoutedNos_ClosedValueRefused) {
    ASSERT_FALSE(fixt11_closing_347()->enum_values(347).empty()) << "the overlay did not close it";
    expect_ending(run_own_cell("W-3c 347 refused", t_closing_347(), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "347=Shift_JIS\x01");
                               }),
                  "R 5/347 D 45=2 | stays | c | app=0 adm=0");
}

TEST(W3cStandardTag, RoutedNos_OpenValuePasses) {
    expect_ending(run_own_cell("W-3c 347 passes", t_closing_347(), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "347=UTF-8\x01");
                               }),
                  kDelivered);
}

TEST(W3cStandardTag, Fix44Route_PlainT_Passes) {
    expect_ending(
        run_own_cell("W-3c 347 FIX44 T", own_fixture(Shape::T, {fixt11(), fix50sp2(), fix44()}),
                     true, Arm::Active,
                     [](const Rig& r) {
                         return frame_35("D", r.peer_ctx(2), kNos(),
                                         "1128=6\x01"
                                         "347=KOI8-R\x01");
                     }),
        kDelivered);
}

TEST(W3cStandardTag, Fix44Route_A_RefusedByTheRoute) {
    expect_ending(
        run_own_cell("W-3c 347 FIX44 A", own_fixture(Shape::A, {fixt11(), fix50sp2(), fix44()}),
                     true, Arm::Active,
                     [](const Rig& r) {
                         return frame_35("D", r.peer_ctx(2), kNos(),
                                         "1128=6\x01"
                                         "347=KOI8-R\x01");
                     }),
        "R 5/347 D 45=2 | stays | c | app=0 adm=0");
}

// The four tags, one cell each: S closing the tag where the routed table answers otherwise.
TEST(W3cFourTags, Tag1128_10_OnT_RoutesToLatest) {
    expect_ending(run_own_cell("W-3c 1128", own_fixture(Shape::T, {fixt11(), fix50sp2(), latest()}),
                               true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos() + "3115=1.5\x01",
                                                   "1128=10\x01");
                               }),
                  kDelivered);
}

// CU, PartyEntitlementsRequest: FIX50SP2 has it, FIX44 closes 35 without it, and it requires no
// field.
TEST(W3cFourTags, Tag35_OnFix44S_AFix50sp2OnlyMsgType) {
    bool in_44 = false;
    for (const auto& m : fix44()->messages()) {
        in_44 = in_44 || m.msg_type == "CU";
    }
    ASSERT_FALSE(in_44) << "the recomputed difference moved: pick another MsgType";
    Fixture f;
    f.registry = make_registry({fixt11(), fix44(), fix50sp2()});
    f.session_dict = fix44();
    f.default_appl = dd::application_version::v44;
    f.peer_1137 = "6";
    expect_ending(
        run_own_cell("W-3c 35", std::move(f), true, Arm::Active,
                     [](const Rig& r) { return frame_35("CU", r.peer_ctx(2), "", "1128=9\x01"); }),
        kDelivered);
}

const DictPtr& fixt11_closing_1129_1156() {
    static const DictPtr d = overlay_on(
        "FIXT11.xml", std::string(kFixtHead) +
                          "<fields><field number='1129' name='CstmApplVerID' type='STRING'>"
                          "<value enum='Z' description='ZED'/></field>"
                          "<field number='1156' name='ApplExtID' type='INT'>"
                          "<value enum='7' description='SEVEN'/></field></fields></fix>");
    return d;
}

TEST(W3cFourTags, Tag1129_ByTheRoute) {
    ASSERT_FALSE(fixt11_closing_1129_1156()->enum_values(1129).empty())
        << "the overlay did not close it";
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2()});
    f.session_dict = fixt11_closing_1129_1156();
    expect_ending(run_own_cell("W-3c 1129", std::move(f), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "1129=other\x01");
                               }),
                  kDelivered);
}

TEST(W3cFourTags, Tag1156_ByTheRoute) {
    ASSERT_FALSE(fixt11_closing_1129_1156()->enum_values(1156).empty())
        << "the overlay did not close it";
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2()});
    f.session_dict = fixt11_closing_1129_1156();
    expect_ending(run_own_cell("W-3c 1156", std::move(f), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos(), "1156=303\x01");
                               }),
                  kDelivered);
}

// (d): A, an overlay's header tag on Logon and Heartbeat, through FIXT11.
const DictPtr& fix50sp2_with_header_tags() {
    static const DictPtr d =
        overlay_on("FIX50SP2.xml", std::string(k50Head) + "<header>" + kHeaderMembers() +
                                       "</header><fields>" + kHeaderDefs() + "</fields></fix>");
    return d;
}

Fixture a_overlay_fixture() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2()});
    f.session_dict = fix50sp2_with_header_tags();
    return f;
}

TEST(W3d, HeaderTagOnLogon_Accepted) {
    expect_ending(run_own_cell("W-3d Logon", a_overlay_fixture(), true, Arm::NotConnected,
                               [](const Rig& r) {
                                   return logon_frame(r.peer_ctx(1),
                                                      "1137=9\x01"
                                                      "5020=v\x01");
                               }),
                  "Logon | Active | c | app=0 adm=0");
}

TEST(W3d, HeaderTagOnHeartbeat_Accepted) {
    expect_ending(
        run_own_cell("W-3d Heartbeat", a_overlay_fixture(), true, Arm::Active,
                     [](const Rig& r) { return frame_35("0", r.peer_ctx(2), {}, "5020=v\x01"); }),
        "- | stays | c | app=0 adm=1");
}

// (e) and (f): a header pair.
std::string header_pair_overlay(const char* head) {
    return std::string(head) + "<header>" + kPairMembers() + "</header>" + kPairFields() + "</fix>";
}

const DictPtr& fix50sp2_with_header_pair() {
    static const DictPtr d = overlay_on("FIX50SP2.xml", header_pair_overlay(k50Head));
    return d;
}

const DictPtr& fixt11_with_header_pair() {
    static const DictPtr d = overlay_on("FIXT11.xml", header_pair_overlay(kFixtHead));
    return d;
}

// What one callback saw: the Data, 34, and the same through a clone and a reify handle's view.
struct PairSeen {
    int calls = 0;
    std::string data;
    std::string seq;
    std::string clone_data;
    std::string reify_data;
    dd::application_version resolved = dd::application_version::Unknown;
};

// fixpp_msg_clone over the view, as the C engine's receive path wraps it, read by tag.
std::string clone_field_text(const CountingApp::View& v, std::uint16_t tag) {
    fixpp_msg inbound{};
    inbound.view = &v;
    fixpp_msg_t* clone = nullptr;
    if (const fixpp_error_t e = fixpp_msg_clone(&inbound, &clone); e != FIXPP_ERR_OK) {
        return "clone error " + std::to_string(static_cast<int>(e));
    }
    const char* bytes = nullptr;
    std::size_t len = 0;
    const fixpp_error_t e = fixpp_msg_get_string(clone, tag, &bytes, &len);
    std::string text = e == FIXPP_ERR_OK ? std::string{bytes, len}
                                         : "read error " + std::to_string(static_cast<int>(e));
    (void)fixpp_msg_destroy(clone);
    return text;
}

void watch_pair(Rig& r, PairSeen& seen) {
    const auto read = [&seen](const CountingApp::View& v) {
        ++seen.calls;
        seen.data = field_text(v, 5031);
        seen.seq = field_text(v, 34);
        seen.resolved = v.resolved_application_version();
        seen.clone_data = clone_field_text(v, 5031);
        auto h = dd::reify(v,
                           dd::version_profile{.session = dd::session_version::vt11,
                                               .default_appl = dd::application_version::v50sp2,
                                               .has_per_message_override = true,
                                               ._reserved = 0},
                           std::pmr::new_delete_resource());
        if (h) {
            seen.reify_data = field_text(h->view(), 5031);
        } else {
            seen.reify_data = "reify error " + std::to_string(static_cast<int>(h.error()));
        }
    };
    r.app->on_app = read;
    r.app->on_admin = read;
}

void expect_pair_whole(const PairSeen& seen, const std::string& data, unsigned seq) {
    EXPECT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.data, data) << "the Data whole: its length against the count";
    EXPECT_EQ(seen.seq, std::to_string(seq)) << "MsgSeqNum the header's";
    EXPECT_EQ(seen.clone_data, data) << "a clone reads as the view does";
    EXPECT_EQ(seen.reify_data, data) << "a reify's view reads as the view does";
}

struct PairData {
    const char* name;
    const char* data;
};

// The field's own terminator supplies the closing SOH of `SOH 1128=10 SOH`.
const PairData kEmbeddedVersion{.name = "Embedded1128",
                                .data =
                                    "\x01"
                                    "1128=10"};
const PairData kEmbeddedSeq{.name = "Embedded34",
                            .data =
                                "\x01"
                                "34=7"};
const PairData kNonTagBytes{.name = "NonTagBytes",
                            .data =
                                "ab\x01"
                                "zz"};

class W3ePair : public testing::TestWithParam<PairData> {};

// (e): own version, selection stays S: the embedded 1128 is not read as the header's.
TEST_P(W3ePair, OwnVersion_SelectionStaysS_DataWhole) {
    const std::string data = GetParam().data;
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2(), latest()});
    f.session_dict = fix50sp2_with_header_pair();
    CellRun run = enter_arm(std::move(f), Arm::Active, RigOptions{});
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    PairSeen seen;
    watch_pair(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), pair_field(data)));
    expect_ending(run, kDelivered);
    expect_pair_whole(seen, data, 2);
    EXPECT_EQ(seen.resolved, dd::application_version::v50sp2) << "1137's slot, S's own";
}

INSTANTIATE_TEST_SUITE_P(W3, W3ePair, testing::Values(kEmbeddedVersion, kNonTagBytes),
                         [](const testing::TestParamInfo<PairData>& i) {
                             return std::string{i.param.name};
                         });

class W3fPair : public testing::TestWithParam<PairData> {};

// (f): the same pair on a frame routed to another table: T, by its header 1128.
TEST_P(W3fPair, T_RoutedByHeader1128_DeliveredDataWhole) {
    const std::string data = GetParam().data;
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2(), latest()});
    f.session_dict = fixt11_with_header_pair();
    CellRun run = enter_arm(std::move(f), Arm::Active, RigOptions{});
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    PairSeen seen;
    watch_pair(*run.rig, seen);
    run.ending = run.rig->deliver(
        frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=10\x01" + pair_field(data)));
    expect_ending(run, kDelivered);
    expect_pair_whole(seen, data, 2);
    EXPECT_EQ(seen.resolved, dd::application_version::vlatest) << "the header's 1128";
}

// (f): the transport route of A: a Heartbeat judged by FIXT11, which does not declare the pair.
TEST_P(W3fPair, A_TransportRoute_DeliveredDataWhole) {
    const std::string data = GetParam().data;
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2()});
    f.session_dict = fix50sp2_with_header_pair();
    CellRun run = enter_arm(std::move(f), Arm::Active, RigOptions{});
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    PairSeen seen;
    watch_pair(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2), {}, pair_field(data)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    expect_pair_whole(seen, data, 2);
    EXPECT_EQ(seen.resolved, dd::application_version::Unknown) << "an admin message";
}

// One W-3 (f) cell keeps open()'s private build, for `bind_fixt_routes_`'s `pair_source` (tasks.md
// T007).
TEST(W3fPrivateBuild, T_RoutedByHeader1128_DeliveredDataWhole) {
    const std::string data = kEmbeddedSeq.data;
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2(), latest()});
    f.session_dict = fixt11_with_header_pair();
    RigOptions o;
    o.private_build = true;
    CellRun run = enter_arm(std::move(f), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    PairSeen seen;
    watch_pair(*run.rig, seen);
    run.ending = run.rig->deliver(
        frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=10\x01" + pair_field(data)));
    expect_ending(run, kDelivered);
    expect_pair_whole(seen, data, 2);
    EXPECT_EQ(seen.resolved, dd::application_version::vlatest) << "the header's 1128";
}

INSTANTIATE_TEST_SUITE_P(W3, W3fPair, testing::Values(kEmbeddedSeq, kEmbeddedVersion, kNonTagBytes),
                         [](const testing::TestParamInfo<PairData>& i) {
                             return std::string{i.param.name};
                         });

// (f), Collision: the routed dictionary's body pair shares its Length tag with S's: S's pair reads.
const DictPtr& fix50sp2_with_colliding_body_pair() {
    static const DictPtr d =
        overlay_on("FIX50SP2.xml",
                   std::string(k50Head) +
                       "<messages><message name='NewOrderSingle' msgtype='D' msgcat='app'>"
                       "<field name='VenuePairLen' required='N' />"
                       "<field name='VenueOtherData' required='N' /></message></messages><fields>"
                       "<field number='5030' name='VenuePairLen' type='LENGTH' />"
                       "<field number='5032' name='VenueOtherData' type='DATA' /></fields></fix>");
    return d;
}

TEST(W3fCollision, SHeaderPairReadsOverTheRoutedBodyPair) {
    ASSERT_EQ(fix50sp2_with_colliding_body_pair()->length_pair_data_tag(5030), 5032);
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2_with_colliding_body_pair()});
    f.session_dict = fixt11_with_header_pair();
    CellRun run = enter_arm(std::move(f), Arm::Active, RigOptions{});
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    PairSeen seen;
    watch_pair(*run.rig, seen);
    const std::string data =
        "\x01"
        "34=7";
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), pair_field(data)));
    expect_ending(run, kDelivered);
    expect_pair_whole(seen, data, 2);
}

// (g): the trailer limit: a tag an overlay adds to S's trailer, on a frame routed to another table.
TEST(W3g, OverlayTrailerTag_RoutedFrame_Refused) {
    static const DictPtr s =
        overlay_on("FIXT11.xml",
                   std::string(kFixtHead) +
                       "<trailer><field name='VenueTrailer' required='N' /></trailer><fields>"
                       "<field number='5050' name='VenueTrailer' type='STRING' /></fields></fix>");
    Fixture f;
    f.registry = make_registry({fixt11(), fix50sp2()});
    f.session_dict = s;
    expect_ending(run_own_cell("W-3g", std::move(f), true, Arm::Active,
                               [](const Rig& r) {
                                   return frame_35("D", r.peer_ctx(2), kNos() + "5050=v\x01");
                               }),
                  "R 2/5050 D 45=2 | stays | c | app=0 adm=0");
}

// (h): NoHops's structure, on each route. Own registry: FIX44 and FIX42 are not in the shared set.
struct HopsRoute {
    const char* name;
    Shape shape;
    const char* header_1128;
};

const HopsRoute kHopsRoutes[] = {
    {.name = "T_FIX50SP2", .shape = Shape::T, .header_1128 = ""},
    {.name = "T_FIX44", .shape = Shape::T, .header_1128 = "1128=6\x01"},
    {.name = "T_FIX42", .shape = Shape::T, .header_1128 = "1128=4\x01"},
    {.name = "T_Latest", .shape = Shape::T, .header_1128 = "1128=10\x01"},
    {.name = "A_own", .shape = Shape::A, .header_1128 = ""},
};

Fixture hops_fixture(Shape s) {
    return own_fixture(s, {fixt11(), fix50sp2(), latest(), fix44(), fix42()});
}

class W3hNoHops : public testing::TestWithParam<HopsRoute> {};

std::string hops_frame(const Rig& r, const HopsRoute& route, const std::string& hops) {
    return frame_35("D", r.peer_ctx(2), kNos(), route.header_1128 + hops);
}

TEST_P(W3hNoHops, CountHigh_Refused) {
    const HopsRoute& c = GetParam();
    expect_ending(
        run_own_cell(std::string("W-3h high ") + c.name, hops_fixture(c.shape), true, Arm::Active,
                     [&](const Rig& r) {
                         return hops_frame(r, c,
                                           "627=2\x01"
                                           "628=X\x01");
                     }),
        "R 1/628 D 45=2 | stays | c | app=0 adm=0");
}

TEST_P(W3hNoHops, MissingDelimiter_Refused) {
    const HopsRoute& c = GetParam();
    expect_ending(run_own_cell(std::string("W-3h nodelim ") + c.name, hops_fixture(c.shape), true,
                               Arm::Active,
                               [&](const Rig& r) {
                                   return hops_frame(r, c,
                                                     "627=1\x01"
                                                     "630=5\x01");
                               }),
                  "R 1/628 D 45=2 | stays | c | app=0 adm=0");
}

TEST_P(W3hNoHops, Valid_DeliveredAndGroupReadsOneInstance) {
    const HopsRoute& c = GetParam();
    int instances = -1;
    CellRun run = enter_arm(hops_fixture(c.shape), Arm::Active, RigOptions{});
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    run.rig->app->on_app = [&](const CountingApp::View& v) {
        instances = static_cast<int>(v.offsets().group_slices(627).size());
    };
    run.ending = run.rig->deliver(hops_frame(*run.rig, c,
                                             "627=1\x01"
                                             "628=X\x01"));
    expect_ending(run, kDelivered);
    EXPECT_EQ(instances, 1);
}

// Validation off: all three delivered, research R-4's reading on T and L.
TEST_P(W3hNoHops, ValidationOff_AllDelivered) {
    const HopsRoute& c = GetParam();
    for (const char* hops : {"627=2\x01"
                             "628=X\x01",
                             "627=1\x01"
                             "630=5\x01",
                             "627=1\x01"
                             "628=X\x01"}) {
        expect_ending(
            run_own_cell(std::string("W-3h off ") + c.name, hops_fixture(c.shape), false,
                         Arm::Active, [&](const Rig& r) { return hops_frame(r, c, hops); }),
            kDelivered);
    }
}

INSTANTIATE_TEST_SUITE_P(W3, W3hNoHops, testing::ValuesIn(kHopsRoutes),
                         [](const testing::TestParamInfo<HopsRoute>& i) {
                             return std::string{i.param.name};
                         });

}  // namespace
}  // namespace fixpp::session::test099
