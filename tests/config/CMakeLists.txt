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

# ── T026 + T028: selector-kind matrix + deferred/guard cells ─────────────────

add_executable(test_load_selectors
    test_load_selectors.cpp
)
target_compile_definitions(test_load_selectors PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_load_selectors PRIVATE
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
set_target_properties(test_load_selectors PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_load_selectors COMMAND test_load_selectors)
set_tests_properties(test_load_selectors PROPERTIES LABELS "config;044")

# ── T031: multi-session [default]+[[session]] defaults test ──────────────────

add_executable(test_load_multisession_defaults
    test_load_multisession_defaults.cpp
)
target_compile_definitions(test_load_multisession_defaults PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_load_multisession_defaults PRIVATE
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
set_target_properties(test_load_multisession_defaults PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_load_multisession_defaults COMMAND test_load_multisession_defaults)
set_tests_properties(test_load_multisession_defaults PROPERTIES LABELS "config;044")

# ── T008 + T009: logger equivalence + absence tests (045) ───────────────────

find_package(tomlplusplus CONFIG REQUIRED)

add_executable(test_load_logger
    test_load_logger.cpp
)
target_compile_definitions(test_load_logger PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
# White-box cells (T008_OtlpSinkCount) call detail::resolve_engine_logger directly,
# which requires mappers.hpp from src/config/ (private header) and toml++ headers.
# The toml_include.hpp ODR shim is in src/config/; linking tomlplusplus::tomlplusplus
# directly is permitted here because this target never exposes toml++ in its
# installed headers (it is a test binary, not a library).
target_include_directories(test_load_logger PRIVATE
    "${CMAKE_SOURCE_DIR}/src/config"
)
target_link_libraries(test_load_logger PRIVATE
    fixpp::config_toml
    fixpp::session
    fixpp::core
    fixpp::tls
    fixpp::dictionary
    fixpp::transport
    fixpp::log
    tomlplusplus::tomlplusplus
    GTest::gtest
    GTest::gtest_main
    "${FIXPP_CFG_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_CFG_OPENSSL_SSL_LIB}"
    "${FIXPP_CFG_ZLIB_LIB}"
)
# Propagate the OTLP gate to the test binary so #ifdef FIXPP_CONFIG_HAS_OTLP
# compiles the same sub-cells as the loader.  fixpp::log is PRIVATE to
# fixpp_config_toml and does NOT auto-propagate; link it explicitly above.
if(TARGET fixpp::log_otlp)
    target_link_libraries(test_load_logger PRIVATE fixpp::log_otlp)
    target_compile_definitions(test_load_logger PRIVATE FIXPP_CONFIG_HAS_OTLP)
endif()
set_target_properties(test_load_logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_load_logger COMMAND test_load_logger)
set_tests_properties(test_load_logger PROPERTIES LABELS "config;045")

# ── T014/T014a/T015: logger negative battery + redaction + collect-ALL (045) ──

add_executable(test_load_logger_negative
    test_load_logger_negative.cpp
)
target_compile_definitions(test_load_logger_negative PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
# White-box cells call detail::resolve_engine_logger and
# detail::construct_loggers_if_clean (private headers in src/config/).
target_include_directories(test_load_logger_negative PRIVATE
    "${CMAKE_SOURCE_DIR}/src/config"
)
target_link_libraries(test_load_logger_negative PRIVATE
    fixpp::config_toml
    fixpp::session
    fixpp::core
    fixpp::tls
    fixpp::dictionary
    fixpp::transport
    fixpp::log
    tomlplusplus::tomlplusplus
    GTest::gtest
    GTest::gtest_main
    "${FIXPP_CFG_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_CFG_OPENSSL_SSL_LIB}"
    "${FIXPP_CFG_ZLIB_LIB}"
)
if(TARGET fixpp::log_otlp)
    target_link_libraries(test_load_logger_negative PRIVATE fixpp::log_otlp)
    target_compile_definitions(test_load_logger_negative PRIVATE FIXPP_CONFIG_HAS_OTLP)
endif()
set_target_properties(test_load_logger_negative PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_load_logger_negative COMMAND test_load_logger_negative)
set_tests_properties(test_load_logger_negative PROPERTIES LABELS "config;045")

# ── T030: QuickFIX parity table (SC-004) ─────────────────────────────────────
# Pure data test — no loader link needed; GTest only.

add_executable(test_quickfix_parity_table
    test_quickfix_parity_table.cpp
)
target_link_libraries(test_quickfix_parity_table PRIVATE
    GTest::gtest
    GTest::gtest_main
)
set_target_properties(test_quickfix_parity_table PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME test_quickfix_parity_table COMMAND test_quickfix_parity_table)
set_tests_properties(test_quickfix_parity_table PROPERTIES LABELS "config;044")
