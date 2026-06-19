# tests/config/fuzz/CMakeLists.txt
# T037 — libFuzzer harness for load_toml_config().
# 044-toml-session-config — [const §VII.7] MUST (parser-touching), Gate-B blocker.
# Built only when FIXPP_BUILD_FUZZ=ON (Clang + libFuzzer).
#
# Smoke command (≤30s, Gate-B gate: ≥10 min):
#   ./build/linux-clang-asan/bin/fuzz_toml_loader \
#       -max_total_time=20 -runs=20000 -max_len=8192 \
#       tests/config/fuzz/corpus/toml_config_loader
#
# Seed corpus: tests/config/fuzz/corpus/toml_config_loader/

# OpenSSL + ZLIB: fixpp::config_toml links fixpp_tls PRIVATE; the static TLS
# lib pulls in OpenSSL.  Mirror the boilerplate from tests/config/CMakeLists.txt
# exactly.
find_package(OpenSSL 3.0 REQUIRED)
find_library(FIXPP_CFGFUZZ_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_library(FIXPP_CFGFUZZ_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
find_package(ZLIB REQUIRED)
find_library(FIXPP_CFGFUZZ_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_CFGFUZZ_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_CFGFUZZ_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_CFGFUZZ_OPENSSL_CRYPTO_LIB)
    set(FIXPP_CFGFUZZ_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(FIXPP_CFGFUZZ_OPENSSL_SSL_LIB STREQUAL "FIXPP_CFGFUZZ_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_CFGFUZZ_OPENSSL_SSL_LIB)
    set(FIXPP_CFGFUZZ_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(FIXPP_CFGFUZZ_ZLIB_LIB STREQUAL "FIXPP_CFGFUZZ_ZLIB_LIB-NOTFOUND"
   OR NOT FIXPP_CFGFUZZ_ZLIB_LIB)
    set(FIXPP_CFGFUZZ_ZLIB_LIB ZLIB::ZLIB)
endif()

# T037 — fuzz_toml_loader: feed arbitrary bytes to load_toml_config() and
# assert the noexcept boundary never terminates or triggers UB.
# -fsanitize=fuzzer,address,undefined: address for heap/stack, undefined for
# parser internals (matches fuzz_wire_parser pattern for parser-touching targets).
add_executable(fuzz_toml_loader fuzz_toml_loader.cpp)
target_link_libraries(fuzz_toml_loader PRIVATE
    fixpp::config_toml
    "${FIXPP_CFGFUZZ_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_CFGFUZZ_OPENSSL_SSL_LIB}"
    "${FIXPP_CFGFUZZ_ZLIB_LIB}"
)
target_compile_options(fuzz_toml_loader PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_toml_loader PRIVATE -fsanitize=fuzzer,address,undefined)
set_target_properties(fuzz_toml_loader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
