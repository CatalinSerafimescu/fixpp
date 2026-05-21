// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/seqnum_manager.hpp
//
// fixpp::session::SeqnumManager — inbound/outbound sequence-number counters
// serialised by fixpp::sync::async_mutex (005 US2 / T031 / data-model E3).
//
// Anchors:
//   data-model.md E3 / E4 / Invariants I-2 / I-4 / I-8
//   research.md D-7 ([2f §7.3] defence-in-depth pattern)
//   spec FR-008/009/010
//   [FIX-SL §4.1] ordered-sequence integrity
//   contracts/seqnum.hpp (shape oracle)
//
// Responsibilities (E3):
//   - Inbound: check_inbound(seq) — compare seq vs next-expected:
//       in-seq  → advance next_inbound_, return ok
//       too-low → return session_seqnum_too_low (no PossDup — S-010 out of scope)
//       too-high → return session_seqnum_gap_unrecoverable (recovery deferred I-4)
//   - Outbound: next_outbound() — read next counter without advancing
//               advance_outbound() — advance and return the ASSIGNED seq
//       Overflow at seqnum_max → return store_seqnum_overflow (I-8 — reuse [2e §6.7])
//   - Counter bookkeeping serialised by fixpp::sync::async_mutex (D-7).
//     Under the per-session-strand discipline this is structurally zero-contention
//     (defence-in-depth per [2f §7.3]).
//
// Invariants enforced:
//   I-2: counters advance by exactly +1; zero drift over a long run.
//   I-4: too-high is session-fatal; no ResendRequest; caller emits Logout+disconnect.
//   I-8: seqnum_max overflow is session-fatal; no wrap.
//
// No std::mutex used here (grep gate [const §XV.9]).
// No asio::awaitable in this header (no coroutine in the public interface).
#pragma once

#include <cstdint>

#include <fixpp/core/error.hpp>       // expected_t / error
#include <fixpp/session/seqnum.hpp>   // seqnum_t / seqnum_min / seqnum_max
#include <fixpp/core/sync/async_mutex.hpp>  // fixpp::sync::async_mutex

#include <asio/awaitable.hpp>

namespace fixpp::session {

// ── SeqnumManager ────────────────────────────────────────────────────────────
//
// Thread-safety: every public method acquires the internal async_mutex before
// reading/writing the counters. Under the per-session-strand discipline the
// mutex is always uncontended (fast-path CAS succeeds on every call); the
// async_mutex serialisation is defence-in-depth per [2f §7.3] D-7.
//
// Lifecycle: construct once per Session at Session::open(); destroy at close.
// The async_mutex must be drained (cancel_and_drain()) before destruction so
// its destructor's terminate-precondition is not triggered. The Session
// manages the lifetime; the SeqnumManager object has no independent lifecycle.

class SeqnumManager {
public:
    // Construct with initial counters at seqnum_min (1).
    SeqnumManager() noexcept = default;

    SeqnumManager(const SeqnumManager&)            = delete;
    SeqnumManager& operator=(const SeqnumManager&) = delete;
    SeqnumManager(SeqnumManager&&)                 = delete;
    SeqnumManager& operator=(SeqnumManager&&)      = delete;

    ~SeqnumManager() noexcept(false) = default;

    // ── Inbound check ────────────────────────────────────────────────────────
    //
    // check_inbound(seq): compare seq against next-expected inbound counter.
    //   in-seq  → advance counter, return ok.
    //   too-low  → return unexpected{session_seqnum_too_low=69}   (session-fatal).
    //   too-high → return unexpected{session_seqnum_gap_unrecoverable=70} (session-fatal).
    //
    // Caller is responsible for the session-fatal disposition (emitting
    // Logout-with-text + disconnect) on any unexpected return. I-4: no
    // ResendRequest is emitted by 005; the recovery feature is deferred.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        check_inbound(seqnum_t seq) noexcept;

    // ── Outbound assign ──────────────────────────────────────────────────────
    //
    // assign_outbound(): atomically read-then-advance the outbound counter.
    //   Returns the ASSIGNED sequence number (the value to stamp on the message).
    //   next_outbound_ is then incremented.
    //   Overflow at seqnum_max → return unexpected{store_seqnum_overflow=60} (I-8).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>>
        assign_outbound() noexcept;

    // ── Read-only accessors (for test inspection) ─────────────────────────────
    //
    // These bypass the mutex and are ONLY safe from a single-threaded test or
    // when the session strand has quiesced. NOT for production use.

    [[nodiscard]] seqnum_t next_inbound_unsafe() const noexcept {
        return next_inbound_;
    }
    [[nodiscard]] seqnum_t next_outbound_unsafe() const noexcept {
        return next_outbound_;
    }

    // drain(): required before destruction to satisfy async_mutex destructor
    // precondition. Safe to call even if no lock was ever acquired.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
        drain() noexcept {
        return mutex_.cancel_and_drain();
    }

#ifdef FIXPP_TEST_HOOKS
    // Test-only: directly seed counter values (used by overflow tests).
    // NOT for production use. Gated by FIXPP_TEST_HOOKS ([const §XV.9]).
    // Usage: seqnum_overflow test seeds next_outbound_ = seqnum_max
    //   to avoid ~4 billion assign_outbound() calls.
    void set_counters_for_test(seqnum_t next_inbound, seqnum_t next_outbound) noexcept {
        next_inbound_  = next_inbound;
        next_outbound_ = next_outbound;
    }
#endif

private:
    seqnum_t next_inbound_  = seqnum_min;  // next expected inbound (starts at 1)
    seqnum_t next_outbound_ = seqnum_min;  // next outbound to assign (starts at 1)
    fixpp::sync::async_mutex mutex_;       // serialises counter reads/writes (D-7)
};

}  // namespace fixpp::session
