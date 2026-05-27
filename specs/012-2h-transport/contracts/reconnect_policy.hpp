// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::ReconnectPolicy — value type with QuickFIX/J-aligned
// schedule-array shape (Clarifications 2026-05-27 Q2=C). Re-emitted per the
// Appendix D §D.5 amendment that supersedes the v0.3 design-doc §4.4 5-field
// shape; design-doc-original {initial_delay, max_delay, multiplier,
// max_attempts, jitter} is REPLACED by {schedule, jitter, max_attempts}.
//
// Reference-engine convergence (Clarifications Q2 sweep):
//   - QuickFIX-cpp single fixed m_reconnectInterval = 30 (no jitter, no cap)
//   - QuickFIX/J int[] reconnectIntervalInSeconds indexed by failure count
//     with plateau-at-last per IoSessionInitiator::computeNextRetryConnectDelay
//     :318-319 (`if (index >= reconnectIntervalInMillis.length) millis =
//     reconnectIntervalInMillis[reconnectIntervalInMillis.length - 1]`).
//   - Fix8 single fixed _login_retry_interval + optional _login_retries cap
//     (0 = unbounded; no jitter).
//
// Hybrid C: adopt QFJ schedule-array API; ship v0.2 exponential as the
// defaults() materialised array; ship defaults_quickfix_compat() for
// industry-canonical operators. The constitutional [const §XV]
// thundering-herd defense is preserved in defaults() (jitter + cap);
// operators opt out via defaults_quickfix_compat() (no jitter, no cap).

#pragma once

#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace fixpp::transport {

// Reconnection back-off envelope for initiator-side sessions per
// [FIX-SL §4.3.1]. Consumed by the session-module Phase-4 spec FSM that drives
// reconnect; 2h owns the value type, the schedule helper, and the defaults
// factories. The FSM consults Clock::steady_now() (per [2d §7.9]) to schedule
// retries.
//
// Reconnect mints a FRESH Transport per attempt via TransportFactory::make(...)
// per Clarifications 2026-05-27 Q1=B — matching QuickFIX-cpp / QuickFIX/J /
// Fix8 industry pattern. The previous Transport is destroyed BEFORE the new
// one is requested; the FSM holds at most one live Transport per session at
// any time.
//
// Operators opt in to unbounded reconnect via max_attempts = 0 explicitly per
// [const §XII.5] no-implicit-default rule.
struct ReconnectPolicy {
    // Per-attempt delay table indexed by attempt-number. ≥ 1 entry. The LAST
    // entry serves as the PLATEAU for attempts beyond schedule.size() — matches
    // QuickFIX/J `IoSessionInitiator::computeNextRetryConnectDelay():318-319`
    // semantics.
    //
    // Allocator: against the engine's PMR root resource at construction; passed
    // in by the factory.
    std::pmr::vector<std::chrono::milliseconds> schedule;

    // ±jitter randomisation; range [0.0, 1.0]. 0.0 = deterministic; 1.0 =
    // full ±100% spread. defaults() = 0.10 (±10%); defaults_quickfix_compat() =
    // 0.0 (industry-canonical no-jitter). Deterministic-per-attempt (seeded
    // from session id + attempt number) for test repeatability per
    // [const §VII.7] fuzz determinism.
    double jitter {0.0};

    // Cap on total reconnect attempts before the FSM surfaces
    // transport_reconnect_limit_exceeded. 0 = UNBOUNDED (opt-in only).
    // defaults() = 10 (numeric ceiling per [const §XV] thundering-herd-pattern
    // intent); defaults_quickfix_compat() = 0 (industry-canonical no-cap).
    std::uint32_t max_attempts {0};

    // ── Schedule helper ────────────────────────────────────────────────────
    //
    // Returns schedule[std::min(attempt_n, schedule.size() - 1)] *
    //         (1.0 + uniform_real(-jitter, +jitter)).
    //
    // The plateau-at-last semantics match QuickFIX/J IoSessionInitiator::
    // computeNextRetryConnectDelay():318-319. Jitter is deterministic-per-
    // attempt (seeded from session id + attempt number) — see the .cpp body.
    [[nodiscard]] std::chrono::milliseconds
        delay_for_attempt(std::uint32_t attempt_n) const noexcept;

    // ── Defaults factories ──────────────────────────────────────────────────
    //
    // defaults() — v0.2 exponential schedule materialised:
    //   schedule = [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s,
    //               25.6s, 30s]
    //   jitter = 0.10  (±10%)
    //   max_attempts = 10
    //
    // Cumulative wall-clock envelope at max_attempts=10: 73-89 s (±10% jitter
    // spread per design-doc §1.1 corrected numeric; pre-jitter sum = 81 100 ms,
    // jitter widens both ways).
    //
    // The mr parameter is the engine PMR root used to allocate the schedule
    // vector; if null, the engine's default upstream resource is used.
    [[nodiscard]] static ReconnectPolicy
        defaults(std::pmr::memory_resource* mr = nullptr);

    // defaults_quickfix_compat() — industry-canonical QuickFIX-cpp / Fix8
    // shape: single fixed 30 s interval, no jitter, no cap:
    //   schedule = [30s]
    //   jitter = 0.0
    //   max_attempts = 0  (UNBOUNDED — matches QuickFIX-cpp's single
    //                      m_reconnectInterval default behaviour and Fix8's
    //                      0=unbounded default; operators opt out by also
    //                      setting max_attempts = N).
    //
    // The mr parameter is the engine PMR root used to allocate the
    // single-entry schedule vector.
    [[nodiscard]] static ReconnectPolicy
        defaults_quickfix_compat(std::pmr::memory_resource* mr = nullptr);
};

}  // namespace fixpp::transport
