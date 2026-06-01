// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// src/transport/reconnect_policy.cpp — body of fixpp::transport::ReconnectPolicy.
//
// Anchors:
//   [2h §4.4]      — value-type shape post-Appendix-D §D.5 amendment.
//   [2h §6.6]:1190 — transport_reconnect_limit_exceeded (FSM-side, not here).
//   QFJ IoSessionInitiator::computeNextRetryConnectDelay():318-319 — plateau-at-last.
//   [const §VII.7] — fuzz determinism contract: same (session_id_seed, attempt_n)
//                    deterministically reproduces the delay.
//
// The body lives outside the header so the deterministic RNG implementation
// stays a private detail; downstream code depends only on the noexcept
// signature.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fixpp/transport/reconnect_policy.hpp>
#include <random>

namespace fixpp::transport {

namespace {

// Round `base * mult` to the nearest millisecond. Defensively saturates at
// zero on the impossible case mult < 0 (jitter > 1.0 is rejected at the
// factory site, but the helper is noexcept and must not return negative).
[[nodiscard]] std::chrono::milliseconds scale_ms(std::chrono::milliseconds base,
                                                 double mult) noexcept {
    if (mult < 0.0) {
        return std::chrono::milliseconds{0};
    }
    const double scaled = static_cast<double>(base.count()) * mult;
    return std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(scaled + 0.5)};
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// delay_for_attempt
//
// schedule[std::min(attempt_n, schedule.size() - 1)] *
//     (1.0 + uniform_real(-jitter, +jitter))
//
// Plateau-at-last per QFJ IoSessionInitiator::computeNextRetryConnectDelay
// :318-319. Jitter RNG is reseeded per call from (session_id_seed, attempt_n)
// so the same pair always returns the same delay — fuzz-determinism per
// [const §VII.7].
// ─────────────────────────────────────────────────────────────────────────────
std::chrono::milliseconds ReconnectPolicy::delay_for_attempt(
    std::uint32_t attempt_n) const noexcept {
    if (schedule.empty()) {
        // Defensive: factories guarantee schedule.size() >= 1. A user-constructed
        // policy with an empty schedule lands here; surface zero rather than
        // index UB.
        return std::chrono::milliseconds{0};
    }
    const auto last_idx = static_cast<std::uint32_t>(schedule.size() - 1);
    const auto idx = (attempt_n < last_idx) ? attempt_n : last_idx;
    const auto base = schedule[idx];

    if (jitter <= 0.0) {
        return base;
    }

    // seed_seq mixes both 32-bit halves of session_id_seed with attempt_n so
    // every (seed, attempt) pair gets its own minstd_rand state. minstd_rand is
    // small + deterministic; replay across architectures matches the
    // [const §VII.7] requirement.
    const auto lo = static_cast<std::uint32_t>(session_id_seed & 0xFFFFFFFFU);
    const auto hi = static_cast<std::uint32_t>(session_id_seed >> 32);
    std::seed_seq sseq{lo, hi, attempt_n};
    std::minstd_rand rng(sseq);
    std::uniform_real_distribution<double> dist(-jitter, +jitter);

    return scale_ms(base, 1.0 + dist(rng));
}

// ─────────────────────────────────────────────────────────────────────────────
// defaults
//
// v0.2 exponential schedule materialised against the engine's PMR root.
// [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s] —
// pre-jitter sum 81 100 ms, ±10% jitter → 73-89 s envelope at attempt=10.
// ─────────────────────────────────────────────────────────────────────────────
ReconnectPolicy ReconnectPolicy::defaults(std::pmr::memory_resource* mr) {
    using ms = std::chrono::milliseconds;
    auto* res = mr ? mr : std::pmr::get_default_resource();
    return ReconnectPolicy{
        .schedule =
            std::pmr::vector<ms>{
                {ms{100}, ms{200}, ms{400}, ms{800}, ms{1600}, ms{3200}, ms{6400}, ms{12800},
                 ms{25600}, ms{30000}},
                std::pmr::polymorphic_allocator<ms>{res},
            },
        .jitter = 0.10,
        .max_attempts = 10,
        .session_id_seed = 0,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// defaults_quickfix_compat
//
// {schedule = [30s], jitter = 0.0, max_attempts = 0 (unbounded)} —
// industry-canonical QuickFIX-cpp / QuickFIX/J / Fix8 shape.
// ─────────────────────────────────────────────────────────────────────────────
ReconnectPolicy ReconnectPolicy::defaults_quickfix_compat(std::pmr::memory_resource* mr) {
    using ms = std::chrono::milliseconds;
    auto* res = mr ? mr : std::pmr::get_default_resource();
    return ReconnectPolicy{
        .schedule =
            std::pmr::vector<ms>{
                {ms{30000}},
                std::pmr::polymorphic_allocator<ms>{res},
            },
        .jitter = 0.0,
        .max_attempts = 0,
        .session_id_seed = 0,
    };
}

}  // namespace fixpp::transport
