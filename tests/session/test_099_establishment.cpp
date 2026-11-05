// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_establishment.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-5 (tasks.md T012): data-model E-7's
// `NC-e` to `NC-i` and `LS-e` to `LS-i`, every input on T, A and L, NotConnected as the acceptor
// and LogonSent as the initiator, then the row's named cells. The double faults and the consume
// split are asserted as E-7 writes them. Non-validating twins take research R-18's `off` lines.
//
// Each cell's trace line compares with the `on` logs. The `LS-h` cells stay direct-constructed,
// delivered through attach_accepted_transport, and read against E-7's row: research R-18 lists that
// column as no measurement. An Engine's reconnect step refuses the identity before LogonSent.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/session/session_event.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include "mock_reconnect_factory.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

// ── Validating ──────────────────────────────────────────────────────────────────────────

const KeyedRow kNotConnected[] = {
    {.cell = "NC-e", .input = "Logon 1137 absent", .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=empty", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137=99",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137=abc",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=4", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=9", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=10", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon undeclared 5999",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137=4 + 5999 after",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137 absent + 5999 after",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 5999 + 1137 absent",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137=99 + 5999 after",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 141=Y 1137=4",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 141=Y 1137=9", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS Logon 1137=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS Logon 1137=4", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-f",
     .input = "wrongBS Logon 1137=99",
     .t = "- | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-f",
     .input = "wrongBS Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale Logon 1137=9", .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale Logon 1137=4", .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g",
     .input = "stale Logon 1137=99",
     .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g",
     .input = "stale Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137=9",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137=4",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137=99",
     .t = "- | Disc | c | app=0 adm=0 | auth",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137=9",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137=4",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137=99",
     .t = "Logout | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogonSent[] = {
    {.cell = "LS-e", .input = "Logon 1137 absent", .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=empty", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 1137=99",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 1137=abc",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=4", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=9", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=10", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon undeclared 5999",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 1137=4 + 5999 after",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 1137 absent + 5999 after",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 5999 + 1137 absent",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 1137=99 + 5999 after",
     .t = "R 2/5999 A 45=1 | stays | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 141=Y 1137=4",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 141=Y 1137=9", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS Logon 1137=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS Logon 1137=4", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-f",
     .input = "wrongBS Logon 1137=99",
     .t = "- | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-f",
     .input = "wrongBS Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Logon 1137=9", .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Logon 1137=4", .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-g",
     .input = "stale Logon 1137=99",
     .t = "Logout | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-g",
     .input = "stale Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137=9",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137=4",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137=99",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137=9",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137=4",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137=99",
     .t = "Logout | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | k | app=0 adm=0"},
};

class W5NotConnected : public testing::TestWithParam<KeyedCell> {};
TEST_P(W5NotConnected, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, true, Arm::NotConnected, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(On, W5NotConnected, testing::ValuesIn(on_every_shape(kNotConnected)),
                         keyed_cell_name);

class W5LogonSent : public testing::TestWithParam<KeyedCell> {};
TEST_P(W5LogonSent, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, true, Arm::LogonSent, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(On, W5LogonSent, testing::ValuesIn(on_every_shape(kLogonSent)),
                         keyed_cell_name);

// ── Validation off: research R-18's `off` lines and mtd.log's ───────────────────────────

const KeyedRow kNotConnectedOff[] = {
    {.cell = "NC-e", .input = "Logon 1137=9", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=10", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137 absent", .t = "R 1/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=empty", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=99", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon undeclared 5999", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=abc", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 1137=4", .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS Logon 1137=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS Logon 1137=99", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS Logon 1137=4", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS Logon 1137 absent", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-f", .input = "wrongBS NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale Logon 1137=9", .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g",
     .input = "stale Logon 1137=99",
     .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale Logon 1137=4", .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g",
     .input = "stale Logon 1137 absent",
     .t = "R 10/52 A 45=1 | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-g", .input = "stale Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137 absent + 5999 after",
     .t = "R 1/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 5999 + 1137 absent",
     .t = "R 1/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137=99 + 5999 after",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 1137=4 + 5999 after",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e",
     .input = "Logon 141=Y 1137=4",
     .t = "R 5/1137 A 45=1 | Disc | c | app=0 adm=0"},
    {.cell = "NC-e", .input = "Logon 141=Y 1137=9", .t = "Logon | Active | c | app=0 adm=0"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137=9",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137=99",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137=4",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "NC-h",
     .input = "mTLS-offlist Logon 1137 absent",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137=9",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137=99",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137=4",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "NC-i",
     .input = "posture-mismatch Logon 1137 absent",
     .t = "Logout | Disc | k | app=0 adm=0"},
};

const KeyedRow kLogonSentOff[] = {
    {.cell = "LS-e", .input = "Logon 1137=9", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=10", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137 absent", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=empty", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=99", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon undeclared 5999", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=abc", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=4", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS Logon 1137=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS Logon 1137=99", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS Logon 1137=4", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS Logon 1137 absent", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-f", .input = "wrongBS NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Logon 1137=9", .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Logon 1137=99", .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Logon 1137=4", .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Logon 1137 absent", .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-g", .input = "stale Heartbeat", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "LS-e",
     .input = "Logon 1137 absent + 5999 after",
     .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 5999 + 1137 absent", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=99 + 5999 after", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 1137=4 + 5999 after", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 141=Y 1137=4", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-e", .input = "Logon 141=Y 1137=9", .t = "- | Active | c | app=0 adm=0"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137=9",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137=99",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137=4",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "LS-h",
     .input = "mTLS-offlist Logon 1137 absent",
     .t = "- | Disc | c | app=0 adm=0 | auth"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137=9",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137=99",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137=4",
     .t = "Logout | Disc | k | app=0 adm=0"},
    {.cell = "LS-i",
     .input = "posture-mismatch Logon 1137 absent",
     .t = "Logout | Disc | k | app=0 adm=0"},
};

class W5NotConnectedOff : public testing::TestWithParam<KeyedCell> {};
TEST_P(W5NotConnectedOff, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, false, Arm::NotConnected, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(Off, W5NotConnectedOff,
                         testing::ValuesIn(on_every_shape(kNotConnectedOff)), keyed_cell_name);

class W5LogonSentOff : public testing::TestWithParam<KeyedCell> {};
TEST_P(W5LogonSentOff, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, false, Arm::LogonSent, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(Off, W5LogonSentOff, testing::ValuesIn(on_every_shape(kLogonSentOff)),
                         keyed_cell_name);

// ── The named cells ─────────────────────────────────────────────────────────────────────

class W5PerShape : public testing::TestWithParam<Shape> {};
std::string shape_param_name(const testing::TestParamInfo<Shape>& i) { return shape_name(i.param); }
INSTANTIATE_TEST_SUITE_P(Named, W5PerShape, testing::Values(Shape::T, Shape::A, Shape::L),
                         shape_param_name);

// `141=Y` with `1137=4` on a validating initiator: refused ahead of the reset unit. The Reject
// takes the number after open()'s Logon. Behind the reset unit, the reset moves the counter first,
// so the two endings differ by this counter, and only that unit raises the event.
TEST_P(W5PerShape, Initiator141YWith1137_4_NoReset) {
    const CellRun run =
        run_keyed_cell(GetParam(), true, Arm::LogonSent, input("Logon 141=Y 1137=4"));
    expect_ending(run, "R 5/1137 A 45=1 | Disc | c | app=0 adm=0");
    if (run.setup_failure.empty()) {
        EXPECT_EQ(session_test_access::seqnum_mgr(*run.rig->sess).peek_outbound(), 3U);
        for (auto const& ev : run.rig->sess->recent_events()) {
            EXPECT_FALSE(std::holds_alternative<session_event_sequence_numbers_reset>(ev))
                << "the reset unit ran on a refused Logon";
        }
    }
}

// A malformed NoHops on a Logon: the validator's Reject naming 628, stays, kept, in both arms
// (data-model E-3).
TEST_P(W5PerShape, MalformedNoHopsOnLogon_NotConnected) {
    const CellRun run =
        run_own_cell(std::string("W-5 NoHops NC ") + shape_name(GetParam()),
                     shape_fixture(GetParam()), true, Arm::NotConnected, [](const Rig& r) {
                         return wire(kFT(), "35=A\x01" + hdr(r.peer_ctx(1)) +
                                                "627=2\x01"
                                                "628=X\x01" +
                                                kLogonBody() + "1137=9\x01");
                     });
    expect_ending(run, "R 1/628 A 45=1 | stays | k | app=0 adm=0");
}

TEST_P(W5PerShape, MalformedNoHopsOnLogon_LogonSent) {
    const CellRun run =
        run_own_cell(std::string("W-5 NoHops LS ") + shape_name(GetParam()),
                     shape_fixture(GetParam()), true, Arm::LogonSent, [](const Rig& r) {
                         return wire(kFT(), "35=A\x01" + hdr(r.peer_ctx(1)) +
                                                "627=2\x01"
                                                "628=X\x01" +
                                                kLogonBody() + "1137=9\x01");
                     });
    expect_ending(run, "R 1/628 A 45=1 | stays | k | app=0 adm=0");
}

// S2 reads a Logon only: a Heartbeat carrying 1137 draws the gate's Reject and stays.
TEST(W5Named, HeartbeatCarrying1137_T_NotConnected_GateRejectStays) {
    const CellRun run =
        run_own_cell("W-5 Heartbeat 1137", shape_fixture(Shape::T), true, Arm::NotConnected,
                     [](const Rig& r) { return frame_35("0", r.peer_ctx(1), "1137=9\x01"); });
    expect_ending(run, "R 2/1137 0 45=1 | stays | k | app=0 adm=0");
}

// The initiator's block tests the registry, not the slot: A with S's base unregistered and 1137
// naming S's own version is refused in both roles.
Fixture a_with_base_unregistered() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix44()});
    f.session_dict = fix50sp2();
    f.default_appl = dd::application_version::v44;
    f.peer_1137 = "6";
    return f;
}

TEST(W5Named, BlockTestsTheRegistry_Acceptor) {
    const CellRun run = run_own_cell(
        "W-5 base unregistered NC", a_with_base_unregistered(), true, Arm::NotConnected,
        [](const Rig& r) { return logon_frame(r.peer_ctx(1), "1137=9\x01"); });
    expect_ending(run, "R 5/1137 A 45=1 | Disc | c | app=0 adm=0");
}

TEST(W5Named, BlockTestsTheRegistry_Initiator) {
    const CellRun run =
        run_own_cell("W-5 base unregistered LS", a_with_base_unregistered(), true, Arm::LogonSent,
                     [](const Rig& r) { return logon_frame(r.peer_ctx(1), "1137=9\x01"); });
    expect_ending(run, "R 5/1137 A 45=1 | Disc | c | app=0 adm=0");
}

// A transport dictionary with 1137 optional: the gate passes an absent one, so the initiator's
// block answers it, 373=1.
TEST(W5Named, Optional1137_Absent_InitiatorBlockAnswers) {
    static const DictPtr optional_1137 = edited_vendored("FIXT11.xml", [](std::string xml) {
        const std::string from = "name='DefaultApplVerID' required='Y'";
        const auto at = xml.find(from);
        if (at != std::string::npos) {
            xml.replace(at, from.size(), "name='DefaultApplVerID' required='N'");
        }
        return xml;
    });
    ASSERT_TRUE(optional_1137->field_valid_for("A", 1137));
    Fixture f;
    f.registry = make_registry({optional_1137, fix50sp2()});
    f.session_dict = optional_1137;
    const CellRun run = run_own_cell("W-5 optional 1137", std::move(f), true, Arm::LogonSent,
                                     [](const Rig& r) { return logon_frame(r.peer_ctx(1), ""); });
    expect_ending(run, "R 1/1137 A 45=1 | Disc | c | app=0 adm=0");
}

// ── Reconnect: a non-validating initiator's second reply has no 1137 ────────────────────

std::function<void(SessionConfig&)> with_reconnect() {
    return [](SessionConfig& cfg) {
        cfg.transport_factory_override =
            std::make_shared<fixpp::test_support::MockReconnectFactory>();
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    };
}

// The first reply resolved to the FIX 5.0 SP2 table, whose pair S lacks. Reconnected, the session
// is in LogonSent again. On failure returns the setup message.
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

CellRun first_connection_then_reconnect() {
    RigOptions o;
    o.validate = false;
    o.role = session_role::initiator;
    o.tweak = with_reconnect();
    CellRun run = enter_arm(own_fixture(Shape::T, {fixt11(), fix50sp2_with_body_pair()}),
                            Arm::LogonSent, std::move(o));
    if (!run.setup_failure.empty()) {
        return run;
    }
    (void)run.rig->deliver(logon_frame(run.rig->peer_ctx(1), "1137=9\x01"));
    if (run.rig->sess->state() != fsm_state::Active) {
        run.setup_failure = "first reply did not establish";
        return run;
    }
    if (auto f = reconnect(*run.rig)) {
        run.setup_failure = *f;
    }
    return run;
}

TEST(W5Reconnect, SecondReplyWithNo1137_NegotiatedReadsAsToday_FrameUsesS) {
    CellRun run = first_connection_then_reconnect();
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(r.next_in()), ""));
    ASSERT_EQ(r.sess->state(), fsm_state::Active) << "the second reply must establish";
    EXPECT_EQ(r.sess->negotiated_version_profile().default_appl, dd::application_version::v50sp2)
        << "negotiated_version_profile() keeps the first reply's version (FR-027)";
    // On S, whose hooks lack the pair, the Data's 34 is the frame's last and is too high; on the
    // first connection's table the frame would be in sequence and delivered.
    const unsigned next = r.next_in();
    run.ending = r.deliver(frame_35("D", r.peer_ctx(next),
                                    kNos() + pair_field("x\x01"
                                                        "34=70")));
    expect_ending(run, "- | stays | k | app=0 adm=0");
    EXPECT_EQ(run.ending.next_in_after, next) << log_form(run.ending);
}

// FR-027's establishment half: a frame an establishment arm scans with S, not with the first
// connection's table, so the Data's too-high 34 is the header's reading. Disconnected, kept.
TEST(W5Reconnect, EstablishmentArmScansWithS_DisconnectedKept) {
    CellRun run = first_connection_then_reconnect();
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Rig& r = *run.rig;
    run.ending = r.deliver(frame_35("D", r.peer_ctx(r.next_in()),
                                    kLogonBody() + "1137=9\x01" +
                                        pair_field("x\x01"
                                                   "34=70") +
                                        "35=A\x01"));
    expect_ending(run, "- | Disc | k | app=0 adm=0");
}

// ── Scan and Interpret: a pair on the transport dictionary's Logon ──────────────────────

const DictPtr& fixt11_with_logon_pair() {
    static const DictPtr d =
        overlay_on("FIXT11.xml",
                   "<fix type='FIXT' major='1' minor='1' servicepack='0'><messages>"
                   "<message name='Logon' msgtype='A' msgcat='admin'>" +
                       kPairMembers() + "</message></messages>" + kPairFields() + "</fix>");
    return d;
}

Fixture a_with_logon_pair() {
    Fixture f;
    f.registry = make_registry({fixt11_with_logon_pair(), fix50sp2()});
    f.session_dict = fix50sp2();
    return f;
}

struct ScanCase {
    const char* name;
    Arm arm;
    const char* data;
    const char* ending;
};

const ScanCase kScanCases[] = {
    {.name = "NC_34",
     .arm = Arm::NotConnected,
     .data = "\x01"
             "34=7",
     .ending = "Logon | Active | c | app=0 adm=0"},
    {.name = "NC_141",
     .arm = Arm::NotConnected,
     .data = "\x01"
             "141=Y",
     .ending = "Logon | Active | c | app=0 adm=0"},
    {.name = "LS_34",
     .arm = Arm::LogonSent,
     .data = "\x01"
             "34=7",
     .ending = "- | Active | c | app=0 adm=0"},
    {.name = "LS_141",
     .arm = Arm::LogonSent,
     .data = "\x01"
             "141=Y",
     .ending = "- | Active | c | app=0 adm=0"},
};

class W5Scan : public testing::TestWithParam<ScanCase> {};

TEST_P(W5Scan, NextNumInIsTheHeadersAndNoReset) {
    const ScanCase& c = GetParam();
    const CellRun run = run_own_cell(
        std::string("W-5 scan ") + c.name, a_with_logon_pair(), true, c.arm, [&](const Rig& r) {
            return logon_frame(r.peer_ctx(1), "1137=9\x01" + pair_field(c.data));
        });
    expect_ending(run, c.ending);
    if (!run.setup_failure.empty()) {
        return;
    }
    EXPECT_EQ(run.rig->next_in(), 2U);
    for (auto const& ev : run.rig->sess->recent_events()) {
        EXPECT_FALSE(std::holds_alternative<session_event_sequence_numbers_reset>(ev))
            << "a 141=Y in a Data field reset the session";
    }
}

INSTANTIATE_TEST_SUITE_P(Named, W5Scan, testing::ValuesIn(kScanCases),
                         [](const testing::TestParamInfo<ScanCase>& i) {
                             return std::string{i.param.name};
                         });

struct InterpretCase {
    const char* name;
    Arm arm;
    bool validate;
    const char* ending;
};

const InterpretCase kInterpretCases[] = {
    {.name = "NC_on",
     .arm = Arm::NotConnected,
     .validate = true,
     .ending = "Logon | Active | c | app=0 adm=0"},
    {.name = "NC_off",
     .arm = Arm::NotConnected,
     .validate = false,
     .ending = "Logon | Active | c | app=0 adm=0"},
    {.name = "LS_on",
     .arm = Arm::LogonSent,
     .validate = true,
     .ending = "- | Active | c | app=0 adm=0"},
    {.name = "LS_off",
     .arm = Arm::LogonSent,
     .validate = false,
     .ending = "- | Active | c | app=0 adm=0"},
};

class W5Interpret : public testing::TestWithParam<InterpretCase> {};

TEST_P(W5Interpret, NegotiatedStaysTheRealOne) {
    const InterpretCase& c = GetParam();
    const CellRun run =
        run_own_cell(std::string("W-5 interpret ") + c.name, a_with_logon_pair(), c.validate, c.arm,
                     [](const Rig& r) {
                         return logon_frame(r.peer_ctx(1), "1137=9\x01" + pair_field("\x01"
                                                                                     "1137=6"));
                     });
    expect_ending(run, c.ending);
    if (run.setup_failure.empty()) {
        EXPECT_EQ(run.rig->sess->negotiated_version_profile().default_appl,
                  dd::application_version::v50sp2);
    }
}

INSTANTIATE_TEST_SUITE_P(Named, W5Interpret, testing::ValuesIn(kInterpretCases),
                         [](const testing::TestParamInfo<InterpretCase>& i) {
                             return std::string{i.param.name};
                         });

// A second 1137 in the frame: interpret_logon's reading, its `case 1137` overwriting, with
// validation off.
TEST(W5Named, Second1137_LastWins_Off) {
    const CellRun run = run_own_cell("W-5 second 1137 off", shape_fixture(Shape::T), false,
                                     Arm::NotConnected, [](const Rig& r) {
                                         return logon_frame(r.peer_ctx(1),
                                                            "1137=9\x01"
                                                            "1137=10\x01");
                                     });
    expect_ending(run, "Logon | Active | c | app=0 adm=0");
    if (run.setup_failure.empty()) {
        EXPECT_EQ(run.rig->sess->negotiated_version_profile().default_appl,
                  dd::application_version::vlatest);
    }
}

// ── A Reject that cannot go out ────────────────────────────

// toAdmin runs ahead of a Reject's transmit, so a throw there is how an establishment refusal's
// emit fails with no fault in the transport. fire_to_admin_'s callers record Disconnected and
// return app_callback_threw, and nothing is sent.
std::string app_callback_threw_err() {
    return "err=" + std::to_string(static_cast<int>(fixpp::core::error::app_callback_threw));
}

CellRun refused_with_throwing_to_admin(Shape shape, Arm arm, const char* id) {
    RigOptions o;
    o.validate = true;
    o.role = role_of(arm);
    CellRun run = enter_arm(shape_fixture(shape), arm, std::move(o));
    if (run.setup_failure.empty()) {
        Rig& r = *run.rig;
        r.app->on_to_admin = [](const CountingApp::View&) {
            throw std::runtime_error{"toAdmin throws"};
        };
        run.ending = r.deliver(input(id).make(r.peer_ctx(1)));
    }
    return run;
}

class W5RejectCannotGoOut : public testing::TestWithParam<Arm> {};
INSTANTIATE_TEST_SUITE_P(BothArms, W5RejectCannotGoOut,
                         testing::Values(Arm::NotConnected, Arm::LogonSent),
                         [](const testing::TestParamInfo<Arm>& i) {
                             return std::string{arm_name(i.param)};
                         });

// The gate's Reject on a Logon, E-7's S2: on L the gate names 1137, and the disconnect it would
// take follows a Reject that went out.
TEST_P(W5RejectCannotGoOut, GateReject_1137_TheErrorReturns) {
    const CellRun run = refused_with_throwing_to_admin(Shape::L, GetParam(), "Logon 1137=99");
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    EXPECT_EQ(run.ending.err, app_callback_threw_err()) << log_form(run.ending);
    EXPECT_EQ(short_form(run.ending), "- | Disc | k | app=0 adm=0");
}

// A gate Reject that would stay in the arm: only the failed emit ends the session.
TEST_P(W5RejectCannotGoOut, GateReject_Undeclared_TheErrorReturns) {
    const CellRun run =
        refused_with_throwing_to_admin(Shape::T, GetParam(), "Logon undeclared 5999");
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    EXPECT_EQ(run.ending.err, app_callback_threw_err()) << log_form(run.ending);
    EXPECT_EQ(short_form(run.ending), "- | Disc | k | app=0 adm=0");
}

// S3's initiator block, on T where the gate passes the Logon: the check of the number has run, so
// this consumes as `LS-e` does.
TEST(W5RejectCannotGoOutS3, Initiator_TheErrorReturns) {
    const CellRun run = refused_with_throwing_to_admin(Shape::T, Arm::LogonSent, "Logon 1137=99");
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    EXPECT_EQ(run.ending.err, app_callback_threw_err()) << log_form(run.ending);
    EXPECT_EQ(short_form(run.ending), "- | Disc | c | app=0 adm=0");
}

// S3's initiator block with no clock: Session::open() resolves the clock and does not require one,
// so a directly constructed session can have none. `LS-e`'s ending, with an empty SendingTime.
TEST(W5NoClock, InitiatorS3_RefusesAndStampsNothing) {
    RigOptions o;
    o.validate = true;
    o.role = session_role::initiator;
    Rig r{shape_fixture(Shape::T), std::move(o)};
    r.engine.clock = nullptr;
    const auto setup = r.open();
    ASSERT_FALSE(setup.has_value()) << setup.value_or("");
    const Ending e = r.deliver(input("Logon 1137=99").make(r.peer_ctx(1)));
    EXPECT_EQ(short_form(e), "R 5/1137 A 45=1 | Disc | c | app=0 adm=0");
    ASSERT_EQ(e.out.size(), 1U);
    EXPECT_TRUE(tag_value(e.out[0], 52).empty()) << tag_value(e.out[0], 52);
}

}  // namespace
}  // namespace fixpp::session::test099
