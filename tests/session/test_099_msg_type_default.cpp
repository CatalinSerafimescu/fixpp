// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_msg_type_default.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-25 (tasks.md T018, T069, T072, T079):
// data-model E-10's `MT-a` to `MT-p`, every cell on T, A and L, acceptor and initiator, validating
// and not, and the row's named cells.
//
// An input the parent repository's research/probes/099_fixt_session_today/gate_a_l2r2/mtz2.log
// keys carries that log's id and frame, built by its `E` helper's field order, and a run under
// FIXPP_099_TRACE=<file> appends the Logon's ending in the log's form and key. So what the gate
// answers can be compared with that log by key; tasks.md T069 runs the comparison.
//
// The cap in this file is spelled from E-10, not read from the session, because a witness that read
// the constant it checks could not fail when the constant moved.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/reify.hpp>
#include <fixpp/session/session_event.hpp>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mock_reconnect_factory.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

using Version = dd::application_version;

// E-10's "at most 256".
constexpr int kCapFromE10 = 256;

// One entry, as mtz2's harness spells it: `-` leaves the member out.
std::string E(const std::string& mt, const std::string& v, const std::string& dir = "S",
              const std::string& ind = "Y") {
    std::string x = "372=" + mt + "\x01";
    if (dir != "-") {
        x += "385=" + dir + "\x01";
    }
    if (v != "-") {
        x += "1130=" + v + "\x01";
    }
    if (ind != "-") {
        x += "1410=" + ind + "\x01";
    }
    return x;
}

std::string count(const std::string& n) { return "384=" + n + "\x01"; }

const std::string& kOneEntryD10() {
    static const std::string value = count("1") + E("D", "10");
    return value;
}

// `384=<n>` and n entries, `U0`, `U1`, ...
std::string distinct_group(int n) {
    std::string g = count(std::to_string(n));
    for (int i = 0; i < n; ++i) {
        g += E("U" + std::to_string(i), "10");
    }
    return g;
}

std::string repeated_d_group(int n) {
    std::string g = count(std::to_string(n));
    for (int i = 0; i < n; ++i) {
        g += E("D", "10");
    }
    return g;
}

std::string advert_group() {
    std::string g = count("312");
    for (int i = 0; i < 156; ++i) {
        g += "372=D\x01"
             "385=S\x01"
             "372=8\x01"
             "385=R\x01";
    }
    return g;
}

using Kept = std::vector<std::pair<std::string, Version>>;

Kept distinct_kept(int n) {
    Kept k;
    for (int i = 0; i < n; ++i) {
        k.emplace_back("U" + std::to_string(i), Version::vlatest);
    }
    return k;
}

constexpr const char* kActive = "Active";
constexpr const char* kNone = "-";

// E-10's literals. 1130's Reject consumes on the block's path and keeps on the gate's.
const std::string& kBad1130Block() {
    static const std::string value = "R 5/1130 A 45=1 | Disc | c | app=0 adm=0";
    return value;
}
const std::string& kBad1130Gate() {
    static const std::string value = "R 5/1130 A 45=1 | Disc | k | app=0 adm=0";
    return value;
}
const std::string& kCap() {
    static const std::string value = "R 5/384 A 45=1 | Disc | c | app=0 adm=0";
    return value;
}
const std::string& kLong372Block() {
    static const std::string value = "R 5/372 A 45=1 | Disc | c | app=0 adm=0";
    return value;
}
const std::string& kLong372Gate() {
    static const std::string value = "R 5/372 A 45=1 | Disc | k | app=0 adm=0";
    return value;
}
const std::string& kNo385() {
    static const std::string value = "R 1/385 A 45=1 | Disc | c | app=0 adm=0";
    return value;
}
const std::string& kCountWrong() {
    static const std::string value = "R 1/372 A 45=1 | Disc | k | app=0 adm=0";
    return value;
}

std::string stays(const std::string& reason_tag) {
    return "R " + reason_tag + " A 45=1 | stays | k | app=0 adm=0";
}

struct MtRow {
    std::string cell;   // E-10's id, what W25Walk compares
    std::string id;     // mtz2.log's input id where it has one
    std::string group;  // what follows `1137=9` on the Logon
    std::string ta;     // the validating ending on T and A
    std::string l;      // on L, whose ApplVerID and MsgType code sets are closed; kNone: as T, A
    Kept kept;          // what the record site keeps on a Logon that reaches it
};

const std::vector<MtRow>& rows() {
    static const std::vector<MtRow> all = [] {
        const Kept d10{{"D", Version::vlatest}};
        const Kept d9{{"D", Version::v50sp2}};
        const Kept none{};
        std::vector<MtRow> v;
        v.push_back({.cell = "MT-a",
                     .id = "MT-a",
                     .group = kOneEntryD10(),
                     .ta = kActive,
                     .l = kNone,
                     .kept = d10});
        v.push_back({.cell = "MT-b",
                     .id = "MT-b 99",
                     .group = count("1") + E("D", "99"),
                     .ta = kBad1130Block(),
                     .l = kBad1130Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-b",
                     .id = "MT-b abc",
                     .group = count("1") + E("D", "abc"),
                     .ta = kBad1130Block(),
                     .l = kBad1130Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-c",
                     .id = "MT-c 4",
                     .group = count("1") + E("D", "4"),
                     .ta = kBad1130Block(),
                     .l = kNone,
                     .kept = Kept{{"D", Version::v42}}});
        v.push_back({.cell = "MT-c",
                     .id = "MT-c empty",
                     .group = count("1") + E("D", ""),
                     .ta = kBad1130Block(),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-d",
                     .id = "MT-d no 1130",
                     .group = count("1") + E("D", "-"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-e",
                     .id = "MT-e 257xD",
                     .group = repeated_d_group(257),
                     .ta = kCap(),
                     .l = kNone,
                     .kept = d10});
        v.push_back({.cell = "MT-e",
                     .id = "MT-e 257 distinct",
                     .group = distinct_group(257),
                     .ta = kCap(),
                     .l = kLong372Gate(),
                     .kept = distinct_kept(kCapFromE10)});
        v.push_back({.cell = "MT-e",
                     .id = "256 distinct",
                     .group = distinct_group(kCapFromE10),
                     .ta = kActive,
                     .l = kLong372Gate(),
                     .kept = distinct_kept(kCapFromE10)});
        v.push_back({.cell = "MT-e",
                     .id = "256xD",
                     .group = repeated_d_group(kCapFromE10),
                     .ta = kActive,
                     .l = kNone,
                     .kept = d10});
        v.push_back({.cell = "MT-f",
                     .id = "MT-f nine",
                     .group = count("1") + E("ABCDEFGHI", "10"),
                     .ta = kLong372Block(),
                     .l = kLong372Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-f",
                     .id = "eight bytes",
                     .group = count("1") + E("ABCDEFGH", "10"),
                     .ta = kActive,
                     .l = kLong372Gate(),
                     .kept = Kept{{"ABCDEFGH", Version::vlatest}}});
        v.push_back({.cell = "MT-g",
                     .id = "MT-g 372=0",
                     .group = count("1") + E("0", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-g",
                     .id = "372=0 1130=4",
                     .group = count("1") + E("0", "4"),
                     .ta = kBad1130Block(),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-g",
                     .id = "372=0 1130=99",
                     .group = count("1") + E("0", "99"),
                     .ta = kBad1130Block(),
                     .l = kBad1130Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-h",
                     .id = "MT-h two D",
                     .group = count("2") + E("D", "9") + E("D", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = d10});
        v.push_back({.cell = "MT-h",
                     .id = "MT-h two D later 9",
                     .group = count("2") + E("D", "10") + E("D", "9"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = d9});
        v.push_back({.cell = "MT-i",
                     .id = "MT-i R 99",
                     .group = count("1") + E("D", "99", "R"),
                     .ta = kBad1130Block(),
                     .l = kBad1130Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-i",
                     .id = "MT-i 1410=N 99",
                     .group = count("1") + E("D", "99", "S", "N"),
                     .ta = kBad1130Block(),
                     .l = kBad1130Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-i",
                     .id = "MT-i no1410 99",
                     .group = count("1") + E("D", "99", "S", "-"),
                     .ta = kBad1130Block(),
                     .l = kBad1130Gate(),
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j R 10",
                     .group = count("1") + E("D", "10", "R"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j R 4",
                     .group = count("1") + E("D", "4", "R"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j R empty",
                     .group = count("1") + E("D", "", "R"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j N 10",
                     .group = count("1") + E("D", "10", "S", "N"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j N 4",
                     .group = count("1") + E("D", "4", "S", "N"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j N empty",
                     .group = count("1") + E("D", "", "S", "N"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j no1410 10",
                     .group = count("1") + E("D", "10", "S", "-"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j no1410 4",
                     .group = count("1") + E("D", "4", "S", "-"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-j",
                     .id = "MT-j no1410 empty",
                     .group = count("1") + E("D", "", "S", "-"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-k",
                     .id = "MT-k no 385",
                     .group = count("1") + E("D", "10", "-"),
                     .ta = kNo385(),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-l",
                     .id = "MT-l 384=0",
                     .group = count("0") + E("D", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-l",
                     .id = "MT-l 384=-1",
                     .group = count("-1") + E("D", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-l",
                     .id = "MT-l no 384",
                     .group = E("D", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-l",
                     .id = "two blocks 384=0 then 384=1",
                     .group = count("0") + "553=u\x01" + count("1") + E("D", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-m",
                     .id = "MT-m",
                     .group = count("1") + E("D", "9") + "553=u\x01" + E("D", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = d9});
        // Past a non-member the scan stops whatever the count still allows.
        v.push_back({.cell = "MT-m",
                     .id = "MT-m count 2",
                     .group = count("2") + E("D", "9") + "553=u\x01" + E("D", "10"),
                     .ta = kCountWrong(),
                     .l = kNone,
                     .kept = d9});
        v.push_back({.cell = "MT-n",
                     .id = "MT-n 2 with 1",
                     .group = count("2") + E("D", "10"),
                     .ta = kCountWrong(),
                     .l = kNone,
                     .kept = d10});
        v.push_back({.cell = "MT-n",
                     .id = "MT-n 1 with 2",
                     .group = count("1") + E("D", "9") + E("D", "10"),
                     .ta = kCountWrong(),
                     .l = kNone,
                     .kept = d9});
        v.push_back({.cell = "MT-n",
                     .id = "385 first",
                     .group = count("1") + "385=S\x01"
                                           "372=D\x01"
                                           "1130=10\x01"
                                           "1410=Y\x01",
                     .ta = kCountWrong(),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-o",
                     .id = "MT-o advert 312",
                     .group = advert_group(),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-o",
                     .id = "advert 070",
                     .group = count("2") + "372=D\x01"
                                           "385=S\x01"
                                           "372=8\x01"
                                           "385=R\x01",
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-p",
                     .id = "384=abc",
                     .group = count("abc") + E("D", "10"),
                     .ta = stays("5/384"),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-p",
                     .id = "384= empty",
                     .group = count("") + E("D", "10"),
                     .ta = stays("5/384"),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-p",
                     .id = "384=1.5",
                     .group = count("1.5") + E("D", "10"),
                     .ta = stays("5/384"),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-p",
                     .id = "385=X",
                     .group = count("1") + E("D", "10", "X"),
                     .ta = stays("5/385"),
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "MT-p",
                     .id = "1410=X",
                     .group = count("1") + E("D", "10", "S", "X"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        // Not cells of E-10's table: mtz2 keys them, and each follows E-10's rules.
        v.push_back({.cell = "A 372 L closes",
                     .id = "372=U1 counted",
                     .group = count("1") + E("U1", "10"),
                     .ta = kActive,
                     .l = kLong372Gate(),
                     .kept = Kept{{"U1", Version::vlatest}}});
        v.push_back({.cell = "A 372 L closes",
                     .id = "372=U1 advert",
                     .group = count("1") + "372=U1\x01"
                                           "385=S\x01",
                     .ta = kActive,
                     .l = kLong372Gate(),
                     .kept = none});
        v.push_back({.cell = "Other member",
                     .id = "1406=abc",
                     .group = count("1") + E("D", "10") + "1406=abc\x01",
                     .ta = stays("5/1406"),
                     .l = kNone,
                     .kept = d10});
        // W-25's "Within an instance": a member repeated reads first-wins.
        v.push_back({.cell = "Within an instance",
                     .id = "first 1130 answers",
                     .group = count("1") + "372=D\x01"
                                           "385=S\x01"
                                           "1130=10\x01"
                                           "1130=9\x01"
                                           "1410=Y\x01",
                     .ta = kActive,
                     .l = kNone,
                     .kept = d10});
        v.push_back({.cell = "Within an instance",
                     .id = "372= empty",
                     .group = count("1") + E("", "10"),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        v.push_back({.cell = "Within an instance",
                     .id = "1410= empty",
                     .group = count("1") + E("D", "10", "S", ""),
                     .ta = kActive,
                     .l = kNone,
                     .kept = none});
        // mtz2.log's line for an empty 385, on both shapes: the gate names it, so on a validating
        // session the scan does not read it.
        v.push_back({.cell = "Within an instance",
                     .id = "385= empty",
                     .group = count("1") + E("D", "10", ""),
                     .ta = stays("5/385"),
                     .l = kNone,
                     .kept = none});
        return v;
    }();
    return all;
}

struct MtCell {
    const MtRow* row;
    Shape shape;
    bool acceptor;
    bool validate;
};

std::vector<MtCell> all_cells() {
    std::vector<MtCell> cells;
    for (const MtRow& r : rows()) {
        for (const Shape s : {Shape::T, Shape::A, Shape::L}) {
            for (const bool acceptor : {true, false}) {
                for (const bool validate : {true, false}) {
                    cells.push_back(
                        MtCell{.row = &r, .shape = s, .acceptor = acceptor, .validate = validate});
                }
            }
        }
    }
    return cells;
}

Arm arm_of(bool acceptor) { return acceptor ? Arm::NotConnected : Arm::LogonSent; }

std::string established(bool acceptor) {
    return std::string(acceptor ? "Logon" : "-") + " | Active | c | app=0 adm=0";
}

std::string validating_literal(const MtCell& c) {
    const std::string& e = c.shape == Shape::L && c.row->l != kNone ? c.row->l : c.row->ta;
    return e == kActive ? established(c.acceptor) : e;
}

CellRun open_cell(Fixture fixture, bool acceptor, bool validate, RigOptions o = {}) {
    o.validate = validate;
    o.role = acceptor ? session_role::acceptor : session_role::initiator;
    return enter_arm(std::move(fixture), arm_of(acceptor), std::move(o));
}

// `1137=9`, then the group. 1137 answers a frame no entry covers.
std::string reply(const Rig& r, const std::string& group) {
    return logon_frame(r.peer_ctx(1), "1137=9\x01" + group);
}

// A NewOrderSingle with no 1128, carrying the field only FIX Latest's table declares there.
std::string nos_3115(const Rig& r, unsigned seq) {
    return frame_35("D", r.peer_ctx(seq), kNos() + "3115=1.5\x01");
}

constexpr const char* kDelivered = "- | stays | c | app=1 adm=0";
constexpr const char* kRefused3115 = "R 2/3115 D 45=2 | stays | c | app=0 adm=0";

// D's entry among `kept`, or 1137's version, which every cell here negotiates.
Version answering_version(const Kept& kept) {
    for (const auto& [mt, version] : kept) {
        if (mt == "D") {
            return version;
        }
    }
    return Version::v50sp2;
}

// A profile that is not the frame's: `reify` answers by the byte whatever it is passed.
const dd::version_profile kOtherProfile{.session = dd::session_version::v42,
                                        .default_appl = Version::v42,
                                        .has_per_message_override = false,
                                        ._reserved = 0};

// What one frame's view said.
struct ViewSeen {
    int calls = 0;
    Version accessor = Version::Unknown;
    std::optional<dd::resolved_message_version> reified;
};

void watch_view(Rig& r, ViewSeen& seen) {
    r.app->on_app = [&seen](const CountingApp::View& v) {
        ++seen.calls;
        seen.accessor = v.resolved_application_version();
        if (auto h = dd::reify(v, kOtherProfile, std::pmr::new_delete_resource())) {
            seen.reified = h->version();
        }
    };
}

// The accessor, and `reify` agreeing with it in all three bytes.
void expect_answered(const ViewSeen& seen, Version version) {
    EXPECT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.accessor, version);
    ASSERT_TRUE(seen.reified.has_value()) << "reify returned no handle";
    EXPECT_EQ(seen.reified->k, dd::resolved_message_version::kind::application);
    EXPECT_EQ(seen.reified->session, dd::session_version::vt11);
    EXPECT_EQ(seen.reified->application, version);
}

Kept sorted(Kept k) {
    std::ranges::sort(k);
    return k;
}

// ── Every cell of E-10's table ──────────────────────────────────────────────────────────

class W25Endings : public testing::TestWithParam<MtCell> {};

TEST_P(W25Endings, Logon) {
    const MtCell& c = GetParam();
    CellRun run = open_cell(shape_fixture(c.shape), c.acceptor, c.validate);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, c.row->group));
    append_trace(std::string(shape_name(c.shape)) + "\t" + (c.validate ? "on" : "off") + "\t" +
                 arm_name(arm_of(c.acceptor)) + "\t" + c.row->id + "\t" + log_form(run.ending));
    expect_ending(run, c.validate ? validating_literal(c) : established(c.acceptor));
    if (r.sess->state() != fsm_state::Active) {
        return;
    }
    EXPECT_EQ(session_test_access::msg_type_defaults(*r.sess), sorted(c.row->kept))
        << "what the record site keeps, sorted by MsgType";
    // The frame the entry would answer: a validating session delivers 3115 on FIX Latest's table
    // alone, so the version answers the ending.
    const Version answer = answering_version(c.row->kept);
    ViewSeen seen;
    watch_view(r, seen);
    run.ending = r.deliver(nos_3115(r, 2));
    const bool delivered = !c.validate || answer == Version::vlatest;
    expect_ending(run, delivered ? kDelivered : kRefused3115);
    if (delivered) {
        expect_answered(seen, answer);
    }
}

INSTANTIATE_TEST_SUITE_P(W25, W25Endings, testing::ValuesIn(all_cells()),
                         [](const testing::TestParamInfo<MtCell>& i) {
                             return name_token(i.param.row->cell) + "_" +
                                    shape_name(i.param.shape) + "_" +
                                    (i.param.acceptor ? "acceptor" : "initiator") + "_" +
                                    (i.param.validate ? "on" : "off") + "_" +
                                    name_token(i.param.row->id);
                         });

// The ids walked equal E-10's, spelled here and not read from the table.
TEST(W25Walk, IdsWalkedEqualE10sCells) {
    const std::set<std::string> expected{"MT-a", "MT-b", "MT-c", "MT-d", "MT-e", "MT-f",
                                         "MT-g", "MT-h", "MT-i", "MT-j", "MT-k", "MT-l",
                                         "MT-m", "MT-n", "MT-o", "MT-p"};
    std::set<std::string> walked;
    for (const MtRow& r : rows()) {
        if (r.cell.starts_with("MT-")) {
            walked.insert(r.cell);
        }
    }
    EXPECT_EQ(walked, expected);
}

// ── Named cells, on each shape and role ─────────────────────────────────────────────────

struct ShapeRole {
    Shape shape;
    bool acceptor;
};

std::string shape_role_name(const testing::TestParamInfo<ShapeRole>& i) {
    return std::string(shape_name(i.param.shape)) + (i.param.acceptor ? "_acceptor" : "_initiator");
}

const ShapeRole kShapeRoles[] = {
    {.shape = Shape::T, .acceptor = true}, {.shape = Shape::T, .acceptor = false},
    {.shape = Shape::A, .acceptor = true}, {.shape = Shape::A, .acceptor = false},
    {.shape = Shape::L, .acceptor = true}, {.shape = Shape::L, .acceptor = false}};

class W25AfterMtA : public testing::TestWithParam<ShapeRole> {
protected:
    // Validating, reached Active on MT-a's entry.
    CellRun run_;
    void SetUp() override {
        run_ = open_cell(shape_fixture(GetParam().shape), GetParam().acceptor, true);
        ASSERT_TRUE(run_.setup_failure.empty()) << run_.setup_failure;
        (void)run_.rig->deliver(reply(*run_.rig, kOneEntryD10()));
        ASSERT_EQ(run_.rig->sess->state(), fsm_state::Active);
    }
};

INSTANTIATE_TEST_SUITE_P(W25, W25AfterMtA, testing::ValuesIn(kShapeRoles), shape_role_name);

// Explicit has precedence over the default.
TEST_P(W25AfterMtA, Explicit1128_9_Answers) {
    Rig& r = *run_.rig;
    run_.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos() + "3115=1.5\x01", "1128=9\x01"));
    expect_ending(run_, kRefused3115);
}

// FIX 5.0 SP2's OrderCancelRequest, whose required fields this frame carries.
const std::string& kCancelBody() {
    static const std::string value =
        "41=O\x01"
        "11=C\x01"
        "55=X\x01"
        "54=1\x01"
        "60=20240101-00:00:00\x01";
    return value;
}

// Another MsgType has no entry: 1137's slot takes it and FIX Latest's does not.
TEST_P(W25AfterMtA, AnotherMsgType_1137sSlotAnswers) {
    Rig& r = *run_.rig;
    ViewSeen seen;
    watch_view(r, seen);
    run_.ending = r.deliver(frame_35("F", r.peer_ctx(2), kCancelBody()));
    expect_ending(run_, kDelivered);
    expect_answered(seen, Version::v50sp2);
}

TEST_P(W25AfterMtA, TheEntrysMsgType_FixLatestAnswers) {
    Rig& r = *run_.rig;
    ViewSeen seen;
    watch_view(r, seen);
    run_.ending = r.deliver(nos_3115(r, 2));
    expect_ending(run_, kDelivered);
    expect_answered(seen, Version::vlatest);
}

TEST_P(W25AfterMtA, AnotherMsgType_FieldOnlyLatestDeclaresIsRefused) {
    const std::uint16_t tag = only_field(*latest(), "F", {fix50sp2().get()});
    ASSERT_NE(tag, 0U) << "a field only FIX Latest's OrderCancelRequest declares, recomputed";
    const auto f = latest()->field("F", tag);
    ASSERT_TRUE(f.has_value());
    Rig& r = *run_.rig;
    const std::string field = std::to_string(tag) + "=" + plain_value(*latest(), *f) + "\x01";
    run_.ending = r.deliver(frame_35("F", r.peer_ctx(2), kCancelBody() + field));
    expect_ending(run_, "R 2/" + std::to_string(tag) + " F 45=2 | stays | c | app=0 adm=0");
}

class W25Named : public testing::TestWithParam<ShapeRole> {};
INSTANTIATE_TEST_SUITE_P(W25, W25Named, testing::ValuesIn(kShapeRoles), shape_role_name);

// E-10: "with two, either Reject may answer". T and A reach the block, L's gate names 1130 first.
TEST_P(W25Named, BothFaults_No385And1130_99_EitherAnswers) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(shape_fixture(p.shape), p.acceptor, true);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, count("1") + E("D", "99", "-")));
    append_trace(std::string(shape_name(p.shape)) + "\ton\t" + arm_name(arm_of(p.acceptor)) +
                 "\tboth faults: no 385, 1130=99\t" + log_form(run.ending));
    const std::string got = short_form(run.ending);
    if (p.shape == Shape::L) {
        EXPECT_EQ(got, kBad1130Gate());
    } else {
        EXPECT_TRUE(got == kNo385() || got == kBad1130Block()) << got;
    }
}

TEST_P(W25Named, BothFaults_NotValidating_Establishes) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(shape_fixture(p.shape), p.acceptor, false);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, count("1") + E("D", "99", "-")));
    append_trace(std::string(shape_name(p.shape)) + "\toff\t" + arm_name(arm_of(p.acceptor)) +
                 "\tboth faults: no 385, 1130=99\t" + log_form(run.ending));
    expect_ending(run, established(p.acceptor));
}

// ── Named cells, on each shape: the initiator's ─────────────────────────────────────────

class W25Initiator : public testing::TestWithParam<Shape> {};
INSTANTIATE_TEST_SUITE_P(W25, W25Initiator, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

std::string bad_1130_ending(Shape s) { return s == Shape::L ? kBad1130Gate() : kBad1130Block(); }

// `141=Y` with a bad 1130: refused ahead of the reset unit, as W-5's cell with a bad 1137 is. The
// Reject takes the number after open()'s Logon, and only that unit raises the event.
TEST_P(W25Initiator, Reply141YWithBad1130_NoReset) {
    CellRun run = open_cell(shape_fixture(GetParam()), false, true);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(logon_frame(r.peer_ctx(1),
                                       "141=Y\x01"
                                       "1137=9\x01" +
                                           count("1") + E("D", "99")));
    expect_ending(run, bad_1130_ending(GetParam()));
    EXPECT_EQ(session_test_access::seqnum_mgr(*r.sess).peek_outbound(), 3U);
    for (auto const& ev : r.sess->recent_events()) {
        EXPECT_FALSE(std::holds_alternative<session_event_sequence_numbers_reset>(ev))
            << "the reset unit ran on a refused Logon";
    }
}

// `LS-h`'s shape: a direct-constructed initiator, an off-list identity. S3's block stands ahead of
// mTLS on this arm, so the entry answers and no authorization runs.
TEST_P(W25Initiator, OffListIdentity_MtB_NoAuth) {
    RigOptions o;
    o.mtls = true;
    CellRun run = open_cell(shape_fixture(GetParam()), false, true, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, count("1") + E("D", "99")));
    expect_ending(run, bad_1130_ending(GetParam()));
}

std::function<void(SessionConfig&)> with_reconnect() {
    return [](SessionConfig& cfg) {
        cfg.transport_factory_override =
            std::make_shared<fixpp::test_support::MockReconnectFactory>();
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    };
}

std::optional<std::string> reconnect(Rig& r) {
    auto fut = asio::co_spawn(r.ioc, r.sess->drive_reconnect(), asio::use_future);
    if (!pump_ready(r.ioc, fut, std::chrono::seconds{30})) {
        return std::string("RECONNECT-PUMP-MISS");
    }
    if (auto rr = fut.get(); !rr) {
        return "RECONNECT-REFUSED err=" + std::to_string(static_cast<int>(rr.error()));
    }
    for (int i = 0; i < 4; ++i) {
        r.ioc.restart();
        r.ioc.poll();
    }
    if (r.sess->state() != fsm_state::LogonSent) {
        return std::string("RECONNECT-STATE ") + state_name(r.sess->state());
    }
    return std::nullopt;
}

// The reply on this connection, then a NOS with 3115 at the number after it. What that frame did,
// as `<n> delivered, <c or k>`. A reconnected session writes through its mock transport, not the
// rig's capture, so a Reject is read by what it leaves: not delivered, the number consumed.
std::string reply_then_nos(Rig& r, const std::string& group) {
    (void)r.deliver(logon_frame(r.peer_ctx(r.next_in()), "1137=9\x01" + group));
    EXPECT_EQ(r.sess->state(), fsm_state::Active) << "the reply must establish";
    const int app_before = r.app->app;
    const Ending e = r.deliver(frame_35("D", r.peer_ctx(r.next_in()), kNos() + "3115=1.5\x01"));
    return std::to_string(e.from_app - app_before) + " delivered, " +
           (e.next_in_after != e.next_in_before ? "c" : "k");
}

const std::string& kDefaultAnswered() {
    static const std::string value = "1 delivered, c";
    return value;
}
const std::string& kOtherTableRefused() {
    static const std::string value = "0 delivered, c";
    return value;
}

// Reconnect whose Logon declares none: 1137 answers, the previous connection's entry gone.
TEST_P(W25Initiator, ReconnectDeclaringNone_1137Answers) {
    RigOptions o;
    o.tweak = with_reconnect();
    CellRun run = open_cell(shape_fixture(GetParam()), false, true, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    EXPECT_EQ(reply_then_nos(r, kOneEntryD10()), kDefaultAnswered());
    if (auto f = reconnect(r)) {
        FAIL() << *f;
    }
    EXPECT_EQ(reply_then_nos(r, ""), kOtherTableRefused());
}

// A reply the block refuses stores nothing, and the next Logon clears what was there.
TEST_P(W25Initiator, RefusedThenValid) {
    RigOptions o;
    o.tweak = with_reconnect();
    CellRun run = open_cell(shape_fixture(GetParam()), false, true, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, count("2") + E("D", "10") + E("D", "99")));
    expect_ending(run, bad_1130_ending(GetParam()));
    if (auto f = reconnect(r)) {
        FAIL() << *f;
    }
    EXPECT_EQ(reply_then_nos(r, ""), kOtherTableRefused());
    if (auto f = reconnect(r)) {
        FAIL() << *f;
    }
    EXPECT_EQ(reply_then_nos(r, kOneEntryD10()), kDefaultAnswered());
}

// ── A body pair's Data: the scan uses the transport route's hooks ───────────────────────

const DictPtr& fixt11_with_logon_pair() {
    static const DictPtr d =
        overlay_on("FIXT11.xml",
                   "<fix type='FIXT' major='1' minor='1' servicepack='0'><messages>"
                   "<message name='Logon' msgtype='A' msgcat='admin'>" +
                       kPairMembers() + "</message></messages>" + kPairFields() + "</fix>");
    return d;
}

const DictPtr& latest_with_logon_pair() {
    static const DictPtr d = orchestra_overlay_on_latest(
        "<fixr:repository xmlns:fixr=\"http://fixprotocol.io/2020/orchestra/repository\" "
        "name=\"FIX.Latest\" version=\"FIX.Latest_EP303\"><fixr:fields>"
        "<fixr:field id=\"5030\" name=\"VenuePairLen\" type=\"Length\" />"
        "<fixr:field id=\"5031\" name=\"VenuePairData\" type=\"data\" lengthId=\"5030\" />"
        "</fixr:fields><fixr:messages>"
        "<fixr:message id=\"11\" name=\"Logon\" msgType=\"A\"><fixr:structure>"
        "<fixr:fieldRef id=\"5030\" /><fixr:fieldRef id=\"5031\" /></fixr:structure></fixr:message>"
        "</fixr:messages></fixr:repository>");
    return d;
}

// The shape, with a transport dictionary whose Logon declares the pair. `dict_hooks::none()`
// answers the standard table alone, so only such a pair tells the two walks apart.
Fixture transport_pair_fixture(Shape s) {
    Fixture f;
    switch (s) {
        case Shape::T:
            f.registry = make_registry({fixt11_with_logon_pair(), fix50sp2(), latest()});
            f.session_dict = fixt11_with_logon_pair();
            break;
        case Shape::A:
            f.registry = make_registry({fixt11_with_logon_pair(), fix50sp2(), latest()});
            f.session_dict = fix50sp2();
            break;
        case Shape::L:
            f.registry = make_registry({fix50sp2(), latest_with_logon_pair()});
            f.session_dict = latest_with_logon_pair();
            break;
    }
    return f;
}

class W25DataCell : public testing::TestWithParam<ShapeRole> {};
INSTANTIATE_TEST_SUITE_P(W25, W25DataCell, testing::ValuesIn(kShapeRoles), shape_role_name);

// Data embedding a whole counted entry with `1130=10`: read, it would be a default for D.
TEST_P(W25DataCell, EmbeddedEntry_NoDefaultAppears) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(transport_pair_fixture(p.shape), p.acceptor, true);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, pair_field("ab\x01"
                                               "384=1\x01"
                                               "372=D\x01"
                                               "1130=10\x01"
                                               "385=S\x01"
                                               "1410=Y")));
    expect_ending(run, established(p.acceptor));
    run.ending = r.deliver(nos_3115(r, 2));
    expect_ending(run, kRefused3115);
}

// Data embedding `SOH 372=D SOH 1130=4`: read, a counted entry with no dictionary would refuse it.
TEST_P(W25DataCell, EmbeddedUnserviceable_NotRefused) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(transport_pair_fixture(p.shape), p.acceptor, true);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    run.ending = r.deliver(reply(r, pair_field("ab\x01"
                                               "384=1\x01"
                                               "372=D\x01"
                                               "1130=4\x01"
                                               "385=S\x01"
                                               "1410=Y")));
    expect_ending(run, established(p.acceptor));
}

// ── `MT-f`: E-10's `**The eight-byte boundary**` ────────────────────────────────────────

// An eight-byte MsgType FIX Latest does not declare: no delivery, on T and A, and the Reject's tag
// differs by shape, so it is not asserted.
class W25EightBytes : public testing::TestWithParam<ShapeRole> {};
const ShapeRole kTA[] = {{.shape = Shape::T, .acceptor = true},
                         {.shape = Shape::T, .acceptor = false},
                         {.shape = Shape::A, .acceptor = true},
                         {.shape = Shape::A, .acceptor = false}};
INSTANTIATE_TEST_SUITE_P(W25, W25EightBytes, testing::ValuesIn(kTA), shape_role_name);

TEST_P(W25EightBytes, UndeclaredOnFixLatest_NotDelivered) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(shape_fixture(p.shape), p.acceptor, true);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(reply(r, count("1") + E("ABCDEFGH", "10")));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    run.ending = r.deliver(frame_35("ABCDEFGH", r.peer_ctx(2), "58=x\x01"));
    EXPECT_EQ(run.ending.from_app, 0) << log_form(run.ending);
}

// FIX44, registered for the default's slot, with two eight-byte MsgTypes built from strings, each
// with member Text and both added to field 35, which FIX44 closes. The sibling is declared so that
// a lookup taking it for `ABCDEFGH` delivers it.
const DictPtr& fix44_with_eight_byte_types() {
    static const DictPtr d =
        overlay_on("FIX44.xml",
                   "<fix type='FIX' major='4' minor='4' servicepack='0'><messages>"
                   "<message name='VenueEightH' msgtype='ABCDEFGH' msgcat='app'>"
                   "<field name='Text' required='N' /></message>"
                   "<message name='VenueEightX' msgtype='ABCDEFGX' msgcat='app'>"
                   "<field name='Text' required='N' /></message>"
                   "</messages><fields><field number='35' name='MsgType' type='STRING'>"
                   "<value enum='ABCDEFGH' description='VENUE_EIGHT_H' />"
                   "<value enum='ABCDEFGX' description='VENUE_EIGHT_X' />"
                   "</field></fields></fix>");
    return d;
}

Fixture eight_byte_fixture(Shape s) {
    return own_fixture(s, {fixt11(), fix50sp2(), fix44_with_eight_byte_types()});
}

const std::string& kEightByteEntry() {
    static const std::string value = count("1") + E("ABCDEFGH", "6");
    return value;
}

// The gate's answer: 373 and 371, state and consume. A Reject's 372 is not asserted (E-10).
void expect_refused_on_35(const Ending& e) {
    ASSERT_EQ(e.out.size(), 1U) << log_form(e);
    EXPECT_EQ(tag_value(e.out[0], 35), "3");
    EXPECT_EQ(tag_value(e.out[0], 373), "5") << log_form(e);
    EXPECT_EQ(tag_value(e.out[0], 371), "35") << log_form(e);
    EXPECT_EQ(e.after, fsm_state::Active);
    EXPECT_EQ(e.next_in_after, e.next_in_before + 1) << log_form(e);
    EXPECT_EQ(e.from_app, 0);
}

class W25EightBytesDeclared : public testing::TestWithParam<ShapeRole> {
protected:
    CellRun run_;
    void SetUp() override {
        run_ = open_cell(eight_byte_fixture(GetParam().shape), GetParam().acceptor, true);
        ASSERT_TRUE(run_.setup_failure.empty()) << run_.setup_failure;
        (void)run_.rig->deliver(reply(*run_.rig, kEightByteEntry()));
        ASSERT_EQ(run_.rig->sess->state(), fsm_state::Active);
    }
};
INSTANTIATE_TEST_SUITE_P(W25, W25EightBytesDeclared, testing::ValuesIn(kTA), shape_role_name);

TEST_P(W25EightBytesDeclared, NoApplVerId_DeliveredOnTheDefaultsSlot) {
    Rig& r = *run_.rig;
    Version accessor = Version::Unknown;
    r.app->on_app = [&accessor](const CountingApp::View& v) {
        accessor = v.resolved_application_version();
    };
    run_.ending = r.deliver(frame_35("ABCDEFGH", r.peer_ctx(2), "58=x\x01"));
    EXPECT_EQ(run_.ending.from_app, 1) << log_form(run_.ending);
    EXPECT_TRUE(run_.ending.out.empty()) << log_form(run_.ending);
    EXPECT_EQ(accessor, Version::v44);
}

TEST_P(W25EightBytesDeclared, Explicit1128_9_Refused) {
    Rig& r = *run_.rig;
    expect_refused_on_35(r.deliver(frame_35("ABCDEFGH", r.peer_ctx(2), "58=x\x01", "1128=9\x01")));
}

TEST_P(W25EightBytesDeclared, Sibling_1137Answers_Refused) {
    Rig& r = *run_.rig;
    expect_refused_on_35(r.deliver(frame_35("ABCDEFGX", r.peer_ctx(2), "58=x\x01")));
}

// Not validating, a named boundary cell: T, A and L.
class W25EightBytesOff : public testing::TestWithParam<ShapeRole> {};
INSTANTIATE_TEST_SUITE_P(W25, W25EightBytesOff, testing::ValuesIn(kShapeRoles), shape_role_name);

TEST_P(W25EightBytesOff, NoApplVerId_Delivered) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(shape_fixture(p.shape), p.acceptor, false);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(reply(r, count("1") + E("ABCDEFGH", "10")));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    Version accessor = Version::Unknown;
    r.app->on_app = [&accessor](const CountingApp::View& v) {
        accessor = v.resolved_application_version();
    };
    run.ending = r.deliver(frame_35("ABCDEFGH", r.peer_ctx(2)));
    expect_ending(run, kDelivered);
    EXPECT_EQ(accessor, Version::vlatest);
}

// ── Storage: E-10's one block ───────────────────────────────────────────────────────────

class W25Storage : public testing::TestWithParam<ShapeRole> {};
INSTANTIATE_TEST_SUITE_P(W25, W25Storage, testing::ValuesIn(kShapeRoles), shape_role_name);

// Reserved in open(), and no Logon grows it: the same capacity and the same block after a Logon
// holding as many entries as it can keep. L's gate closes the domain to these MsgTypes, so its cell
// does not validate.
TEST_P(W25Storage, ReservedInOpen_AFullLogonDoesNotMoveIt) {
    const ShapeRole p = GetParam();
    CellRun run = open_cell(shape_fixture(p.shape), p.acceptor, /*validate=*/p.shape != Shape::L);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    ASSERT_EQ(session_test_access::msg_type_defaults_capacity(*r.sess),
              static_cast<std::size_t>(kCapFromE10));
    const auto* const block = session_test_access::msg_type_defaults_block(*r.sess);
    (void)r.deliver(reply(r, distinct_group(kCapFromE10)));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    EXPECT_EQ(session_test_access::msg_type_defaults(*r.sess).size(),
              static_cast<std::size_t>(kCapFromE10));
    EXPECT_EQ(session_test_access::msg_type_defaults_capacity(*r.sess),
              static_cast<std::size_t>(kCapFromE10));
    EXPECT_EQ(session_test_access::msg_type_defaults_block(*r.sess), block);
}

TEST(W25StorageNonFixt, ReservesNothing) {
    RigOptions o;
    o.validate = false;
    o.tweak = [](SessionConfig& cfg) {
        cfg.begin_string = "FIX.4.4";
        cfg.default_appl_ver_id.reset();
    };
    Fixture f{.registry = make_registry({fix44()}), .session_dict = fix44()};
    const CellRun run = enter_arm(std::move(f), Arm::NotConnected, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    EXPECT_EQ(session_test_access::msg_type_defaults_capacity(*run.rig->sess), 0U);
}

// ── The dictionary and the cap ──────────────────────────────────────────────────────────

std::string read_text(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// `added` goes between `before` and `after`, which the frozen file holds adjacent, once.
void insert_between(std::string& text, const std::string& before, const std::string& after,
                    const std::string& added) {
    const std::string anchor = before + after;
    const auto at = text.find(anchor);
    ASSERT_NE(at, std::string::npos) << anchor;
    ASSERT_EQ(text.find(anchor, at + 1), std::string::npos) << anchor;
    text.insert(at + before.size(), added);
}

// The vendored FIXT11.xml is the frozen QuickFIX C++ copy plus exactly these lines, at E-10's
// placement: the group last in Logon, 384 and 385 after 383, 1410 directly before 1409.
TEST(W25Dictionary, VendoredFixt11IsTheFrozenCopyPlusTheGroupAndThreeFields) {
    std::string expected = read_text(std::filesystem::path{FIXPP_TEST_SOURCE_DIR} /
                                     "tests/fuzz/corpus/dict_xml_loader/"
                                     "seed_fixt11_real_dictionary.xml");
    ASSERT_FALSE(expected.empty());
    const std::string actual = read_text(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIXT11.xml");
    ASSERT_NE(expected, actual) << "the group is not in the vendored file";
    insert_between(expected, "   <field name='EncodedText' required='N' />\n",
                   "  </message>\n  <message name='XMLnonFIX'",
                   "   <group name='NoMsgTypes' required='N'>\n"
                   "    <field name='RefMsgType' required='N' />\n"
                   "    <field name='MsgDirection' required='N' />\n"
                   "    <field name='RefApplVerID' required='N' />\n"
                   "    <field name='RefApplExtID' required='N' />\n"
                   "    <field name='RefCstmApplVerID' required='N' />\n"
                   "    <field name='DefaultVerIndicator' required='N' />\n"
                   "   </group>\n");
    insert_between(expected, "  <field number='383' name='MaxMessageSize' type='LENGTH' />\n",
                   "  <field number='464'",
                   "  <field number='384' name='NoMsgTypes' type='NUMINGROUP' />\n"
                   "  <field number='385' name='MsgDirection' type='CHAR'>\n"
                   "   <value enum='R' description='RECEIVE' />\n"
                   "   <value enum='S' description='SEND' />\n"
                   "  </field>\n");
    insert_between(expected,
                   "  <field number='1408' name='DefaultCstmApplVerID' type='STRING' />\n",
                   "  <field number='1409'",
                   "  <field number='1410' name='DefaultVerIndicator' type='BOOLEAN' />\n");
    EXPECT_EQ(actual, expected);
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (auto at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

// E-10: kCapFromE10 exceeds every vendored dictionary's message count. Recomputed from the raw XML,
// and the count is shown able to read a real dictionary's.
TEST(W25Dictionary, CapExceedsTheLargestVendoredMessageCount) {
    std::size_t largest = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator{FIXPP_DICT_DATA_DIR}) {
        if (e.path().extension() != ".xml") {
            continue;
        }
        const std::string text = read_text(e.path());
        largest = std::max(largest, count_occurrences(text, "<message ") +
                                        count_occurrences(text, "<fixr:message "));
    }
    const std::string fix50 =
        read_text(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX50SP2.xml");
    EXPECT_EQ(count_occurrences(fix50, "<message "), fix50sp2()->messages().size())
        << "the recount reads what the loader reads";
    EXPECT_GT(largest, 0U);
    EXPECT_LT(largest, static_cast<std::size_t>(kCapFromE10));
}

}  // namespace
}  // namespace fixpp::session::test099
