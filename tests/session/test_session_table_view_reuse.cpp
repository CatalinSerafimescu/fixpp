// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_session_table_view_reuse.cpp
//
// fixpp#215 item 1 — `Dictionary::as_table_view()` must be walked ONCE per
// opened session, not once per consumer of the view.
//
// `as_table_view()` has no cache: every call is a full walk of every message,
// group and field, and since 083 it also builds the per-context delimiter
// store. Before this change `Session::open()` walked it TWICE on its own
// (`inbound_tv_`, then the strict validator's owned copy), and a C-ABI session
// paid a THIRD walk because `fixpp_session_open` had already built its own view
// for the outbound commit path and had no way to hand it over.
//
// The instrument is 083 T049's W-11a seam (`as_table_view_call_count()`,
// src/dictionary/dictionary_internal.hpp) — the same counter that pins "zero
// rebuilds per message". A wall-clock measurement would be the wrong tool here:
// the defect is a COUNT of walks, and the count is exactly what the counter
// reports, with no host-variance term.
//
// NON-VACUITY (why these numbers are evidence and not decoration): on the
// unfixed tree W1 reads 2, not 1, and W2 reads 1, not 0 — `open()` ignored any
// pre-built view because `SessionConfig` had no field to carry one. Both
// assertions therefore fail on the pre-fixpp#215 tree and pass here.
//
// fixpp#215 item 1, Option C (`.specify/215-dictionary-view.md`) — W1/W2
// migrated from the retired `dictionary_view` field to
// `SessionConfig::dict_snapshot` (a `shared_ptr<const dictionary_snapshot>`
// minted by `fixpp::dict::make_dictionary_snapshot`); W2's identity pin now
// tracks the SNAPSHOT's refcount (open() adopts an ALIASING view into it via
// `shared_dictionary_view`, so the snapshot's control block is shared with
// `inbound_tv_`, not copied).
//
// W3 replaced (Gate B round-1 triage, finding C5): the original W3 fed a
// group-FREE Logon and asserted only `state() == Active`, which cannot
// distinguish a dictionary-backed parse from the dictionary-free
// `Parser() = default` ctor — reaching Active proves the view was stored, not
// that it was WIRED into parsing. W3 now dispatches a group-bearing
// ExecutionReport (NoLegs(555) x2 + a trailing outer field) through a
// CONFIG-SUPPLIED snapshot and asserts the callback observes the
// membership-bounded slices (2 legs, trailing field correctly excluded from
// the last leg) — a boundary only dictionary-backed parsing produces.
//
// Anchors: fixpp#215 item 1; 083 T049/T050 (W-11a seam, C-9.2a);
//          066 T002 (Session inbound table_view); 041 T014 (validator build);
//          [const §XV.1] (config-time construction only);
//          Gate B round-1 triage finding C5 (W3 group-blindness).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/wire/view.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dictionary_internal.hpp"  // 083 T049 W-11a seam: as_table_view_call_count()
#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

// Minimal SOH-delimited frame builder — only the Logon this file needs, to
// reach Active and prove the view is WIRED and not merely stored.
std::vector<std::byte> make_logon_frame() {
    std::string body;
    body += "35=A\x01";
    body += "34=1\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";

    std::string full = "8=FIX.4.2\x01" "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

struct Fixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    Fixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // validate_inbound_messages=true deliberately: that is the arm that builds
    // the SECOND view inside open(), so it is the arm the count pin must cover.
    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_validation_test_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = true;
        return cfg;
    }

    void run_open(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    }

    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }
};

}  // namespace

// ============================================================================
// W1 — open() with NO pre-built snapshot walks the Dictionary exactly ONCE.
//
// Reads 2 on the unfixed tree: `inbound_tv_` and the strict validator each
// called `cfg_.dictionary->as_table_view()` independently. The validator still
// owns its table_view BY VALUE — that is frozen ("SC-007: no virtual edge",
// validator.hpp) and untouched here — but it is now COPY-constructed from the
// view open() already resolved, rather than re-derived from the Dictionary.
// ============================================================================
TEST(SessionTableViewReuse, OpenWalksTheDictionaryExactlyOnce) {
    Fixture fix;
    auto cfg = fix.make_cfg();
    ASSERT_EQ(cfg.dict_snapshot, nullptr) << "precondition: config supplies no snapshot";

    // Reset AFTER building the config: constructing the fixture Dictionary must
    // not be attributed to open().
    fixpp::dict::detail::reset_as_table_view_call_count();

    Session sess{fix.engine, cfg};
    fix.run_open(sess);

    EXPECT_EQ(fixpp::dict::detail::as_table_view_call_count(), 1u)
        << "fixpp#215 item 1: open() must walk the Dictionary ONCE and share the result with "
           "the validator, not walk it once per consumer. This reads 2 on the unfixed tree.";
}

// ============================================================================
// W2 — open() with a config-supplied snapshot walks the Dictionary ZERO
// times, and ADOPTS an aliased view of THAT exact snapshot.
//
// The count alone would not distinguish "adopted the supplied snapshot" from
// "adopted it and also kept a private copy of the tables"; the snapshot's
// use_count() pins identity (open() adopts via the aliasing
// shared_dictionary_view() helper, which shares the snapshot's control block
// rather than copying the table_view — §6 seam 4). Together they are the
// whole C-ABI claim: `fixpp_session_open` mints ONE snapshot (pinned
// separately by 083 W-11a in tests/capi) and passes it through
// `SessionConfig::dict_snapshot`, so the C-ABI total is 1 walk, not 3.
// ============================================================================
TEST(SessionTableViewReuse, OpenAdoptsAConfigSuppliedSnapshotAndWalksZeroTimes) {
    Fixture fix;
    auto cfg = fix.make_cfg();

    // Stand in for the C-ABI: mint the ONE snapshot up front, from
    // cfg.dictionary (the field's derivation requirement), and hand it over.
    auto snap = fixpp::dict::make_dictionary_snapshot(cfg.dictionary);
    ASSERT_NE(snap, nullptr);
    cfg.dict_snapshot = snap;

    // Construct FIRST, then sample. `Session` stores a SessionConfig BY VALUE
    // (session.hpp: "constructing `SessionConfig cfg_;` from a ..."), so
    // construction alone bumps the count by one. Sampling before construction
    // would make the assertion below pass whether or not open() adopted
    // anything — a count identity that proves nothing.
    Session sess{fix.engine, cfg};
    long const use_count_before_open = snap.use_count();

    fixpp::dict::detail::reset_as_table_view_call_count();

    fix.run_open(sess);

    EXPECT_EQ(fixpp::dict::detail::as_table_view_call_count(), 0u)
        << "fixpp#215 item 1: a config that already carries a snapshot must make open() walk the "
           "Dictionary ZERO further times. This reads 1 on the unfixed tree, where SessionConfig "
           "had no field to carry a snapshot and open() always built its own.";

    EXPECT_GT(snap.use_count(), use_count_before_open)
        << "open() must take a strong reference (via shared_dictionary_view's aliasing "
           "shared_ptr, which shares the SNAPSHOT's control block) to THE SUPPLIED snapshot "
           "object. Sampled across open() ALONE (the construction copy is already in the "
           "baseline), so this rises only if inbound_tv_ was aliased from cfg_.dict_snapshot. "
           "Zero new walks paired with an unchanged count would mean open() had silently "
           "stopped resolving a view at all.";
}

// ============================================================================
// W3 — the adopted CONFIG-SUPPLIED snapshot drives GROUP boundaries, not
// merely a group-free parse (Gate B round-1 triage C5).
//
// The original W3 fed a group-free Logon and asserted only state()==Active,
// which a dictionary-FREE `Parser() = default` ctor also satisfies — reaching
// Active proves the view was stored, not that it was wired into parsing.
// This version feeds a group-bearing ExecutionReport (NoLegs(555) x2 +
// TRAILING TransactTime(60) after the group — the same shape
// test_066_validator_on_grouped_test.cpp uses to discriminate dict-backed
// from dict-free group extents) through a session opened with a
// CONFIG-SUPPLIED snapshot, and asserts the fromApp callback observes
// membership-bounded slices: exactly 2 leg instances, with the trailing
// field correctly excluded from the last one. A dictionary-free parse could
// not produce this boundary at all.
// ============================================================================
namespace {
class GroupCapturingApplication : public Application {
public:
    int from_app_calls = 0;
    std::size_t leg_count = 0;
    bool last_leg_has_trailing_tag60 = true;  // default true: un-run callback must not silently pass

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const SessionId& /*id*/) override {
        ++from_app_calls;
        auto slices = msg.offsets().group_slices(555);
        leg_count = slices.size();
        if (leg_count >= 1) {
            std::string_view last{reinterpret_cast<char const*>(slices[leg_count - 1].data),
                                  slices[leg_count - 1].len};
            last_leg_has_trailing_tag60 = last.find("60=") != std::string_view::npos;
        }
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>&,
        const SessionId&) override {
        return {};
    }
};
}  // namespace

TEST(SessionTableViewReuse, AdoptedSnapshotDrivesGroupBoundaries) {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;
    {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }
    auto app = std::make_shared<GroupCapturingApplication>();
    engine.application = app;

    SessionConfig cfg;
    cfg.sender_comp_id = "ISLD";
    cfg.target_comp_id = "TW";
    cfg.begin_string = "FIX.4.4";
    cfg.heartbeat_interval = 0s;  // disable liveness
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
    cfg.dict_snapshot = fixpp::dict::make_dictionary_snapshot(cfg.dictionary);
    ASSERT_NE(cfg.dict_snapshot, nullptr);
    cfg.executor_override = ioc.get_executor();
    cfg.transport_send = [&transport](std::span<const std::byte> frame) {
        transport.capture_outbound(frame);
    };
    cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;

    Session sess{engine, cfg};

    // open() -> our Logon out; feed the peer's Logon reply -> Active.
    {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        std::string logon_body = "35=A\x01" "34=1\x01" "49=TW\x01"
                                 "52=20240101-00:00:00.000\x01" "56=ISLD\x01"
                                 "98=0\x01" "108=0\x01";
        auto logon = fixpp_test_support::make_frame("FIX.4.4", logon_body);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto frame = fixpp_test_support::make_execution_report_frame(suffix, /*seq=*/2, "TW", "ISLD");
    {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }

    ASSERT_EQ(app->from_app_calls, 1)
        << "a config-supplied snapshot must reach fromApp for a well-formed group-bearing "
           "ExecutionReport";
    EXPECT_EQ(app->leg_count, 2u)
        << "the CONFIG-SUPPLIED snapshot must drive dictionary-backed group boundaries: "
           "NoLegs(555)=2 must resolve to exactly 2 membership-bounded leg slices. A "
           "dictionary-free Parser() ctor cannot produce this at all.";
    EXPECT_FALSE(app->last_leg_has_trailing_tag60)
        << "the trailing outer field TransactTime(60), AFTER the group, must NOT be absorbed "
           "into the last leg's membership-bounded slice — only dictionary-backed group extent "
           "resolution excludes it correctly.";
}

}  // namespace fixpp::session::test
