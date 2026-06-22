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

# T017 — [2h §9 seam #15] in-flight exclusivity (Phase 3a + RC#E runtime cells).
add_executable(transport_inflight_exclusivity
  test_inflight_exclusivity.cpp)
target_include_directories(transport_inflight_exclusivity PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp"
  "${CMAKE_SOURCE_DIR}/tests" # for "transport/loopback_tls_fixture.hpp"
)
target_link_libraries(transport_inflight_exclusivity PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(transport_inflight_exclusivity PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_inflight_exclusivity
         COMMAND transport_inflight_exclusivity)
set_tests_properties(transport_inflight_exclusivity PROPERTIES
  LABELS "transport;012;us1;seam15;gate-b-witness;rc-e")

# ── T030 — [2h §9 seam #13] ReconnectPolicy schedule + delay_for_attempt ─────

add_executable(transport_reconnect_policy_schedule
  test_reconnect_policy_schedule.cpp)
target_link_libraries(transport_reconnect_policy_schedule PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_reconnect_policy_schedule
         COMMAND transport_reconnect_policy_schedule)
set_tests_properties(transport_reconnect_policy_schedule PROPERTIES
  LABELS "transport;012;us2;seam13;reconnect")

# ── T039 — [2h §9 seam #10] mock_transport backpressure + FR-037 partial-write ─

add_executable(transport_backpressure
  test_backpressure.cpp)
target_link_libraries(transport_backpressure PRIVATE
  fixpp_transport
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_backpressure
         COMMAND transport_backpressure)
set_tests_properties(transport_backpressure PROPERTIES
  LABELS "transport;012;us4;seam10;mock")

# ── T034 — [2h §9 seam #14] asio_listener accept + cancel Option-A contract ─

add_executable(transport_listener_acceptor
  test_listener_acceptor.cpp)
target_include_directories(transport_listener_acceptor PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp"
  "${CMAKE_SOURCE_DIR}/tests" # for "transport/loopback_tls_fixture.hpp"
)
# OpenSSL must be linked explicitly: fixpp_transport uses asio::ssl (which
# pulls in OpenSSL symbols) but exposes it as PRIVATE, so executables that
# link the static library must repeat the OpenSSL link themselves.
target_link_libraries(transport_listener_acceptor PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(transport_listener_acceptor PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_listener_acceptor
         COMMAND transport_listener_acceptor)
set_tests_properties(transport_listener_acceptor PROPERTIES
  LABELS "transport;012;us3;seam14;gate-b-witness;rc-a")

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

add_executable(transport_listener_accept_error_cancel
  test_listener_accept_error_cancel.cpp)
target_include_directories(transport_listener_accept_error_cancel PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(transport_listener_accept_error_cancel PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main)
add_test(NAME transport_listener_accept_error_cancel
         COMMAND transport_listener_accept_error_cancel)
set_tests_properties(transport_listener_accept_error_cancel PROPERTIES
  LABELS "transport;012;us3;seam14;accept-error;cancel")

add_executable(transport_factory_paths
  test_transport_factory_paths.cpp)
target_include_directories(transport_factory_paths PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_link_libraries(transport_factory_paths PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(transport_factory_paths PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_factory_paths
         COMMAND transport_factory_paths)

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

# ── RC#D (gate-b/r1 + r2) — truncated-close vs EOF distinct variant witness ──
#
# SC-006: stream_truncated → transport_read_truncated (distinct from transport_read_eof).
# FR-006 internal contradiction resolved: distinct-named-variant path wins over
# the "v1.0 treats as eof" parenthetical.
# gate-b/r2 adds a LIVE loopback witness (drop peer TCP without SSL_shutdown).

add_executable(transport_close_truncated_mapping
  test_close_truncated_mapping.cpp)
target_include_directories(transport_close_truncated_mapping PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp"
  "${CMAKE_SOURCE_DIR}/tests" # for "transport/loopback_tls_fixture.hpp"
)
target_link_libraries(transport_close_truncated_mapping PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(transport_close_truncated_mapping PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_close_truncated_mapping
         COMMAND transport_close_truncated_mapping)
set_tests_properties(transport_close_truncated_mapping PROPERTIES
  LABELS "transport;012;gate-b-witness;rc-d;sc006")

# ── 013 Phase 5 T038 — TLS validation outcome → session_event ring taxonomy ──
#
# 5 master-enum cells (tls_handshake_failed/expired, tls_rsa_key_too_large,
# tls_cert_der_too_large, tls_san_entries_exceeded, tls_pin_mismatch) +
# sub_reason sub-cells (expired, tls_pin_empty_at_open) via real TLS loopback.
# tls_load_cancelled NOT exercisable via this path (factory-construction failure).
# [013 FR-026 / FR-027 / FR-028 / D-15 / data-model §E-5/§E-6]

add_executable(transport_tls_validation_failed_taxonomy
  test_tls_validation_failed_taxonomy.cpp)
target_include_directories(transport_tls_validation_failed_taxonomy PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_listener.hpp"
  "${CMAKE_SOURCE_DIR}/tests" # for "transport/loopback_tls_fixture.hpp"
)
target_link_libraries(transport_tls_validation_failed_taxonomy PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
)
target_compile_definitions(transport_tls_validation_failed_taxonomy PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_tls_validation_failed_taxonomy
         COMMAND transport_tls_validation_failed_taxonomy)
set_tests_properties(transport_tls_validation_failed_taxonomy PROPERTIES
  LABELS "transport;013;us3;tls;taxonomy;gate-b-witness"
  TIMEOUT 60)

# ── RC#C (gate-b/r1) — verify_peer noexcept PMR OOM boundary witness ──────────
#
# Tests that verify_peer(noexcept) does NOT propagate std::bad_alloc when the PMR
# resource throws on the N-th allocation. Covers boundary (N=1), mid (N=2),
# tail (N=3), and no-throw success per [[feedback_trap_throw_pmr_witness_enumerate_sites]].
# Design anchor: spec.md FR-035; .specify/2h-transport.md §1 item 5.

add_executable(transport_verify_peer_pmr_oom
  test_verify_peer_pmr_oom.cpp)
target_link_libraries(transport_verify_peer_pmr_oom PRIVATE
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_verify_peer_pmr_oom
         COMMAND transport_verify_peer_pmr_oom)
set_tests_properties(transport_verify_peer_pmr_oom PROPERTIES
  LABELS "transport;012;gate-b-witness;rc-c;pmr-oom;tls")

add_executable(transport_asio_tls_transport_error_paths
  test_asio_tls_transport_error_paths.cpp)
target_include_directories(transport_asio_tls_transport_error_paths PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/tests"
)
target_link_libraries(transport_asio_tls_transport_error_paths PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_TRANSPORT_OPENSSL_SSL_LIB}"
  "${FIXPP_TRANSPORT_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_TRANSPORT_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main
)
target_compile_definitions(transport_asio_tls_transport_error_paths PRIVATE
  FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
add_test(NAME transport_asio_tls_transport_error_paths
         COMMAND transport_asio_tls_transport_error_paths)

# ── 043 T005 — asio_plain_transport loopback live-I/O witness (SC-001) ─────────
#
# Tests: connect/read/write cycle, no-TLS-ClientHello (first byte 0x38),
# cancellation → transport_read_cancelled, post-close → transport_already_closed,
# idempotent close, factory smoke (kind/reload_credentials/cert_source_snapshot),
# make_accepted() → connected transport.
# No OpenSSL link required (plain TCP, no TLS).

add_executable(transport_asio_plain_transport
  test_asio_plain_transport.cpp)
target_include_directories(transport_asio_plain_transport PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_plain_transport.hpp"
)
target_link_libraries(transport_asio_plain_transport PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_asio_plain_transport
         COMMAND transport_asio_plain_transport)
set_tests_properties(transport_asio_plain_transport PROPERTIES
  LABELS "transport;043;us1;sc001;plain"
  TIMEOUT 60)

# ── 043 T006 — asio_plain_transport Config TCP knob + fast-close witness ────────
#
# Tests: tcp_nodelay observable on established socket (SC-008 / FR-010),
# close() returns in < 500ms with tls_close_timeout=2s in cfg (FR-011),
# no TLS close-notify bytes on close, ConnectInfo populated, second connect →
# transport_already_connected.
# No OpenSSL link required (plain TCP, no TLS).

add_executable(transport_asio_plain_transport_config
  test_asio_plain_transport_config.cpp)
target_include_directories(transport_asio_plain_transport_config PRIVATE
  "${CMAKE_SOURCE_DIR}/src"   # for "transport/asio_plain_transport.hpp"
)
target_link_libraries(transport_asio_plain_transport_config PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_asio_plain_transport_config
         COMMAND transport_asio_plain_transport_config)
set_tests_properties(transport_asio_plain_transport_config PROPERTIES
  LABELS "transport;043;us1;sc008;plain;config"
  TIMEOUT 60)

# ── 046-atomic-shared-ptr (NFR-017) transport consumer witnesses ──────────────

# T008 (plan row 6-consumers): publish/acquire witness for
# asio_tls_transport_factory::cert_source_slot_
# (fixpp::sync::atomic_shared_ptr<fixpp::tls::cert_source>).
# Writer thread calls reload_credentials(); reader threads call cert_source_snapshot().
# Assertions: each reader verifies the returned pointer is a known stub instance;
# a torn pointer would fail dynamic_cast or return an out-of-range id.
add_executable(transport_factory_cert_source_publish_acquire
  test_transport_factory_cert_source_publish_acquire.cpp)
target_link_libraries(transport_factory_cert_source_publish_acquire PRIVATE
  fixpp_transport
  fixpp_tls
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_factory_cert_source_publish_acquire
         COMMAND transport_factory_cert_source_publish_acquire)
set_tests_properties(transport_factory_cert_source_publish_acquire PROPERTIES
  LABELS "transport;046;nfr017;publish_acquire"
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp"
  TIMEOUT 30)

# T019 (plan row +B): forced-fallback link test.
# Constructs fixpp::sync::atomic_shared_ptr<int> in a binary that links
# fixpp_transport; proves shard_guard symbols resolve transitively through
# fixpp_transport → fixpp_sync.
add_executable(transport_forced_fallback_link
  test_transport_forced_fallback_link.cpp)
target_link_libraries(transport_forced_fallback_link PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_forced_fallback_link
         COMMAND transport_forced_fallback_link)
set_tests_properties(transport_forced_fallback_link PROPERTIES
  LABELS "transport;046;nfr017;fallback_link")
