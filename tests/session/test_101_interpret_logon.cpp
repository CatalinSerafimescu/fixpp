// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_interpret_logon.cpp
//
// 101-inbound-reject-fidelity, tasks.md T012 (W101-30; FR-004, research D4): `interpret_logon`,
// public C++, reads the frame's first 35. A frame whose first 35 is not A is refused as a Logon
// whatever its later 35s say; one whose first 35 is A is read as a Logon (the conflict is
// `FrameHeader::msg_type_conflict`'s, which only the Session's arms read).

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <span>
#include <string>

#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

std::string logon_shaped(const std::string& first, const std::string& later) {
    return t99::wire("FIX.4.4", "35=" + first + "\x01" +
                                    t99::hdr(t99::Ctx{.snd = "TW", .tgt = "ISLD", .seq = 1}) +
                                    t99::kLogonBody() + "35=" + later + "\x01");
}

auto interpret(const std::string& frame) {
    return interpret_logon(std::as_bytes(std::span{frame.data(), frame.size()}), "TW", "ISLD",
                           "FIX.4.4");
}

TEST(InterpretLogon101, W101_30_DThenAWithLogonFields_Refused) {
    const auto r = interpret(logon_shaped("D", "A"));
    ASSERT_FALSE(r.has_value()) << "read as a Logon";
    EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_logon);
}

TEST(InterpretLogon101, W101_30_AThenDWithLogonFields_Accepted) {
    const auto r = interpret(logon_shaped("A", "D"));
    ASSERT_TRUE(r.has_value()) << "error " << static_cast<int>(r.error());
    EXPECT_EQ(r->heartbt_int, 30);
}

}  // namespace
}  // namespace fixpp::session::test101
