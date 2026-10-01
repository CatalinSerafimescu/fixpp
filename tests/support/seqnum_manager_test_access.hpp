#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/seqnum_manager_test_access.hpp — TEST-ONLY access to SeqnumManager.
//
// fixpp#511: `SeqnumManager` declares `friend struct seqnum_manager_test_access;`
// unconditionally, and this header holds its one definition; a test never defines
// its own struct of the same name. Add new accessors here, never as members of
// `SeqnumManager` gated behind a test macro, which would make a test TU's class differ
// from the library's. Never installed; never reachable from production code.

#include <fixpp/session/seqnum_manager.hpp>

namespace fixpp::session {

struct seqnum_manager_test_access {
    // Seed both counters directly, without the mutex, so an overflow test can
    // start at seqnum_max instead of calling assign_outbound() billions of times.
    // Distinct from the production hydrate()/set_next_*(), which take the lock.
    static void set_counters(SeqnumManager& m, seqnum_t next_inbound,
                             seqnum_t next_outbound) noexcept {
        m.next_inbound_ = next_inbound;
        m.next_outbound_ = next_outbound;
    }

    // The internal async_mutex. The drain-lifecycle tests (T021, FR-011 / SC-004)
    // acquire it directly to manufacture a holder that is in flight during drain.
    [[nodiscard]] static fixpp::sync::async_mutex& mutex(SeqnumManager& m) noexcept {
        return m.mutex_;
    }
};

}  // namespace fixpp::session
