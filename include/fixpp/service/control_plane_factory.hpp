// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/service/control_plane_factory.hpp
//
// fixpp::service::ControlPlaneFactory — MINIMAL abstract interface stub.
// EngineConfig holds a std::unique_ptr<ControlPlaneFactory> (data-model E4
// pinned shape; "null permitted"), so the value type needs this base
// COMPLETE now. Same "minimal real skeleton, downstream extends" pattern as
// D-1 / D-4: the concrete control-plane factory + its full surface are owned
// by 2j; this feature ships only the polymorphic bind target.
#pragma once

namespace fixpp::service {

class ControlPlaneFactory {
public:
    ControlPlaneFactory()                                      = default;
    ControlPlaneFactory(const ControlPlaneFactory&)            = delete;
    ControlPlaneFactory& operator=(const ControlPlaneFactory&) = delete;
    ControlPlaneFactory(ControlPlaneFactory&&)                 = delete;
    ControlPlaneFactory& operator=(ControlPlaneFactory&&)      = delete;
    virtual ~ControlPlaneFactory()                             = default;
    // Full control-plane surface owned by 2j (extends this base).
};

}  // namespace fixpp::service
