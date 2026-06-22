# src/tls/CMakeLists.txt
# fixpp::tls — cert_source, Pinset, CipherPolicy, SecurityProfile [arch §4.6]
# Phase 2 (011-tls-policy T010): converted from INTERFACE to STATIC as
# certificate.cpp is the first real translation unit. OpenSSL tightened to
# REQUIRED here; top-level CMakeLists.txt keeps QUIET while this sub-tree
# may be excluded at configure time (it is always included in practice).

find_package(OpenSSL 3.0 REQUIRED)

add_library(fixpp_tls STATIC
  certificate.cpp
  pinset.cpp
  file_cert_source.cpp    # T033 — Phase 4 (US2): file_cert_source + factory
  security_profile.cpp    # T045 — Phase 5 (US3): make_ssl_ctx_config
  verify_peer.cpp         # T046 — Phase 5 (US3): verify_peer + last_handshake_sub_reason
)
add_library(fixpp::tls ALIAS fixpp_tls)

target_include_directories(fixpp_tls PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core + asio [arch §2.3 / arch §4.6].
# cert_source.hpp includes asio/awaitable.hpp (the async interface is part of
# the public API surface); asio::asio is PUBLIC so callers (tests, 2h-transport)
# that include fixpp/tls/*.hpp see the asio headers transitively.
# OpenSSL is PRIVATE (implementation detail; callers only see fixpp/tls/*.hpp
# which includes core/error.hpp, not OpenSSL headers directly).
target_link_libraries(fixpp_tls
  PUBLIC  fixpp_core
          fixpp_sync   # 046 (NFR-017): atomic_shared_ptr fallback shard_guard symbols (pinset snapshot_)
          asio::asio
  PRIVATE OpenSSL::Crypto
)
