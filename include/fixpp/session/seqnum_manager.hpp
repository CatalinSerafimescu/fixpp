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
//       too-high → return session_seqnum_too_high (120, 014 FR-016 / E-4;
//                  replaces slot-74 stand-in session_test_request_unanswered;
//                  slot 70 session_seqnum_gap_unrecoverable deleted per 013 T006a)
//       in-seq at seqnum_max → return store_seqnum_overflow, counter unchanged
//                  (092 FR-019: NextNumIn never wraps)
//   - Outbound: next_outbound() — read next counter without advancing
//               advance_outbound() — advance and return the ASSIGNED seq
//       Overflow at seqnum_max → return store_seqnum_overflow (I-8 — reuse [2e §6.7])
//   - Counter bookkeeping serialised by fixpp::sync::async_mutex (D-7).
//     Under the per-session-strand discipline this is structurally zero-contention
//     (defence-in-depth per [2f §7.3]).
//
// Invariants enforced:
//   I-2: counters advance by exactly +1; zero drift over a long run.
//   I-4: check_inbound only classifies a too-high MsgSeqNum; the caller's
//        disposition depends on its state. Re-derive the callers with
//        `grep -n "check_inbound(" src/session/session.cpp`.
//   I-8: seqnum_max overflow is session-fatal; no wrap. Both counters: the inbound
//        side is 092 FR-019 (contract C-3 I-7).
//
// No std::mutex used here (grep gate [const §XV.9]).
// No asio::awaitable in this header (no coroutine in the public interface).
#pragma once

#include <asio/awaitable.hpp>
#include <fixpp/core/error.hpp>             // expected_t / error
#include <fixpp/core/sync/async_mutex.hpp>  // fixpp::sync::async_mutex
#include <fixpp/session/seqnum.hpp>         // seqnum_t / seqnum_min / seqnum_max

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

    SeqnumManager(const SeqnumManager&) = delete;
    SeqnumManager& operator=(const SeqnumManager&) = delete;
    SeqnumManager(SeqnumManager&&) = delete;
    SeqnumManager& operator=(SeqnumManager&&) = delete;

    ~SeqnumManager() noexcept(false) = default;

    // ── Inbound check ────────────────────────────────────────────────────────
    //
    // check_inbound(seq): compare seq against next-expected inbound counter.
    // It only classifies (I-4): the caller's disposition depends on its state and
    // configuration. Find the callers with the recipe under I-4 above.
    //   in-seq  → advance counter, return ok.
    //   too-low  → return unexpected{session_seqnum_too_low=69}, counter unchanged.
    //   too-high → return unexpected{session_seqnum_too_high=120}, counter unchanged
    //              (014 FR-016 / E-4; slot 70 deleted per 013 T006a).
    //   in-seq at seqnum_max → return unexpected{store_seqnum_overflow=60}, counter unchanged
    //              (092 FR-019 / contract C-3 I-7: no next value exists, so it cannot be consumed).
    //
    // The disposition is per result (092 FR-019). Too-low and too-high keep their
    // context-dependent handling at the caller. store_seqnum_overflow requires FR-019's
    // silent transition to Disconnected: no Reject, no Logout, no delivery.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> check_inbound(
        seqnum_t seq) noexcept;

    // ── Outbound assign ──────────────────────────────────────────────────────
    //
    // peek_outbound(): synchronously read the NEXT outbound counter without advancing.
    //   ONLY safe to call on the session strand (single-threaded, no contention).
    //   Under the per-session-strand discipline this is always the case.
    //   Used by admin builders (build_logon, build_logout, etc.) to obtain the seqnum
    //   BEFORE deciding whether to advance — "peek first, assign on success" (F6 pattern).
    //   [gate-b/r1-green: RC#A; 005 data-model.md E3 singular "next outbound seqnum_t"]
    [[nodiscard]] seqnum_t peek_outbound() const noexcept { return next_outbound_; }

    // assign_outbound(): atomically read-then-advance the outbound counter.
    //   Returns the ASSIGNED sequence number (the value to stamp on the message).
    //   next_outbound_ is then incremented.
    //   Overflow at seqnum_max → return unexpected{store_seqnum_overflow=60} (I-8).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> assign_outbound() noexcept;

    // ── Read-only accessors (for test inspection) ─────────────────────────────
    //
    // These bypass the mutex and are ONLY safe from a single-threaded test or
    // when the session strand has quiesced. NOT for production use.

    [[nodiscard]] seqnum_t next_inbound_unsafe() const noexcept { return next_inbound_; }
    [[nodiscard]] seqnum_t next_outbound_unsafe() const noexcept { return next_outbound_; }

    // hydrate(next_inbound, next_outbound): set BOTH counters from store-recovered
    // values on cold open (FR-008 / 029-persistent-seqnum-hydrate C1).
    // Acquires mutex_; on drain/cancel → std::unexpected(session_already_closed).
    // Sets next_inbound_ = next_inbound; next_outbound_ = next_outbound.
    // No validation — caller supplies store-recovered values ≥ 1.
    // Production path; distinct from the test-only
    // seqnum_manager_test_access::set_counters, which takes no lock.
    // C1.2: idempotent at the value level; one-shot guard lives in the caller
    // (ensure_hydrated_), not here.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> hydrate(
        seqnum_t next_inbound, seqnum_t next_outbound) noexcept;

    // reset_to_one(): reset both counters to seqnum_min (1) for a successful
    // ResetSeqNumFlag(141)=Y handshake (FR-017 §150: "both sides advance
    // next_expected_inbound and next_expected_outbound to 1").
    // Mutex-guarded; production path — NOT test-hook-gated.
    // Must be called BEFORE the caller emits session_event_sequence_numbers_reset
    // so the event fires after post-reset state is consistent (FR-018).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset_to_one() noexcept;

    // set_next_inbound(n): force next_inbound_ to n for an inbound
    // SequenceReset(35=4) NewSeqNo(36) application (FIX-SL §4.8 — GapFill or
    // Reset mode, when NewSeqNo > current expected). Mutex-guarded; production
    // path. Only the inbound counter moves (a SequenceReset affects the peer→us
    // stream only); store persistence — when applicable — is the session's
    // responsibility, mirroring reset_to_one()'s split.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> set_next_inbound(
        seqnum_t n) noexcept;

    // set_next_outbound(n): force next_outbound_ to n for the 032 initiator
    // peer_ack_sent_reset_flag outbound restore (Mechanism A: restore-after-reset).
    // Mutex-guarded; production path. Only the outbound counter moves.
    // Store persistence — when applicable — is the session's responsibility,
    // mirroring set_next_inbound()'s split. [032 contract C1/Mechanism A]
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> set_next_outbound(
        seqnum_t n) noexcept;

    // drain(): required before destruction to satisfy async_mutex destructor
    // precondition. Safe to call even if no lock was ever acquired.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> drain() noexcept {
        return mutex_.cancel_and_drain();
    }

    // fixpp#511: test-only access to private state goes through ONE named friend,
    // defined in tests/support/seqnum_manager_test_access.hpp (never
    // installed). Unconditional on purpose: a member gated behind a test macro
    // would make a test TU's SeqnumManager a different class from the library's,
    // an ODR violation (ill-formed, no diagnostic required).
    friend struct seqnum_manager_test_access;

private:
    seqnum_t next_inbound_ = seqnum_min;   // next expected inbound (starts at 1)
    seqnum_t next_outbound_ = seqnum_min;  // next outbound to assign (starts at 1)
    fixpp::sync::async_mutex mutex_;       // serialises counter reads/writes (D-7)
};

}  // namespace fixpp::session
