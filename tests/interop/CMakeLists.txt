# tests/interop/CMakeLists.txt — 016-interop-harness (T002)
#
# Session-layer interop gate. This subtree IS the test deliverable (FR-026):
# happy/ drivers + goldens, thorny/ corpus, parity/ witnesses, support/ machinery.
# Orchestration (counterparty spawn, capture proxy, badge emit) lives in the
# gitignored parent ../phase-9-harness/ — NOT here (contract:
# parent-harness-gate-contract.md).
find_package(GTest CONFIG REQUIRED)

# OpenSSL/ZLIB linkage helper for TLS-logon cells. fixpp_transport exposes asio::ssl
# PRIVATEly, so executables linking the static libs must repeat the OpenSSL link
# (same pattern as tests/transport/CMakeLists.txt).
find_package(OpenSSL 3.0 REQUIRED)
find_package(ZLIB REQUIRED)
find_library(FIXPP_INTEROP_OPENSSL_CRYPTO_LIB NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
find_library(FIXPP_INTEROP_OPENSSL_SSL_LIB NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
find_library(FIXPP_INTEROP_ZLIB_LIB NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
if(NOT FIXPP_INTEROP_OPENSSL_CRYPTO_LIB OR FIXPP_INTEROP_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_INTEROP_OPENSSL_CRYPTO_LIB-NOTFOUND")
    set(FIXPP_INTEROP_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(NOT FIXPP_INTEROP_OPENSSL_SSL_LIB OR FIXPP_INTEROP_OPENSSL_SSL_LIB STREQUAL "FIXPP_INTEROP_OPENSSL_SSL_LIB-NOTFOUND")
    set(FIXPP_INTEROP_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(NOT FIXPP_INTEROP_ZLIB_LIB OR FIXPP_INTEROP_ZLIB_LIB STREQUAL "FIXPP_INTEROP_ZLIB_LIB-NOTFOUND")
    set(FIXPP_INTEROP_ZLIB_LIB ZLIB::ZLIB)
endif()

# ── interop_support — golden diff + descriptor + fixture machinery (T004/T005/T006) ──
# golden_diff.cpp is standard-library-only; the fixture (.cpp) brings up an Engine
# and so links the session/transport/tls stack.
add_library(interop_support STATIC
    support/golden_diff.cpp
    support/interop_fixture.cpp
)
target_include_directories(interop_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"            # consumers: #include "support/..."
)
target_link_libraries(interop_support PUBLIC
    fixpp_session
    fixpp_tls
    fixpp_transport
    GTest::gtest
    # fixpp_transport/_tls expose asio::ssl (OpenSSL) PRIVATEly, so every consumer
    # executable must repeat the OpenSSL/ZLIB link. PUBLIC ⇒ inherited transitively.
    "${FIXPP_INTEROP_OPENSSL_SSL_LIB}"
    "${FIXPP_INTEROP_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_INTEROP_ZLIB_LIB}"
)
target_compile_definitions(interop_support PUBLIC
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)

# ── Aggregation targets the quickstart references (cmake --build --target interop_*) ──
# Empty at Phase 1 ("builds an empty suite"); per-phase tasks attach drivers via
# add_dependencies(interop_<group> <test_target>).
add_custom_target(interop_happy)
add_custom_target(interop_thorny)
add_custom_target(interop_parity)

# ── fixpp_add_interop_test — used by US1/US2/US3 phase tasks ──────────────────
# fixpp_add_interop_test(NAME <exe> GROUP <happy|thorny|parity> LABEL <ctest-label>
#                        SOURCES <src...> [TLS])
# TLS ⇒ link OpenSSL/ZLIB + define the fixture dir for tls-logon cells.
function(fixpp_add_interop_test)
    set(options TLS)
    set(oneValue NAME GROUP LABEL)
    set(multiValue SOURCES)
    cmake_parse_arguments(IT "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    add_executable(${IT_NAME} ${IT_SOURCES})
    target_link_libraries(${IT_NAME} PRIVATE
        interop_support
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(${IT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/src"      # concrete transport/listener headers (tests-only)
        "${CMAKE_SOURCE_DIR}/tests"    # support/minimal_dictionary.hpp, loopback fixtures
    )
    if(IT_TLS)
        target_link_libraries(${IT_NAME} PRIVATE
            "${FIXPP_INTEROP_OPENSSL_SSL_LIB}"
            "${FIXPP_INTEROP_OPENSSL_CRYPTO_LIB}"
            "${FIXPP_INTEROP_ZLIB_LIB}"
        )
        target_compile_definitions(${IT_NAME} PRIVATE
            FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
        )
    endif()
    add_test(NAME ${IT_NAME} COMMAND ${IT_NAME})
    set_tests_properties(${IT_NAME} PROPERTIES LABELS "${IT_LABEL}")
    add_dependencies(interop_${IT_GROUP} ${IT_NAME})
endfunction()

# ── support smoke (T002 Phase-1 checkpoint: the suite configures + builds + runs) ──
# Runtime-validates the support layer: golden parse/diff + descriptor + probe token.
fixpp_add_interop_test(
    NAME interop_support_smoke_test
    GROUP happy
    LABEL "interop;016;interop-support"
    SOURCES support_smoke_test.cpp
)
