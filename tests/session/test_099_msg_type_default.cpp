// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_msg_type_default.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-25's first cell (tasks.md T018): a
// validating T session given a Logon carrying a Message Type Default entry reaches Active
// (FR-032, data-model E-10's `MT-a`). A FIXT11.xml declaring no 384 answers such a Logon 373=2,
// 371=384, research R-18's mtd.log's line, so this cell's edit is tasks.md T021's. The rest of W-25
// joins this file in its own phase.

#include <gtest/gtest.h>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

TEST(W25FirstCell, T_LogonWithMsgTypeDefaultEntry_ReachesActive) {
    expect_ending(run_keyed_cell(Shape::T, /*validate=*/true, Arm::NotConnected,
                                 input("Logon Message Type Default 385=S 1410=Y")),
                  "Logon | Active | c | app=0 adm=0");
}

}  // namespace
}  // namespace fixpp::session::test099
