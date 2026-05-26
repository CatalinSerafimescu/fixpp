// src/tls/pinset.cpp
// Pinset implementation — FIXS-RC1 §5 add-then-remove rotation surface.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.3 + §6.2 + §6.5 + §6.5.2.
// Writer synchronisation: std::shared_mutex per [2g §6.5.2] — CITE AND STOP.
//   (Per NEW-P2-6 the consolidated three-point rationale lives at §6.5.2;
//    do NOT re-derive at impl site.)
// Reader path: lock-free — single acquire-load on snapshot_ per [2g §6.2].
// PMR lifetime: Config::mr MUST outlive every published snapshot (§4.6 / §6.2).
// Allocation failure: routed through [2a §4.2] trap_throw → tls_pinset_alloc_failed.
//
// added_at NOTE: v0.1 uses std::chrono::system_clock::now() directly.
// [2d §7.9] effective_clock wiring via SessionConfig is NOT yet plumbed into
// Pinset::Config in Phase 3. Next phase revisit when Config gains a clock field.

#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/pinset.hpp>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <utility>

namespace fixpp::tls {

namespace {

// Resolve the effective memory resource: use cfg.mr if non-null, else the
// PMR new-delete resource (always available, outlives everything).
std::pmr::memory_resource* resolve_mr(std::pmr::memory_resource* mr) noexcept {
    return mr ? mr : std::pmr::new_delete_resource();
}

// Clone an existing pin_snapshot into a new one using `mr`, then append `p`.
// Returns the new snapshot.
// Throws std::bad_alloc if mr is exhausted (caller catches → alloc_failed).
std::shared_ptr<const pin_snapshot> clone_and_append(
    std::shared_ptr<const pin_snapshot> const& old_snap, pin p, std::pmr::memory_resource* mr) {
    // Allocate the new vector in the PMR arena.
    auto new_snap = std::make_shared<pin_snapshot>(mr);
    new_snap->reserve(old_snap->size() + 1);
    // Copy old pins. Each pin's pmr::string / pmr::vector uses its stored
    // allocator (which was set to `mr` at add() time); the copy constructors
    // propagate the allocator per std::pmr::string / std::pmr::vector rules.
    for (auto const& old_p : *old_snap) {
        new_snap->push_back(old_p);
    }
    new_snap->push_back(std::move(p));
    return new_snap;
}

// Clone an existing pin_snapshot minus the element matching `fp`. Returns
// the cloned snapshot; `found` is set to true iff a pin matching `fp` was
// elided. Caller inspects `found` (not the return value) to decide whether
// to report tls_pin_not_found.
// Throws std::bad_alloc if mr is exhausted.
std::shared_ptr<const pin_snapshot> clone_minus(std::shared_ptr<const pin_snapshot> const& old_snap,
                                                std::array<std::byte, 32> const& fp,
                                                std::pmr::memory_resource* mr, bool& found) {
    found = false;
    auto new_snap = std::make_shared<pin_snapshot>(mr);
    new_snap->reserve(old_snap->size());
    for (auto const& old_p : *old_snap) {
        if (old_p.sha256 == fp) {
            found = true;
            continue;  // skip (remove this pin)
        }
        new_snap->push_back(old_p);
    }
    return new_snap;
}

}  // namespace

// ── Pinset default constructor ────────────────────────────────────────────────
Pinset::Pinset() : Pinset(Config{}) {}

// ── Pinset constructor ────────────────────────────────────────────────────────
Pinset::Pinset(Config cfg)
    : cfg_{cfg},
      mr_{resolve_mr(cfg.mr)},
      writer_{},
      snapshot_{std::make_shared<pin_snapshot>(mr_)}  // initial empty snapshot
{}

// ── Destructor ────────────────────────────────────────────────────────────────
Pinset::~Pinset() = default;

// ── Move ──────────────────────────────────────────────────────────────────────
Pinset::Pinset(Pinset&& other) noexcept {
    std::unique_lock<std::shared_mutex> lk(other.writer_);
    cfg_ = other.cfg_;
    mr_ = other.mr_;
    snapshot_.store(other.snapshot_.load(std::memory_order_acquire), std::memory_order_release);
    other.snapshot_.store(std::make_shared<pin_snapshot>(other.mr_), std::memory_order_release);
}

Pinset& Pinset::operator=(Pinset&& other) noexcept {
    if (this == &other) return *this;
    // Lock both to avoid concurrent readers on `other` seeing a torn state.
    std::unique_lock<std::shared_mutex> lk1(writer_);
    std::unique_lock<std::shared_mutex> lk2(other.writer_);
    cfg_ = other.cfg_;
    mr_ = other.mr_;
    snapshot_.store(other.snapshot_.load(std::memory_order_acquire), std::memory_order_release);
    other.snapshot_.store(std::make_shared<pin_snapshot>(other.mr_), std::memory_order_release);
    return *this;
}

// ── add ───────────────────────────────────────────────────────────────────────
core::expected_t<void> Pinset::add(Certificate const& cert) {
    std::unique_lock<std::shared_mutex> lk(writer_);  // serialise writers [2g §6.5.2]

    auto cur = snapshot_.load(std::memory_order_relaxed);

    // Capacity check (FR-010 / [2g §6.6] line 992).
    if (cur->size() >= cfg_.max_pins) {
        return std::unexpected(core::error::tls_pinset_capacity_exhausted);
    }

    // Duplicate check by SHA-256.
    for (auto const& p : *cur) {
        if (p.sha256 == cert.sha256_) {
            return std::unexpected(core::error::tls_pin_already_present);
        }
    }

    // Build the new pin with PMR-copied diagnostic strings.
    // Wrap in a try/catch to route std::bad_alloc → tls_pinset_alloc_failed
    // per [2a §4.2] trap_throw pattern.
    try {
        pin new_pin;
        new_pin.sha256 = cert.sha256_;
        new_pin.subject_dn = std::pmr::string{cert.subject_dn_, mr_};
        new_pin.san_dns = std::pmr::vector<std::pmr::string>{mr_};
        for (auto const& sv : cert.san_dns_names_) {
            // Construct each PMR string explicitly with the resource, then push.
            new_pin.san_dns.push_back(std::pmr::string{sv, mr_});
        }
        // v0.1: system_clock::now() — see file-level NOTE on [2d §7.9].
        new_pin.added_at = std::chrono::system_clock::now();

        auto new_snap = clone_and_append(cur, std::move(new_pin), mr_);
        snapshot_.store(std::move(new_snap), std::memory_order_release);
    } catch (std::bad_alloc const&) {
        return std::unexpected(core::error::tls_pinset_alloc_failed);
    }

    return {};
}

// ── remove ────────────────────────────────────────────────────────────────────
core::expected_t<void> Pinset::remove(std::array<std::byte, 32> const& sha256) {
    std::unique_lock<std::shared_mutex> lk(writer_);  // serialise writers [2g §6.5.2]

    auto cur = snapshot_.load(std::memory_order_relaxed);
    bool found = false;

    try {
        auto new_snap = clone_minus(cur, sha256, mr_, found);
        if (!found) {
            return std::unexpected(core::error::tls_pin_not_found);
        }
        snapshot_.store(std::move(new_snap), std::memory_order_release);
    } catch (std::bad_alloc const&) {
        return std::unexpected(core::error::tls_pinset_alloc_failed);
    }

    return {};
}

// ── find ─────────────────────────────────────────────────────────────────────
// Lock-free: single acquire-load per [2g §6.2]. Linear scan.
pin_view Pinset::find(std::array<std::byte, 32> const& sha256) const noexcept {
    auto snap = snapshot_.load(std::memory_order_acquire);
    for (auto const& p : *snap) {
        if (p.sha256 == sha256) {
            return pin_view{snap, &p};
        }
    }
    return pin_view{std::move(snap), nullptr};
}

// ── snapshot ──────────────────────────────────────────────────────────────────
std::shared_ptr<const pin_snapshot> Pinset::snapshot() const noexcept {
    return snapshot_.load(std::memory_order_acquire);
}

// ── size ──────────────────────────────────────────────────────────────────────
std::size_t Pinset::size() const noexcept {
    auto snap = snapshot_.load(std::memory_order_acquire);
    return snap->size();
}

// ── contains ─────────────────────────────────────────────────────────────────
bool Pinset::contains(std::array<std::byte, 32> const& sha256) const noexcept {
    auto snap = snapshot_.load(std::memory_order_acquire);
    for (auto const& p : *snap) {
        if (p.sha256 == sha256) return true;
    }
    return false;
}

// ── make_pinset factory ───────────────────────────────────────────────────────
// Wraps construction in try/catch so allocation failure surfaces as
// expected_t::unexpected{tls_pinset_alloc_failed} per [2a §4.2] trap_throw.
core::expected_t<std::shared_ptr<Pinset>> Pinset::make_pinset(
    Config cfg, std::pmr::memory_resource* mr) noexcept {
    if (cfg.mr == nullptr) cfg.mr = mr;
    try {
        return std::make_shared<Pinset>(cfg);
    } catch (std::bad_alloc const&) {
        return std::unexpected(core::error::tls_pinset_alloc_failed);
    }
}

}  // namespace fixpp::tls
