// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_service_negative.cpp
//
// 086 T021 (US6, FR-011b) — the ❌ cell for `fixpp::service`: it MUST NOT reach a
// C++ engine header (contracts/include-interface.md §1 row 4,
// `fixpp::service` column). [arch §8] — the service consumes the engine through
// the C ABI only.
//
// ⚠️ NOT A BUILD TARGET — the SOURCE of a configure-time try_compile() asserted
// FALSE (contracts §4a; run_consumer_witness.cmake:107 FATAL_ERRORs on any
// non-zero build exit).
//
// This cell has its OWN demonstrated-red obligation and its own revert
// (the $<INSTALL_INTERFACE:...> line in src/service/CMakeLists.txt alone --
// cited by CONSTRUCT, not by line: this is an INSTRUCTION a future verifier acts
// on, and a stale number would have them revert a comment and observe nothing.
// FR-011e.) The C-ABI revert reds this
// probe too — fixpp_service links fixpp_capi — so it cannot stand in for the
// service demonstration.

#include <fixpp/wire/parser.hpp>
