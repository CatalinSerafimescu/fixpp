// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/otel/trace_context.hpp
//
// Canonical home for fixpp::otel::trace_context in the otel module.
//
// The struct itself is defined in include/fixpp/core/trace_context.hpp
// (owned by 007-threading-clock, [2d §1.2]) in the fixpp::otel namespace
// because it is a shared trivial POD used by both the core threading
// machinery (seqlock snapshot, session_local<>) and the otel module.
//
// This header re-exports fixpp::otel::trace_context from the otel module's
// include tree so that otel/ consumers can write:
//     #include <fixpp/otel/trace_context.hpp>
// without depending on the core/ include path directly.
//
// 017 owned amendment #3 / contracts/adjacent-amendments.md T011:
// "confirm/alias fixpp::otel::trace_context over the existing
//  fixpp::core::trace_context (32 B: 16-B trace_id + 8-B span_id +
//  1-B flags + pad)".
//
// Clarification: trace_context is ALREADY defined in fixpp::otel (not
// fixpp::core). The existing core/trace_context.hpp puts the struct in
// namespace fixpp::otel. This otel-module header simply re-includes it so
// otel/ module sources have a canonical otel/ include path to use.
//
// Anchor: [2d §1.2] / [2k §4.2] / data-model.md §trace_context.
#pragma once

#include <fixpp/core/trace_context.hpp>

// fixpp::otel::trace_context is defined in the included header above
// (in namespace fixpp::otel). No additional alias needed.
