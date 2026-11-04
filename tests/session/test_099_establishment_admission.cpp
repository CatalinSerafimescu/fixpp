// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_establishment_admission.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-9 (tasks.md T016): data-model E-7's
// `NC-a` to `NC-d`, `LS-a` to `LS-d` and `LO-a` to `LO-g`, every input on T, A and L. An
// application or mismatched frame in an establishment arm disconnects, nothing sent (S1);
// LogoutSent drains as at `c0a828ef`, and `D…5` confirms the Logout. `*-admin` is the other admin
// frames the keyed logs hold: T's line on T and A (E-7's "A answers as T"), L's on L.
//
// Each cell's trace line compares with research R-18's `on` logs. The validation-off twin takes
// the `off` logs' lines, which have the same ending but for `D…A` and `0…A` with Logon fields; its
// mutants are contracts/witnesses.md's.

#include <gtest/gtest.h>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

// ── Validating ──────────────────────────────────────────────────────────────────────────

const KeyedRow kNotConnected[] = {
    {"NC-a", "rep35 D..0 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..3 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..4 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..A past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..5 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 5..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 0..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 3..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 4..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 A..0 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..A logon-fields", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 0..A logon-fields", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 0,D header", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D,D header", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..0 past seq+5", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=99 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=abc hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=0 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=1 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=4 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=empty hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-c", "1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"NC-c", "1128=abc past", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS 1128=9", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS 1128=10", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS missing 11", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS missing 11 seq+5", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Heartbeat 1128=99 hdr", "R 5/1128 0 45=1 | stays | k | app=0 adm=0"},
    {"NC-admin", "Heartbeat 1128=10 hdr", "R 5/1128 0 45=1 | stays | k | app=0 adm=0", nullptr,
     "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "TestRequest 1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Heartbeat", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "TestRequest missing 112", "R 1/112 1 45=1 | stays | k | app=0 adm=0"},
    {"NC-admin", "TestRequest", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Logout", "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogonSent[] = {
    {"LS-a", "rep35 D..0 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..3 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..4 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..A past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..5 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 5..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 0..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 3..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 4..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 A..0 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..A logon-fields", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 0..A logon-fields", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 0,D header", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D,D header", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..0 past seq+5", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=99 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=abc hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=0 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=1 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=4 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=empty hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-c", "1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"LS-c", "1128=abc past", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS 1128=9", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS 1128=10", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS missing 11", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS missing 11 seq+5", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Heartbeat 1128=99 hdr", "R 5/1128 0 45=1 | stays | k | app=0 adm=0"},
    {"LS-admin", "Heartbeat 1128=10 hdr", "R 5/1128 0 45=1 | stays | k | app=0 adm=0", nullptr,
     "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "TestRequest 1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Heartbeat", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "TestRequest missing 112", "R 1/112 1 45=1 | stays | k | app=0 adm=0"},
    {"LS-admin", "TestRequest", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Logout", "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogoutSent[] = {
    {"LO-a", "rep35 D..0 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..3 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..4 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..A past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..5 past", "- | Disc | k | app=0 adm=0"},
    {"LO-a", "rep35 5..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 0..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 3..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 4..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 A..0 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..A logon-fields", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 0..A logon-fields", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 0,D header", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D,D header", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..0 past seq+5", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=99 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=abc hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=0 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=1 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=4 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=empty hdr", "- | stays | k | app=0 adm=0"},
    {"LO-c", "1128=6 past", "- | stays | k | app=0 adm=0"},
    {"LO-c", "1128=abc past", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS 1128=9", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS 1128=10", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS missing 11", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS missing 11 seq+5", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=9", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=10", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137 absent", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=empty", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=99", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon undeclared 5999", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=abc", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=4", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137=9", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137=99", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137=4", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137 absent", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS NOS", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137=9", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137=99", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137=4", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137 absent", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale NOS", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Heartbeat", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Heartbeat 1128=99 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Heartbeat 1128=10 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "TestRequest 1128=6 past", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Heartbeat", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "TestRequest missing 112", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "TestRequest", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Logout", "- | Disc | k | app=0 adm=0"},
};

class W9NotConnected : public testing::TestWithParam<KeyedCell> {};
TEST_P(W9NotConnected, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, true, Arm::NotConnected, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(On, W9NotConnected, testing::ValuesIn(on_every_shape(kNotConnected)),
                         keyed_cell_name);

class W9LogonSent : public testing::TestWithParam<KeyedCell> {};
TEST_P(W9LogonSent, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, true, Arm::LogonSent, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(On, W9LogonSent, testing::ValuesIn(on_every_shape(kLogonSent)),
                         keyed_cell_name);

class W9LogoutSent : public testing::TestWithParam<KeyedCell> {};
TEST_P(W9LogoutSent, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, true, Arm::LogoutSent, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(On, W9LogoutSent, testing::ValuesIn(on_every_shape(kLogoutSent)),
                         keyed_cell_name);

// ── Validation off: research R-18's `off` lines ─────────────────────────────────────────

const KeyedRow kNotConnectedOff[] = {
    {"NC-a", "rep35 D..0 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..3 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..4 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..A past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..5 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 5..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 0..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 3..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 4..D past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 A..0 past", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..A logon-fields", "Logon | Active | c | app=0 adm=0"},
    {"NC-a", "rep35 0..A logon-fields", "Logon | Active | c | app=0 adm=0"},
    {"NC-a", "rep35 0,D header", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D,D header", "- | Disc | k | app=0 adm=0"},
    {"NC-a", "rep35 D..0 past seq+5", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=99 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=abc hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=4 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-b", "1128=empty hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-c", "1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"NC-c", "1128=abc past", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS 1128=9", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS 1128=10", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS missing 11", "- | Disc | k | app=0 adm=0"},
    {"NC-d", "NOS missing 11 seq+5", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Heartbeat 1128=99 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Heartbeat 1128=10 hdr", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "TestRequest 1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Heartbeat", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "TestRequest missing 112", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "TestRequest", "- | Disc | k | app=0 adm=0"},
    {"NC-admin", "Logout", "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogonSentOff[] = {
    {"LS-a", "rep35 D..0 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..3 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..4 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..A past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..5 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 5..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 0..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 3..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 4..D past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 A..0 past", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..A logon-fields", "- | Active | c | app=0 adm=0"},
    {"LS-a", "rep35 0..A logon-fields", "- | Active | c | app=0 adm=0"},
    {"LS-a", "rep35 0,D header", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D,D header", "- | Disc | k | app=0 adm=0"},
    {"LS-a", "rep35 D..0 past seq+5", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=99 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=abc hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=4 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-b", "1128=empty hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-c", "1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"LS-c", "1128=abc past", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS 1128=9", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS 1128=10", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS missing 11", "- | Disc | k | app=0 adm=0"},
    {"LS-d", "NOS missing 11 seq+5", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Heartbeat 1128=99 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Heartbeat 1128=10 hdr", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "TestRequest 1128=6 past", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Heartbeat", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "TestRequest missing 112", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "TestRequest", "- | Disc | k | app=0 adm=0"},
    {"LS-admin", "Logout", "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogoutSentOff[] = {
    {"LO-a", "rep35 D..0 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..3 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..4 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..A past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..5 past", "- | Disc | k | app=0 adm=0"},
    {"LO-a", "rep35 5..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 0..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 3..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 4..D past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 A..0 past", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..A logon-fields", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 0..A logon-fields", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 0,D header", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D,D header", "- | stays | k | app=0 adm=0"},
    {"LO-a", "rep35 D..0 past seq+5", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=99 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=abc hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=4 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-b", "1128=empty hdr", "- | stays | k | app=0 adm=0"},
    {"LO-c", "1128=6 past", "- | stays | k | app=0 adm=0"},
    {"LO-c", "1128=abc past", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS 1128=9", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS 1128=10", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS missing 11", "- | stays | k | app=0 adm=0"},
    {"LO-d", "NOS missing 11 seq+5", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=9", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=10", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137 absent", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=empty", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=99", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon undeclared 5999", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=abc", "- | stays | k | app=0 adm=0"},
    {"LO-e", "Logon 1137=4", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137=9", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137=99", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137=4", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS Logon 1137 absent", "- | stays | k | app=0 adm=0"},
    {"LO-f", "wrongBS NOS", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137=9", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137=99", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137=4", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Logon 1137 absent", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale NOS", "- | stays | k | app=0 adm=0"},
    {"LO-g", "stale Heartbeat", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Heartbeat 1128=99 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Heartbeat 1128=10 hdr", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "TestRequest 1128=6 past", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Heartbeat", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "TestRequest missing 112", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "TestRequest", "- | stays | k | app=0 adm=0"},
    {"LO-admin", "Logout", "- | Disc | k | app=0 adm=0"},
};

class W9NotConnectedOff : public testing::TestWithParam<KeyedCell> {};
TEST_P(W9NotConnectedOff, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, false, Arm::NotConnected, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(Off, W9NotConnectedOff,
                         testing::ValuesIn(on_every_shape(kNotConnectedOff)), keyed_cell_name);

class W9LogonSentOff : public testing::TestWithParam<KeyedCell> {};
TEST_P(W9LogonSentOff, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, false, Arm::LogonSent, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(Off, W9LogonSentOff, testing::ValuesIn(on_every_shape(kLogonSentOff)),
                         keyed_cell_name);

class W9LogoutSentOff : public testing::TestWithParam<KeyedCell> {};
TEST_P(W9LogoutSentOff, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, false, Arm::LogoutSent, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(Off, W9LogoutSentOff, testing::ValuesIn(on_every_shape(kLogoutSentOff)),
                         keyed_cell_name);

}  // namespace
}  // namespace fixpp::session::test099
