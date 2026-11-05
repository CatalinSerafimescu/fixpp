// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_version_fields.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-2 (tasks.md T009): a field only
// version B defines is rejected when the frame resolves to another version and accepted when it
// resolves to B, on T and A, for FIX50SP2, FIX Latest and FIX 4.4 (US1 scenarios 2 and 5). The
// fields are a set difference recomputed over the vendored files, but for FIX Latest's 3115,
// whose valid value is 1.5 (the matrix's preamble).

#include <gtest/gtest.h>

#include <string>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

struct VersionField {
    std::string msg_type;
    std::uint16_t tag = 0;
    std::string value;
    std::string body;  // the rest of a frame valid on both routes
};

// In FIX50SP2's NewOrderSingle and not in FIX44's.
VersionField fix50sp2_only() {
    const std::uint16_t tag = only_field(*fix50sp2(), "D", {fix44().get(), fixt11().get()});
    const auto field = fix50sp2()->field("D", tag);
    return {.msg_type = "D",
            .tag = tag,
            .value = field ? plain_value(*fix50sp2(), *field) : std::string{},
            .body = kNos()};
}
// In FIX Latest's NewOrderSingle and not in FIX50SP2's (the matrix's preamble).
VersionField latest_only() {
    return {.msg_type = "D", .tag = 3115, .value = "1.5", .body = kNos()};
}
// The FIX 4.4-only pair: recomputed, a MsgType and a tag FIX50SP2, the other route here, lacks
// there.
VersionField fix44_only() {
    static const VersionField f = [] {
        const OnlyPair p = only_pair(*fix44(), {fix50sp2().get()});
        std::string body = required_body(*fix44(), p.msg_type);
        if (p.group_no_tag != 0) {
            body += std::to_string(p.group_no_tag) + "=1\x01";
        }
        return VersionField{
            .msg_type = p.msg_type, .tag = p.tag, .value = p.value, .body = std::move(body)};
    }();
    return f;
}

std::string frame_of(const Rig& r, const VersionField& f, const char* header_1128) {
    return frame_35(f.msg_type, r.peer_ctx(2),
                    f.body + std::to_string(f.tag) + "=" + f.value + "\x01", header_1128);
}

std::string reject_on_field(const VersionField& f) {
    return "R 2/" + std::to_string(f.tag) + " " + f.msg_type + " 45=2 | stays | c | app=0 adm=0";
}

Fixture all_versions(Shape s) { return own_fixture(s, {fixt11(), fix50sp2(), latest(), fix44()}); }

constexpr const char* kDelivered = "- | stays | c | app=1 adm=0";

// A session whose only application version is `v`, the comparand of US1 scenario 5.
Fixture twin_of(const DictPtr& version_dict, dd::application_version v, const char* wire_1137) {
    Fixture f;
    f.registry = make_registry({fixt11(), version_dict});
    f.session_dict = fixt11();
    f.default_appl = v;
    f.peer_1137 = wire_1137;
    return f;
}

struct W2Cell {
    const char* name{};
    VersionField (*field)() = nullptr;
    const char* to_b{};  // the 1128 that resolves the frame to B, or empty for the negotiated one
    const char* to_other{};  // the 1128 that resolves it to another version
    DictPtr (*other)() = nullptr;
    dd::application_version other_version;
    const char* other_wire{};
};

DictPtr fix44_ptr() { return fix44(); }
DictPtr fix50sp2_ptr() { return fix50sp2(); }

const W2Cell kCells[] = {
    {.name = "FIX50SP2",
     .field = fix50sp2_only,
     .to_b = "",
     .to_other = "1128=6\x01",
     .other = fix44_ptr,
     .other_version = dd::application_version::v44,
     .other_wire = "6"},
    {.name = "Latest",
     .field = latest_only,
     .to_b = "1128=10\x01",
     .to_other = "",
     .other = fix50sp2_ptr,
     .other_version = dd::application_version::v50sp2,
     .other_wire = "9"},
    {.name = "FIX44",
     .field = fix44_only,
     .to_b = "1128=6\x01",
     .to_other = "",
     .other = fix50sp2_ptr,
     .other_version = dd::application_version::v50sp2,
     .other_wire = "9"},
};

struct W2Param {
    W2Cell cell;
    Shape shape{};
};

class W2VersionFields : public testing::TestWithParam<W2Param> {};

TEST_P(W2VersionFields, ResolvedToBAccepted) {
    const auto& [c, s] = GetParam();
    const VersionField f = c.field();
    ASSERT_NE(f.tag, 0) << "the recomputed difference is empty";
    const CellRun run = run_own_cell(std::string("W-2 ") + c.name + " to B " + shape_name(s),
                                     all_versions(s), /*validate=*/true, Arm::Active,
                                     [&](const Rig& r) { return frame_of(r, f, c.to_b); });
    expect_ending(run, kDelivered);
}

TEST_P(W2VersionFields, ResolvedToOtherRejected_AsOnTheTwin) {
    const auto& [c, s] = GetParam();
    const VersionField f = c.field();
    ASSERT_NE(f.tag, 0) << "the recomputed difference is empty";
    const CellRun run = run_own_cell(std::string("W-2 ") + c.name + " to other " + shape_name(s),
                                     all_versions(s), /*validate=*/true, Arm::Active,
                                     [&](const Rig& r) { return frame_of(r, f, c.to_other); });
    expect_ending(run, reject_on_field(f));
    const CellRun twin = run_own_cell(std::string("W-2 ") + c.name + " twin " + shape_name(s),
                                      twin_of(c.other(), c.other_version, c.other_wire),
                                      /*validate=*/true, Arm::Active,
                                      [&](const Rig& r) { return frame_of(r, f, ""); });
    expect_ending(twin, reject_on_field(f));
}

std::vector<W2Param> w2_params() {
    std::vector<W2Param> p;
    for (Shape s : {Shape::T, Shape::A}) {
        for (const auto& c : kCells) {
            p.push_back(W2Param{.cell = c, .shape = s});
        }
    }
    return p;
}

INSTANTIATE_TEST_SUITE_P(W2, W2VersionFields, testing::ValuesIn(w2_params()),
                         [](const testing::TestParamInfo<W2Param>& i) {
                             return std::string{i.param.cell.name} + "_" +
                                    shape_name(i.param.shape);
                         });

// ── The spurious-hit arm: a route that always accepts cannot pass ───────────────────────
//
// A frame on a non-own slot, rejected for a field only S's table has there.

TEST(W2SpuriousHit, T_FieldOnlyFixt11Has_RejectedOnTheRoute) {
    const CellRun run = run_own_cell(
        "W-2 spurious T", all_versions(Shape::T), /*validate=*/true, Arm::Active, [](const Rig& r) {
            return frame_35("D", r.peer_ctx(2), kNos() + "1137=9\x01", "1128=9\x01");
        });
    expect_ending(run, "R 2/1137 D 45=2 | stays | c | app=0 adm=0");
}

TEST(W2SpuriousHit, A_FieldOnlyFix50sp2Has_RejectedOnTheFix44Route) {
    const VersionField f = fix50sp2_only();
    ASSERT_NE(f.tag, 0) << "the recomputed difference is empty";
    const CellRun run =
        run_own_cell("W-2 spurious A", all_versions(Shape::A), /*validate=*/true, Arm::Active,
                     [&](const Rig& r) { return frame_of(r, f, "1128=6\x01"); });
    expect_ending(run, reject_on_field(f));
}

}  // namespace
}  // namespace fixpp::session::test099
