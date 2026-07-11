# tests/tls/CMakeLists.txt
# 011-tls-policy Phase 3 (US1) test executables.
# Phase 2 foundational types have no standalone tests
# (exercised by Phase 3/4/5 tests per plan.md Phase 2 rationale).
# Phase 3 tests land here per T025.
find_package(GTest CONFIG REQUIRED)
# OpenSSL needed for Phase 4 (US2) tests that exercise file_cert_source:
# fixpp_tls's PRIVATE OpenSSL::Crypto is not auto-propagated to test
# executables when linking the static library.
find_package(OpenSSL 3.0 REQUIRED)
# Conan-generated OpenSSL targets use $<$<CONFIG:Release>:...> generator
# expressions; in Debug cmake builds, OpenSSL::Crypto has no IMPORTED_LOCATION.
# Work around by discovering the actual library path from the conan data variables.
# openssl_PACKAGE_FOLDER_RELEASE and openssl_LIB_DIRS_RELEASE are set by the
# Conan-generated module data file (module-OpenSSL-*-data.cmake).
find_library(FIXPP_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_library(FIXPP_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_package(ZLIB REQUIRED)  # OpenSSL statically linked against zlib
find_library(FIXPP_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
# In non-debug presets (asan, ubsan, tsan, coverage, gcc-release), Conan does not
# generate the OpenSSL/ZLIB data files, so the find_library vars resolve to NOTFOUND.
# Fall back to the imported targets set up by find_package(OpenSSL) / find_package(ZLIB),
# which are always available from either the Conan-generated or system FindOpenSSL.cmake.
if(FIXPP_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_OPENSSL_CRYPTO_LIB)
    set(FIXPP_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(FIXPP_OPENSSL_SSL_LIB STREQUAL "FIXPP_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_OPENSSL_SSL_LIB)
    set(FIXPP_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(FIXPP_ZLIB_LIB STREQUAL "FIXPP_ZLIB_LIB-NOTFOUND" OR NOT FIXPP_ZLIB_LIB)
    set(FIXPP_ZLIB_LIB ZLIB::ZLIB)
endif()

# Helper function: add a TLS GTest executable.
# Usage: fixpp_add_tls_test(NAME <name> SOURCES <src...>)
function(fixpp_add_tls_test)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE
        fixpp::tls
        GTest::gtest
        GTest::gtest_main
    )
    set_target_properties(${ARG_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    set_tests_properties(${ARG_NAME} PROPERTIES LABELS "tls;011")
endfunction()

# ── Grouped bucket: pure TLS tests (14 .cpp → 1 binary) ──────────────────────
# 068-test-binary-grouping: all 14 share LABELS "tls;011" (the fixpp_add_tls_test
# default, unmodified) — no genuinely-concurrent thread/thread_pool, no
# real socket, no global allocation-counting (PMR failure witnesses use a
# LOCAL std::pmr::monotonic_buffer_resource with null_memory_resource
# upstream, per-instance — isolation-safe, D3 groupable). OpenSSL::Crypto/SSL
# + ZLIB::ZLIB + FIXPP_TLS_FIXTURE_DIR are unioned onto every member — a
# harmless no-op for the ones that don't reference them (same pattern as the
# `config`/`dictionary` module buckets). Dispositions in
# specs/068-test-binary-grouping/dispositions.md.
fixpp_add_tls_test(NAME tls_pure_tests
    SOURCES
        test_pinset_single_thread_ordering.cpp         # T011 seam #9
        test_pinset_add_then_remove_deterministic.cpp  # T012 seam #1
        test_pinset_snapshot_outlives_pinset.cpp        # T016 seam #18
        test_pinset_per_counterparty_sharing.cpp        # T017 FR-009a
        test_pinset_add_pmr_fail.cpp                    # T019 PMR failure witness (local pmr)
        test_make_file_cert_source_factory.cpp          # T026 seam #17
        test_load_credentials_cancellation.cpp          # T028 seam #13 (single-threaded ioc.run())
        test_file_cert_source_pmr_fail.cpp              # T029 cold-path PMR witness (local pmr)
        test_cipher_allow_list_static_assert.cpp         # T035 seam #10
        test_security_profile_mapping.cpp                # T036 seam #11
        test_tls_error_variants.cpp                      # T037 seam #12
        test_verify_peer_t039.cpp                         # T038/T020 seam #14
        test_security_profile_empty_pinset.cpp            # T039 /clarify Q2
        test_certificate_lifetimebound.cpp                # T040 FR-017
)
target_compile_definitions(tls_pure_tests PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# OpenSSL::Crypto resolves via Conan Release data even in Debug cmake build;
# use OPENSSL_CRYPTO_LIBRARY which is set by find_package(OpenSSL).
target_link_libraries(tls_pure_tests PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# ── Phase 3 (US1) standalone tests (FR-002) ──────────────────────────────────

# T013 — seam #2: TSan stress (concurrent add/remove + find)
fixpp_add_tls_test(NAME tls_pinset_add_then_remove_stress
    SOURCES test_pinset_add_then_remove_stress.cpp)

# T014 — seam #15: rotation does not affect in-flight handshake
fixpp_add_tls_test(NAME tls_pinset_rotation_does_not_affect_in_flight
    SOURCES test_pinset_rotation_does_not_affect_in_flight.cpp)

# T015 — seam #16: pin_view lifetime under concurrent rotation
fixpp_add_tls_test(NAME tls_pin_view_lifetime_under_rotation
    SOURCES test_pin_view_lifetime_under_rotation.cpp)

# T018 — seam #7: zero-alloc dual gate (counting_resource + mallocnesia)
fixpp_add_tls_test(NAME tls_handshake_alloc_guard
    SOURCES test_tls_handshake_alloc_guard.cpp)
target_include_directories(tls_handshake_alloc_guard PRIVATE
    "${CMAKE_SOURCE_DIR}/tests")

# ── Phase 4 (US2) standalone tests ───────────────────────────────────────────
# Note: Phase 4 tests that use file_cert_source (which uses OpenSSL) must
# explicitly link OpenSSL::Crypto (PRIVATE in fixpp_tls, not auto-propagated).

# T027 — seam #8: HSM-style async_signer_ref mock (asio::thread_pool)
fixpp_add_tls_test(NAME tls_hsm_async_signer_mock
    SOURCES test_hsm_async_signer_mock.cpp)

# ── Phase 5 (US3) standalone tests ───────────────────────────────────────────

# T035 negative-compile: source in cipher_policy_banned_negative.cpp contains a
# banned CBC token and MUST fail with a static_assert at build time (FR-011).
# Registered as a CTest with WILL_FAIL TRUE — build failure = test pass.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_executable(tls_cipher_banned_negative_compile EXCLUDE_FROM_ALL
        cipher_policy_banned_negative.cpp)
    target_include_directories(tls_cipher_banned_negative_compile PRIVATE
        "${CMAKE_SOURCE_DIR}/include")
    target_compile_features(tls_cipher_banned_negative_compile PRIVATE cxx_std_23)
    add_test(
        NAME tls_cipher_allow_list_negative_compile
        COMMAND cmake --build "${CMAKE_BINARY_DIR}" --target tls_cipher_banned_negative_compile
    )
    set_tests_properties(tls_cipher_allow_list_negative_compile PROPERTIES
        WILL_FAIL TRUE
        LABELS "tls;011;compile_negative"
    )
endif()

# T036 deprecated-enumerator try_compile: referencing SecurityProfile::one_way_ca
# with -Wdeprecated-declarations -Werror MUST fail (the attribute fires a warning
# which becomes an error). Source: security_profile_deprecated_negative.cpp.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_executable(tls_security_profile_deprecated_negative EXCLUDE_FROM_ALL
        security_profile_deprecated_negative.cpp)
    target_include_directories(tls_security_profile_deprecated_negative PRIVATE
        "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(tls_security_profile_deprecated_negative PRIVATE
        fixpp::tls)
    target_compile_features(tls_security_profile_deprecated_negative PRIVATE cxx_std_23)
    target_compile_options(tls_security_profile_deprecated_negative PRIVATE
        -Wdeprecated-declarations -Werror=deprecated-declarations)
    add_test(
        NAME tls_security_profile_deprecated_compile
        COMMAND cmake --build "${CMAKE_BINARY_DIR}" --target tls_security_profile_deprecated_negative
    )
    set_tests_properties(tls_security_profile_deprecated_compile PROPERTIES
        WILL_FAIL TRUE
        LABELS "tls;011;compile_negative"
    )
endif()

# T036 / T037 / T038 / T020 / T039 / T040 registered in the tls_pure_tests
# bucket above (068-test-binary-grouping).

# ── 046-atomic-shared-ptr (NFR-017) TLS consumer witnesses ───────────────────

# T008 (plan row 6-consumers): publish/acquire witness for Pinset::snapshot_
# (fixpp::sync::atomic_shared_ptr<const pin_snapshot>).
# Two-thread: writer add/remove on main; reader find/snapshot on std::thread.
# TSan is the ordering oracle; subject_dn comparison discriminates torn reads.
fixpp_add_tls_test(NAME tls_pinset_snapshot_publish_acquire
    SOURCES test_pinset_snapshot_publish_acquire.cpp)
set_tests_properties(tls_pinset_snapshot_publish_acquire PROPERTIES
    LABELS "tls;011;046;nfr017;publish_acquire"
    ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_SOURCE_DIR}/tools/tsan/asio.supp")

# T019 (plan row +B): forced-fallback link test.
# Constructs fixpp::sync::atomic_shared_ptr<int> in a binary that links
# fixpp::tls; proves shard_guard symbols resolve transitively through fixpp::tls.
fixpp_add_tls_test(NAME tls_forced_fallback_link
    SOURCES test_tls_forced_fallback_link.cpp)
set_tests_properties(tls_forced_fallback_link PROPERTIES
    LABELS "tls;011;046;nfr017;fallback_link")

# ── T018 mallocnesia gate ─────────────────────────────────────────────────────
# Registers a second CTest entry that invokes check_alloc.py to intercept
# global malloc via LD_PRELOAD. Only runs if mallocnesia.so is present.
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/mallocnesia/libmallocnesia.so")
    add_test(
        NAME tls_handshake_alloc_guard_mallocnesia
        COMMAND
            python3 "${CMAKE_SOURCE_DIR}/tools/check_alloc.py"
            "--binary" "$<TARGET_FILE:tls_handshake_alloc_guard>"
    )
    set_tests_properties(tls_handshake_alloc_guard_mallocnesia PROPERTIES
        LABELS "tls;011;alloc_guard"
        DEPENDS tls_handshake_alloc_guard
    )
endif()
