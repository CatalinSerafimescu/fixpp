// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_establishment_admission.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-9 (tasks.md T016): data-model E-7's
// `NC-a` to `NC-d`, `LS-a` to `LS-d` and `LO-a` to `LO-g`, every input on T, A and L. An
// application or mismatched frame in an establishment arm disconnects, nothing sent (S1);
// LogoutSent drains as before this feature, and `D…5` confirms the Logout. `*-admin` is the other
// admin frames the keyed logs hold: T's line on T and A (E-7's "A answers as T"), L's on L.
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
    {.cell = "NC-a", .input = "rep35 D..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..3 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..4 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..A past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..5 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 5..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 0..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 3..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 4..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 A..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..A logon-fields", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 0..A logon-fields", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 0,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..0 past seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=99 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=abc hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=0 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=1 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=4 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=empty hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-c", .input = "1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-c", .input = "1128=abc past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS 1128=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS 1128=10", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS missing 11", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS missing 11 seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin",
     .input = "Heartbeat 1128=99 hdr",
     .t = "R 5/1128 0 45=1 | stays | k | app=0 adm=0"},
    {.cell = "NC-admin",
     .input = "Heartbeat 1128=10 hdr",
     .t = "R 5/1128 0 45=1 | stays | k | app=0 adm=0",
     .a = nullptr,
     .l = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "TestRequest 1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin",
     .input = "TestRequest missing 112",
     .t = "R 1/112 1 45=1 | stays | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "TestRequest", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "Logout", .t = "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogonSent[] = {
    {.cell = "LS-a", .input = "rep35 D..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..3 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..4 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..A past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..5 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 5..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 0..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 3..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 4..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 A..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..A logon-fields", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 0..A logon-fields", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 0,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..0 past seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=99 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=abc hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=0 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=1 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=4 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=empty hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-c", .input = "1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-c", .input = "1128=abc past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS 1128=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS 1128=10", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS missing 11", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS missing 11 seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin",
     .input = "Heartbeat 1128=99 hdr",
     .t = "R 5/1128 0 45=1 | stays | k | app=0 adm=0"},
    {.cell = "LS-admin",
     .input = "Heartbeat 1128=10 hdr",
     .t = "R 5/1128 0 45=1 | stays | k | app=0 adm=0",
     .a = nullptr,
     .l = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "TestRequest 1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin",
     .input = "TestRequest missing 112",
     .t = "R 1/112 1 45=1 | stays | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "TestRequest", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "Logout", .t = "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogoutSent[] = {
    {.cell = "LO-a", .input = "rep35 D..0 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..3 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..4 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..A past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..5 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 5..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 0..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 3..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 4..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 A..0 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..A logon-fields", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 0..A logon-fields", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 0,D header", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D,D header", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..0 past seq+5", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=99 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=abc hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=0 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=1 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=4 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=empty hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-c", .input = "1128=6 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-c", .input = "1128=abc past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS 1128=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS 1128=10", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS missing 11", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS missing 11 seq+5", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=10", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137 absent", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=empty", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=99", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon undeclared 5999", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=abc", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=4", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137=99", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137=4", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137 absent", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS NOS", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137=99", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137=4", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137 absent", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale NOS", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Heartbeat", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Heartbeat 1128=99 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Heartbeat 1128=10 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "TestRequest 1128=6 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Heartbeat", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "TestRequest missing 112", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "TestRequest", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Logout", .t = "- | Disc | k | app=0 adm=0"},
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
    {.cell = "NC-a", .input = "rep35 D..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..3 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..4 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..A past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..5 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 5..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 0..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 3..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 4..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 A..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..A logon-fields", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 0..A logon-fields", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 0,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-a", .input = "rep35 D..0 past seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=99 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=abc hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=4 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-b", .input = "1128=empty hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-c", .input = "1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-c", .input = "1128=abc past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS 1128=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS 1128=10", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS missing 11", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-d", .input = "NOS missing 11 seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "Heartbeat 1128=99 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "Heartbeat 1128=10 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "TestRequest 1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "TestRequest missing 112", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "TestRequest", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-admin", .input = "Logout", .t = "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogonSentOff[] = {
    {.cell = "LS-a", .input = "rep35 D..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..3 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..4 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..A past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..5 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 5..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 0..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 3..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 4..D past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 A..0 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..A logon-fields", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 0..A logon-fields", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 0,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D,D header", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-a", .input = "rep35 D..0 past seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=99 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=abc hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=4 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-b", .input = "1128=empty hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-c", .input = "1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-c", .input = "1128=abc past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS 1128=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS 1128=10", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS missing 11", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-d", .input = "NOS missing 11 seq+5", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "Heartbeat 1128=99 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "Heartbeat 1128=10 hdr", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "TestRequest 1128=6 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "TestRequest missing 112", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "TestRequest", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-admin", .input = "Logout", .t = "- | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogoutSentOff[] = {
    {.cell = "LO-a", .input = "rep35 D..0 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..3 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..4 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..A past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..5 past", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 5..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 0..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 3..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 4..D past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 A..0 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..A logon-fields", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 0..A logon-fields", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 0,D header", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D,D header", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-a", .input = "rep35 D..0 past seq+5", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=99 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=abc hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=4 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-b", .input = "1128=empty hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-c", .input = "1128=6 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-c", .input = "1128=abc past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS 1128=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS 1128=10", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS missing 11", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-d", .input = "NOS missing 11 seq+5", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=10", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137 absent", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=empty", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=99", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon undeclared 5999", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=abc", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-e", .input = "Logon 1137=4", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137=99", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137=4", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS Logon 1137 absent", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-f", .input = "wrongBS NOS", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137=9", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137=99", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137=4", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Logon 1137 absent", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale NOS", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-g", .input = "stale Heartbeat", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Heartbeat 1128=99 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Heartbeat 1128=10 hdr", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "TestRequest 1128=6 past", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Heartbeat", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "TestRequest missing 112", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "TestRequest", .t = "- | stays | k | app=0 adm=0"},
    {.cell = "LO-admin", .input = "Logout", .t = "- | Disc | k | app=0 adm=0"},
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
