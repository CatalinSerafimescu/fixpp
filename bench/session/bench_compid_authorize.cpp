// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/bench_compid_authorize.cpp — T033 [P] [US2] Phase 4
//
// CompIdAuthorizationPolicy::authorize(peer_identity, asserted_compid) bench.
//
// Ceiling per plan.md §Performance Goals / SC-003 / [const §VIII.2]:
//   authorize() lookup ≤ 5 µs p99 (sub-microsecond fraction of the
//   ≤ 5 ms p99 handshake-to-Logon-reject latency budget).
//
// Storage: std::unordered_map<principal, compid_set> backed by the pimpl's
// allocator; in this bench we use the default allocator (policy-level
// monotonic arena is not needed for the lookup itself — the hot path is a
// hash lookup + linear scan of a typically small compid set).
//
// Two benchmark cases:
//   BM_CompidAuthorize_HitCN    — CN-based principal matches (success path).
//   BM_CompidAuthorize_MissPN   — Unrecognized principal (miss path; returns
//                                  session_compid_unauthorized).
//
// CI gate: > 5 µs p99 = failure. ±5% regression per [const §VIII.2];
// baselines under bench/baselines/session/.
//
// Anchors: plan.md T033; spec.md §US2 AC5 / SC-003; [const §VIII.2].

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/tls/peer_identity.hpp>

namespace {

// ── Shared state setup ────────────────────────────────────────────────────────

// A pre-built peer_identity with CN "ACME-PROD-01".
static fixpp::tls::peer_identity make_cn_pid() {
    fixpp::tls::peer_identity pid;
    pid.subject_dn = "CN=ACME-PROD-01,O=Acme Corp,C=US";
    return pid;
}

// A policy with a single binding: "ACME-PROD-01" → "ACME01".
static fixpp::session::CompIdAuthorizationPolicy make_policy() {
    fixpp::session::CompIdAuthorizationPolicy p;
    p.add_binding("ACME-PROD-01", "ACME01");
    return p;
}

// ── BM_CompidAuthorize_HitCN ─────────────────────────────────────────────────
//
// Measures: successful authorize() where the CN matches the only binding.
// Expected: ≤ 5 µs p99 (sub-µs on warm cache — the hot path is a hash lookup
// + string comparison).

void BM_CompidAuthorize_HitCN(benchmark::State& state) {
    const auto policy = make_policy();
    const auto pid    = make_cn_pid();

    for (auto _ : state) {
        auto result = policy.authorize(pid, "ACME01");
        // Prevent optimizer from eliding the call.
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_CompidAuthorize_HitCN)->Unit(benchmark::kMicrosecond)->MinTime(0.5);

// ── BM_CompidAuthorize_MissPN ─────────────────────────────────────────────────
//
// Measures: miss path — principal extracted from pid is NOT in the policy.
// Expected: comparable to hit path (one hash lookup, one miss).

void BM_CompidAuthorize_MissPN(benchmark::State& state) {
    const auto policy = make_policy();

    // Peer with a different CN — not in the policy.
    fixpp::tls::peer_identity pid_miss;
    pid_miss.subject_dn = "CN=STRANGER-01,O=Unknown,C=US";

    for (auto _ : state) {
        auto result = policy.authorize(pid_miss, "STRANGER");
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_CompidAuthorize_MissPN)->Unit(benchmark::kMicrosecond)->MinTime(0.5);

// ── BM_CompidAuthorize_EmptyPolicy ───────────────────────────────────────────
//
// Empty policy (default-deny path). Should be the fastest case (early return
// on empty map). Documents the floor cost.

void BM_CompidAuthorize_EmptyPolicy(benchmark::State& state) {
    const fixpp::session::CompIdAuthorizationPolicy policy;  // empty
    const auto pid = make_cn_pid();

    for (auto _ : state) {
        auto result = policy.authorize(pid, "ACME01");
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_CompidAuthorize_EmptyPolicy)->Unit(benchmark::kMicrosecond)->MinTime(0.5);

}  // namespace
