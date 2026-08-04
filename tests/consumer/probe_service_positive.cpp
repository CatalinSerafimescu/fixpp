// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_service_positive.cpp
//
// 086 T020 (US6, FR-011a) — the ✅ cells for `fixpp::service`
// (contracts/include-interface.md §1, `fixpp::service` column):
// the plugin header AND the C ABI must both resolve.
//
// The C-ABI half is not incidental. `fixpp::service` declares NO C-ABI include
// root of its own — it reaches <fix/c_api.h> transitively through its existing
// link to fixpp::capi (src/service/CMakeLists.txt:30, contract §2). Narrowing
// fixpp::capi is exactly the change that could sever that path, so this TU is
// what keeps FR-011a honest.
//
// COMPILE-ONLY (OBJECT library, no main) — contracts §4 / research.md R5.
//
// Pairs with probe_service_negative.cpp; neither alone establishes the row
// (FR-008a).

#include <fix/c_api.h>

#include <fixpp/service/control_plane_factory.hpp>

namespace {
[[maybe_unused]] fixpp_version_t probe_service_use() { return fixpp_library_version(); }
}  // namespace
