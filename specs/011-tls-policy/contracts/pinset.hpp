// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-006..010 + FR-009a.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.3 (Pinset add-then-remove rotation)
// + §6.2 (lock-free reader / serialised-writer publication) + §6.5
// (FIXS rotation invariants) + §6.5.2 (CONSOLIDATED mutex-choice rationale).
// Shapes below are re-emitted VERBATIM from 2g §4.3.
// Spec anchors: spec.md FR-006 (add/remove separate, no atomic-swap;
// SHA-256-of-DER fingerprint key per Clarify Q4), FR-007 (zero-alloc hot path),
// FR-008 (pin_view lifetime-bound), FR-009 (mid-session-mutable), FR-009a
// (per-counterparty granularity recommended per Clarify Q5), FR-010
// (max_pins = 16).

#pragma once

// ============================================================================
// Pin fingerprint = 32-byte SHA-256 of the leaf cert's DER encoding
// (Clarify Q4; [FIXS RC1 §5]).
// ============================================================================
//
// namespace fixpp::tls {
//
// using pin_fingerprint = std::array<std::byte, 32>;
//
// }  // namespace fixpp::tls

// ============================================================================
// pin — owned diagnostic record + 32-byte fingerprint.
//
// Re-emitted VERBATIM from 2g §4.3 lines 411-416. The v0.1 caller-owned-
// bytes-aliasing Certificate field is dropped; the snapshot only stores owned
// diagnostic fields (PMR-allocated copies of subject/SAN strings) so a long-
// lived shared_ptr<const pin_snapshot> can outlive the bytes the caller
// passed to add().
//
// CRITICAL (NEW-P1-4 close): pin carries diagnostic fields (subject_dn /
// san_dns / added_at) — they are NOT optional. The bundle's earlier
// `array<pin_fingerprint, max_pins>` shape silently retired the 2g round-3
// P1-1 PMR-resource-lifetime contract (Pinset::Config::mr outliving snapshots)
// and the operator's ability to see WHAT CERT matched/missed (SC-006 implicit
// dependency).
// ============================================================================
//
// namespace fixpp::tls {
//
// struct pin {
//     std::array<std::byte, 32>            sha256;       // SHA-256 fingerprint of the pinned leaf cert's DER bytes.
//     std::pmr::string                     subject_dn;   // PMR-copied at add() time from the caller-supplied Certificate.
//     std::pmr::vector<std::pmr::string>   san_dns;      // PMR-copied at add() time; bounded by Pinset::Config::max_pins indirectly + arena.
//     core::time_point                     added_at;     // Wall-clock UTC at add() time, sourced from effective_clock per [2d §7.9].
// };
//
// // pin_snapshot — re-emitted VERBATIM from 2g §4.3 line 421:
// //   using pin_snapshot = std::pmr::vector<pin>;
// // The snapshot is the immutable container readers acquire via snapshot()
// // or pin_view::snapshot_. ALWAYS allocated PMR; lifetime is the holding
// // shared_ptr's. The PMR backing resource is Pinset::Config::mr (or engine
// // default if null); see §4.6 ownership rules + §6.2 PMR-lifetime paragraph
// // for the round-3 P1-1 binding contract: mr MUST outlive every snapshot the
// // Pinset ever publishes, NOT merely the Pinset instance itself.
// using pin_snapshot = std::pmr::vector<pin>;
//
// }  // namespace fixpp::tls

// ============================================================================
// pin_view — value-typed lookup result for find(). Re-emitted VERBATIM from
// 2g §4.3 lines 427-433.
// ============================================================================
//
// namespace fixpp::tls {
//
// struct pin_view {
//     std::shared_ptr<const pin_snapshot> snapshot;                                   // pins the matched entry's lifetime (and the entire snapshot).
//     pin const*                          value [[clang::lifetimebound]] = nullptr;   // bounded by *this (i.e., by `snapshot`); attribute on same line as field per N-P3-1.
//
//     [[nodiscard]] bool found() const noexcept { return value != nullptr; }
//     explicit operator bool() const noexcept { return found(); }
// };
//
// }  // namespace fixpp::tls

// ============================================================================
// Pinset — mutable container with mid-session-mutable rotation. Re-emitted
// VERBATIM from 2g §4.3 lines 435-528.
// ============================================================================
//
// namespace fixpp::tls {
//
// class Pinset {
//  public:
//     struct Config {
//         // Lifetime contract (v0.4 / round-3 P1-1 close): `mr` MUST outlive
//         // every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes
//         // — i.e., the union of `Pinset` instance lifetime AND every reader-
//         // held snapshot lifetime, NOT merely the `Pinset` instance itself.
//         // The §6.5 binding contract permits a reader to hold a
//         // `shared_ptr<const pin_snapshot>` past `~Pinset()`; each `pin` in
//         // the snapshot carries `pmr::string` / `pmr::vector<pmr::string>`
//         // members whose allocators capture this resource at add() time and
//         // call deallocate() on it at snapshot teardown. The default path
//         // (null → engine-resolved `mr_`) is satisfied by construction — the
//         // engine's PMR resource per `[2d §4.4]` outlives every session and
//         // every Pinset that any session reaches via `[arch §5.6]`'s mid-
//         // session-mutable carve-out. Callers passing a user-owned
//         // `std::pmr::memory_resource*` (e.g., a user
//         // `monotonic_buffer_resource` for tests or a dedicated arena) MUST
//         // keep it alive for the full union of (a) every `shared_ptr<Pinset>`
//         // holder lifetime AND (b) every `pin_view` / `shared_ptr<const
//         // pin_snapshot>` reader lifetime — i.e., until the last reader-held
//         // snapshot drains. The §9 seam #18 (post-`~Pinset()` snapshot
//         // lifetime) exercises this contract under ASan + TSan.
//         std::pmr::memory_resource* mr {nullptr};
//         std::size_t                max_pins {16};   // §1.1 cap.
//     };
//
//     // [arch §6] rule-4 factory entry point.
//     [[nodiscard]] static core::expected_t<std::shared_ptr<Pinset>>
//         make_pinset(Config cfg, std::pmr::memory_resource* mr) noexcept;
//
//     explicit Pinset(Config cfg = {});
//     ~Pinset();
//
//     Pinset(Pinset const&) = delete;
//     Pinset& operator=(Pinset const&) = delete;
//     Pinset(Pinset&&) noexcept;
//     Pinset& operator=(Pinset&&) noexcept;
//
//     // (1) Add a pin. Succeeds and returns expected_t<void>{} unless:
//     //   - max_pins exceeded → unexpect{error::tls_pinset_capacity_exhausted};
//     //   - the pin (by SHA-256) is already present → unexpect{tls_pin_already_present}.
//     // The new pin is observable to the next find() that begins after add()
//     // returns (release-acquire ordering on the snapshot pointer; §6.2).
//     // OLD pins are NOT removed — that is the explicit remove() call.
//     // add(...) deep-copies the diagnostic strings (subject_dn / san_dns)
//     // into the snapshot's PMR arena per RC#1; the input Certificate is
//     // consumed for its SHA-256 + subject_dn + san list and its own storage
//     // is not retained.
//     //
//     // CRITICAL (Codex P1-2 + NEW-P1-4): the input is a Certificate (NOT a
//     // bare pin_fingerprint) per 2g §4.3 line 487 — the input is consumed
//     // for its diagnostic fields at add() time.
//     [[nodiscard]] core::expected_t<void>
//         add(Certificate const& cert);
//
//     // (2) Remove a pin (by SHA-256). Returns expected_t<void>{} on success;
//     //     unexpect{error::tls_pin_not_found} if absent. Removed pin is NOT
//     //     observable to any find() that begins after remove() returns
//     //     (release-acquire ordering; §6.2). Pinset § §4.3 binding contract:
//     //     remove() keys on the fingerprint; add() does NOT.
//     [[nodiscard]] core::expected_t<void>
//         remove(std::array<std::byte, 32> const& sha256);
//
//     // (3) Lookup-by-fingerprint on the handshake-hot path. v0.2 / RC#1
//     //     returns a value-typed pin_view that carries the snapshot
//     //     shared_ptr; the matched entry's lifetime is pinned for as long as
//     //     the caller holds the pin_view. Bounded latency (§6.3 row 1 split:
//     //     snapshot_acquire ≤ 30 ns + linear_scan_16 ≤ 100 ns; ≤ 130 ns
//     //     combined p99 at max_pins = 16).
//     [[nodiscard]] pin_view
//         find(std::array<std::byte, 32> const& sha256) const noexcept;
//
//     // (4) Explicit-snapshot accessor — the published reachability for
//     //     handshake-time pinset access. The TLS handshake (in 2h) captures
//     //     snapshot() ONCE before per-peer-cert lookup, then scans the
//     //     snapshot directly for the entire handshake (per the §6.5.1
//     //     BINDING CONTRACT — "TLS handshake-time pinset access MUST use
//     //     Pinset::snapshot() captured once at handshake start, and scan the
//     //     captured snapshot, not call find() repeatedly"). See
//     //     security_profile.hpp for the verify_peer signature change that
//     //     consumes the captured snapshot directly (NEW-P1-1 close).
//     [[nodiscard]] std::shared_ptr<const pin_snapshot>
//         snapshot() const noexcept;
//
//     // Diagnostic / test-only.
//     [[nodiscard]] std::size_t size() const noexcept;
//     [[nodiscard]] bool        contains(std::array<std::byte, 32> const& sha256) const noexcept;
//
//  private:
//     // Config                                                  cfg_;
//     // std::pmr::memory_resource*                              mr_;          // Resolved at construction.
//     // mutable std::shared_mutex                               writer_;      // serialises add() / remove() against each other; readers do NOT take it. See §6.5.2 for the consolidated [const §XV.9] rationale.
//     // std::atomic<std::shared_ptr<const pin_snapshot>>        snapshot_;    // immutable snapshot; replaced (release) on every add/remove.
// };
//
// }  // namespace fixpp::tls

// ============================================================================
// §6.5.2 std::shared_mutex consolidated rationale — single source of truth.
//
// Per NEW-P2-6 in the Opus adversarial review and 2g §6.5.2 lines 968-978:
// the rationale is CONSOLIDATED at 2g §6.5.2 (one site), with all other
// surfaces carrying only an INHERITANCE PIN pointing here. The v0.1 N-P1-3
// burn ("three-rationales-in-three-sites") was explicitly closed by 2g §6.5.2;
// the bundle must not re-introduce the anti-pattern by re-deriving the
// rationale at plan.md / data-model.md / research.md / pinset.hpp. This
// header-level commentary IS the rationale anchor for the bundle; every other
// site cites `[2g §6.5.2]` and STOPS, per NEW-P2-6 fix.
//
// 2g §6.5.2 verbatim (consolidated three-point reasoning):
//
//   1. The TYPE used is std::shared_mutex, NOT std::mutex. [const §XV.9] line
//      215 binds `std::mutex in coroutine context`; [const §XI.3] line 146
//      binds plain std::mutex in any header that includes asio::awaitable<>.
//      The Pinset header declares `mutable std::shared_mutex writer_;` (§4.3).
//      Neither rule binds against std::shared_mutex. [2f §6.6]'s
//      enforcement-of-[const §XV.9] grep gate scans for <mutex> (or std::mutex
//      declaration) — it does not fire on <shared_mutex> / std::shared_mutex.
//
//   2. The USE is fully synchronous. Pinset::add(...) / Pinset::remove(...)
//      return expected_t<void> directly — they are NOT awaitables. There is
//      no coroutine context across the writer lock by API construction.
//
//   3. The READER path is lock-free. Pinset::find / Pinset::snapshot do an
//      acquire-load on std::atomic<std::shared_ptr<const pin_snapshot>> and
//      never touch writer_. Readers do not contend with writers; the choice
//      of writer mutex only affects writer-vs-writer contention, which is
//      operationally vanishing (one rotation per cert-renewal cycle ≈ 30-90
//      days; §1.1).
//
// Together: std::shared_mutex satisfies [const §XV.9] (the rule binds
// std::mutex, not std::shared_mutex), satisfies [2f §6.6]'s grep gate by
// construction (no <mutex> include, no std::mutex use), and is the simplest
// correct primitive for the writer-only synchronisation actually needed.
// async_mutex would introduce an awaitable surface where the operation is
// synchronous — contradicting simplicity-first and adding cost to every
// writer call without measurable benefit.
// ============================================================================

// ============================================================================
// Per-counterparty granularity (FR-009a / Clarify Q5 — recommended pattern):
//
//   std::shared_ptr<Pinset> counterparty_X_pins = std::make_shared<Pinset>();
//   counterparty_X_pins->add(counterparty_current_leaf_cert);  // pass Certificate
//
//   SessionConfig session_a;  session_a.counterparty_id = "X"; session_a.pinset = counterparty_X_pins;
//   SessionConfig session_b;  session_b.counterparty_id = "X"; session_b.pinset = counterparty_X_pins;
//
//   // Rotation applies atomically to both sessions:
//   counterparty_X_pins->add(counterparty_new_leaf_cert);
//   // ... counterparty cuts over ...
//   counterparty_X_pins->remove(sha256_of_their_old_leaf);
//
// Engine-wide sharing across DIFFERENT counterparties is discouraged
// (conflates trust authorities) — see Clarifications 2026-05-23.
// ============================================================================

// ============================================================================
// Contract assertions (verified at /speckit-verify):
//
//   1. add(Certificate const&) is the binding signature per 2g §4.3 line 487
//      (Codex P1-2 + NEW-P1-4 close — NOT add(pin_fingerprint)).
//   2. remove(std::array<std::byte, 32> const&) keys on fingerprint per
//      2g §4.3 line 494-495.
//   3. find(std::array<std::byte, 32> const&) returns pin_view per 2g §4.3
//      line 503-504.
//   4. snapshot() returns shared_ptr<const pin_snapshot> where pin_snapshot
//      is std::pmr::vector<pin> (NOT array<pin_fingerprint, max_pins>) per
//      2g §4.3 line 421 (NEW-P1-4 close).
//   5. pin carries diagnostic fields (subject_dn / san_dns / added_at) per
//      2g §4.3 lines 411-416 (NEW-P1-4 close — the diagnostic envelope is
//      load-bearing for SC-006 distinct-named-error-variant operator-
//      observability).
//   6. NO atomic-swap / bulk-set API exists on Pinset (FR-006 / 2g §6.5
//      invariant 3).
//   7. find allocates 0 bytes on the hot path; verified via
//      tests/perf/test_tls_handshake_alloc_guard.cpp with counting_resource +
//      mallocnesia LD_PRELOAD dual-gate (FR-007 / [[reference_mallocnesia_path]]
//      / [[feedback_tracking_pmr_resource_false_pass]]).
//   8. add over max_pins refused with tls_pinset_capacity_exhausted; no silent
//      eviction (FR-010 / 2g §6.6 line 992).
//   9. remove of an absent fp refused with tls_pin_not_found (2g §6.6 line 990).
//  10. Pinset writer holds std::shared_mutex (NOT std::mutex; NOT async_mutex)
//      per [2g §6.5.2] consolidated rationale.
//  11. Concurrent find+remove leaves the find's pin_view valid for its lifetime
//      (FR-008 invariant; [2g §9 seam #16]
//      tests/tls/test_pin_view_lifetime_under_rotation.cpp under TSan).
//  12. Mid-handshake rotation does not affect the in-flight handshake (FR-009);
//      [2g §9 seam #15] tests/tls/test_pinset_rotation_does_not_affect_in_flight.cpp
//      witnesses snapshot()-captured-once contract per §6.5.1.
//  13. Pinset::Config::mr lifetime contract: mr MUST outlive every snapshot
//      the Pinset ever publishes (NOT merely the Pinset instance itself); the
//      [2g §9 seam #18] tests/tls/test_pinset_snapshot_outlives_pinset.cpp
//      witnesses under ASan + TSan.
//  14. Per-counterparty shared_ptr<Pinset> reuse across SessionConfigs is the
//      recommended pattern (FR-009a / Clarify Q5);
//      tests/tls/test_pinset_per_counterparty_sharing.cpp asserts atomic
//      visibility across two sessions sharing one Pinset.
