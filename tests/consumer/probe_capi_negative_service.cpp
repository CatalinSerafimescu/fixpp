// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/consumer/probe_capi_negative_service.cpp
//
// 086 T014 (US1, FR-006) — a SECOND, DISTINCT ❌ cell for `fixpp::capi`:
// the service plugin header MUST NOT resolve either
// (contracts/include-interface.md §1 row 3, `fixpp::capi` column).
//
// Not covered by probe_capi_negative.cpp. <fixpp/service/control_plane_factory.hpp>
// is the one C++ header this feature deliberately republishes at a SECOND
// installed root (include/service-iface/), so a mis-wired `fixpp::capi` that
// picked up that root would leak this cell while the <fixpp/wire/parser.hpp>
// probe still passed. Measured FALSE at ISO=ON in research.md R4 row 4.
//
// ⚠️ NOT A BUILD TARGET — see probe_capi_negative.cpp. This is the SOURCE of a
// configure-time try_compile() asserted FALSE (contracts §4a).

#include <fixpp/service/control_plane_factory.hpp>
