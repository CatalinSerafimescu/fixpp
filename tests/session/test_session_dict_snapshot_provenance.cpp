// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_session_dict_snapshot_provenance.cpp
//
// fixpp#215 item 1, Option C (`.specify/215-dictionary-view.md` §6 seams 1 +
// 2) — C4: a `SessionConfig::dict_snapshot` whose `source()` disagrees with
// `SessionConfig::dictionary` must be REJECTED at `open()`, not silently
// adopted. Under the retired `dictionary_view` field this was undetectable
// (recorded as `L-215-1`, now removed — see `B-215-2`,
// `spec/behaviors-and-limitations.md`) and would have silently driven
// inbound parsing/validation from the wrong grammar.
//
// NOTE on what could NOT be reproduced from the design doc: §6 seam 1
// specifies a "runtime-RED, today, on the unfixed tree" instrument written
// against the LEGACY `dictionary_view` field (mismatched view adopted on a
// bare null check, driven to Active, fed a discriminating frame). That field
// no longer exists in this tree — this round implements Option C in full in
// one pass rather than sequencing the runtime-RED as a separate
// pre-migration step, so the doc's specific historical instrument cannot be
// executed here. What IS proven RED (see the two TESTs below and their
// companion mutation check) is the NEW instrument: `Session::open()`'s
// provenance gate itself, by temporarily removing it and observing both
// tests below fail — see the Gate-B report for the exact observed output.
//
// The two dictionaries are SEPARATELY loaded (distinct `Dictionary` objects
// — provenance is `shared_ptr` POINTER identity, not value equality) and
// share `<fix major="4" minor="2">` (so `which_session_version()` is EQUAL,
// which is why a version compare could not have substituted for identity),
// disagreeing on exactly one rule: `OrderQty(38)` on `NewOrderSingle` is
// optional in A, required in B.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

// Build a minimal SOH-delimited FIX frame with correct BodyLength(9) + CheckSum(10).
std::vector<std::byte> make_raw_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) {
        body += extra_body;
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

std::vector<std::byte> make_logon_frame(std::uint32_t seq = 1, std::string_view sender = "TW",
                                        std::string_view target = "ISLD") {
    std::string extra;
    extra += "98=0\x01";
    extra += "108=30\x01";
    return make_raw_frame("FIX.4.2", "A", seq, sender, target, extra);
}

// NewOrderSingle(35=D) with ClOrdID/Side/TransactTime present, OrderQty(38)
// DELIBERATELY OMITTED — the discriminating field between dictionary A
// (optional) and dictionary B (required).
std::vector<std::byte> make_nos_frame_without_order_qty(std::uint32_t seq = 2) {
    std::string body;
    body += "11=ORD001\x01";
    body += "54=1\x01";
    body += "60=20240101-00:00:00.000\x01";
    return make_raw_frame("FIX.4.2", "D", seq, "TW", "ISLD", body);
}

std::string extract_field(std::span<const std::byte> frame, std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return {};
    }
    return wire.substr(pos, end - pos);
}

struct ProvenanceFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    ProvenanceFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::shared_ptr<const fixpp::dict::Dictionary> dict) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = std::move(dict);
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = true;
        return cfg;
    }

    // Returns the result of open() WITHOUT asserting it succeeded — the
    // mismatch case must fail, so the caller inspects the expected_t itself.
    auto run_open(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }

    bool has_reject_with_reason(int reason) const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "3") {
                auto r373 = extract_field(frame, 373);
                if (!r373.empty() && std::stoi(r373) == reason) {
                    return true;
                }
            }
        }
        return false;
    }
};

}  // namespace

// ============================================================================
// Seam 1 — provenance rejection: dict_snapshot->source() != cfg.dictionary
// must fail open() with invalid_session_config, before the session becomes
// observable (no Active, no wire traffic driven by the wrong grammar).
// ============================================================================
TEST(SessionDictSnapshotProvenance, MismatchedSnapshotRejectedAtOpen) {
    ProvenanceFixture fix;
    auto dict_a = fixpp::test_support::make_validation_test_dictionary();
    auto dict_b = fixpp::test_support::make_validation_test_dictionary_required_order_qty();
    ASSERT_NE(dict_a, dict_b) << "precondition: two SEPARATELY loaded Dictionary objects";

    auto cfg = fix.make_cfg(dict_a);
    // Snapshot minted from B, config's dictionary is A — a mismatched pair.
    cfg.dict_snapshot = fixpp::dict::make_dictionary_snapshot(dict_b);
    ASSERT_NE(cfg.dict_snapshot, nullptr);

    Session sess{fix.engine, cfg};
    auto r = fix.run_open(sess);

    ASSERT_FALSE(r.has_value()) << "a mismatched dict_snapshot must be rejected, not silently "
                                   "adopted (fixpp#215 C4 / B-215-2)";
    EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "a rejected open() must never reach a state where the wrong grammar could fire";
}

// ============================================================================
// Seam 2 — provenance acceptance: the ONLY legitimate pairing (snapshot
// derived from the SAME shared_ptr as cfg.dictionary) must not be refused,
// and the adopted view must be A's — proven not by state() alone (seam 6 /
// C5's lesson: reaching Active does not discriminate grammars) but by
// feeding the OrderQty-omitting frame that dictionary A accepts and
// dictionary B would reject.
// ============================================================================
TEST(SessionDictSnapshotProvenance, MatchingSnapshotAcceptedAtOpen) {
    ProvenanceFixture fix;
    auto dict_a = fixpp::test_support::make_validation_test_dictionary();

    auto cfg = fix.make_cfg(dict_a);
    cfg.dict_snapshot = fixpp::dict::make_dictionary_snapshot(dict_a);  // SAME shared_ptr as dictionary
    ASSERT_NE(cfg.dict_snapshot, nullptr);
    ASSERT_EQ(cfg.dict_snapshot->source(), cfg.dictionary);

    Session sess{fix.engine, cfg};
    auto r = fix.run_open(sess);
    ASSERT_TRUE(r.has_value()) << "the config's own dictionary paired with its own snapshot must "
                                  "not be refused (open() error: "
                               << (r.has_value() ? 0 : static_cast<int>(r.error())) << ")";

    // Drive to Active (initiator: open -> LogonSent -> feed peer Logon ack).
    fix.feed(sess, make_logon_frame());
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Feed the discriminating frame: NewOrderSingle without OrderQty(38).
    // Dictionary A declares it optional -> no reject. If the wrong (B)
    // grammar had somehow fired, this would reject with reason=1.
    fix.feed(sess, make_nos_frame_without_order_qty());

    EXPECT_FALSE(fix.has_reject_with_reason(1))
        << "the adopted view must be A's (OrderQty optional) — a Reject(373=1) here would mean "
           "B's grammar fired instead";
}

}  // namespace fixpp::session::test
