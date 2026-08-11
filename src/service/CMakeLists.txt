# src/service/CMakeLists.txt
# fixpp::service — gRPC control plane + iceoryx2 data plane [arch §4.11]
# The service binary (fixppd) consumes the engine through the C ABI ONLY.
# [arch §8] — never includes engine internal headers.
# Phase 3 default — revisit when FIXPP_BUILD_SERVICE becomes functional.

add_library(fixpp_service INTERFACE)
add_library(fixpp::service ALIAS fixpp_service)

# 086 (issue #218) — the service plugin boundary, [arch §8].
#
# ⚠️ THIS LINE IS NOT INHERITED FROM fixpp_capi. Narrowing that target does not
# touch it, and every other requirement of this feature can be satisfied while
# this line survives untouched — which is exactly how the SECOND instance of the
# same defect went unrecorded until now (FR-011d). The install root here is
# declared independently and must be narrowed independently.
#
# service-iface carries ONLY include/fixpp/service/**. fixpp::service reaches the
# C ABI through its existing link to fixpp_capi below — it declares no C-ABI root
# of its own, so the two roots compose rather than duplicating.
#
# BUILD stays the whole tree: in-tree behaviour is out of scope and unchanged
# (data-model I9/I10).
target_include_directories(fixpp_service INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/service-iface>"
)

# service depends on capi only — never on C++ engine internals [arch §2.3]
target_link_libraries(fixpp_service INTERFACE fixpp_capi)
