// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// CONTRACT FRAGMENT — ONE of the two genuine public-surface deltas in feature
// 014-transport-active-binding (this error-enum append; the other is the
// TransportFactory::cert_source_snapshot() pure-virtual promotion, documented
// in realized-behavior.md C4). Published by /speckit-plan (Phase 1). The
// implementing change lands in include/fixpp/core/error.hpp. NO behavioural
// change is permitted between this contract and the implementing header
// without re-running Gate A.
//
// ── error::session_seqnum_too_high (FR-016) ──────────────────────────────────
// Append ONE variant to the `error` enum, after the highest 013 slot
// (session_invalid_argument = 119). 012 occupies 94..115; 013 occupies
// 116..119; the next free slot is 120. The /speckit-implement step MUST
// cross-check the actual error.hpp to confirm the boundary is still 119
// (no +-N drift) before assigning 120. NEVER renumber an existing slot.
//
// Append-only per [const §X.4]. This is a C++ enum value ONLY — it is NOT a
// fixpp_error_t C-ABI symbol (the 2i C-ABI mapping is a later feature), so it
// triggers no abidiff and emits no extern "C" symbol ([const §IX.5]/§X.2).

namespace fixpp::core {

enum class error : /* underlying type unchanged */ {
    // ... existing variants 0..119 UNCHANGED ...
    //   slot 70  — PERMANENT HOLE (session_seqnum_gap_unrecoverable, deleted 013 T006a; never reused)
    //   slot 74  — session_test_request_unanswered: KEEPS its real meaning
    //              (FR-004 inbound-liveness window). 014 stops MISUSING it as
    //              the seqnum-too-high stand-in (see below); slot 74 itself is
    //              unchanged and still emitted for genuine liveness timeouts.
    //   slot 115 — transport_accept_cancelled (012 boundary)
    //   slot 116 — session_seqnum_reset_mismatch (013)
    //   slot 117 — session_compid_unauthorized   (013)
    //   slot 118 — session_testreqid_mismatch    (013)
    //   slot 119 — session_invalid_argument       (013)

    session_seqnum_too_high = 120,   // NEW (014, FR-016): a semantically-correct
                                     // inbound seqnum-too-high gap, replacing the
                                     // slot-74 (session_test_request_unanswered)
                                     // stand-in returned by
                                     // SeqnumManager::check_inbound's too-high
                                     // branch (src/session/seqnum_manager.cpp:77).
};

}  // namespace fixpp::core

// ── Consumer-impact note (zero behavioural change) ───────────────────────────
// The too-high branch is reachable only in LogonSent / LogonReceived handshake
// states (Active-state too-high is intercepted earlier -> AwaitingResend per
// 013 FR-009). All THREE production callers of check_inbound discard the
// returned code and simply disconnect:
//     src/session/session.cpp:904   (LogonSent)
//     src/session/session.cpp:1261  (Active warm-up)
//     src/session/session.cpp:1703  (LogonReceived)
// So swapping 74 -> 120 changes no observable session output today; it removes
// a latent landmine (a future consumer wiring the code into a SessionEvent/log
// would otherwise surface a phantom "TestRequest unanswered"). The
// seqnum-manager test assertion at tests/session/seqnum_manager_test.cpp:145
// flips from session_test_request_unanswered to session_seqnum_too_high.
