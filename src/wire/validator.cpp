// SPDX-License-Identifier: AGPL-3.0-or-later
// src/wire/validator.cpp — fixpp::wire out-of-line translation unit.
//
// INTENTIONALLY carries no validator algorithm. dictionary_driven_validator
// holds fixpp::dict::table_view BY VALUE ([2b §6.5]/SC-007), and table_view
// has no production definition — it is the value contract owned by 2c, only
// supplied (complete) by the seam #1 test double
// tests/support/mock_dict_table.hpp. Per that seam's single-definition rule,
// production wire code never instantiates a validator, so the class is
// HEADER-ONLY (all 5 overrides inline in include/fixpp/wire/validator.hpp,
// authored T046) and is materialised only in test TUs.
//
// This TU is retained so the fixpp_wire target's source list / build wiring
// is unchanged ([const §III.2]); it deliberately defines nothing that needs
// the (deferred) real table_view. The design doc's .cpp split assumed a real
// 2c table_view; with the 2c-deferred mock, header-only instantiation is the
// only correct C++ — recorded in specs/004-wire-codec/tasks.md Phase 6.

#include <fixpp/wire/parser.hpp>  // anchors the wire TU; no table_view here.
