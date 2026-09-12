// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/ubsan_plant.hpp — 089 T098a (FR-021a): a test-only
// hook that executes one piece of undefined behaviour UBSan is expected to
// report, so the `ubsan` matrix arm can be shown to actually detect a
// finding rather than reporting clean because it never ran (quickstart.md
// § Step 4, "Plant a UBSan finding" row).
//
// OFF BY DEFAULT: gated on the presence of a dedicated, otherwise-unused
// test env var. Nothing in the harness, the CI workflows or any committed
// preset sets it, so a normal matrix run cannot reach the `std::getenv`
// check below with anything but an absent variable and therefore never
// reaches the undefined behaviour.
//
// Re-deriving the arms this hook exists for: run the planted cell under
// `ubsan` WITH the preset's injected UBSAN_OPTIONS (halt_on_error=1) present
// — the process must abort and the cell log must carry UBSan's own
// `runtime error:` line. Run the same cell with that injected environment
// removed (UBSan's default mode is recoverable) — the process must survive
// and the cell must pass. Run the same cell under `normal` — no sanitizer is
// linked in, so only the marker below is observable and the cell must pass.
#pragma once

#include <climits>
#include <cstdio>
#include <cstdlib>

namespace fixpp::interop::support {

// Prints a marker (so a caller can confirm this function ran) and then
// performs a signed integer overflow, which UBSan's signed-integer-overflow
// check reports as "runtime error: signed integer overflow ...". No-op
// unless FIXPP_INTEROP_UBSAN_PLANT is set to a non-empty value.
inline void maybe_run_ubsan_plant()
{
    char const* v = std::getenv("FIXPP_INTEROP_UBSAN_PLANT");  // NOLINT(concurrency-mt-unsafe) -- single-threaded test setup
    if (v == nullptr || v[0] == '\0') {
        return;
    }
    // Marker goes to stderr, unbuffered, so it is observable even when the
    // process is aborted immediately after the undefined behaviour below
    // (stdout is redirected to a file by the runner and would otherwise be
    // fully buffered and lost on abort).
    std::fputs("T098A-UBSAN-PLANT-MARKER\n", stderr);
    // `volatile` defeats compile-time constant folding of the overflow so
    // the check fires at runtime rather than being diagnosed (or silently
    // wrapped) at compile time.
    volatile int x = INT_MAX;
    x = x + 1;
    std::fprintf(stderr, "T098A-UBSAN-PLANT-SURVIVED x=%d\n", x);
    std::fflush(stderr);
}

}  // namespace fixpp::interop::support
