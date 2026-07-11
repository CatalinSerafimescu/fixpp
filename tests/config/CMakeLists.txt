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

find_package(tomlplusplus CONFIG REQUIRED)

# ── 068-test-binary-grouping US2: config_044_tests grouped bucket (5 .cpp) ──
# T011/T012 happy-path + T018/T019 negative-battery + T026/T028 selectors +
# T031 multisession-defaults + T030 QuickFIX parity table. All 5 previously
# carried the identical LABELS "config;044" (D4 bucket key) — pure TOML-load
# unit tests, no thread/global-alloc/int-main (grepped). Union of link-libs:
# test_quickfix_parity_table needs only gtest ("Pure data test — no loader
# link needed" per its own comment); linking the config/session/tls stack for
# it too is a harmless no-op.
add_executable(config_044_tests
    test_load_happy_path.cpp
    test_load_negative_battery.cpp
    test_load_selectors.cpp
    test_load_multisession_defaults.cpp
    test_quickfix_parity_table.cpp
)
target_compile_definitions(config_044_tests PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(config_044_tests PRIVATE
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
set_target_properties(config_044_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME config_044_tests COMMAND config_044_tests)
set_tests_properties(config_044_tests PROPERTIES LABELS "config;044")

# ── 068-test-binary-grouping US2: config_045_tests grouped bucket (4 .cpp) ──
# T008/T009 logger equivalence + T014/T014a/T015 logger negative battery +
# T018/T019 multi-session logger overrides + T020/T021 deferred surface. All
# 4 previously carried the identical LABELS "config;045" (D4 bucket key).
# White-box cells (logger.cpp/logger_negative.cpp/logger_overrides.cpp) need
# src/config/ (private headers) + toml++; deferred_surface.cpp doesn't
# reference them — harmless additive union (same pattern as 044's bucket).
add_executable(config_045_tests
    test_load_logger.cpp
    test_load_logger_negative.cpp
    test_load_logger_overrides.cpp
    test_load_deferred_surface.cpp
)
target_compile_definitions(config_045_tests PRIVATE
    FIXPP_CONFIG_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
# White-box cells (T008_OtlpSinkCount et al.) call detail::resolve_engine_logger /
# detail::construct_loggers_if_clean directly, which require mappers.hpp from
# src/config/ (private header) and toml++ headers. The toml_include.hpp ODR shim
# is in src/config/; linking tomlplusplus::tomlplusplus directly is permitted
# here because this target never exposes toml++ in its installed headers (it is
# a test binary, not a library).
target_include_directories(config_045_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/config"
)
target_link_libraries(config_045_tests PRIVATE
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
    target_link_libraries(config_045_tests PRIVATE fixpp::log_otlp)
    target_compile_definitions(config_045_tests PRIVATE FIXPP_CONFIG_HAS_OTLP)
endif()
set_target_properties(config_045_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME config_045_tests COMMAND config_045_tests)
set_tests_properties(config_045_tests PROPERTIES LABELS "config;045")
