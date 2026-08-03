# tests/packaging/real_client/CMakeLists.txt — 084 T038/T039/T040.
#
# The REAL-CLIENT witness: fixpp's own perf/interop driver, built UNMODIFIED
# against the INSTALLED package.
#
# ── What makes this different from the other four packaging witnesses ────────
# tests/consumer/ and tests/packaging/clean_env/ are consumers written FOR the
# witness: a few dozen lines each, touching a handful of headers. They prove the
# package resolves. They cannot prove it is SUFFICIENT, because the surface they
# reach was chosen after the package existed.
#
# fixpp_perf_driver.cpp was not. It is ~1700 lines written for a different
# purpose (the cross-engine perf rig), it predates this feature, and it reaches
# 16 fixpp headers across core/session/transport/tls/wire/dict, brings up a real
# Engine with initiator AND acceptor sessions over a real loopback socket, and
# runs a FIX workload end to end. If the package under-declares anything a
# serious async client needs, this is the tier that finds it.
#
# ── The asio question this tier answers FIRST ────────────────────────────────
# include/fixpp/session/engine.hpp is a PUBLIC header whose PUBLIC API is stated
# in asio types — `asio::awaitable<...> send(...)`, `Engine(asio::any_io_executor,
# ...)`, and asio data members. asio is therefore an INTERFACE dependency, and
# the export must propagate its include directories or no async consumer can
# compile a single line.
#
# No previous witness included an asio header. consumer_witness.cpp reaches
# dict/wire/v44 only; clean_env_consumer.cpp less than that. So until this file
# existed, `asio::asio` propagating bare (rather than wrapped in $<LINK_ONLY:>)
# through fixpp_session/fixpp_tls was asserted by reading fixppTargets.cmake, and
# by nothing that compiles. This target is what turns that into evidence.
#
# ── DEVIATION FROM tasks.md T038 — recorded, not silent ──────────────────────
# tasks.md T038 says "add the packaged-variant path to perf/CMakeLists.txt".
# This is a separate standalone project instead, for two reasons:
#
#   1. perf/ is shared with the interop rig (another workstream's live
#      QuickFIX/Fix8 comparison). A packaged-variant branch inside its
#      CMakeLists would put a second dependency-resolution mechanism into a file
#      that workstream owns and rebuilds.
#   2. T040 requires the compile and link lines to carry NO producer source-tree
#      path. A target defined inside perf/ inherits the top-level directory
#      scope, so satisfying T040 there would mean proving a negative about
#      inherited state. Here the project() boundary makes it structural: this
#      file is CONFIGURED FROM A COPY under the witness work dir, so there is no
#      source-tree path available to leak in the first place.
#
# The driver source itself is used byte-for-byte UNMODIFIED — that is the point
# of the tier and the reason the deviation is safe. See
# run_real_client_witness.cmake, which does the copying.
cmake_minimum_required(VERSION 3.28)
project(fixpp_real_client_witness CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT FIXPP_STAGE_PREFIX)
  message(FATAL_ERROR "FIXPP_STAGE_PREFIX (staged `cmake --install` prefix) must be set")
endif()

# The ONLY fixpp hint given — same contract as tests/consumer/. No include
# directory, no library directory, no archive name, no fixpp-dependency
# find_package(): anything more would weaken the claim (SC-001).
list(PREPEND CMAKE_PREFIX_PATH "${FIXPP_STAGE_PREFIX}")

find_package(fixpp REQUIRED)

# Threads is the CLIENT'S own dependency, not something fixpp should declare:
# fixpp_perf_driver.cpp itself spawns std::thread and uses std::mutex/condition
# _variable for its round-trip loop. A client needing Threads::Threads for its
# OWN std::thread use is not a gap in fixpp's package.
find_package(Threads REQUIRED)

# src/ and shim/ are both populated by run_real_client_witness.cmake INSIDE the
# work dir, next to this copied file. Relative paths only — an absolute path back
# to the producer tree is exactly what T040 forbids.
add_executable(real_client_witness src/fixpp_perf_driver.cpp)

# One include directory for BOTH witness-local shims: the HdrHistogram
# stand-in (shim/hdr/…, angle-bracket <hdr/hdr_histogram.h>) and the dictionary
# helper stand-in (shim/support/…, quoted "support/minimal_dictionary.hpp" —
# quoted includes fall back to the compiler's normal include search path when
# no sibling file of that name exists next to the copied .cpp, which is the
# case here since run_real_client_witness.cmake no longer copies
# tests/support/minimal_dictionary.hpp into the scratch project; see
# shim/support/minimal_dictionary.hpp for why).
target_include_directories(real_client_witness PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shim")

# The dictionary shim loads the SHIPPED FIX42.xml from the staged install
# prefix through the public XmlLoader API (see shim/support/minimal_dictionary
# .hpp). FIXPP_STAGE_PREFIX is already known here (passed by
# run_real_client_witness.cmake at configure time, above).
target_compile_definitions(real_client_witness PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_STAGE_PREFIX}/share/fixpp/dictionaries\"")

# fixpp::fixpp is the umbrella. Note what is NOT here, all of which
# perf/CMakeLists.txt needs when building in-tree against raw archives:
#   * find_package(OpenSSL)/find_package(ZLIB) + the three find_library() probes
#   * the hand-ordered fixpp_session/transport/tls → ssl → crypto → z link line
#   * "${CMAKE_SOURCE_DIR}/src" and "${CMAKE_SOURCE_DIR}/tests" include dirs
# The exported target graph supplies every one of them. Their absence here is a
# load-bearing part of the assertion, not a simplification.
target_link_libraries(real_client_witness PRIVATE fixpp::fixpp Threads::Threads)
