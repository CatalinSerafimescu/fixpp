#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/session_test_access.hpp — TEST-ONLY access to Session internals.
//
// fixpp#511: `Session` declares `friend struct session_test_access;`
// unconditionally. Define this struct only in this header; a second definition
// of the same name in another TU of the program reintroduces the ODR defect.
// A test that needs private state includes this header. Add new accessors HERE,
// never as members of `Session` gated behind a test macro: the library is
// compiled without it, so a TU that defined the macro would see a different
// `Session`. Never installed; never reachable from production code. Needs no
// test macro.

#include <cstddef>
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
};

}  // namespace fixpp::session
