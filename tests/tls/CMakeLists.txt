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

# ── Phase 3 (US1) tests ───────────────────────────────────────────────────────

# T011 — seam #9: single-thread ordering
fixpp_add_tls_test(NAME tls_pinset_single_thread_ordering
    SOURCES test_pinset_single_thread_ordering.cpp)

# T012 — seam #1: deterministic add-then-remove
fixpp_add_tls_test(NAME tls_pinset_add_then_remove_deterministic
    SOURCES test_pinset_add_then_remove_deterministic.cpp)

# T013 — seam #2: TSan stress (concurrent add/remove + find)
fixpp_add_tls_test(NAME tls_pinset_add_then_remove_stress
    SOURCES test_pinset_add_then_remove_stress.cpp)

# T014 — seam #15: rotation does not affect in-flight handshake
fixpp_add_tls_test(NAME tls_pinset_rotation_does_not_affect_in_flight
    SOURCES test_pinset_rotation_does_not_affect_in_flight.cpp)

# T015 — seam #16: pin_view lifetime under concurrent rotation
fixpp_add_tls_test(NAME tls_pin_view_lifetime_under_rotation
    SOURCES test_pin_view_lifetime_under_rotation.cpp)

# T016 — seam #18: snapshot outlives Pinset
fixpp_add_tls_test(NAME tls_pinset_snapshot_outlives_pinset
    SOURCES test_pinset_snapshot_outlives_pinset.cpp)

# T017 — FR-009a: per-counterparty sharing
fixpp_add_tls_test(NAME tls_pinset_per_counterparty_sharing
    SOURCES test_pinset_per_counterparty_sharing.cpp)

# T018 — seam #7: zero-alloc dual gate (counting_resource + mallocnesia)
fixpp_add_tls_test(NAME tls_handshake_alloc_guard
    SOURCES test_tls_handshake_alloc_guard.cpp)

# T019 — PMR failure witness (tls_pinset_alloc_failed)
fixpp_add_tls_test(NAME tls_pinset_add_pmr_fail
    SOURCES test_pinset_add_pmr_fail.cpp)

# ── Phase 4 (US2) tests ───────────────────────────────────────────────────────
# Note: Phase 4 tests that use file_cert_source (which uses OpenSSL) must
# explicitly link OpenSSL::Crypto (PRIVATE in fixpp_tls, not auto-propagated).

# T026 — seam #17: make_file_cert_source factory parity
fixpp_add_tls_test(NAME tls_make_file_cert_source_factory
    SOURCES test_make_file_cert_source_factory.cpp)
target_compile_definitions(tls_make_file_cert_source_factory PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# OpenSSL::Crypto resolves via Conan Release data even in Debug cmake build;
# use OPENSSL_CRYPTO_LIBRARY which is set by find_package(OpenSSL).
target_link_libraries(tls_make_file_cert_source_factory PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# T027 — seam #8: HSM-style async_signer_ref mock (header-only types, no OpenSSL)
fixpp_add_tls_test(NAME tls_hsm_async_signer_mock
    SOURCES test_hsm_async_signer_mock.cpp)

# T028 — seam #13: load_credentials cancellation recipe
fixpp_add_tls_test(NAME tls_load_credentials_cancellation
    SOURCES test_load_credentials_cancellation.cpp)
target_link_libraries(tls_load_credentials_cancellation PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# T029 — cold-path PMR failure witness for file_cert_source
fixpp_add_tls_test(NAME tls_file_cert_source_pmr_fail
    SOURCES test_file_cert_source_pmr_fail.cpp)
target_compile_definitions(tls_file_cert_source_pmr_fail PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(tls_file_cert_source_pmr_fail PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# ── Phase 5 (US3) tests ───────────────────────────────────────────────────────

# T035 — seam #10: CipherPolicy allow-list static_assert witnesses.
fixpp_add_tls_test(NAME tls_cipher_allow_list_static_assert
    SOURCES test_cipher_allow_list_static_assert.cpp)
target_link_libraries(tls_cipher_allow_list_static_assert PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

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

# T036 — seam #11: SecurityProfile-to-OpenSSL-mode row mapping.
# Needs OpenSSL because fixpp_tls static lib embeds certificate.cpp (OpenSSL).
# FIXPP_TLS_FIXTURE_DIR needed by the ExtractCapsReadsFileCertSourceConfig test
# (added in /speckit-verify coverage remediation — covers F-04 dynamic_cast path).
fixpp_add_tls_test(NAME tls_security_profile_mapping
    SOURCES test_security_profile_mapping.cpp)
target_compile_definitions(tls_security_profile_mapping PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(tls_security_profile_mapping PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# T037 — seam #12: all tls_* error variants exercised.
fixpp_add_tls_test(NAME tls_error_variants
    SOURCES test_tls_error_variants.cpp)
target_link_libraries(tls_error_variants PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# T038 — seam #14: verify_peer cert-parameter validation + short-circuit order.
# + T020 (014 FR-012): real Ed25519 fixture cell (sigalg_disallowed via real cert).
fixpp_add_tls_test(NAME tls_verify_peer_t039
    SOURCES test_verify_peer_t039.cpp)
target_link_libraries(tls_verify_peer_t039 PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")
target_compile_definitions(tls_verify_peer_t039 PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# T039 — /clarify Q2: empty Pinset rejection.
fixpp_add_tls_test(NAME tls_security_profile_empty_pinset
    SOURCES test_security_profile_empty_pinset.cpp)
target_link_libraries(tls_security_profile_empty_pinset PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

# T040 — FR-017: [[clang::lifetimebound]] smoke test.
fixpp_add_tls_test(NAME tls_certificate_lifetimebound
    SOURCES test_certificate_lifetimebound.cpp)
target_link_libraries(tls_certificate_lifetimebound PRIVATE
    "${FIXPP_OPENSSL_CRYPTO_LIB}" "${FIXPP_OPENSSL_SSL_LIB}" "${FIXPP_ZLIB_LIB}")

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
