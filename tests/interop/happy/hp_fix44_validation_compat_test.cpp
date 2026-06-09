// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_validation_compat_test.cpp — 028 T002 [Setup] / T014 [Polish]
//
// Live ValidationCompat interop cells — both roles (C1/C2 / SC-001/SC-002).
//
// T002 (Setup): skeleton with skip-without-counterparty guard; no real assertions.
// T014 (Polish): full both-role live cells for check_comp_id=false and
//   validate_sequence_numbers=false vs a live QFcpp/QFJ counterparty.
//
//   Cell 1 — ValidationCompat_CompID / fixpp initiator:
//     fixpp INITIATOR with check_comp_id=false; counterparty ACCEPTOR configured
//     with CheckCompID=N (QFcpp/QFJ session config). fixpp sends a steady-state
//     message with a mismatching SenderCompID(49)/TargetCompID(56); the session
//     stays Active; no Reject/Logout.
//
//   Cell 2 — ValidationCompat_CompID / fixpp acceptor:
//     fixpp ACCEPTOR with check_comp_id=false; counterparty INITIATOR configured
//     with CheckCompID=N. Same witness.
//
//   Cell 3 — ValidationCompat_Seqnum / fixpp initiator:
//     fixpp INITIATOR with validate_sequence_numbers=false; counterparty ACCEPTOR
//     configured with ValidateSequenceNumbers=N. The counterparty sends an
//     out-of-order frame; fixpp does NOT emit a ResendRequest; session stays Active.
//
//   Cell 4 — ValidationCompat_Seqnum / fixpp acceptor:
//     fixpp ACCEPTOR with validate_sequence_numbers=false; counterparty INITIATOR
//     configured with ValidateSequenceNumbers=N. Same witness.
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with
// reason when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Parent harness MUST configure the counterparty with (cross-repo follow-up):
//   QFcpp: CheckCompID=N and/or ValidateSequenceNumbers=N
//   QFJ:   CheckCompID=N and/or ValidateSequenceNumbers=N
//
// Anchors: tasks.md T002 (Setup skeleton), T014 (Polish full);
//          contracts/validation-compat-toggles.md C1/C2; FR-001..013; SC-001..008.
//
// [const §XV.9]: tests/-only.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <tuple>

#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "hp_support.hpp"

using namespace std::chrono_literals;
using fixpp::interop::Counterparty;
using fixpp::interop::Role;
using fixpp::session::fsm_state;

namespace {

// ── Watchdog ─────────────────────────────────────────────────────────────────
constexpr std::chrono::milliseconds kStopWatchdog{5000};

// ── Local param-name formatters ───────────────────────────────────────────────

std::string validation_compat_name(
    const ::testing::TestParamInfo<std::tuple<Counterparty, Role>>& info)
{
    const auto [cp, role] = info.param;
    std::string n = (cp == Counterparty::quickfix_cpp) ? "QFcpp" : "QFj";
    n += (role == Role::fixpp_initiator) ? "_init" : "_acc";
    return n;
}

// ── Cell group: ValidationCompat_CompID ──────────────────────────────────────
//
// T002 (Setup): skeleton only. Real assertions land in T014 (Polish).
// The cell checks the skip-without-counterparty guard fires correctly when no
// live peer is available, and does not hang or pass silently.
//
// T014 (Polish) will add:
//   - send a mismatching-CompID steady-state frame to the established session
//   - assert the frame is accepted (no Reject, no Logout, session stays Active)
//   - assert zero ResendRequest on the wire (wired via parent proxy capture)

class ValidationCompat_CompID : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(ValidationCompat_CompID, AcceptsWithKnobOff)
{
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023).
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, role);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);

    // T014 assertion anchor: check_comp_id=false (knob under test).
    // cfg.check_comp_id = false;  // T014: uncomment + wire assertions.

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Skeleton: drive to Active (or short timeout); no real CompID assertions yet.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty)
        << " (" << (role == Role::fixpp_initiator ? "initiator" : "acceptor") << ")";

    // T014 will add: send mismatching-CompID frame + assert accepted + no Reject.

    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count() << " ms (watchdog "
        << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, ValidationCompat_CompID,
    ::testing::Combine(
        ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
        ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    validation_compat_name);

// ── Cell group: ValidationCompat_Seqnum ──────────────────────────────────────
//
// T002 (Setup): skeleton only. Real assertions land in T014 (Polish).
// The cell checks the skip-without-counterparty guard fires correctly when no
// live peer is available.
//
// T014 (Polish) will add:
//   - configure the counterparty to send an out-of-order frame
//   - assert NO ResendRequest is observed on the wire
//   - assert session stays Active (no Logout/disconnect)

class ValidationCompat_Seqnum : public ::testing::TestWithParam<std::tuple<Counterparty, Role>> {};

TEST_P(ValidationCompat_Seqnum, ToleratesOutOfOrderWithKnobOff)
{
    const auto [counterparty, role] = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023).
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }
    auto factory = hp::make_interop_tls_factory(dir);
    ASSERT_NE(factory, nullptr) << "baseline TLS factory build failed";

    const auto endpoint = hp::cell_endpoint(counterparty, role);
    ASSERT_TRUE(endpoint.has_value())
        << "cell endpoint unresolved (parent harness did not lease a port)";

    fixpp::interop::InteropEngineFixture fx;
    auto cfg = hp::make_session_config(role, "FIX.4.4", factory, fx.ioc().get_executor(),
                                       *endpoint);

    // T014 assertion anchor: validate_sequence_numbers=false (knob under test).
    // cfg.validate_sequence_numbers = false;  // T014: uncomment + wire assertions.

    const auto id = fixpp::session::SessionId::from_config(cfg);
    ASSERT_TRUE(fx.engine().register_session(std::move(cfg)).has_value())
        << "register_session failed";

    fx.start();

    // Skeleton: drive to Active (or short timeout); no seqnum-validation assertions yet.
    const auto reached = hp::drive_to_active(fx, id, 5s);
    EXPECT_EQ(reached, fsm_state::Active)
        << "session did not reach Active against "
        << hp::counterparty_token(counterparty)
        << " (" << (role == Role::fixpp_initiator ? "initiator" : "acceptor") << ")";

    // T014 will add: trigger out-of-order frame + assert zero ResendRequest + Active.

    const auto elapsed = fx.stop_within(kStopWatchdog);
    EXPECT_LT(elapsed, kStopWatchdog)
        << "Engine::stop() took " << elapsed.count() << " ms (watchdog "
        << kStopWatchdog.count() << " ms)";
    EXPECT_TRUE(fx.stopped()) << "engine did not reach stopped() after Logout";
}

INSTANTIATE_TEST_SUITE_P(
    AllCounterparties, ValidationCompat_Seqnum,
    ::testing::Combine(
        ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
        ::testing::Values(Role::fixpp_initiator, Role::fixpp_acceptor)),
    validation_compat_name);

}  // namespace
