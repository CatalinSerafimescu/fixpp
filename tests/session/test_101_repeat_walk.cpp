// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_repeat_walk.cpp
//
// 101-inbound-reject-fidelity, tasks.md T011 (W101-29 (a), (b), (d)): research D5's repeat walk
// on a real validating FIXT Session in Active, under the owner ruling research D5 records: every
// field, header and body, is read with the selected route's hooks, one Length+Data carry runs
// across the header's stop, and S's header union classifies each field. The answer is 14 when a
// later 35 lies past the header, else 13 when one lies in it.
//
// Each cell asserts the Reject's 373, 371, 372 and 45, not its Text(58): the cells witness the
// walk, and the text has witnesses of its own (W101-12).
//
// Each cell asserts the pair it needs as a precondition on the loaded dictionaries, so a
// dictionary change fails the precondition rather than the claim:
//   (a) a Length the route pairs and S and the standard table do not (an overlay on FIX50SP2);
//   (b) an overlay pair, the mirror of
//   `W17HeaderLengthPairs.OnlyTheDataTagInTheHeader_IsAHeaderPair`; (d) a custom S built from the
//   vendored FIXT11 with one edit.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/wire/dict_hooks.hpp>
#include <string>
#include <string_view>

#include "session/scan_frame_header.hpp"
#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

// The one Reject the repeat refusal draws: 371=35, 372 = the first 35, 45 = the first 34.
void expect_repeat_reject(const Ending101& e, std::string_view reason, std::string_view first) {
    SCOPED_TRACE(describe(e));
    ASSERT_EQ(e.of_type("3").size(), 1U) << "exactly one Reject";
    const Decoded& rj = *e.of_type("3").front();
    EXPECT_EQ(rj.get(373), reason) << "SessionRejectReason(373)";
    EXPECT_EQ(rj.get(371), "35");
    EXPECT_EQ(rj.get(372), first);
    EXPECT_EQ(rj.get(45), "2");
    EXPECT_EQ(e.after, fsm_state::Active);
}

std::string counted(std::uint16_t length_tag, std::uint16_t data_tag, const std::string& data) {
    return std::to_string(length_tag) + "=" + std::to_string(data.size()) + "\x01" +
           std::to_string(data_tag) + "=" + data + "\x01";
}

// ── (a): a Length only the route declares, its Data embedding `SOH 35=X` ────────────────
//
// The route's pair is 099's overlay on FIX50SP2 (`fix50sp2_with_body_pair`), which S (FIXT11)
// does not declare; the precondition below checks both halves, and that the standard table
// answers neither. A shipped pair would serve as well: research D5's re-derive recipe (a
// non-standard DATA field of a shipped FIX50SP2 or FIX Latest dictionary, read from the loaded
// Dictionary) finds one if it exists; the evidence file (T011) records the reading.

TEST(RepeatWalk101, W101_29a_RouteOnlyLength_DataCounted_InHeaderRepeat_Reject13) {
    const DictPtr& routed = t99::fix50sp2_with_body_pair();
    ASSERT_EQ(routed->length_pair_data_tag(5030), 5031) << "the route pairs the Length";
    ASSERT_EQ(t99::fixt11()->length_pair_data_tag(5030), 0) << "S does not";
    ASSERT_EQ(fixpp::wire::dict_hooks::none().data_tag_for_length(5030), 0)
        << "nor the standard table";
    t99::Fixture fx;
    fx.registry = t99::make_registry({t99::fixt11(), routed, t99::latest()});
    fx.session_dict = t99::fixt11();
    auto rig = started(spec(Kind::fixt).with(fx), Arm::Active);
    ASSERT_TRUE(rig);
    const std::string body = counted(5030, 5031,
                                     std::string("a\x01"
                                                 "35=X"));
    expect_repeat_reject(rig->deliver(rig->frame("D", 2, body, "35=0\x01")), "13", "D");
}

// ── (b): an S header Length whose Data is the stopping field ────────────────────────────

const DictPtr& fixt11_with_header_length() {
    static const DictPtr d =
        t99::overlay_on("FIXT11.xml",
                        "<fix type='FIXT' major='1' minor='1' servicepack='0'><header>"
                        "<field name='VenuePairLen' required='N' /></header><messages>"
                        "<message name='Heartbeat' msgtype='0' msgcat='admin'>"
                        "<field name='VenuePairData' required='N' /></message></messages>"
                        "<fields><field number='5030' name='VenuePairLen' type='LENGTH' />"
                        "<field number='5031' name='VenuePairData' type='DATA' /></fields></fix>");
    return d;
}

t99::Fixture fixture_over(const DictPtr& s) {
    t99::Fixture f;
    f.registry = t99::make_registry({s, t99::fix50sp2(), t99::latest()});
    f.session_dict = s;
    return f;
}

t99::Fixture header_length_fixture() { return fixture_over(fixt11_with_header_length()); }

void expect_header_length_preconditions() {
    const dd::table_view s = fixt11_with_header_length()->as_table_view();
    ASSERT_TRUE(s.is_header_tag(5030));
    ASSERT_FALSE(s.is_header_tag(5031));
    ASSERT_FALSE(dd::is_fixt_standard_header_tag(5031));
    const auto pairs = dd::header_length_pairs(*fixt11_with_header_length());
    ASSERT_EQ(pairs.size(), 1U);
    ASSERT_EQ(pairs[0].length_tag, 5030);
    ASSERT_EQ(pairs[0].data_tag, 5031);
}

TEST(RepeatWalk101, W101_29b_HeaderLength_DataIsStoppingField_InHeaderRepeat_Reject13) {
    expect_header_length_preconditions();
    auto rig = started(spec(Kind::fixt).with(header_length_fixture()), Arm::Active);
    ASSERT_TRUE(rig);
    const std::string body = counted(5030, 5031,
                                     std::string("a\x01"
                                                 "35=X"));
    expect_repeat_reject(rig->deliver(rig->frame("D", 2, body, "35=0\x01")), "13", "D");
}

TEST(RepeatWalk101, W101_29b_HeaderLength_DataIsStoppingField_PastHeaderRepeat_Reject14) {
    expect_header_length_preconditions();
    auto rig = started(spec(Kind::fixt).with(header_length_fixture()), Arm::Active);
    ASSERT_TRUE(rig);
    const std::string body = counted(5030, 5031,
                                     std::string("a\x01"
                                                 "35=X")) +
                             "58=t\x01"
                             "35=0\x01";
    expect_repeat_reject(rig->deliver(rig->frame("D", 2, body)), "14", "D");
}

// ── (d): the residual the ruling closes ─────────────────────────────────────────────────
//
// A custom S: FIXT11 with ApplExtID(1156), a tag of `dict::is_fixt_standard_header_tag`, taken
// out of its <header>, retyped as a Length and paired with a Data tag outside S's header union.
// `dict::header_length_pairs` then skips the pair, so no route carries it: the route's hooks split
// the Data, which is a run of well-formed fields, so the frame scans without a fault.

const DictPtr& custom_s() {
    static const DictPtr d = t99::edited_vendored("FIXT11.xml", [](std::string x) {
        auto replace = [&x](std::string_view from, std::string_view to) {
            const auto at = x.find(from);
            if (at != std::string::npos) {
                x.replace(at, from.size(), to);
            }
        };
        replace("<field name='ApplExtID' required='N' />", "");
        replace("<field number='1156' name='ApplExtID' type='INT' />",
                "<field number='1156' name='ApplExtID' type='LENGTH' />"
                "<field number='5031' name='VenuePairData' type='DATA' />");
        replace(
            "<message name='Heartbeat' msgtype='0' msgcat='admin'>",
            "<message name='Heartbeat' msgtype='0' msgcat='admin'>"
            "<field name='ApplExtID' required='N' /><field name='VenuePairData' required='N' />");
        return x;
    });
    return d;
}

TEST(RepeatWalk101, W101_29d_CustomSPairOnStandardHeaderTag_RouteSplitsData_Reject14) {
    const dd::table_view s = custom_s()->as_table_view();
    ASSERT_TRUE(dd::is_fixt_standard_header_tag(1156));
    ASSERT_FALSE(s.is_header_tag(1156)) << "S's own header omits the Length";
    ASSERT_EQ(s.length_pair_data_tag(1156), 5031) << "S pairs the Length";
    ASSERT_FALSE(s.is_header_tag(5031));
    ASSERT_FALSE(dd::is_fixt_standard_header_tag(5031));
    ASSERT_TRUE(dd::header_length_pairs(*custom_s()).empty()) << "no route carries the pair";

    const t99::Fixture fx = fixture_over(custom_s());
    auto rig = started(spec(Kind::fixt).with(fx), Arm::Active);
    ASSERT_TRUE(rig);
    const std::string data =
        "1=a\x01"
        "35=X\x01"
        "2=b";
    const std::string frame = rig->frame("D", 2, counted(1156, 5031, data), "35=0\x01");

    // The header scan the session runs, with the route's hooks, records no fault.
    const auto routes = session_test_access::routes(rig->session());
    ASSERT_TRUE(routes);
    // The entry the session binds: {FIX50SP2, S} when S has header pairs, else {FIX50SP2, none}.
    const auto* route =
        routes->find(t99::fix50sp2().get(),
                     routes->has_pair_source(custom_s().get()) ? custom_s().get() : nullptr);
    ASSERT_NE(route, nullptr);
    ASSERT_EQ(route->owner->length_pair_data_tag(1156), 0) << "the route does not pair 1156";
    const auto hdr =
        detail::scan_frame_header(std::as_bytes(std::span{frame.data(), frame.size()}),
                                  fixpp::wire::dict_hooks::for_table_view(*route->owner));
    EXPECT_EQ(hdr.fault, fixpp::wire::field_fault::none);

    expect_repeat_reject(rig->deliver(frame), "14", "D");
}

}  // namespace
}  // namespace fixpp::session::test101
