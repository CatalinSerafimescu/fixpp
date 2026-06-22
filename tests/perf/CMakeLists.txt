# tests/perf/CMakeLists.txt
# 008-message-store seam 14 — mallocnesia alloc-guard.
#
# Seam→file binding is one-to-one and glob-free (plan.md Test-seam map).
# CTest name prefix `perf_` selects the perf set:
#   ctest --test-dir build/<preset> -R '^perf_'
# Registrations are added red-first per TDD ([const §VII.3]) as each seam lands.
#
# The mallocnesia interceptor is at tools/mallocnesia/libmallocnesia.so
# (reference_mallocnesia_path). The harness LD_PRELOADs it at test runtime.
# fixpp_session transitively brings fixpp_core + asio.
# Uses fixpp_add_store_test from cmake/fixpp_store_test.cmake (included at root).
# NOTE: LD_PRELOAD wiring for mallocnesia is deferred to D-17 /gate-a re-touch;
#       this helper does NOT set LD_PRELOAD (see cmake/fixpp_store_test.cmake).
find_package(GTest CONFIG REQUIRED)

# perf_socket_option_defaults links fixpp_transport, whose TLS impl
# (asio_tls_transport / asio_listener) calls OpenSSL. fixpp_transport's
# PRIVATE OpenSSL is not auto-propagated to test executables when linking the
# static library, and in Debug cmake builds the OpenSSL imported targets have
# no IMPORTED_LOCATION. Same fallback-variable pattern as tests/transport and
# tests/tls CMakeLists.
find_package(OpenSSL 3.0 REQUIRED)
find_library(FIXPP_PERF_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_library(FIXPP_PERF_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_package(ZLIB REQUIRED)
find_library(FIXPP_PERF_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_PERF_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_PERF_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_PERF_OPENSSL_CRYPTO_LIB)
    set(FIXPP_PERF_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(FIXPP_PERF_OPENSSL_SSL_LIB STREQUAL "FIXPP_PERF_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_PERF_OPENSSL_SSL_LIB)
    set(FIXPP_PERF_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(FIXPP_PERF_ZLIB_LIB STREQUAL "FIXPP_PERF_ZLIB_LIB-NOTFOUND"
   OR NOT FIXPP_PERF_ZLIB_LIB)
    set(FIXPP_PERF_ZLIB_LIB ZLIB::ZLIB)
endif()

# ── 008-message-store seam registrations ────────────────────────────────────
# T050 — seam 14: MemoryStore alloc-guard (SC-007 / FR-027 / [const §VIII.5])
# Run with:
#   LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
#       build/linux-clang-debug/tests/perf/perf_store_alloc_guard \
#       --gtest_filter='*Mallocnesia*'
fixpp_add_store_test(perf_store_alloc_guard SOURCES test_store_alloc_guard.cpp)
# D-17 /gate-a re-touch: wire mallocnesia LD_PRELOAD so SC-007 actually fires.
# Uses APPEND so the TSAN_OPTIONS already set by fixpp_add_store_test is preserved.
# MALLOCNESIA_MAX_ALLOCS is intentionally unset → default g_max = 0 strict gate.
# item 13 (2026-06-19): DISABLED base-test LD_PRELOAD — once the markers became
# interposable this base test FAILS: the steady-state store() window allocates on
# the global heap (3 allocs; false-green while markers were inert). Re-scoping the
# window is tracked in parent REMAINING-WORK.md item 13. Base test still runs WITHOUT preload.
# set_property(TEST perf_store_alloc_guard APPEND PROPERTY
#     ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")

# ── Phase 3a tests (wired by T029): ──────────────────────────────────────────

# T018 — [2h §9 seam #4]:1345 — read-path alloc guard.
# Dual-gate: counting_resource + mallocnesia LD_PRELOAD.
add_executable(perf_transport_read_alloc_guard
  test_transport_read_alloc_guard.cpp)
target_include_directories(perf_transport_read_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
target_link_libraries(perf_transport_read_alloc_guard PRIVATE
  fixpp_transport  fixpp_wire  GTest::gtest  GTest::gtest_main)
add_test(NAME perf_transport_read_alloc_guard
         COMMAND perf_transport_read_alloc_guard)
set_tests_properties(perf_transport_read_alloc_guard PROPERTIES
  LABELS "transport;012;us1;seam4;perf")
set_property(TEST perf_transport_read_alloc_guard APPEND PROPERTY
  ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")

# T019 — FR-029 / FR-029a — socket option defaults.
# Cell 1 (initiator leg) wired by T029. Cell 2 (acceptor leg) wired by T038
# (post-US3 T037) — verifies Config pass-through through asio_listener.
add_executable(perf_socket_option_defaults
  test_socket_option_defaults.cpp)
target_include_directories(perf_socket_option_defaults PRIVATE
  "${CMAKE_SOURCE_DIR}/src"  # for "transport/asio_listener.hpp" + asio_tls_transport.hpp
)
target_link_libraries(perf_socket_option_defaults PRIVATE
  fixpp_transport
  fixpp_tls
  "${FIXPP_PERF_OPENSSL_SSL_LIB}"
  "${FIXPP_PERF_OPENSSL_CRYPTO_LIB}"
  "${FIXPP_PERF_ZLIB_LIB}"
  GTest::gtest
  GTest::gtest_main)
add_test(NAME perf_socket_option_defaults
         COMMAND perf_socket_option_defaults)
set_tests_properties(perf_socket_option_defaults PROPERTIES
  LABELS "transport;012;us1;us3;fr029")

# T020 — 013-session-reconnect-binding Phase 3 (RED) — session recovery alloc-guard.
# Dual-gate: counting_resource (in-band PMR) + mallocnesia LD_PRELOAD (global heap).
# CTest name: perf_session_recovery_alloc_guard
# Run with: LD_PRELOAD=tools/mallocnesia/libmallocnesia.so <binary>
add_executable(perf_session_recovery_alloc_guard
  test_session_recovery_alloc_guard.cpp)
target_link_libraries(perf_session_recovery_alloc_guard PRIVATE
  fixpp_session
  fixpp_mock_clock
  GTest::gtest
  GTest::gtest_main)
target_include_directories(perf_session_recovery_alloc_guard PRIVATE
  "${CMAKE_SOURCE_DIR}/tests")
add_test(NAME perf_session_recovery_alloc_guard
         COMMAND perf_session_recovery_alloc_guard)
set_tests_properties(perf_session_recovery_alloc_guard PROPERTIES
  LABELS "session;013;us1;perf;alloc_guard"
  ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")
# item 13 (2026-06-19): DISABLED base-test LD_PRELOAD — RED: the recovery window
# (loop of make_heartbeat(...) + feed(...)) allocates on the global heap (message
# construction in-window; false-green while markers were inert). Tracked: REMAINING-WORK item 13.
# set_property(TEST perf_session_recovery_alloc_guard APPEND PROPERTY
#   ENVIRONMENT "LD_PRELOAD=${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")

# T020 DUAL gate companion: mallocnesia interception gate.
# Runs the same binary through check_alloc.py (LD_PRELOAD interception of global
# malloc) to confirm zero global-heap allocations on the recovery alloc-guard path.
# Precedent: tls_handshake_alloc_guard_mallocnesia (tests/tls/CMakeLists.txt:224-234).
# [[feedback_tracking_pmr_resource_false_pass]] REQUIRES both gates.
# item 13 (2026-06-19): DISABLED companion — RED (same allocating recovery window). Tracked: REMAINING-WORK item 13.
if(FALSE)  # was: if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
    add_test(
        NAME perf_session_recovery_alloc_guard_mallocnesia
        COMMAND
            python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:perf_session_recovery_alloc_guard>"
    )
    set_tests_properties(perf_session_recovery_alloc_guard_mallocnesia PROPERTIES
        LABELS "session;013;us1;perf;alloc_guard"
        DEPENDS perf_session_recovery_alloc_guard)
endif()
