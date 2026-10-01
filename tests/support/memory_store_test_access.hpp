#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/memory_store_test_access.hpp — TEST-ONLY access to MemoryStore.
//
// fixpp#511: `MemoryStore` declares `friend struct memory_store_test_access;`
// unconditionally. Define this struct only in this header; a second definition
// of the same name in another TU of the program reintroduces the ODR defect.
// Add new accessors here, never as members of `MemoryStore` gated behind a test
// macro, which would make a test TU's class differ from the library's. Never
// installed; never reachable from production code.

#include <fixpp/session/memory_store.hpp>

namespace fixpp::session {

struct memory_store_test_access {
    // Force one direction's seqnum counter to `value`.
    // PRECONDITION: single-threaded (no concurrent store/retrieve calls).
    static void set_counter(MemoryStore& store, direction_t dir, seqnum_t value) noexcept {
        store.counter_for(dir) = value;
    }
};

}  // namespace fixpp::session
