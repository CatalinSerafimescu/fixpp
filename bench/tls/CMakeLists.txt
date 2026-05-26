# bench/tls/CMakeLists.txt
# 011-tls-policy benches (Phase 3 US1 + Phase 5 US3 — [2g §6.3]).
# T021/T022: Pinset snapshot + find latency (Phase 3).
# T041: verify_peer latency (Phase 5 — placeholder; target added when Phase 5 lands).
find_package(benchmark CONFIG REQUIRED)

# Helper: add a TLS benchmark executable.
function(fixpp_add_tls_bench name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE
        fixpp::tls
        benchmark::benchmark
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    # Register as a CTest entry; it emits JSON/stdout results but is soft-gate only.
    add_test(NAME ${name} COMMAND ${name} --benchmark_format=console)
    set_tests_properties(${name} PROPERTIES LABELS "tls;011;bench")
endfunction()

# T021 — seam #4: Pinset::snapshot() p99 ceiling.
fixpp_add_tls_bench(bench_pinset_snapshot_acquire bench_pinset_snapshot_acquire.cpp)

# T022 — seam #5: Pinset::find(sha256) at max_pins=16, p99 ceiling.
fixpp_add_tls_bench(bench_pinset_find bench_pinset_find.cpp)

# T041 — Phase 5 (US3): verify_peer p99 latency per [2g §6.3].
# Soft-gate: records timing; no hard CI fail in the bench file itself.
# Needs OpenSSL because fixpp_tls static lib embeds certificate.cpp (OpenSSL PRIVATE).
fixpp_add_tls_bench(bench_verify_peer_in_envelope bench_verify_peer_in_envelope.cpp)
# OpenSSL link: same pattern as Phase 4 tests.
find_package(OpenSSL 3.0 REQUIRED)
find_library(FIXPP_BENCH_OPENSSL_CRYPTO_LIB
    NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_BENCH_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_BENCH_OPENSSL_CRYPTO_LIB-NOTFOUND"
   OR NOT FIXPP_BENCH_OPENSSL_CRYPTO_LIB)
    set(FIXPP_BENCH_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
find_library(FIXPP_BENCH_OPENSSL_SSL_LIB
    NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_BENCH_OPENSSL_SSL_LIB STREQUAL "FIXPP_BENCH_OPENSSL_SSL_LIB-NOTFOUND"
   OR NOT FIXPP_BENCH_OPENSSL_SSL_LIB)
    set(FIXPP_BENCH_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
find_package(ZLIB REQUIRED)
find_library(FIXPP_BENCH_ZLIB_LIB
    NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}"
    NO_DEFAULT_PATH
)
if(FIXPP_BENCH_ZLIB_LIB STREQUAL "FIXPP_BENCH_ZLIB_LIB-NOTFOUND" OR NOT FIXPP_BENCH_ZLIB_LIB)
    set(FIXPP_BENCH_ZLIB_LIB ZLIB::ZLIB)
endif()
target_link_libraries(bench_verify_peer_in_envelope PRIVATE
    "${FIXPP_BENCH_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_BENCH_OPENSSL_SSL_LIB}"
    "${FIXPP_BENCH_ZLIB_LIB}")
