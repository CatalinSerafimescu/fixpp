# tests/transport/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

# OpenSSL needed for T044/T045 (seam #13 witness) that exercises file_cert_source.
# fixpp_tls's PRIVATE OpenSSL::Crypto is not auto-propagated to test executables
# when linking the static library. Same pattern as tests/tls/CMakeLists.txt.
find_package(OpenSSL 3.0 REQUIRED)
find_library(FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_library(FIXPP_TRANSPORT_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_package(ZLIB REQUIRED)
find_library(FIXPP_TRANSPORT_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB)
    set(FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(FIXPP_TRANSPORT_OPENSSL_SSL_LIB STREQUAL "FIXPP_TRANSPORT_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_TRANSPORT_OPENSSL_SSL_LIB)
    set(FIXPP_TRANSPORT_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(FIXPP_TRANSPORT_ZLIB_LIB STREQUAL "FIXPP_TRANSPORT_ZLIB_LIB-NOTFOUND"
   OR NOT FIXPP_TRANSPORT_ZLIB_LIB)
    set(FIXPP_TRANSPORT_ZLIB_LIB ZLIB::ZLIB)
endif()

add_executable(transport_smoke_test transport_smoke_test.cpp)
target_link_libraries(transport_smoke_test PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_smoke COMMAND transport_smoke_test)

# ── Phase 3a tests — partial wire-up. ────────────────────────────────────────
#
# T014..T017 have runnable Phase 3a unit tests (Phase 2 abstract types only).
# They also contain DISABLED_ integration cells that require asio_tls_transport.
# T029 enables the DISABLED_ cells after asio_tls_transport (T026/T027) ships.
#
# T013 (test_cancellation_propagation.cpp): comment-blocked because its
# forward declaration of make_asio_tls_transport requires the Phase 3b impl
# to satisfy the linker. Wired by T029.

# T014 — [2h §9 seam #7] pinset rotation unit tests (Phase 3a runnable).
add_executable(transport_tls_handshake_pinset_rotation
  test_tls_handshake_pinset_rotation.cpp)
target_link_libraries(transport_tls_handshake_pinset_rotation PRIVATE
  fixpp_transport
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_tls_handshake_pinset_rotation
         COMMAND transport_tls_handshake_pinset_rotation)
set_tests_properties(transport_tls_handshake_pinset_rotation PROPERTIES
  LABELS "transport;012;us1;seam7;tls")

# T015 — [2h §9 seam #8] durable-before-transmit contract shape (Phase 3a).
add_executable(transport_durable_before_transmit_ordering
  test_durable_before_transmit_ordering.cpp)
target_link_libraries(transport_durable_before_transmit_ordering PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_durable_before_transmit_ordering
         COMMAND transport_durable_before_transmit_ordering)
set_tests_properties(transport_durable_before_transmit_ordering PROPERTIES
  LABELS "transport;012;us1;seam8")

# T016 — [2h §9 seam #9] CompID/TLS identity binding (Phase 3a unit tests).
add_executable(transport_compid_tls_identity_binding
  test_compid_tls_identity_binding.cpp)
target_link_libraries(transport_compid_tls_identity_binding PRIVATE
  fixpp_transport
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_compid_tls_identity_binding
         COMMAND transport_compid_tls_identity_binding)
set_tests_properties(transport_compid_tls_identity_binding PROPERTIES
  LABELS "transport;012;us1;seam9;tls")

# T017 — [2h §9 seam #15] in-flight exclusivity (Phase 3a error code asserts).
add_executable(transport_inflight_exclusivity
  test_inflight_exclusivity.cpp)
target_link_libraries(transport_inflight_exclusivity PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_inflight_exclusivity
         COMMAND transport_inflight_exclusivity)
set_tests_properties(transport_inflight_exclusivity PROPERTIES
  LABELS "transport;012;us1;seam15")

# ── T029 wiring: transport_cancellation_propagation ──────────────────────────

# T013 — [2h §9 seam #5] — cancellation propagation contract (Phase 3b wiring).
add_executable(transport_cancellation_propagation
  test_cancellation_propagation.cpp)
target_link_libraries(transport_cancellation_propagation PRIVATE
  fixpp_transport  fixpp_tls  GTest::gtest  GTest::gtest_main)
add_test(NAME transport_cancellation_propagation
         COMMAND transport_cancellation_propagation)
set_tests_properties(transport_cancellation_propagation PROPERTIES
  LABELS "transport;012;us1;seam5")

# ── T044/T045: 011 Gate B F-1 carryover — seam #13 witness ───────────────────
#
# FR-033 + SC-008: 8-cell cancellable_dispatch-recipe witness
# (4 deterministic cases × 2 executor modes) using a real Session* +
# session_executor. Requires fixpp_session (for Session + make_session_executor)
# + fixpp_tls (for file_cert_source fixtures).
# Design anchors: [2g §6.4]:926-929, [2d §6.5], [2d §4.8].

add_executable(transport_load_credentials_seam13_witness
  test_load_credentials_seam13_witness.cpp)
target_link_libraries(transport_load_credentials_seam13_witness PRIVATE
  fixpp_session
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_include_directories(transport_load_credentials_seam13_witness PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
target_compile_definitions(transport_load_credentials_seam13_witness PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_load_credentials_seam13_witness
         COMMAND transport_load_credentials_seam13_witness)
set_tests_properties(transport_load_credentials_seam13_witness PROPERTIES
  LABELS "transport;012;gate-b-witness;seam13;tls")
