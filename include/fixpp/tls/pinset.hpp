#pragma once
// include/fixpp/tls/pinset.hpp
// fixpp::tls::Pinset — mid-session-mutable pin set for FIXS-RC1 §5 rotation.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.3 (Pinset add-then-remove rotation)
// + §6.2 (lock-free reader / serialised-writer publication)
// + §6.5 (FIXS rotation invariants)
// + §6.5.2 (CONSOLIDATED mutex-choice rationale — cite-and-stop per NEW-P2-6)
//
// API re-emitted verbatim from [2g §4.3] lines 437-518 (contracts/pinset.hpp).
// Spec anchors: FR-006 (add/remove separate), FR-007 (zero-alloc hot path),
// FR-008 (pin_view lifetime-bound), FR-009 (mid-session-mutable),
// FR-009a (per-counterparty granularity), FR-010 (max_pins=16).
//
// Writer synchronisation: std::shared_mutex per [2g §6.5.2] consolidated
// rationale. See contracts/pinset.hpp §6.5.2 — CITE AND STOP.

#include <array>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/certificate.hpp>
#include <memory>
#include <memory_resource>
#include <shared_mutex>
#include <string>
#include <vector>

namespace fixpp::tls {

// ── pin_fingerprint ───────────────────────────────────────────────────────────
// SHA-256 of the leaf cert's DER encoding (Clarify Q4; [FIXS RC1 §5]).
using pin_fingerprint = std::array<std::byte, 32>;

// ── pin ───────────────────────────────────────────────────────────────────────
// Owned diagnostic record + 32-byte fingerprint.
// Re-emitted from [2g §4.3] lines 411-416. PMR-allocated copies of
// subject/SAN strings so a long-lived shared_ptr<const pin_snapshot>
// can outlive the Certificate the caller passed to add().
//
// CRITICAL (NEW-P1-4 close): diagnostic fields are NOT optional.
struct pin {
    std::array<std::byte, 32> sha256;  // SHA-256 fingerprint of the pinned leaf cert's DER bytes.
    std::pmr::string subject_dn;  // PMR-copied at add() time from the caller-supplied Certificate.
    std::pmr::vector<std::pmr::string>
        san_dns;  // PMR-copied at add() time; bounded by max_pins indirectly.
    std::chrono::system_clock::time_point
        added_at;  // Wall-clock UTC at add() time.
                   // NOTE: uses std::chrono::system_clock directly for v0.1.
                   // [2d §7.9] effective_clock is sourced via SessionConfig /
                   // EngineConfig — not yet wired to Pinset::Config in Phase 3.
                   // Next phase revisit: bind via Config::clock when available.
};

// ── pin_snapshot ──────────────────────────────────────────────────────────────
// Immutable snapshot container. Re-emitted from [2g §4.3] line 421.
// Lifetime is the holding shared_ptr's.
// The PMR backing resource is Pinset::Config::mr; see §4.6 ownership rules.
using pin_snapshot = std::pmr::vector<pin>;

// ── pin_view ─────────────────────────────────────────────────────────────────
// Value-typed lookup result for find(). Re-emitted from [2g §4.3] lines 427-433.
// NOTE: the contract oracle places [[clang::lifetimebound]] on `value` as a field
// annotation (N-P3-1). Clang only supports this attribute on function
// parameters, not data members. The attribute is therefore omitted from the
// field declaration; the lifetime contract is enforced by the struct invariant:
// `value` always points into `snapshot->data()` and is valid for `*this`'s lifetime.
struct pin_view {
    std::shared_ptr<const pin_snapshot>
        snapshot;                // pins the matched entry's lifetime (and the entire snapshot).
    pin const* value = nullptr;  // bounded by *this (i.e., by `snapshot`).

    [[nodiscard]] bool found() const noexcept { return value != nullptr; }
    explicit operator bool() const noexcept { return found(); }
};

// ── Pinset ────────────────────────────────────────────────────────────────────
// Mutable container with mid-session-mutable rotation.
// Re-emitted from [2g §4.3] lines 435-528.
class Pinset {
public:
    struct Config {
        // Lifetime contract (v0.4 / round-3 P1-1 close): `mr` MUST outlive
        // every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes.
        // The default path (null → std::pmr::new_delete_resource()) satisfies
        // this by construction. User-owned resources (e.g., monotonic_buffer_resource
        // for tests) MUST stay alive until the last reader-held snapshot drains.
        // See contracts/pinset.hpp §6.5.2 binding contract for full text.
        std::pmr::memory_resource* mr{nullptr};
        std::size_t max_pins{16};
    };

    // [arch §6] rule-4 factory entry point. Wraps construction in trap_throw so
    // any PMR-allocation throw surfaces as tls_pinset_alloc_failed.
    [[nodiscard]] static core::expected_t<std::shared_ptr<Pinset>> make_pinset(
        Config cfg, std::pmr::memory_resource* mr) noexcept;

    Pinset();  // default: Config{} (mr=nullptr, max_pins=16)
    explicit Pinset(Config cfg);
    ~Pinset();

    Pinset(Pinset const&) = delete;
    Pinset& operator=(Pinset const&) = delete;
    Pinset(Pinset&& other) noexcept;
    Pinset& operator=(Pinset&& other) noexcept;

    // (1) Add a pin from a Certificate. Deep-copies subject_dn + san_dns into
    // the snapshot's PMR arena. Errors:
    //   max_pins exceeded → unexpect{tls_pinset_capacity_exhausted}
    //   SHA-256 already present → unexpect{tls_pin_already_present}
    //   PMR allocation failure → unexpect{tls_pinset_alloc_failed}
    [[nodiscard]] core::expected_t<void> add(Certificate const& cert);

    // (2) Remove a pin by SHA-256 fingerprint.
    //   absent → unexpect{tls_pin_not_found}
    [[nodiscard]] core::expected_t<void> remove(std::array<std::byte, 32> const& sha256);

    // (3) Lookup on the handshake-hot path. Lock-free — acquire-load on
    // snapshot_, linear scan. Returns pin_view with snapshot shared_ptr pinned.
    [[nodiscard]] pin_view find(std::array<std::byte, 32> const& sha256) const noexcept;

    // (4) Explicit-snapshot accessor — TLS handshake captures this ONCE per
    // [2g §6.5.1] BINDING CONTRACT and scans the captured snapshot.
    [[nodiscard]] std::shared_ptr<const pin_snapshot> snapshot() const noexcept;

    // Diagnostic / test-only.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool contains(std::array<std::byte, 32> const& sha256) const noexcept;

private:
    Config cfg_;
    std::pmr::memory_resource* mr_;  // resolved at construction.
    mutable std::shared_mutex
        writer_;  // serialises add/remove vs each other; readers do NOT take it. [2g §6.5.2].
    std::atomic<std::shared_ptr<const pin_snapshot>>
        snapshot_;  // acquire-load on read; release-store on write [2g §6.2].
};

}  // namespace fixpp::tls
