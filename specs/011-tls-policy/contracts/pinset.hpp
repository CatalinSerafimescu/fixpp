// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Contract shape oracle for 011-tls-policy FR-006..010 + FR-009a.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.3 (Pinset add-then-remove rotation)
// + §6.2 (lock-free reader / serialised-writer publication) + §6.5 (mutex
// choice rationale).
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
//
// ============================================================================
// pin_snapshot — immutable captured state of a Pinset at a publication point.
// ============================================================================
//
// namespace fixpp::tls {
//
// class pin_snapshot {
//  public:
//     // Cap-aligned: the array size is Pinset::Config::max_pins (default 16).
//     // The count_ field tracks the active entry count (≤ max_pins).
//     [[nodiscard]] std::size_t size() const noexcept;
//     [[nodiscard]] bool contains(pin_fingerprint const&) const noexcept;
//     [[nodiscard]] std::span<const pin_fingerprint> entries() const noexcept [[clang::lifetimebound]];
//
//  private:
//     friend class Pinset;
//     // std::array<pin_fingerprint, max_pins> entries_;
//     // std::size_t count_;
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// pin_view — value-typed handle returned by Pinset::find.
// ============================================================================
//
// namespace fixpp::tls {
//
// class pin_view {
//  public:
//     [[nodiscard]] bool             valid() const noexcept;
//     [[nodiscard]] pin_fingerprint  fingerprint() const noexcept [[clang::lifetimebound]];
//
//  private:
//     friend class Pinset;
//     // std::shared_ptr<const pin_snapshot> snapshot_;
//     // const pin_fingerprint*              matched_;   // pointer into snapshot_->entries_
// };
//
// // Invariant (verified at tests/tls/test_pinset_pin_view_outlives_remove.cpp):
// // a pin_view{} constructed from find(fp) that returns valid() == true
// // remains valid for the view's lifetime regardless of any subsequent
// // Pinset::remove(fp) calls. The shared_ptr<const pin_snapshot> in the
// // view keeps the captured snapshot alive.
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Pinset — mutable container with mid-session-mutable rotation.
// ============================================================================
//
// namespace fixpp::tls {
//
// class Pinset {
//  public:
//     struct Config {
//         std::size_t max_pins = 16;     // FR-010; [2g §1.1]
//     };
//
//     explicit Pinset(Config cfg = {});
//     ~Pinset() = default;
//     Pinset(Pinset const&)            = delete;
//     Pinset& operator=(Pinset const&) = delete;
//     Pinset(Pinset&&)                 = delete;
//     Pinset& operator=(Pinset&&)      = delete;
//
//     // Writer path — synchronous, serialised by writer_mu_ (shared_mutex).
//     // NOT coroutine context per [2g §6.5] / D-8; std::shared_mutex is the
//     // correct choice (NOT std::mutex which would be banned in coroutine
//     // context per [const §XV.9]; NOT async_mutex which would suspend
//     // unnecessarily on a synchronous op).
//
//     // FR-006 — add/remove are SEPARATE explicit ops. NO atomic-swap shortcut.
//     // Old fingerprints remain usable for find() until remove() returns.
//     [[nodiscard]] expected_t<void> add(pin_fingerprint fp) noexcept;
//     [[nodiscard]] expected_t<void> remove(pin_fingerprint fp) noexcept;
//
//     // Reader path — lock-free, zero allocation (FR-007). Hot path.
//     // [[clang::lifetimebound]] declares the lifetime contract at the class
//     // surface per [arch §5.5] / [2b §6.4].
//     [[nodiscard]] pin_view find(pin_fingerprint const& fp) const noexcept [[clang::lifetimebound]];
//     [[nodiscard]] bool     contains(pin_fingerprint const& fp) const noexcept;
//
//     // Test / observability accessor — used by the deterministic
//     // add-then-remove test ([2g §9 seam #1]).
//     [[nodiscard]] std::shared_ptr<const pin_snapshot> snapshot() const noexcept;
//
//  private:
//     // mutable std::shared_mutex                              writer_mu_;
//     // std::atomic<std::shared_ptr<const pin_snapshot>>       published_;
//     // Config                                                 cfg_;
// };
//
// }  // namespace fixpp::tls
//
// ============================================================================
// Per-counterparty granularity (FR-009a / Clarify Q5 — recommended pattern):
//
//   std::shared_ptr<Pinset> counterparty_X_pins = std::make_shared<Pinset>();
//   counterparty_X_pins->add(sha256_of_their_current_leaf);
//
//   SessionConfig session_a;  session_a.counterparty_id = "X"; session_a.pinset = counterparty_X_pins;
//   SessionConfig session_b;  session_b.counterparty_id = "X"; session_b.pinset = counterparty_X_pins;
//
//   // Rotation applies atomically to both sessions:
//   counterparty_X_pins->add(sha256_of_their_new_leaf);
//   // ... counterparty cuts over ...
//   counterparty_X_pins->remove(sha256_of_their_old_leaf);
//
// Engine-wide sharing across DIFFERENT counterparties is discouraged
// (conflates trust authorities) — see Clarifications 2026-05-23.
// ============================================================================
//
// Contract assertions (verified at /speckit-verify):
//
//   1. add/remove key on pin_fingerprint = array<byte, 32> (FR-006 / Clarify Q4).
//   2. NO atomic-swap / bulk-set API exists on Pinset (FR-006).
//   3. find returns a value-typed pin_view holding a shared_ptr snapshot (FR-008).
//   4. find allocates 0 bytes on the hot path; verified via
//      tests/perf/test_tls_alloc_guard.cpp with counting_resource +
//      mallocnesia LD_PRELOAD dual-gate (FR-007 / [[reference_mallocnesia_path]] /
//      [[feedback_tracking_pmr_resource_false_pass]]).
//   5. add over max_pins refused with pin_cap_exceeded; no silent eviction (FR-010).
//   6. remove of an absent fp refused with pin_not_present.
//   7. Pinset writer holds std::shared_mutex (NOT std::mutex; NOT async_mutex)
//      per D-8 / [2g §6.5].
//   8. Concurrent find+remove leaves the find's pin_view valid for its lifetime
//      (FR-008 invariant; tests/tls/test_pinset_pin_view_outlives_remove.cpp under TSan).
//   9. Mid-handshake rotation does not affect the in-flight handshake (FR-009);
//      tests/tls/test_pinset_add_then_remove_deterministic.cpp witnesses publication
//      ordering ([2g §9 seam #1]).
//  10. Per-counterparty shared_ptr<Pinset> reuse across SessionConfigs is the
//      recommended pattern (FR-009a / Clarify Q5);
//      tests/tls/test_pinset_per_counterparty_sharing.cpp asserts atomic
//      visibility across two sessions sharing one Pinset.
