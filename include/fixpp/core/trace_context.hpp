// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/core/trace_context.hpp
//
// fixpp::otel::trace_context — minimal 32-byte trivially-copyable,
// standard-layout OTel correlation POD ([2d §1.2]/§4.6; research D-1; E11).
// 2k owns the full OTel surface and EXTENDS this namespace; it does NOT
// redefine this POD (one-direction dependency, [arch §2.3] layering).
//
// The 32-byte size + trivial-copyability + standard-layout are the CONTRACT,
// not advisory: the std::atomic<trace_context> snapshot's is_always_lock_free
// probe and the seqlock memcpy fallback (engine_config.hpp) both depend on it.
// Probe result (T001, 2026-05-19): is_always_lock_free == false on every
// Tier-1 STL (32 B > 16 B CAS) ⇒ the seqlock fallback path is selected, not a
// silent degrade (.specify/decisions/007-threading-clock-probes.md).
//
// NOTE: the `fixpp::current_trace_context` free awaitable (US4 / FR-015 / E8)
// is added to this header by T044; T007 ships the POD only.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixpp::otel {

struct trace_context {
    std::array<std::byte, 16> trace_id{};
    std::array<std::byte, 8>  span_id{};
    std::uint8_t              flags{};
    std::array<std::byte, 7>  _pad{};   // explicit pad to a fixed 32 B
};

static_assert(sizeof(trace_context) == 32
                  && std::is_trivially_copyable_v<trace_context>
                  && std::is_standard_layout_v<trace_context>,
              "fixpp::otel::trace_context must be a 32-byte, "
              "trivially-copyable, standard-layout POD (D-1 / E11)");

}  // namespace fixpp::otel
