#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/session_test_access.hpp — TEST-ONLY access to Session internals.
//
// fixpp#511: `Session` declares `friend struct session_test_access;`
// unconditionally. Define this struct only in this header; a differing
// definition of this struct elsewhere in the program is an ODR violation
// (ill-formed, no diagnostic required).
// A test that needs private state includes this header. Add new accessors HERE,
// never as members of `Session` gated behind a test macro: the library is
// compiled without it, so a TU that defined the macro would see a different
// `Session`. Never installed; production targets must not include this
// header. Needs no test macro.

#include <cstddef>
#include <cstdint>
#include <fixpp/session/session.hpp>
#include <span>

namespace fixpp::session {

struct session_test_access {
    // The internal SeqnumManager. Drain-contract tests (009 T021, FR-011) acquire
    // its async_mutex directly to manufacture a holder that is in flight when
    // close() drains it; other tests seed or read the counters.
    [[nodiscard]] static SeqnumManager& seqnum_mgr(Session& s) noexcept { return s.seqnum_mgr_; }

    // Drive store_then_emit with a caller-supplied frame (034 T010). Its over-bound
    // branch (frame.size() > kMaxMaskableLogonBytes) is production-unreachable,
    // because build_logon caps its output at that bound; a hand-crafted larger 35=A
    // frame exercises the fail-closed skip-store-but-transmit branch against the
    // real bound.
    [[nodiscard]] static asio::awaitable<fixpp::core::expected_t<void>> store_then_emit(
        Session& s, seqnum_t stamped_seq, std::span<const std::byte> frame) noexcept {
        return s.store_then_emit(stamped_seq, frame);
    }

    // True iff open() constructed the inbound validator (validate_inbound_messages
    // and a dictionary). Lets 041 T016 (SC-005) prove the default path constructs
    // none.
    [[nodiscard]] static bool has_validator(Session const& s) noexcept {
        return s.validator_ != nullptr;
    }

    // True iff the session reached lifecycle::closed_drained. The issue #151 C-ABI
    // reaped-close tests wait on this: onLogout fires on the Active edge, before the
    // drain completes, so a fixed sleep could race the closing window.
    [[nodiscard]] static bool is_drained(Session const& s) noexcept {
        return s.state_ == Session::lifecycle::closed_drained;
    }

    // True iff live_peer_id_ holds a value. 043 T008 asserts that both transport
    // attach paths leave it empty on insecure_plain_tcp (D-10).
    [[nodiscard]] static bool live_peer_id_has_value(Session const& s) noexcept {
        return s.live_peer_id_.has_value();
    }

    // The inbound limit L open() stored (093, data-model E-2). Q-13 reads it.
    [[nodiscard]] static std::uint32_t inbound_limit(Session const& s) noexcept {
        return s.inbound_limit_;
    }

    // The capacity of the carry open() allocated: L plus one read (093, data-model E-2).
    [[nodiscard]] static std::size_t carry_capacity(Session const& s) noexcept {
        // Precondition: open() succeeded, so carry_ holds a value.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        return s.carry_->capacity();
    }

    // The size of the per-session parse buffer open() allocated (093, data-model E-2).
    [[nodiscard]] static std::size_t parse_buffer_bytes(Session const& s) noexcept {
        return s.inbound_parse_buf_.size();
    }

    // How many requests the parse buffer's spill witness has seen: every allocation an
    // inbound parse, or a lazy read inside a callback, made past the buffer (093,
    // contract C-3 I-2).
    [[nodiscard]] static std::uint64_t parse_spills(Session const& s) noexcept {
        return s.parse_spill_witness_.spills();
    }

    // Shortens the parse buffer to its first `bytes` (093, contract C-3 I-4): an
    // admitted frame's parse then draws past it, which reaches the spill witness. The
    // defence cells use it after open(); `bytes` must not exceed the current size.
    static void shrink_parse_buffer(Session& s, std::size_t bytes) noexcept {
        s.inbound_parse_buf_ = s.inbound_parse_buf_.first(bytes);
    }

    // Lowers the entry cap every inbound parse runs under (093, data-model E-2): a
    // frame with more fields than `n` then fails its parse with wire_offset_table_full
    // on every lane. The defence cells use it after open().
    static void lower_inbound_entry_cap(Session& s, std::size_t n) noexcept {
        s.inbound_entry_cap_ = n;
    }

    // Whether a 141=Y reset unit's store operation is in flight (093, data-model E-10).
    [[nodiscard]] static bool reset_unit_in_flight(Session const& s) noexcept {
        return s.reset_unit_in_flight_;
    }

    // Whether Engine::stop()'s step 1 has run on the session's strand (093, data-model
    // E-13).
    [[nodiscard]] static bool engine_stop_requested(Session const& s) noexcept {
        return s.engine_stop_requested_;
    }

    // The session's store, or null before open() (093 plan OD-25: a cell issues a
    // competing store operation on it).
    [[nodiscard]] static MessageStore* store(Session& s) noexcept { return s.store_.get(); }

    // The send path's frame-slot flag (fixpp#544 §2.5). Read and write it on the session
    // strand. A flag the test sets stays set: Session::send's holder clears only a flag it
    // set itself, and the fallback leaf never writes it.
    [[nodiscard]] static bool& send_slot_in_use(Session& s) noexcept { return s.send_slot_in_use_; }
};

}  // namespace fixpp::session
