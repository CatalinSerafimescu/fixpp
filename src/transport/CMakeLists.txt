# src/transport/CMakeLists.txt
# fixpp::transport — Transport interface, ASIO TCP/TLS impl [arch §4.5]
#
# Phase 2 header set (abstract interfaces + value types):
#   include/fixpp/transport/endpoint.hpp          (T008)
#   include/fixpp/transport/transport.hpp         (T009)
#   include/fixpp/transport/tls_transport.hpp     (T010)
#   include/fixpp/transport/transport_factory.hpp (T011)
#   include/fixpp/transport/transport_errors.hpp  (T007)
#
# Phase 3b: asio_tls_transport.cpp (T027) + transport_factory.cpp (T028)

add_library(fixpp_transport STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/asio_tls_transport.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/asio_plain_transport.cpp"   # 043 T009
  "${CMAKE_CURRENT_SOURCE_DIR}/transport_factory.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/reconnect_policy.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/asio_listener.cpp"
)
add_library(fixpp::transport ALIAS fixpp_transport)

target_include_directories(fixpp_transport
  PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"  # for the internal asio_tls_transport.hpp
)

# Allowed dependencies: core, tls, log (interfaces only) [arch §2.3]
# Plus OpenSSL and ASIO (SSL) for the concrete impl.
find_package(OpenSSL REQUIRED)

# OpenSSL is PUBLIC, not PRIVATE: the installed public header
# include/fixpp/transport/transport_factory.hpp #includes <asio/ssl> (which pulls
# <openssl/*.h>), so OpenSSL's include dirs are a genuine PUBLIC usage requirement
# of fixpp_transport. PRIVATE happened to work on Linux only because openssl
# headers sit on a system include path; on Windows (conan-only openssl) every
# consumer that transitively includes asio/ssl — fixpp_session/engine.cpp via the
# internal asio_listener.hpp, plus the transport/session/perf test targets — fails
# with "Cannot open include file: 'openssl/conf.h'". PUBLIC propagates the include
# dirs to all of them in one place.
target_link_libraries(fixpp_transport
  PUBLIC
    fixpp_core
    fixpp_sync   # 046 (NFR-017): atomic_shared_ptr fallback shard_guard symbols (cert_source_slot_)
    fixpp_tls
    fixpp_log
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Phase 2 public headers (installed via top-level install(DIRECTORY include/))
target_sources(fixpp_transport PUBLIC
  FILE_SET HEADERS
  BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
  FILES
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/endpoint.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/transport.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/tls_transport.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/transport_factory.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/transport_errors.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/reconnect_policy.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/listener.hpp"
    "${CMAKE_SOURCE_DIR}/include/fixpp/transport/listener_events.hpp"  # 013 T013a
)
