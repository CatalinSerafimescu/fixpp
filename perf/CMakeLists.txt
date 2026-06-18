# perf/CMakeLists.txt — Phase 9 fixpp benchmark driver (gated FIXPP_BUILD_INTEROP_PERF).
#
# The fixpp side of the cross-engine perf rig. Built only when
# FIXPP_BUILD_INTEROP_PERF=ON (default OFF) so it never affects ordinary CI
# builds. Links the session/transport/tls stack exactly like the interop tests
# (tests/interop/CMakeLists.txt) — same OpenSSL/ZLIB static-link-order recipe —
# plus HdrHistogram_c for latency.hgrm emission (same dependency the parent
# ../../phase-9-harness/quickfix-cpp runner uses, so both engines emit a
# standard .hgrm).

include(FetchContent)

find_package(Threads REQUIRED)

# OpenSSL/ZLIB: fixpp_transport/_tls expose asio::ssl (OpenSSL) PRIVATEly, so an
# executable linking the static libs must repeat the OpenSSL/ZLIB link, and the
# fixpp libs must precede libcrypto on the link line (see tests/interop notes).
find_package(OpenSSL 3.0 REQUIRED)
find_package(ZLIB REQUIRED)
find_library(FIXPP_PERF_OPENSSL_CRYPTO_LIB NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
find_library(FIXPP_PERF_OPENSSL_SSL_LIB NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
find_library(FIXPP_PERF_ZLIB_LIB NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
if(NOT FIXPP_PERF_OPENSSL_CRYPTO_LIB OR FIXPP_PERF_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_PERF_OPENSSL_CRYPTO_LIB-NOTFOUND")
    set(FIXPP_PERF_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(NOT FIXPP_PERF_OPENSSL_SSL_LIB OR FIXPP_PERF_OPENSSL_SSL_LIB STREQUAL "FIXPP_PERF_OPENSSL_SSL_LIB-NOTFOUND")
    set(FIXPP_PERF_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(NOT FIXPP_PERF_ZLIB_LIB OR FIXPP_PERF_ZLIB_LIB STREQUAL "FIXPP_PERF_ZLIB_LIB-NOTFOUND")
    set(FIXPP_PERF_ZLIB_LIB ZLIB::ZLIB)
endif()

# HdrHistogram_c — same tag as phase-9-harness/quickfix-cpp/CMakeLists.txt.
set(HDR_LOG_REQUIRED ON CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_INSTALL_SHARED OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_INSTALL_STATIC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    HdrHistogram_c
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG be60a9987ee48d0abf0d7b6a175bad8d6c1585d1)
FetchContent_MakeAvailable(HdrHistogram_c)

add_executable(fixpp_perf_driver fixpp_perf_driver.cpp)

target_include_directories(fixpp_perf_driver PRIVATE
    "${CMAKE_SOURCE_DIR}/src"      # concrete transport/session headers (as tests do)
    "${CMAKE_SOURCE_DIR}/tests"    # support/minimal_dictionary.hpp
)

# fixpp libs FIRST (immediately before OpenSSL) so static-link order resolves the
# OpenSSL symbols libfixpp_tls.a references (mirror tests/interop/CMakeLists.txt).
target_link_libraries(fixpp_perf_driver PRIVATE
    fixpp_session
    fixpp_transport
    fixpp_tls
    "${FIXPP_PERF_OPENSSL_SSL_LIB}"
    "${FIXPP_PERF_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_PERF_ZLIB_LIB}"
    hdr_histogram_static
    Threads::Threads
)
target_compile_definitions(fixpp_perf_driver PRIVATE
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)
set_target_properties(fixpp_perf_driver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
