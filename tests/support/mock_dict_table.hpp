#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/mock_dict_table.hpp
// Seam #1 — historically a concrete, test-owned definition of fixpp::dict::table_view.
//
// 041-validation-gate-wiring (T009 RC-A closure): the real value-typed
// `fixpp::dict::table_view` and `fixpp::dict::field_type` now live in
// `include/fixpp/dict/table_view.hpp` and `include/fixpp/dict/field_type.hpp`.
// `validator.hpp` includes them directly for a complete type (no longer
// forward-declaration-only).
//
// This header is now a COMPATIBILITY SHIM: it simply includes the production
// headers to satisfy test TUs that still include "support/mock_dict_table.hpp"
// before validator.hpp. The production `table_view` provides the same 6-method
// validator surface; since fixpp#456 its chain-style population API is PRIVATE
// and reached only through `fixpp::dict::table_view_builder`, which the same
// header declares — so a test that populates a table names the builder, not the
// view.
//
// Including this header is still enough to keep an existing wire validator test
// working; fixpp#456 changed what a test WRITES, not what it includes — a test
// that populates a table now declares a `table_view_builder` and finishes with
// `build()`. Tests that want the production header directly may include
// <fixpp/dict/table_view.hpp> instead; this header remains for compatibility.

#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
