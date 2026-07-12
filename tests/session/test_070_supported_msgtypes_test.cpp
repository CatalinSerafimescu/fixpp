// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_070_supported_msgtypes_test.cpp
//
// 070-fix44-closeout US3 (S-037) — NoMsgTypes(384) advertise in Logon.
//
// Discriminating witness (FR-008, tasks.md T016):
//   (a) configured [(send,"D"),(receive,"8")] ⇒ outbound Logon carries the exact
//       contiguous group `384=2 | 372=D 385=S | 372=8 385=R` (RefMsgType-then-
//       MsgDirection, FIX44 delimiter order), in configuration order.
//   (b) empty config ⇒ no 384 group (byte-identical baseline).
//   (c) a list too large for the caller's buffer ⇒ build_logon fails closed
//       (std::unexpected, no partial/garbage frame) — no heap, bound-checked writer.
#include <gtest/gtest.h>

#include <array>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/session_config.hpp>
#include <span>
#include <string>
#include <vector>

namespace fixpp::session::test {
namespace {

using fixpp::session::msg_direction;
using fixpp::session::supported_msg_type;

std::string wire_of(std::span<const std::byte> frame) {
    return std::string(reinterpret_cast<const char*>(frame.data()), frame.size());
}

fixpp::core::expected_t<std::span<std::byte>> build(std::span<std::byte> out,
                                                    std::vector<supported_msg_type> types) {
    return fixpp::session::build_logon(
        out, 1, "TW", "ISLD", "FIX.4.4", 30, "20240101-00:00:00.000", false, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{.supported_msg_types = types});
}

// (a) exact contiguous group in configuration order, delimiter 372 first.
TEST(SupportedMsgTypes, EmitsContiguousGroupInOrder) {
    std::array<std::byte, 512> buf{};
    std::vector<supported_msg_type> types{{msg_direction::send, "D"}, {msg_direction::receive, "8"}};
    auto r = build(std::span<std::byte>{buf.data(), buf.size()}, types);
    ASSERT_TRUE(r.has_value());
    const std::string w = wire_of(*r);
    // 384=k then contiguous (372,385) pairs in order.
    EXPECT_NE(w.find("384=2\x01"
                     "372=D\x01"
                     "385=S\x01"
                     "372=8\x01"
                     "385=R\x01"),
              std::string::npos)
        << "expected contiguous 384=2 | 372=D 385=S | 372=8 385=R group; wire=" << w;
}

// (b) empty ⇒ no 384 group.
TEST(SupportedMsgTypes, EmptyEmitsNoGroup) {
    std::array<std::byte, 512> buf{};
    auto r = build(std::span<std::byte>{buf.data(), buf.size()}, {});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(wire_of(*r).find("384="), std::string::npos) << "no 384 when list empty";
}

// (c) overflow ⇒ fail-closed (no partial frame). A tiny buffer + many entries cannot
// fit the group; build_logon must return std::unexpected, not a truncated frame.
TEST(SupportedMsgTypes, OverflowFailsClosed) {
    std::array<std::byte, 256> buf{};  // fits the base Logon; 64 members (~1.3 KB) do not
    std::vector<supported_msg_type> many;
    for (int i = 0; i < 64; ++i) {
        many.push_back({msg_direction::send, "NEWORDERSINGLE"});
    }
    auto r = build(std::span<std::byte>{buf.data(), buf.size()}, many);
    EXPECT_FALSE(r.has_value()) << "oversized group must fail closed, not emit a partial frame";
}

}  // namespace
}  // namespace fixpp::session::test
