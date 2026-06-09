// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/happy/hp_fix44_restart_resume_test.cpp — 029 T002 [Setup] / T014 [Polish]
//
// Live restart-resume interop cells — both roles (W10 / SC-001 / SC-002 / SC-004).
//
// T014 (Polish): both-role live cells: restart a fixpp initiator (and separately
//   an acceptor) mid-session vs a running QFcpp/QFJ peer; resumes both counters
//   from the persisted FileStore; peer-ahead inbound recovers via ResendRequest
//   (enable_next_expected_msg_seq_num=true on the fixpp side so behind-side
//   tolerance applies — the knob-off Logon gate has no ResendRequest arm and
//   would fatal+reconnect, per SC-004 / W5 / L-029-1); no fatal too-low/too-high.
//
//   Cell group: RestartResume_Initiator (fixpp-INITIATOR restarts, QFcpp/QFJ acceptor)
//     Acceptance (counterparty-PRESENT):
//       (a) Session reaches Active after restart.
//       (b) Outbound seqnum resumes from the persisted value (> 1, not reset to 1).
//       (c) Inbound seqnum resumes from the persisted value.
//       (d) No fatal disconnect on the Logon gate (both directions).
//
//   Cell group: RestartResume_Acceptor (fixpp-ACCEPTOR restarts, QFcpp/QFJ initiator)
//     Acceptance (counterparty-PRESENT): same four assertions as above.
//
// LIVE CELLS: require a counterparty. INTEROP_REQUIRE_COUNTERPARTY skips with
// reason when the counterparty port env is absent (FR-023). Never a silent pass.
//
// Parent harness MUST configure the counterparty with (cross-repo follow-up):
//   QFcpp: EnableNextExpectedMsgSeqNum=Y + PersistMessages=Y in the session config
//   QFJ:   EnableNextExpectedMsgSeqNum=Y + PersistMessages=Y in the session settings
// AND must arrange a prior session run (to advance seqnums) before this cell
// connects as a restart. The pre-restart seqnum setup is a parent-repo harness
// orchestration concern (cross-repo, outside this submodule).
//
// Knob state: enable_next_expected_msg_seq_num=true on the fixpp side so the
// Logon gate's behind-side tolerance (789) covers the peer-ahead inbound recovery
// via ResendRequest rather than fatal+reconnect (SC-004, W5, L-029-1).
//
// Anchors: tasks.md T002 (Setup skeleton), T014 (Polish full); contracts/seqnum-hydrate.md
//          C2/C3; spec.md SC-001/SC-002/SC-004; data-model.md W10; research.md D-1/D-9.
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

// ── Local param-name formatters (alphanumeric+underscore only) ───────────────
// GoogleTest rejects names containing dashes (e.g. "quickfix-cpp").
// Mirrors hp_fix44_next_expected_test.cpp's local pattern.

std::string restart_resume_initiator_name(const ::testing::TestParamInfo<Counterparty>& info) {
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_init" : "QFj_init";
}

std::string restart_resume_acceptor_name(const ::testing::TestParamInfo<Counterparty>& info) {
    return (info.param == Counterparty::quickfix_cpp) ? "QFcpp_acc" : "QFj_acc";
}

// ── Cell 1: RestartResume_Initiator ──────────────────────────────────────────
//
// fixpp INITIATOR with a persistent FileStore restarts and connects to a live
// counterparty acceptor; both counters resume from the persisted store.
// Witness: FSM reaches Active + outbound seqnum > 1 (resumed, not reset).
//
// T014 full assertions are added at Polish phase. This skeleton SKIPs when no
// counterparty is present.
//
// Knob state: enable_next_expected_msg_seq_num=true so the Logon-gate behind-side
// tolerance admits a peer-ahead inbound via ResendRequest (SC-004 / W5 / L-029-1).

class RestartResume_Initiator : public ::testing::TestWithParam<Counterparty> {};

TEST_P(RestartResume_Initiator, BothCountersResumeFromStore) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    // Counterparty-required: skip-with-reason when absent (FR-023).
    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // T014 (Polish): full restart-resume assertions go here.
    // For T002 (Setup skeleton) we skip with an explicit placeholder message
    // to distinguish a skeleton skip from a counterparty-absent skip above.
    // The counterparty-PRESENT assertions (a)–(d) are added at T014.
    GTEST_SKIP() << "T002 skeleton — full assertions added at T014 (Polish phase)";
}

INSTANTIATE_TEST_SUITE_P(AllCounterparties, RestartResume_Initiator,
                         ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                         restart_resume_initiator_name);

// ── Cell 2: RestartResume_Acceptor ────────────────────────────────────────────
//
// fixpp ACCEPTOR with a persistent FileStore restarts; the counterparty INITIATOR
// reconnects; both counters resume from the persisted store.
// Witness: FSM reaches Active + outbound seqnum > 1 (resumed, not reset).
//
// T014 full assertions are added at Polish phase.

class RestartResume_Acceptor : public ::testing::TestWithParam<Counterparty> {};

TEST_P(RestartResume_Acceptor, BothCountersResumeFromStore) {
    const auto counterparty = GetParam();
    namespace hp = fixpp::interop::hp;

    INTEROP_REQUIRE_COUNTERPARTY(hp::counterparty_token(counterparty).c_str());

    const char* dir = hp::tls_fixture_dir();
    if (dir == nullptr || dir[0] == '\0') {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    // T014 (Polish): full restart-resume assertions go here.
    GTEST_SKIP() << "T002 skeleton — full assertions added at T014 (Polish phase)";
}

INSTANTIATE_TEST_SUITE_P(AllCounterparties, RestartResume_Acceptor,
                         ::testing::Values(Counterparty::quickfix_cpp, Counterparty::quickfix_j),
                         restart_resume_acceptor_name);

}  // namespace
