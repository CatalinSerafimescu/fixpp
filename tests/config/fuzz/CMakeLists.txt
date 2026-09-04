# tests/config/fuzz/CMakeLists.txt
# T037 — libFuzzer harness for load_toml_config().
# 044-toml-session-config — [const §VII.7] MUST (parser-touching), Gate-B blocker.
# Built only when FIXPP_BUILD_FUZZ=ON (Clang + libFuzzer).
#
# Smoke command (≤30s, Gate-B gate: ≥10 min):
#   ./build/linux-clang-asan/bin/fuzz_toml_loader \
#       -max_total_time=20 -runs=20000 -max_len=8192 \
#       tests/config/fuzz/crashes
#
# ⚠️ THE PATH ABOVE USED TO NAME `tests/config/fuzz/corpus/toml_config_loader/`,
# which HAS NEVER EXISTED — not on disk and not in history. This target has no
# seed corpus; what it has is the tracked crash reproducer in `crashes/`, which
# is what the command now points at. Same class as #209's FINDINGS.md: a
# citation to a path nobody ever created, which reads as coverage and is none.
# Re-derive before trusting either path: `ls tests/config/fuzz/`.

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


# ── Crash-reproducer replay (#213) ───────────────────────────────────────────
#
# `crashes/repro_toml_assert_assume.toml` is a TRACKED reproducer for a crash
# this harness once found, and nothing replayed it — the regression it documents
# was unguarded. #213 is about exactly that state for the `corpus/` trees; this
# is the same defect in a directory the corpus glob does not cover, so it is
# fixed here rather than by widening that glob to a differently-shaped dir.
#
# Mechanics shared with tests/fuzz/ via fixpp_add_fuzz_replay() — see its header
# in cmake/Helpers.cmake for why -runs=0 and the artifact redirection are not
# adjustable per call site.
set(_fixpp_toml_crashes "${CMAKE_CURRENT_SOURCE_DIR}/crashes")

file(GLOB _fixpp_toml_crash_files CONFIGURE_DEPENDS "${_fixpp_toml_crashes}/*")
if(NOT _fixpp_toml_crash_files)
  # An empty scan is an instrument failure, not a clean build: the registration
  # would silently replay nothing while still reporting green.
  message(FATAL_ERROR
    "tests/config/fuzz/crashes/ is empty or missing, so fuzz_replay_toml_crashes would "
    "replay nothing while still reporting green. Restore the reproducer or delete this block.")
endif()

fixpp_add_fuzz_replay(fuzz_replay_toml_crashes fuzz_toml_loader "${_fixpp_toml_crashes}")
