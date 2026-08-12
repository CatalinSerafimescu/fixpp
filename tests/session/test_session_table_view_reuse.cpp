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
// assertions therefore fail on `main` and pass here. See the PR body for the
// recorded pre-fix readings.
//
// Anchors: fixpp#215 item 1; 083 T049/T050 (W-11a seam, C-9.2a);
//          066 T002 (Session inbound table_view); 041 T014 (validator build);
//          [const §XV.1] (config-time construction only).

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
#include <fixpp/dict/table_view.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dictionary_internal.hpp"  // 083 T049 W-11a seam: as_table_view_call_count()
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
// W1 — open() with NO pre-built view walks the Dictionary exactly ONCE.
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
    ASSERT_EQ(cfg.dictionary_view, nullptr) << "precondition: config supplies no view";

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
// W2 — open() with a config-supplied view walks the Dictionary ZERO times, and
// ADOPTS that exact object.
//
// The count alone would not distinguish "adopted the supplied view" from
// "adopted it and also kept a private copy of the tables"; use_count() pins the
// identity. Together they are the whole C-ABI claim: `fixpp_session_open` builds
// ONE view (pinned separately by 083 W-11a in tests/capi) and passes that object
// through `SessionConfig::dictionary_view`, so the C-ABI total is 1, not 3.
// ============================================================================
TEST(SessionTableViewReuse, OpenAdoptsAConfigSuppliedViewAndWalksZeroTimes) {
    Fixture fix;
    auto cfg = fix.make_cfg();

    // Stand in for the C-ABI: build the ONE view up front, from cfg.dictionary
    // (the field's derivation requirement), and hand it over.
    auto tv = std::make_shared<const fixpp::dict::table_view>(cfg.dictionary->as_table_view());
    cfg.dictionary_view = tv;
    long const use_count_before = tv.use_count();  // this test + cfg

    fixpp::dict::detail::reset_as_table_view_call_count();

    Session sess{fix.engine, cfg};
    fix.run_open(sess);

    EXPECT_EQ(fixpp::dict::detail::as_table_view_call_count(), 0u)
        << "fixpp#215 item 1: a config that already carries a view must make open() walk the "
           "Dictionary ZERO further times. This reads 1 on the unfixed tree, where SessionConfig "
           "had no field to carry a view and open() always built its own.";

    EXPECT_GT(tv.use_count(), use_count_before)
        << "the session must hold a strong reference to THE SUPPLIED view object — a count of "
           "zero new walks paired with a dropped reference would mean open() had silently "
           "stopped resolving a view at all.";
}

// ============================================================================
// W3 — the adopted view is WIRED, not merely stored.
//
// Reaching Active requires the peer Logon to be parsed through `inbound_tv_`
// and to survive the strict validator built beside it. If open() stored the
// supplied view without binding the parser and validator to it, W2 would still
// pass (zero walks, one reference) and this would not.
// ============================================================================
TEST(SessionTableViewReuse, AdoptedViewDrivesInboundParsingAndValidation) {
    Fixture fix;
    auto cfg = fix.make_cfg();
    cfg.dictionary_view =
        std::make_shared<const fixpp::dict::table_view>(cfg.dictionary->as_table_view());

    Session sess{fix.engine, cfg};
    fix.run_open(sess);

    auto const logon = make_logon_frame();
    fix.feed(sess, logon);

    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "a session opened over a config-supplied table_view must parse and validate inbound "
           "traffic exactly as one that built its own.";
}

}  // namespace fixpp::session::test
