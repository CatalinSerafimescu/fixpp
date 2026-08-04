// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_umbrella.cpp
//
// 086 T026 (US3, FR-004/FR-011c) — the umbrella column of
// contracts/include-interface.md §1: `fixpp::fixpp` still reaches EVERYTHING,
// including the two headers this feature republishes at isolated roots.
//
// Why a separate TU and not two more #includes in consumer_witness.cpp:
// consumer_witness.cpp:34-37 includes neither <fix/c_api.h> nor the service
// plugin header, so FR-004's C-ABI leg, US3 acceptance scenario 2 and FR-011c
// are witnessed by nothing today — and SC-003 trades on consumer_witness.cpp
// remaining BYTE-UNCHANGED, which is precisely why the new coverage lands here
// instead (contracts §4, last two rows).
//
// This is the regression guard for the whole feature: the isolation is delivered
// by ADDING roots, never by removing anything from the umbrella's root, and this
// TU is what would go red if that ever stopped being true.
//
// COMPILE-ONLY (OBJECT library, no main) — contracts §4 / research.md R5.

#include <fix/c_api.h>
#include <fixpp/service/control_plane_factory.hpp>
#include <fixpp/wire/parser.hpp>

namespace {
[[maybe_unused]] fixpp_version_t probe_umbrella_use() { return fixpp_library_version(); }
}  // namespace
