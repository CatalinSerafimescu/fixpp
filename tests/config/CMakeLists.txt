# tests/config/CMakeLists.txt
# 044-toml-session-config Phase 3 (US1) test executables.
#
# T011 + T012 — happy-path load: field-for-field equivalence (T011) +
# selector-build witness (T012). Both RED in Phase 3a (no mappers yet).
# T035 (Polish) will add the remaining 4 binaries here.

find_package(GTest CONFIG REQUIRED)

# OpenSSL + ZLIB: required because test_load_happy_path links fixpp::config_toml
# which links fixpp::tls (PRIVATE), and TLS's static lib pulls in OpenSSL;
# the PRIVATE chain doesn't auto-propagate to test executables.
# Mirror the boilerplate from tests/tls/CMakeLists.txt exactly.
find_package(OpenSSL 3.0 REQUIRED)
find_library(FIXPP_CFG_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_library(FIXPP_CFG_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_package(ZLIB REQUIRED)
find_library(FIXPP_CFG_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_CFG_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_CFG_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_CFG_OPENSSL_CRYPTO_LIB)
    set(FIXPP_CFG_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(FIXPP_CFG_OPENSSL_SSL_LIB STREQUAL "FIXPP_CFG_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_CFG_OPENSSL_SSL_LIB)
    set(FIXPP_CFG_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(FIXPP_CFG_ZLIB_LIB STREQUAL "FIXPP_CFG_ZLIB_LIB-NOTFOUND" OR NOT FIXPP_CFG_ZLIB_LIB)
    set(FIXPP_CFG_ZLIB_LIB ZLIB::ZLIB)
endif()

# ── T011 + T012: happy-path load ─────────────────────────────────────────────

add_executable(test_load_happy_path
    test_load_happy_path.cpp
)
target_compile_definitions(test_load_happy_path PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_load_happy_path PRIVATE
    fixpp::config_toml
    fixpp::session
    fixpp::core
    fixpp::tls
    fixpp::dictionary
    fixpp::transport
    GTest::gtest
    GTest::gtest_main
    "${FIXPP_CFG_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_CFG_OPENSSL_SSL_LIB}"
    "${FIXPP_CFG_ZLIB_LIB}"
)
set_target_properties(test_load_happy_path PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_load_happy_path COMMAND test_load_happy_path)
set_tests_properties(test_load_happy_path PROPERTIES LABELS "config;044")

# ── T018 + T019: negative-battery load ───────────────────────────────────────

add_executable(test_load_negative_battery
    test_load_negative_battery.cpp
)
target_compile_definitions(test_load_negative_battery PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_load_negative_battery PRIVATE
    fixpp::config_toml
    fixpp::session
    fixpp::core
    fixpp::tls
    fixpp::dictionary
    fixpp::transport
    GTest::gtest
    GTest::gtest_main
    "${FIXPP_CFG_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_CFG_OPENSSL_SSL_LIB}"
    "${FIXPP_CFG_ZLIB_LIB}"
)
set_target_properties(test_load_negative_battery PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_load_negative_battery COMMAND test_load_negative_battery)
set_tests_properties(test_load_negative_battery PROPERTIES LABELS "config;044")
