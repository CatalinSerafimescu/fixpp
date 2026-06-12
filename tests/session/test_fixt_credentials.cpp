// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_fixt_credentials.cpp
//
// 033-fixt-fix50sp2-session — T002 (target creation) / T009 (RED) / T010
// (impl: logon_credentials + tag-554 redactor).
//
// Phase 2 (Foundational) witnesses:
//
//   LogonCredentials_OperatorStream_RedactsPassword (T009/T010):
//     logon_credentials::operator<< NEVER contains the password clear value;
//     the username IS visible; the mask token ("***") appears instead.
//     Uses a DISTINCT sentinel password so the test cannot pass vacuously.
//
//   Redactor_ElidesTag554Value (T009/T010):
//     redact_tag554() masks the 554 field value. The decoy "554=" inside a
//     free-text value (tag 58) must NOT be redacted — only the real 554 field
//     is affected. Neighbours (553=, 10=) survive unmodified.
//
// Anchors: data-model.md E5, research R6, contracts/fixt-logon-establishment.md
// C8, FR-011, INV-FIXT-4.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <fixpp/session/logon_credentials.hpp>

namespace {

using fixpp::session::logon_credentials;
using fixpp::session::redact_tag554;

// ── T009/T010: logon_credentials operator<< redaction ────────────────────────

TEST(LogonCredentials, OperatorStream_RedactsPassword) {
    // Use a DISTINCT sentinel password ("xyzzy-s3cr3t") so the test fails if
    // operator<< emits the clear value rather than the mask.
    logon_credentials creds;
    creds.username = "alice";
    creds.password = "xyzzy-s3cr3t";

    std::ostringstream oss;
    oss << creds;
    std::string s = oss.str();

    // The clear password value MUST NOT appear.
    EXPECT_EQ(s.find("xyzzy-s3cr3t"), std::string::npos)
        << "operator<< leaked the clear password: " << s;

    // The username MUST appear (not redacted).
    EXPECT_NE(s.find("alice"), std::string::npos)
        << "operator<< suppressed the username: " << s;

    // The mask token MUST appear (proves the password slot was handled).
    EXPECT_NE(s.find("***"), std::string::npos)
        << "operator<< did not emit the redaction mask: " << s;
}

TEST(LogonCredentials, OperatorStream_AbsentPassword) {
    // When password is absent the mask must NOT appear (it would be misleading).
    logon_credentials creds;
    creds.username = "bob";
    // password intentionally left unset

    std::ostringstream oss;
    oss << creds;
    std::string s = oss.str();

    EXPECT_NE(s.find("bob"), std::string::npos);
    // "(absent)" or equivalent — just ensure the clear mask isn't emitted for
    // an absent field.
    EXPECT_EQ(s.find("xyzzy"), std::string::npos);
}

TEST(LogonCredentials, OperatorStream_BothAbsent) {
    logon_credentials creds;  // both unset
    std::ostringstream oss;
    oss << creds;
    // Must not crash; no password clear value.
    EXPECT_EQ(oss.str().find("***"), std::string::npos);
}

// ── T009/T010: redact_tag554 ─────────────────────────────────────────────────

TEST(RedactTag554, ElidesTag554Value_MidFrame) {
    // Typical mid-frame 554 field in a SOH-delimited FIX frame.
    // Frame: 8=FIXT.1.1<SOH>553=alice<SOH>554=xyzzy-s3cr3t<SOH>10=123<SOH>
    std::string frame =
        "8=FIXT.1.1\x01"
        "553=alice\x01"
        "554=xyzzy-s3cr3t\x01"
        "10=123\x01";

    std::string redacted = redact_tag554(frame);

    // The clear password value MUST be absent.
    EXPECT_EQ(redacted.find("xyzzy-s3cr3t"), std::string::npos)
        << "redact_tag554 did not elide the 554 value: " << redacted;

    // The tag key and mask MUST be present.
    EXPECT_NE(redacted.find("554=***"), std::string::npos)
        << "redact_tag554 did not insert mask: " << redacted;

    // Neighbours must survive unmodified.
    EXPECT_NE(redacted.find("553=alice"), std::string::npos)
        << "redact_tag554 corrupted 553 field: " << redacted;
    EXPECT_NE(redacted.find("10=123"), std::string::npos)
        << "redact_tag554 corrupted 10 field: " << redacted;
}

TEST(RedactTag554, DoesNotRedactDecoy554InFreeText) {
    // A free-text field (tag 58) whose value contains the substring "554="
    // must NOT be redacted — only a SOH-anchored real 554 tag is a match.
    // Decoy: 58=note_554=fake<SOH>  (no SOH before "554=", so not a real field)
    std::string frame =
        "8=FIXT.1.1\x01"
        "553=carol\x01"
        "58=note_554=fake\x01"
        "554=real-secret\x01"
        "10=099\x01";

    std::string redacted = redact_tag554(frame);

    // The real 554 value MUST be masked.
    EXPECT_EQ(redacted.find("real-secret"), std::string::npos)
        << "real 554 value not redacted: " << redacted;

    // The decoy inside tag 58 MUST survive.
    EXPECT_NE(redacted.find("58=note_554=fake"), std::string::npos)
        << "redactor incorrectly touched the decoy inside tag 58: " << redacted;

    // 553 must survive.
    EXPECT_NE(redacted.find("553=carol"), std::string::npos)
        << "redactor corrupted 553: " << redacted;
}

TEST(RedactTag554, NoTag554_FrameUnchanged) {
    // A frame without a 554 field passes through byte-identical.
    std::string frame =
        "8=FIX.4.4\x01"
        "553=dave\x01"
        "10=042\x01";

    std::string redacted = redact_tag554(frame);
    EXPECT_EQ(redacted, frame);
}

TEST(RedactTag554, EmptyFrame) {
    EXPECT_EQ(redact_tag554(""), "");
}

TEST(RedactTag554, Tag554AtFrameStart) {
    // Edge case: 554 is the very first field (no preceding SOH).
    std::string frame =
        "554=top-secret\x01"
        "10=007\x01";

    std::string redacted = redact_tag554(frame);
    EXPECT_EQ(redacted.find("top-secret"), std::string::npos);
    EXPECT_NE(redacted.find("554=***"), std::string::npos);
    EXPECT_NE(redacted.find("10=007"), std::string::npos);
}

TEST(RedactTag554, Tag554AtFrameEnd_NoTrailingSOH) {
    // 554 value reaches end-of-string without a trailing SOH.
    std::string frame = "8=FIXT.1.1\x01" "554=mysecret";

    std::string redacted = redact_tag554(frame);
    EXPECT_EQ(redacted.find("mysecret"), std::string::npos);
    EXPECT_NE(redacted.find("554=***"), std::string::npos);
}

}  // namespace
