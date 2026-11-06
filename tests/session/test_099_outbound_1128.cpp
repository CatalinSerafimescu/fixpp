// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_outbound_1128.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-11 (tasks.md T088), FR-015: a header
// 1128 on an application-supplied frame reaches the wire before the first body field, read from the
// bytes the peer receives. And the L-row's limit, research R-7: a `toApp` view on T reads no
// application group, because the outbound path keeps S's table.
//
// `build_send_frame_` keeps a tag in the header when `kSendHeaderTags` holds it or when S declares
// it in its header. So only a session whose S does not declare 1128 shows what the set alone does;
// that precondition is its own assertion below.
//
// The expected field order is spelled here rather than read from the send path's own set, so a
// change there cannot move this witness.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <string>
#include <utility>
#include <vector>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

struct WireField {
    std::uint32_t tag{};
    std::string value;
};

// The frame as the peer reads it, field by field. Every value here holds no SOH.
std::vector<WireField> wire_fields(const std::string& frame) {
    std::vector<WireField> fields;
    std::size_t at = 0;
    while (at < frame.size()) {
        const std::size_t eq = frame.find('=', at);
        const std::size_t soh = frame.find('\x01', at);
        if (eq == std::string::npos || soh == std::string::npos || eq > soh) {
            ADD_FAILURE() << "not a field at offset " << at << " of " << frame;
            return fields;
        }
        fields.push_back(
            WireField{.tag = static_cast<std::uint32_t>(std::stoul(frame.substr(at, eq - at))),
                      .value = frame.substr(eq + 1, soh - eq - 1)});
        at = soh + 1;
    }
    return fields;
}

// kNos()'s tags, in its order.
const std::vector<std::uint32_t>& kNosBodyTags() {
    static const std::vector<std::uint32_t> value = {11, 21, 55, 54, 60, 40, 38};
    return value;
}

// Spelled independently of the send path: what a FIXT application message's header holds here.
const std::vector<std::uint32_t>& kExpectedHeaderTags() {
    static const std::vector<std::uint32_t> value = {8, 9, 34, 35, 49, 52, 56, 1128};
    return value;
}

std::vector<std::uint32_t> tags_of(const std::vector<WireField>& fields) {
    std::vector<std::uint32_t> tags;
    tags.reserve(fields.size());
    for (const WireField& f : fields) {
        tags.push_back(f.tag);
    }
    return tags;
}

// The frame opens 8, 9, 35; the header set follows, its 1128 once, with the value the application
// gave; then the body, whole and in the application's order; then the checksum alone.
void expect_1128_in_the_header(const std::string& frame) {
    const std::vector<WireField> fields = wire_fields(frame);
    const std::vector<std::uint32_t> tags = tags_of(fields);
    ASSERT_GE(tags.size(), 3U);
    EXPECT_EQ(tags[0], 8U);
    EXPECT_EQ(tags[1], 9U);
    EXPECT_EQ(tags[2], 35U);

    const auto first_body = std::ranges::find(tags, kNosBodyTags().front());
    ASSERT_NE(first_body, tags.end()) << frame;
    std::vector<std::uint32_t> header(tags.begin(), first_body);
    std::ranges::sort(header);
    EXPECT_EQ(header, kExpectedHeaderTags()) << "ahead of the first body field: " << frame;

    std::vector<std::uint32_t> rest(first_body, tags.end());
    std::vector<std::uint32_t> expected_rest = kNosBodyTags();
    expected_rest.push_back(10);
    EXPECT_EQ(rest, expected_rest) << "the body whole, in order, then the checksum: " << frame;

    EXPECT_EQ(std::count(tags.begin(), tags.end(), 1128U), 1);
    for (const WireField& f : fields) {
        if (f.tag == 1128U) {
            EXPECT_EQ(f.value, "10");
        }
    }
}

std::string shape_param_name(Shape s) { return shape_name(s); }

struct SendCase {
    const char* name{};
    std::string payload;
};

const std::vector<SendCase>& send_cases() {
    static const std::vector<SendCase> cases = {
        {.name = "AfterAllTheBody", .payload = "35=D\x01" + kNos() + "1128=10\x01"},
        {.name = "AfterTheFirstBodyField",
         .payload = std::string("35=D\x01") + "11=C\x01" + "1128=10\x01" +
                    kNos().substr(std::string("11=C\x01").size())},
        {.name = "AlreadyAhead",
         .payload = "35=D\x01"
                    "1128=10\x01" +
                    kNos()},
    };
    return cases;
}

class W11Send : public testing::TestWithParam<std::tuple<Shape, SendCase>> {};

TEST_P(W11Send, Header1128_BeforeTheFirstBodyField) {
    const auto& [shape, c] = GetParam();
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    const std::string err = run.rig->send(c.payload);
    EXPECT_EQ(err, "");
    ASSERT_EQ(run.rig->out.size(), 1U);
    expect_1128_in_the_header(run.rig->out[0]);
}

INSTANTIATE_TEST_SUITE_P(W11, W11Send,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::ValuesIn(send_cases())),
                         [](const auto& i) {
                             return shape_param_name(std::get<0>(i.param)) + "_" +
                                    std::get<1>(i.param).name;
                         });

// Why A is the cell that can tell, recomputed: its S declares no 1128 in its header, so the send
// path's own set is all that keeps it there.
TEST(W11Precondition, ShapeA_SDeclaresNo1128InItsHeader) {
    EXPECT_FALSE(shape_dict(Shape::A)->as_table_view(dd::fixt_framing).is_header_tag(1128));
}

// ── The L-row's limit ───────────────────────────────────────────────────────────────────

const std::string& kPartiesGroup() {
    static const std::string value =
        "453=1\x01"
        "448=P\x01"
        "447=D\x01"
        "452=1\x01";
    return value;
}

// What a callback's view reads of NoPartyIDs.
int party_instances(const CountingApp::View& v) {
    return static_cast<int>(v.offsets().group_slices(453).size());
}

// A `toApp` view uses S's table, and T's is FIXT11, which knows no application group.
TEST(W11ToApp, OnT_ReadsNoApplicationGroup) {
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    int instances = -1;
    run.rig->app->on_to_app = [&](const CountingApp::View& v) { instances = party_instances(v); };
    EXPECT_EQ(run.rig->send("35=D\x01" + kNos() + kPartiesGroup()), "");
    EXPECT_EQ(instances, 0);
    ASSERT_EQ(run.rig->out.size(), 1U);
    EXPECT_NE(run.rig->out[0].find("453=1\x01"), std::string::npos) << "the group still goes out";
}

// The same payload on A, whose S declares the group: the read above can report one.
TEST(W11ToApp, OnA_ReadsTheGroup_TheControl) {
    CellRun run = enter_arm(shape_fixture(Shape::A), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    int instances = -1;
    run.rig->app->on_to_app = [&](const CountingApp::View& v) { instances = party_instances(v); };
    EXPECT_EQ(run.rig->send("35=D\x01" + kNos() + kPartiesGroup()), "");
    EXPECT_EQ(instances, 1);
}

// Inbound on T the view follows the frame's route, so the limit is outbound's alone.
TEST(W11ToApp, OnT_AnInboundViewReadsTheGroup_TheLimitIsOutbounds) {
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    int instances = -1;
    run.rig->app->on_app = [&](const CountingApp::View& v) { instances = party_instances(v); };
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos() + kPartiesGroup()));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(instances, 1);
}

}  // namespace
}  // namespace fixpp::session::test099
