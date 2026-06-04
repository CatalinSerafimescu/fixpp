// tests/session/test_019_msgtype_classifier.cpp
//
// T006 — Unit test for the session-internal admin-vs-app MsgType classifier
// (src/session/msgtype_classifier.hpp). Pins the admin allow-list truth table
// so the extracted helper is a single verified source of truth.
//
// Admin MsgTypes (must return true):   "0","1","2","3","4","5"
// Application MsgTypes (must return false): "D","8","j","W","V","A" (dup-Logon)
// Edge cases: empty string, unknown/garbage strings.
//
// Anchor: specs/019-app-callbacks/research.md D8; tasks.md T006.
// [arch §5.3] — src/ internal header test; not exported to consumers.

#include <gtest/gtest.h>

// Include the session-internal header directly (the test binary links
// fixpp_session which has PRIVATE src/ include paths; we add src/ to the
// test target's include dirs in CMakeLists.txt).
#include "session/msgtype_classifier.hpp"

using fixpp::session::detail::is_admin_msgtype;

// ── Admin MsgType truth table (must return true) ──────────────────────────────

TEST(MsgTypeClassifier, AdminMsgTypesReturnTrue) {
    EXPECT_TRUE(is_admin_msgtype("0")) << "Heartbeat (35=0) must be classified as admin";
    EXPECT_TRUE(is_admin_msgtype("1")) << "TestRequest (35=1) must be classified as admin";
    EXPECT_TRUE(is_admin_msgtype("2")) << "ResendRequest (35=2) must be classified as admin";
    EXPECT_TRUE(is_admin_msgtype("3")) << "Reject (35=3) must be classified as admin";
    EXPECT_TRUE(is_admin_msgtype("4")) << "SequenceReset (35=4) must be classified as admin";
    EXPECT_TRUE(is_admin_msgtype("5")) << "Logout (35=5) must be classified as admin";
}

// ── Application MsgType truth table (must return false) ──────────────────────

TEST(MsgTypeClassifier, AppMsgTypesReturnFalse) {
    // Standard FIX application MsgTypes.
    EXPECT_FALSE(is_admin_msgtype("D")) << "NewOrderSingle (35=D) must NOT be classified as admin";
    EXPECT_FALSE(is_admin_msgtype("8")) << "ExecutionReport (35=8) must NOT be classified as admin";
    EXPECT_FALSE(is_admin_msgtype("j")) << "BusinessMsgReject (35=j) must NOT be classified as admin";
    EXPECT_FALSE(is_admin_msgtype("W")) << "MarketDataFull (35=W) must NOT be classified as admin";
    EXPECT_FALSE(is_admin_msgtype("V")) << "MarketDataRequest (35=V) must NOT be classified as admin";
}

// ── Explicitly excluded: dup-Logon-in-Active (35=A) ──────────────────────────

TEST(MsgTypeClassifier, DupLogonExplicitlyExcluded) {
    // "A" (dup-Logon-in-Active) is deliberately EXCLUDED from the admin list
    // per 005 data-model row 22 / FR-017: "never silent no-op" — the session
    // must emit a Reject rather than silently accepting a second Logon.
    // This defensive divergence from QuickFIX behaviour is intentional.
    EXPECT_FALSE(is_admin_msgtype("A"))
        << "Logon (35=A) must NOT be classified as admin (dup-Logon → Reject, "
           "005 data-model row 22 / FR-017 defensive divergence)";
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST(MsgTypeClassifier, EdgeCases) {
    EXPECT_FALSE(is_admin_msgtype(""))     << "empty string must NOT be admin";
    EXPECT_FALSE(is_admin_msgtype("00"))   << "'00' (two chars) must NOT be admin";
    EXPECT_FALSE(is_admin_msgtype(" "))    << "space must NOT be admin";
    EXPECT_FALSE(is_admin_msgtype("z"))    << "unknown 'z' must NOT be admin";
    EXPECT_FALSE(is_admin_msgtype("9"))    << "'9' must NOT be admin (no such admin type)";
}
