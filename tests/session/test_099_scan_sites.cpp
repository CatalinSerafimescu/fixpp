// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_scan_sites.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-19 (tasks.md T017): at every scan
// site, on T, A and L, a garbled frame, a malformed tag, a Length/Data mismatch and a frame whose
// third field is not 35 keep today's ending, counters and sequence state. Faults and garbles are
// disposed of before the gate, so each shape is baselined with validation off, ahead of any
// production change of this feature. That is why these literals come from a run, and M-16 is the
// mutant. No input carries a pair only a route's table declares: that is W-6's `off-pair`. Each
// literal is read against 093's dispositions (Session::dispose_unparseable_ and the arms' garble
// branch), so it is a pin of the dispositions, not a design. No mutant of this feature moves these.
//
// M-16's cell is the one below them: validating, a frame the peek reads and refuses whose scan
// faults past the header, so the two arm steps meet on one frame and the fault's comes first. Its
// literal is 092 contract C-2's row D-5, read at Session::dispose_unparseable_: the number
// consumed, then Reject 373=0 with no 371, 45 the first 34, 372 the third field. research R-18's
// logs key no such input.

#include <gtest/gtest.h>

#include <span>
#include <string>

#include "session/peek_fixt_route.hpp"
#include "session/scan_frame_header.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

// No MsgType at all: a frame the peek cannot read.
std::string garbled(const Rig& r, unsigned seq) { return wire(kFT, hdr(r.peer_ctx(seq))); }
std::string malformed_tag(const Rig& r, unsigned seq) {
    return wire(kFT, "35=0\x01" + hdr(r.peer_ctx(seq)) + "3a5=1\x01");
}
std::string length_data_mismatch(const Rig& r, unsigned seq) {
    return wire(kFT, "35=0\x01" + hdr(r.peer_ctx(seq)) +
                         "95=50\x01"
                         "96=x\x01");
}
std::string third_field_not_35(const Rig& r, unsigned seq) {
    return wire(kFT, "34=" + std::to_string(seq) +
                         "\x01"
                         "35=0\x01"
                         "49=" +
                         r.peer_ctx(seq).snd +
                         "\x01"
                         "52=" +
                         kFresh +
                         "\x01"
                         "56=" +
                         r.peer_ctx(seq).tgt + "\x01");
}

struct W19Row {
    const char* name;
    std::string (*make)(const Rig&, unsigned);
    Arm arm;
    const char* expected;  // the ending on every shape, baselined with validation off
};

const W19Row kRows[] = {
    {"garbled", garbled, Arm::NotConnected, "- | stays | k | app=0 adm=0"},
    {"garbled", garbled, Arm::LogonSent, "- | stays | k | app=0 adm=0"},
    {"garbled", garbled, Arm::Active, "- | stays | k | app=0 adm=0"},
    {"garbled", garbled, Arm::LogoutSent, "- | stays | k | app=0 adm=0"},
    {"malformed tag", malformed_tag, Arm::NotConnected, "- | Disc | k | app=0 adm=0"},
    {"malformed tag", malformed_tag, Arm::LogonSent, "- | Disc | k | app=0 adm=0"},
    {"malformed tag", malformed_tag, Arm::Active, "R 0/ 0 45=2 | stays | c | app=0 adm=0"},
    {"malformed tag", malformed_tag, Arm::LogoutSent, "- | stays | k | app=0 adm=0"},
    {"Length/Data mismatch", length_data_mismatch, Arm::NotConnected, "- | Disc | k | app=0 adm=0"},
    {"Length/Data mismatch", length_data_mismatch, Arm::LogonSent, "- | Disc | k | app=0 adm=0"},
    {"Length/Data mismatch", length_data_mismatch, Arm::Active,
     "R 5/95 0 45=2 | stays | c | app=0 adm=0"},
    {"Length/Data mismatch", length_data_mismatch, Arm::LogoutSent, "- | stays | k | app=0 adm=0"},
    {"third field not 35", third_field_not_35, Arm::NotConnected, "- | stays | k | app=0 adm=0"},
    {"third field not 35", third_field_not_35, Arm::LogonSent, "- | stays | k | app=0 adm=0"},
    {"third field not 35", third_field_not_35, Arm::Active, "- | stays | k | app=0 adm=0"},
    {"third field not 35", third_field_not_35, Arm::LogoutSent, "- | stays | k | app=0 adm=0"},
};

struct W19Param {
    W19Row row;
    Shape shape;
};

class W19ScanSites : public testing::TestWithParam<W19Param> {};

TEST_P(W19ScanSites, EndingAsToday) {
    const auto& [row, s] = GetParam();
    const CellRun run = run_own_cell(
        std::string("W-19 ") + row.name + " " + arm_name(row.arm) + " " + shape_name(s),
        shape_fixture(s), /*validate=*/false, row.arm,
        [&](const Rig& r) { return row.make(r, first_seq_of(row.arm)); });
    expect_ending(run, row.expected);
}

std::vector<W19Param> w19_params() {
    std::vector<W19Param> p;
    for (Shape s : {Shape::T, Shape::A, Shape::L}) {
        for (const auto& r : kRows) {
            p.push_back(W19Param{r, s});
        }
    }
    return p;
}

INSTANTIATE_TEST_SUITE_P(W19, W19ScanSites, testing::ValuesIn(w19_params()),
                         [](const testing::TestParamInfo<W19Param>& i) {
                             return name_token(i.param.row.name) + "_" + arm_name(i.param.row.arm) +
                                    "_" + shape_name(i.param.shape);
                         });

class W19FaultPastTheStop : public testing::TestWithParam<Shape> {};

TEST_P(W19FaultPastTheStop, TheFaultsDispositionNotTheRefusal) {
    const Shape s = GetParam();
    CellRun run = enter_arm(shape_fixture(s), Arm::Active, RigOptions{});
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    const std::string frame =
        frame_35("D", run.rig->peer_ctx(2), kNos + "3a5=1\x01", "1128=99\x01");
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(frame.data()),
                                           frame.size()};

    // The premise, on this shape's S: the peek reads the frame, and the scan faults on it.
    const auto& s_table =
        *session_test_access::routes(*run.rig->sess)->find(shape_dict(s).get(), nullptr)->owner;
    const auto hooks = fixpp::wire::dict_hooks::for_table_view(s_table);
    const auto peek = detail::peek_fixt_route(bytes, hooks, s_table);
    EXPECT_TRUE(peek.readable) << "the peek must read this frame, or the cell tests no refusal";
    EXPECT_TRUE(peek.header_1128);
    EXPECT_EQ(peek.appl_ver_id, "99");
    EXPECT_EQ(detail::scan_frame_header(bytes, hooks).fault,
              fixpp::wire::field_fault::malformed_tag);

    run.ending = run.rig->deliver(frame);
    expect_ending(run, "R 0/ D 45=2 | stays | c | app=0 adm=0");
}

INSTANTIATE_TEST_SUITE_P(W19, W19FaultPastTheStop, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

}  // namespace
}  // namespace fixpp::session::test099
