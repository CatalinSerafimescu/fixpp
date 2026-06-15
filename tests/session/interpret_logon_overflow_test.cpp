// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/interpret_logon_overflow_test.cpp
//
// 040-inbound-tag-overflow-hardening Phase 4 US2 — T010/T012 (interpret_logon site).
//
// Witnesses that interpret_logon's hand-rolled SOH scanner rejects forged
// wrap-and-continue tag tokens and never stores their values in the result struct.
// Pre-fix the digit loop had no per-digit bound; a token like 42949672961137
// wraps uint32 to 1137 and the value would be stored as default_appl_ver_id.
//
// Cells:
//   W1: token 42949672961137 (wraps to 1137 without fix) — result.has_value() &&
//       result->default_appl_ver_id == std::nullopt  (field skipped, not consumed)
//   W2: token 429496729649 (wraps to 49 without fix) — result successfully parsed &&
//       sender_comp_id validated via expected_sender == "TW" (no forged alias)
//   W3: conforming Logon including real 1137=9 — result.has_value() &&
//       result->default_appl_ver_id has_value() (no regression on legitimate field)
//
// Discriminating construction for W1:
//   Pre-fix: the forged 42949672961137=9 is consumed as case 1137 → default_appl_ver_id="9"
//   Post-fix: accumulate_tag_digit fires at the overflowing digit → goto next_field →
//             field skipped → default_appl_ver_id stays nullopt.
//   A fresh session with NO 1137 field also yields default_appl_ver_id=nullopt, but
//   here the FORGED TOKEN is present (something fires on it) — the correct fix routes
//   it to the skip path; a false fix that simply ignored it would also give nullopt.
//   Discrimination: W3 proves that a REAL 1137=9 does set default_appl_ver_id; so if
//   W1 also set it, the fix is absent (forged 42949672961137 consumed as 1137).
//
// Anchors: research.md D-1/D-3/D-6; data-model.md SC-001; tasks.md T010/T012.
// interpret_logon signature: include/fixpp/session/admin_messages.hpp

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/session/admin_messages.hpp>
#include <span>
#include <string>
#include <vector>

namespace fixpp::session::test {

namespace {

// Encode a string as a vector<byte> FIX frame. We hand-roll a minimal but valid
// framing (8=, 9=, body, 10=) so that interpret_logon sees realistic input.
// checksum must match for interpret_logon to succeed (it does not validate checksum
// itself — that is done by the framer; interpret_logon receives the raw frame bytes).
// We use a known-good approach: set the checksum to 000 (interpret_logon does not
// validate it; only the framer does).
std::vector<std::byte> make_frame(std::string const& body) {
    std::string full;
    full += "8=FIX.4.4\x01";
    full += "9=";
    full += std::to_string(body.size());
    full += "\x01";
    full += body;
    full += "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

constexpr char SOH = '\x01';

}  // namespace

// ── W1: token 42949672961137 must NOT be consumed as DefaultApplVerID(1137) ──
// 42949672961137 is the canonical vector for aliasing tag 1137 under uint32 wrap.
// Place it in an otherwise-valid Logon frame so that interpret_logon returns
// success (all required fields present, CompIDs match) — then assert that
// default_appl_ver_id is nullopt (the forged field was skipped, not consumed).
//
// The frame is a valid FIX.4.4 Logon without a real 1137 field; the only
// "1137" that appears is the forged overflowing token. If the fix is absent,
// default_appl_ver_id == "9" (the forged value). Post-fix: std::nullopt.
TEST(InterpretLogonOverflow, ForgedTag42949672961137_NotConsumedAsDefaultApplVerID) {
    std::string body;
    body += "35=A";
    body += SOH;
    body += "34=1";
    body += SOH;
    body += "49=TW";
    body += SOH;
    body += "52=20240101-00:00:00.000";
    body += SOH;
    body += "56=ISLD";
    body += SOH;
    body += "98=0";
    body += SOH;
    body += "108=30";
    body += SOH;
    // Forged token: 42949672961137 → without bound, wraps to 1137.
    body += "42949672961137=9";
    body += SOH;

    auto frame = make_frame(body);
    auto result = interpret_logon(std::span<const std::byte>{frame},
                                  /*expected_sender=*/"TW",
                                  /*expected_target=*/"ISLD",
                                  /*expected_begin=*/"FIX.4.4");

    ASSERT_TRUE(result.has_value())
        << "interpret_logon must succeed on an otherwise-valid Logon frame";
    EXPECT_FALSE(result->default_appl_ver_id.has_value())
        << "Forged token 42949672961137=9 must NOT be consumed as DefaultApplVerID(1137) "
           "(SC-001/D-3): the field must be skipped via goto next_field on overflow";
}

// ── W2: token 429496729649 (aliases SenderCompID=49) skipped — CompID still validates
// The forged 49 field must be skipped; the real 49=TW is still validated.
// This confirms interpret_logon's CompID check succeeds on the real 49=TW value,
// i.e. the forged field was not consumed under tag 49.
TEST(InterpretLogonOverflow, ForgedTag429496729649_NotConsumedAsSenderCompId) {
    std::string body;
    // Insert forged 49 BEFORE the real 49= to make substitution unambiguous.
    body += "35=A";
    body += SOH;
    body += "34=1";
    body += SOH;
    body += "429496729649=FORGE49";
    body += SOH;  // forged token
    body += "49=TW";
    body += SOH;  // real SenderCompID
    body += "52=20240101-00:00:00.000";
    body += SOH;
    body += "56=ISLD";
    body += SOH;
    body += "98=0";
    body += SOH;
    body += "108=30";
    body += SOH;

    auto frame = make_frame(body);
    // If the forged field had been consumed as 49, the sender would be "FORGE49"
    // and the CompID check would fail (expected_sender="TW"). Passing == forged skipped.
    auto result = interpret_logon(std::span<const std::byte>{frame},
                                  /*expected_sender=*/"TW",
                                  /*expected_target=*/"ISLD",
                                  /*expected_begin=*/"FIX.4.4");

    EXPECT_TRUE(result.has_value())
        << "interpret_logon must validate successfully when the forged 49 field is "
           "skipped and the real 49=TW is seen (SC-001): if forged were consumed, "
           "CompID check would fail (FORGE49 != TW)";
}

// ── W3: conforming Logon with real 1137=9 — default_appl_ver_id is set ───────
// Proves that a REAL tag 1137 is consumed correctly (no regression).
// Pairs with W1: W1 proves forged 42949672961137 → nullopt;
// W3 proves real 1137=9 → has_value(). Together they discriminate.
TEST(InterpretLogonOverflow, ConformingLogonWith1137_DefaultApplVerIdIsSet) {
    std::string body;
    body += "35=A";
    body += SOH;
    body += "34=1";
    body += SOH;
    body += "49=TW";
    body += SOH;
    body += "52=20240101-00:00:00.000";
    body += SOH;
    body += "56=ISLD";
    body += SOH;
    body += "98=0";
    body += SOH;
    body += "108=30";
    body += SOH;
    body += "1137=9";
    body += SOH;  // real DefaultApplVerID: 9 = FIX50SP2

    auto frame = make_frame(body);
    auto result = interpret_logon(std::span<const std::byte>{frame},
                                  /*expected_sender=*/"TW",
                                  /*expected_target=*/"ISLD",
                                  /*expected_begin=*/"FIX.4.4");

    ASSERT_TRUE(result.has_value())
        << "interpret_logon must succeed on conforming FIXT-style Logon (regression guard)";
    ASSERT_TRUE(result->default_appl_ver_id.has_value())
        << "Real tag 1137=9 must be consumed as default_appl_ver_id (no regression)";
    EXPECT_EQ(*result->default_appl_ver_id, "9")
        << "default_appl_ver_id must equal the wire value '9'";
}

}  // namespace fixpp::session::test
