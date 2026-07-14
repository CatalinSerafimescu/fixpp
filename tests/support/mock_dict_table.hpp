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
// validator surface AND the chain-style builder API (add_required, add_valid,
// set_type, set_group_first, add_group_member, add_enum, set_multi_value) that
// these tests use.
//
// No test changes are required to keep existing wire validator tests working.
// Tests that want to migrate to the production header directly may include
// <fixpp/dict/table_view.hpp> instead; this header remains for compatibility.

#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
