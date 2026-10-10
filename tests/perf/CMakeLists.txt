# tests/perf/CMakeLists.txt
# 008-message-store seam 14 — mallocnesia alloc-guard.
#
# Seam→file binding is one-to-one and glob-free (plan.md Test-seam map).
# CTest name prefix `perf_` selects the perf set:
#   ctest --test-dir build/<preset> -R '^perf_'
# Registrations are added red-first per TDD ([const §VII.3]) as each seam lands.
#
# A window here is checked under the mallocnesia interceptor only when it is registered
# through fixpp_add_mallocnesia_test() (cmake/FixppMallocnesia.cmake); the windows that
# are not say so at their registration below.
# fixpp_session transitively brings fixpp_core + asio.
# Uses fixpp_add_store_test from cmake/fixpp_store_test.cmake (included at root).
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
# fixpp#544 (B35, §3 W-D): the steady-state store() cell is W-D, in the driver-template
# window shape, and its global-heap half is registered under the interceptor, Release-only,
# where it must read zero. ⚠️ The FileStore retrieve() cell's global-heap half is NOT CHECKED
# (disclosed, not gated). To run that window by hand:
#   GTEST_FILTER='*FileStoreRetrieve*' python3 tools/check_alloc.py \
#       --binary <build>/bin/perf_store_alloc_guard --mallocnesia <build>/lib/libmallocnesia.so
fixpp_add_store_test(perf_store_alloc_guard SOURCES test_store_alloc_guard.cpp)
fixpp_add_release_mallocnesia_test(
  NAME perf_store_alloc_guard_wd_mallocnesia
  TARGET perf_store_alloc_guard
  CASE "StoreAllocGuard.Mallocnesia_ZeroGlobalHeapStoreSteadyState"
  LABELS perf alloc_guard 544)

# ── Phase 3a tests (wired by T029): ──────────────────────────────────────────

# T018 — [2h §9 seam #4] — read-path alloc guard.
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
# ⚠️ This used to bolt LD_PRELOAD onto the base test via ENVIRONMENT, pointing at the
# hand-built source-tree .so. #448 names this site and tests/log's as the two unguarded
# ones; both ran UNINSTRUMENTED wherever that file was absent, because ld.so ignores an
# unloadable preload rather than failing. It was also invisible to
# check_mallocnesia_population.py by construction: the base test is not named
# *_mallocnesia and carried no `mallocnesia` label, so neither of the checker's two sets
# contained it. A separate gate entry fixes both — it fails closed and it is in the
# population.
#
# The window was MEASURED zero-alloc under the real interceptor before this was turned
# into a binding gate, rather than assumed from the fact that it used to pass.
fixpp_add_mallocnesia_test(
  NAME perf_transport_read_alloc_guard_mallocnesia
  TARGET perf_transport_read_alloc_guard
  LABELS "transport;012;us1;seam4;perf"
  DEPENDS "perf_transport_read_alloc_guard")

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
# CTest name: perf_session_recovery_alloc_guard
# The global-heap half: the Heartbeat steady-state cell is fixpp#544's W-A window, on the
# run-thread rig, and is registered under the interceptor below, Release-only, where it
# must read zero. ⚠️ The AwaitingResend cell's global-heap half is NOT CHECKED (resend/gap mixed
# traffic, disclosed under L-497-1). To run that window by hand:
#   GTEST_FILTER='*AwaitingResend*' python3 tools/check_alloc.py \
#       --binary <build>/bin/perf_session_recovery_alloc_guard \
#       --mallocnesia <build>/lib/libmallocnesia.so
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
fixpp_add_release_mallocnesia_test(
  NAME perf_session_recovery_heartbeat_mallocnesia
  TARGET perf_session_recovery_alloc_guard
  CASE "SessionRecoveryAllocGuardTest.HeartbeatSteadyState_DualGate"
  LABELS perf alloc_guard 544)
