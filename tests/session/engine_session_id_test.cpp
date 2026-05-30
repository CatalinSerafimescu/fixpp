// SPDX-License-Identifier: AGPL-3.0-or-later
// T004 — unit tests for fixpp::session::SessionId.
// Axes: value equality, hash distribution, from_config round-trip,
//       reversed_from_logon acceptor-resolution mapping.
// RED-first per [const §VII.3]; turns GREEN when engine.hpp T004 impl lands.

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session_config.hpp>
#include <gtest/gtest.h>
#include <unordered_map>
#include <unordered_set>

using fixpp::session::SessionId;

namespace {
fixpp::session::SessionConfig make_cfg(std::string bs, std::string sender,
                                       std::string target) {
    fixpp::session::SessionConfig cfg{};
    cfg.begin_string   = std::move(bs);
    cfg.sender_comp_id = std::move(sender);
    cfg.target_comp_id = std::move(target);
    return cfg;
}
}  // namespace

// Value equality — all three fields participate.
TEST(SessionIdTest, EqualWhenAllFieldsMatch) {
    EXPECT_EQ((SessionId{"FIX.4.4", "S", "T"}), (SessionId{"FIX.4.4", "S", "T"}));
}
TEST(SessionIdTest, NotEqualOnBeginString) {
    EXPECT_NE((SessionId{"FIX.4.4", "S", "T"}), (SessionId{"FIX.5.0SP2", "S", "T"}));
}
TEST(SessionIdTest, NotEqualOnSender) {
    EXPECT_NE((SessionId{"FIX.4.4", "A", "T"}), (SessionId{"FIX.4.4", "B", "T"}));
}
TEST(SessionIdTest, NotEqualOnTarget) {
    EXPECT_NE((SessionId{"FIX.4.4", "S", "A"}), (SessionId{"FIX.4.4", "S", "B"}));
}

// Hash distribution.
TEST(SessionIdTest, HashConsistentForEqualObjects) {
    std::hash<SessionId> h;
    EXPECT_EQ(h(SessionId{"FIX.4.4", "S", "T"}), h(SessionId{"FIX.4.4", "S", "T"}));
}
TEST(SessionIdTest, HashDistributedAcrossAllThreeFields) {
    std::hash<SessionId> h;
    std::unordered_set<std::size_t> buckets{
        h({"FIX.4.4", "S", "T"}), h({"FIX.5.0", "S", "T"}),
        h({"FIX.4.4", "X", "T"}), h({"FIX.4.4", "S", "Y"})};
    EXPECT_EQ(buckets.size(), 4u);
}
TEST(SessionIdTest, UsableAsUnorderedMapKey) {
    std::unordered_map<SessionId, int> m;
    m[{"FIX.4.4", "A", "B"}] = 1;
    m[{"FIX.4.4", "C", "D"}] = 2;
    EXPECT_EQ(m.at({"FIX.4.4", "A", "B"}), 1);
    EXPECT_EQ(m.size(), 2u);
}

// from_config round-trip.
TEST(SessionIdTest, FromConfigExtractsAllThreeFields) {
    auto id = SessionId::from_config(make_cfg("FIX.4.4", "MY_SENDER", "MY_TARGET"));
    EXPECT_EQ(id.begin_string,   "FIX.4.4");
    EXPECT_EQ(id.sender_comp_id, "MY_SENDER");
    EXPECT_EQ(id.target_comp_id, "MY_TARGET");
}
TEST(SessionIdTest, FromConfigRoundTrip) {
    auto cfg = make_cfg("FIX.5.0SP2", "INIT", "ACCP");
    EXPECT_EQ(SessionId::from_config(cfg), SessionId::from_config(cfg));
}

// reversed_from_logon acceptor-resolution mapping (R4 / E-2).
// An acceptor registered as {bs, sender=ME, target=PEER} is resolved by a
// Logon with SenderCompID=PEER, TargetCompID=ME.
TEST(SessionIdTest, ReversedFromLogonSwapsCompIds) {
    auto id = SessionId::reversed_from_logon("FIX.4.4", "PEER", "ME");
    EXPECT_EQ(id.begin_string,   "FIX.4.4");
    EXPECT_EQ(id.sender_comp_id, "ME");    // logon_target → our sender
    EXPECT_EQ(id.target_comp_id, "PEER");  // logon_sender → our target
}
TEST(SessionIdTest, ReversedMatchesFromConfigForAcceptor) {
    auto registry_key = SessionId::from_config(make_cfg("FIX.4.4", "ME", "PEER"));
    auto resolved     = SessionId::reversed_from_logon("FIX.4.4", "PEER", "ME");
    EXPECT_EQ(registry_key, resolved);
}
TEST(SessionIdTest, ReversedIsNotIdentity) {
    EXPECT_NE(SessionId::reversed_from_logon("FIX.4.4", "PEER", "ME"),
              (SessionId{"FIX.4.4", "PEER", "ME"}));
}
