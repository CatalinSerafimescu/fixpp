// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_interpret_logon_encrypt_method.cpp
//
// 043-plaintext-tcp-transport — [const §XII.7] inbound EncryptMethod(98) ban.
//
// Witnesses that interpret_logon rejects an inbound Logon carrying
// EncryptMethod(98) ≠ 0 (application-layer encryption is banned; encryption
// lives at TLS only). This closes the pre-existing §XII.7 INBOUND gap that
// S-021 documented as "inbound 98≠0 not handled" (TC-017 backlog) — surfaced
// by 043 because the plaintext profile makes it acute (a peer requesting
// app-layer encryption over plain TCP would otherwise silently get cleartext).
//
// interpret_logon is PROFILE-AGNOSTIC, so this reject applies to every profile
// (TLS and insecure_plain_tcp alike) — the witness is intentionally placed at
// the shared scanner rather than a plaintext-only session round-trip.
//
// Cells (discriminating: the §XII.7 check is the only thing distinguishing
// W-enc-reject-* (RED if removed) from W-enc-accept-zero):
//   W-enc-reject-nonzero  : 98=2       → reject (session_invalid_logon)
//   W-enc-reject-garbage  : 98=GARBAGE → reject (present-but-malformed, fail-closed)
//   W-enc-accept-zero      : 98=0       → accept (positive control / no regression)
//   W-enc-accept-absent    : no 98      → accept (absent is not newly rejected)
//
// Anchors: [const §XII.7]; spec.md FR-009; feature-catalogue S-021 / TC-017.
// interpret_logon signature: include/fixpp/session/admin_messages.hpp

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <span>
#include <string>
#include <vector>

namespace fixpp::session::test {

namespace {

constexpr char SOH = '\x01';

// Minimal valid framing (8=, 9=, body, 10=). interpret_logon does not validate
// the checksum (the framer does), so 10=000 is fine.
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

// A valid FIX.4.4 Logon body with a caller-chosen EncryptMethod field text.
// Pass an empty string to omit tag 98 entirely.
std::string logon_body(std::string const& encrypt_method_field) {
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
    if (!encrypt_method_field.empty()) {
        body += encrypt_method_field;  // e.g. "98=0", "98=2", "98=GARBAGE"
        body += SOH;
    }
    body += "108=30";
    body += SOH;
    return body;
}

fixpp::core::expected_t<logon_interpret_result> interpret(std::string const& body) {
    auto frame = make_frame(body);
    return interpret_logon(std::span<const std::byte>{frame},
                           /*expected_sender=*/"TW",
                           /*expected_target=*/"ISLD",
                           /*expected_begin=*/"FIX.4.4");
}

}  // namespace

// ── W-enc-reject-nonzero: 98=2 → reject ───────────────────────────────────────
TEST(InterpretLogonEncryptMethod, NonZeroEncryptMethodRejected) {
    auto result = interpret(logon_body("98=2"));
    ASSERT_FALSE(result.has_value())
        << "[const §XII.7]: an inbound Logon with EncryptMethod(98)=2 (app-layer "
           "encryption) MUST be rejected — encryption lives at TLS only";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_logon)
        << "EncryptMethod≠0 rejects via the existing session_invalid_logon reason "
           "(no new error slot)";
}

// ── W-enc-reject-garbage: 98=GARBAGE → reject (present-but-malformed, fail-closed)
TEST(InterpretLogonEncryptMethod, MalformedEncryptMethodRejected) {
    auto result = interpret(logon_body("98=GARBAGE"));
    EXPECT_FALSE(result.has_value())
        << "A present-but-malformed EncryptMethod(98) value MUST fail closed "
           "(not be tolerated) — [[feedback_conjunctive_parse_guard_tolerates_malformed_field]]";
}

// ── W-enc-accept-zero: 98=0 → accept (positive control / no regression) ───────
TEST(InterpretLogonEncryptMethod, ZeroEncryptMethodAccepted) {
    auto result = interpret(logon_body("98=0"));
    EXPECT_TRUE(result.has_value())
        << "EncryptMethod(98)=0 (None) is the only permitted value and MUST be accepted "
           "(positive control: proves the reject discriminates on the value, not presence)";
}

// ── W-enc-accept-absent: no 98 → accept (absent not newly rejected) ───────────
TEST(InterpretLogonEncryptMethod, AbsentEncryptMethodAccepted) {
    auto result = interpret(logon_body(""));
    EXPECT_TRUE(result.has_value())
        << "043 does not newly reject a Logon that omits tag 98 — a missing required "
           "field is a separate concern; only present-and-non-zero is banned here";
}

// ── Duplicate-98 cells — [[feedback_forged_token_before_real_tag_self_heals_witness]] ──
//
// These three cells guard the sticky-flag fix (FQ-1 gate-b/r1):
//   NonZeroThenZero: real non-zero FIRST, forged zero AFTER — old last-wins
//     accepted (final 98=0); sticky flag rejects regardless of ordering.
//   GarbageThenZero: malformed FIRST, forged zero AFTER — same class.
//   ZeroThenNonZero: zero FIRST, non-zero AFTER — order-independence (this
//     cell also rejects under last-wins by accident; it guards a sibling
//     mutation "only first occurrence", not the last-wins bug directly).
//
// Mutation evidence (sticky-flag revert → last-wins):
//   NonZeroThenZero: sticky=true case goes RED (last 98=0 → wrongly accepted).
//   GarbageThenZero: same (last 98=0 → wrongly accepted).
//   ZeroThenNonZero: stays GREEN (last 98=2 → rejects, same verdict; this cell
//     discriminates "first-occurrence-only" mutation, not last-wins).

// ── W-enc-reject-nonzero-then-zero: 98=2 then 98=0 → reject ──────────────────
TEST(InterpretLogonEncryptMethod, NonZeroThenZeroEncryptMethodRejected) {
    // Inject TWO tag-98 fields: real non-zero FIRST, forged zero LAST.
    // Old last-wins: final "98=0" → accepted (fail-open). Sticky flag: rejects.
    std::string body;
    body += "35=A\x01";
    body += "34=1\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=2\x01";   // real non-zero EncryptMethod (legit first)
    body += "98=0\x01";   // forged zero after (forged "self-heal")
    body += "108=30\x01";
    auto result = interpret(body);
    ASSERT_FALSE(result.has_value())
        << "[const §XII.7] / [[feedback_forged_token_before_real_tag_self_heals_witness]]: "
           "98=2 followed by 98=0 MUST be rejected — the forged 98=0 must not self-heal "
           "the real non-zero EncryptMethod";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_logon);
}

// ── W-enc-reject-garbage-then-zero: 98=GARBAGE then 98=0 → reject ────────────
TEST(InterpretLogonEncryptMethod, GarbageThenZeroEncryptMethodRejected) {
    // Malformed FIRST (present-but-unparseable), forged zero LAST.
    std::string body;
    body += "35=A\x01";
    body += "34=1\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=GARBAGE\x01";  // present-but-malformed EncryptMethod (legit first)
    body += "98=0\x01";        // forged zero after
    body += "108=30\x01";
    auto result = interpret(body);
    ASSERT_FALSE(result.has_value())
        << "[const §XII.7] / [[feedback_forged_token_before_real_tag_self_heals_witness]]: "
           "98=GARBAGE followed by 98=0 MUST be rejected — malformed EncryptMethod "
           "fails closed even when a later duplicate 98=0 appears";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_logon);
}

// ── W-enc-reject-zero-then-nonzero: 98=0 then 98=2 → reject ──────────────────
TEST(InterpretLogonEncryptMethod, ZeroThenNonZeroEncryptMethodRejected) {
    // Zero FIRST, non-zero AFTER — order-independence of the sticky flag.
    // (Also rejects under old last-wins because the last value is "2", but this
    // cell guards the "first-occurrence-only" mutation: sticky rejects regardless.)
    std::string body;
    body += "35=A\x01";
    body += "34=1\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=0\x01";  // zero first (would be valid alone)
    body += "98=2\x01";  // non-zero after
    body += "108=30\x01";
    auto result = interpret(body);
    ASSERT_FALSE(result.has_value())
        << "[const §XII.7]: 98=0 followed by 98=2 MUST be rejected — the sticky flag "
           "is order-independent; any non-zero EncryptMethod occurrence fails closed";
    EXPECT_EQ(result.error(), fixpp::core::error::session_invalid_logon);
}

}  // namespace fixpp::session::test
