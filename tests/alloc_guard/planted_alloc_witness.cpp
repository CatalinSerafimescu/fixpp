// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/alloc_guard/planted_alloc_witness.cpp
//
// fixpp#448 — THE POSITIVE CONTROL for the whole mallocnesia gate population.
//
// Every other alloc-guard binary asserts that a window does NOT allocate, and a green
// run of those is consistent with two very different worlds: the code is clean, or the
// interceptor is not running. Eighteen entries all reporting "no allocations detected"
// cannot distinguish them, and on `main` the second world was the real one on every
// machine that had not hand-built the .so.
//
// This binary allocates ON PURPOSE inside a guard window. It exists to make the
// distinction observable: if the gate is live, this run FAILS with the interceptor's
// own verdict; if the gate has silently stopped intercepting, it SUCCEEDS, and the
// ctest entry that wraps it goes red. It is the arm that fails when nothing else does.
//
// ⚠️ It is NOT registered WILL_FAIL. See tools/check_alloc.py's --expect-violation
// block: WILL_FAIL accepts any nonzero exit, including the wrapper's own "interceptor
// missing" exit 2, so it would pass on exactly the lane where interception is broken.
// The verdict is inverted inside the wrapper, which can see all three facts at once.
//
// ⚠️ NOT a gtest binary, deliberately. gtest's own machinery allocates, and the point
// here is a window whose ONLY allocation is the planted one — so the interceptor's
// reported count is unambiguous rather than "1 + however much the framework did".

#include <cstdlib>

#include "support/alloc_guard_markers.hpp"

int main() {
    if (alloc_guard_start) alloc_guard_start();

    // volatile so neither clang nor gcc can pair-elide this into nothing at -O2.
    // A deleted allocation would make the control silently measure an empty window,
    // which is the same false green it exists to catch. Release is the lane this runs
    // on, so the elision risk is real and not theoretical.
    void* volatile p = std::malloc(16);
    std::free(const_cast<void*>(p));

    if (alloc_guard_end) alloc_guard_end();  // exits(1) under interception

    // Reached ONLY when the markers are no-ops, i.e. nothing was intercepted.
    // The wrapper treats this exit-0 as the control FAILING, which is correct: it means
    // the planted allocation went unnoticed.
    return 0;
}
