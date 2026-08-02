# tests/packaging/run_real_client_witness.cmake — 084 T038/T039/T040.
#
# The REAL-CLIENT tier. Builds fixpp's own ~1700-line perf/interop driver,
# BYTE-FOR-BYTE UNMODIFIED, against the INSTALLED package, then RUNS it: a real
# Engine with initiator and acceptor sessions over a real loopback socket,
# exchanging a real FIX workload.
#
# Why this tier exists, when four packaging witnesses already pass: every other
# consumer in this feature was written FOR the witness, so the surface it reaches
# was chosen AFTER the package existed. Such a consumer can prove the package
# RESOLVES; it cannot prove it is SUFFICIENT. This driver predates the feature and
# was written for an unrelated purpose, so its demands on the package are
# independent of it. See tests/packaging/real_client/CMakeLists.txt for the asio
# propagation question this is the first witness to actually compile.
#
# ── LEGS ─────────────────────────────────────────────────────────────────────
#   T038/T039  the driver configures, BUILDS and RUNS against the staged prefix,
#              and completes the full measured round-trip count.
#   T040       the compile and link command lines carry NO producer path — not
#              the source tree, not the producing build tree.
#
# Required -D inputs:
#   FIXPP_MAIN_BUILD_DIR  — configured main build dir (build/<preset>)
#   FIXPP_SOURCE_DIR      — repo root (the driver + dictionary helper are COPIED
#                           from here; no path from here reaches a compile line)
#   FIXPP_PROJECT_SRC_DIR — tests/packaging/real_client/
#   FIXPP_WORK_DIR        — scratch dir (fresh; NOT the source tree)
#   FIXPP_CXX_COMPILER / FIXPP_C_COMPILER / FIXPP_BUILD_TYPE

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_SOURCE_DIR FIXPP_PROJECT_SRC_DIR FIXPP_WORK_DIR
             FIXPP_CXX_COMPILER FIXPP_BUILD_TYPE)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_real_client_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_stage "${FIXPP_WORK_DIR}/stage")
set(_proj  "${FIXPP_WORK_DIR}/proj")     # the COPIED witness project
set(_build "${FIXPP_WORK_DIR}/build")
set(_out   "${FIXPP_WORK_DIR}/results")

file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")

# ── 1. Stage-install ─────────────────────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (exit ${_rc}):\n${_o}\n${_e}")
endif()

# ── 2. Copy the project and the client sources OUT of the source tree ────────
# T040 is made structural rather than asserted-after-the-fact: the project is
# CONFIGURED FROM THE COPY, so there is no producer source path available to leak
# into a compile line in the first place. The check in step 5 then verifies the
# structure actually held.
file(COPY "${FIXPP_PROJECT_SRC_DIR}/" DESTINATION "${_proj}")

set(_driver_src "${FIXPP_SOURCE_DIR}/perf/fixpp_perf_driver.cpp")
if(NOT EXISTS "${_driver_src}")
  message(FATAL_ERROR
    "T038: no driver at ${_driver_src}. This tier's entire value is that it builds a "
    "REAL pre-existing client rather than one written for the witness; without the "
    "driver there is nothing to prove and the test must not report a pass.")
endif()
file(COPY "${_driver_src}" DESTINATION "${_proj}/src")

# The driver's only non-public fixpp include. COPIED rather than duplicated into
# the repo on purpose: a checked-in copy would drift silently the moment
# tests/support/minimal_dictionary.hpp changed, and the witness would go on
# compiling a stale helper while claiming to build the real client.
# "support/minimal_dictionary.hpp" is a QUOTED include, so landing it next to the
# copied .cpp resolves it with no include path at all.
set(_dict_helper "${FIXPP_SOURCE_DIR}/tests/support/minimal_dictionary.hpp")
if(NOT EXISTS "${_dict_helper}")
  message(FATAL_ERROR "T038: dictionary helper not found at ${_dict_helper}")
endif()
file(COPY "${_dict_helper}" DESTINATION "${_proj}/src/support")

# The copy must be IDENTICAL to the tracked driver. If this ever diverges, the
# tier is quietly building a modified client and its "unmodified" claim is false.
file(SHA256 "${_driver_src}"                  _driver_hash_src)
file(SHA256 "${_proj}/src/fixpp_perf_driver.cpp" _driver_hash_copy)
if(NOT _driver_hash_src STREQUAL _driver_hash_copy)
  message(FATAL_ERROR
    "T038: the copied driver differs from ${_driver_src}. This tier claims to build the "
    "client UNMODIFIED; that claim is now false.")
endif()
message(STATUS "T038: driver copied unmodified (sha256 ${_driver_hash_src})")

# ── 3. Configure against the staged prefix ───────────────────────────────────
# Producer toolchain, exactly as tests/consumer/ does. This tier is NOT the
# environment-independence tier — that is SC-016's job in run_clean_env_witness
# .cmake, which deliberately passes no toolchain. Layering both claims here would
# make a failure ambiguous about which property broke, the same reason T059 is
# kept separate from T025 and T042 from T041.
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${_proj}"
    -B "${_build}"
    -G "Ninja"
    "-DCMAKE_TOOLCHAIN_FILE=${FIXPP_MAIN_BUILD_DIR}/conan_toolchain.cmake"
    "-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}"
    "-DCMAKE_CXX_COMPILER=${FIXPP_CXX_COMPILER}"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    "-DFIXPP_STAGE_PREFIX=${_stage}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "T038: real-client configure FAILED (exit ${_rc}). find_package(fixpp) did not "
    "supply what a real async client needs.\n${_o}\n${_e}")
endif()

# ── 4. Build ─────────────────────────────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_build}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "T038: real-client BUILD failed (exit ${_rc}). The package resolved but does not "
    "carry the usage requirements this client needs -- check first whether asio's "
    "include directories propagated: engine.hpp states its public API in asio types, "
    "and no earlier witness compiles an asio header.\n${_o}\n${_e}")
endif()

# ── 5. T040: no producer path on the compile or link lines ───────────────────
# Sources: compile_commands.json (every compile line) plus the Ninja link
# variables (compile_commands.json does NOT contain the link line).
set(_cc "${_build}/compile_commands.json")
if(NOT EXISTS "${_cc}")
  message(FATAL_ERROR
    "T040: no compile_commands.json at ${_cc}, so the compile lines cannot be "
    "inspected and this gate would pass vacuously.")
endif()
file(READ "${_cc}" _lines)

set(_ninja "${_build}/build.ninja")
if(NOT EXISTS "${_ninja}")
  message(FATAL_ERROR "T040: no build.ninja at ${_ninja}; the link line cannot be inspected.")
endif()
file(READ "${_ninja}" _ninja_text)
foreach(_key LINK_LIBRARIES LINK_PATH LINK_FLAGS)
  string(REGEX MATCHALL "[^\n]*${_key} = [^\n]*" _hits "${_ninja_text}")
  foreach(_h IN LISTS _hits)
    string(APPEND _lines "\n${_h}")
  endforeach()
endforeach()
if(NOT _lines MATCHES "LINK_LIBRARIES")
  message(FATAL_ERROR
    "T040: no LINK_LIBRARIES line found in build.ninja, so the link line is not "
    "actually being inspected. Refusing to report a pass on an unchecked gate.")
endif()

# Strip the witness's OWN work dir first. Everything under it -- the staged
# prefix, the copied sources, the sub-build -- is legitimately on the compile
# line; a consumer pointing at an install prefix is the whole point. Both the
# path as given and its resolved form are stripped: build/<preset> is a symlink
# into /mnt/wsl/fixppbuild here, and a leak can surface in either form, so
# checking one shape only would false-green.
file(REAL_PATH "${FIXPP_WORK_DIR}"       _work_real)
file(REAL_PATH "${FIXPP_SOURCE_DIR}"     _src_real)
file(REAL_PATH "${FIXPP_MAIN_BUILD_DIR}" _bld_real)
string(REPLACE "${_work_real}"     "<WORK>" _lines "${_lines}")
string(REPLACE "${FIXPP_WORK_DIR}" "<WORK>" _lines "${_lines}")

# string(FIND), not MATCHES: these are literal paths and may contain regex
# metacharacters (`+` and `.` both occur in real prefixes).
foreach(_forbidden "${FIXPP_SOURCE_DIR}" "${_src_real}" "${FIXPP_MAIN_BUILD_DIR}" "${_bld_real}")
  string(FIND "${_lines}" "${_forbidden}" _at)
  if(NOT _at EQUAL -1)
    string(SUBSTRING "${_lines}" ${_at} 200 _ctx)
    message(FATAL_ERROR
      "T040 RED: a producer path leaked onto the compile/link line:\n"
      "  ${_forbidden}\n"
      "  ...${_ctx}...\n"
      "The client would be welded to this machine's checkout: the package alone does "
      "not carry what it needs.")
  endif()
endforeach()
message(STATUS "T040: compile and link lines carry no producer source-tree or build-tree path")

# ── 6. T039: RUN it ──────────────────────────────────────────────────────────
# --role loopback runs initiator AND acceptor in ONE process over a real loopback
# socket, so a live counterparty needs no second engine. --transport plain is the
# driver's default and needs no certificates, which keeps the run free of any
# source-tree fixture path (the TLS leg would need tests/tls/fixtures/).
# Counts are passed explicitly and kept small: this is a packaging witness, not a
# measurement, and the workload defaults are sized for benchmarking.
set(_measured 200)
set(_warmup 20)

set(_exe "${_build}/real_client_witness")
if(NOT EXISTS "${_exe}" AND EXISTS "${_exe}.exe")
  set(_exe "${_exe}.exe")
endif()
if(NOT EXISTS "${_exe}")
  message(FATAL_ERROR "T039: real_client_witness executable not found at ${_exe}")
endif()

execute_process(
  COMMAND "${_exe}"
    --workload wl-04-nos-er-small
    --role loopback
    --transport plain
    --warmup-msgs ${_warmup}
    --measured-msgs ${_measured}
    --out "${_out}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "T039: the real client BUILT against the package but FAILED AT RUNTIME (exit "
    "${_rc}). A package that compiles and links but cannot run is not a working "
    "package.\n${_o}\n${_e}")
endif()
message(STATUS "T039: ${_o}")

# ── 7. Assert the NAMED post-condition, not a proxy ──────────────────────────
# Exit 0 is not sufficient: the driver exits 0 having written a bundle even if
# the measured loop broke early on a timed-out round trip (`if (rtt_ns < 0)
# break;`). The post-condition is that the full measured count completed, which
# only stdout.log records.
foreach(_f summary.json run-config.yaml latency.hgrm stdout.log)
  if(NOT EXISTS "${_out}/${_f}")
    message(FATAL_ERROR "T039: the run produced no ${_f} in ${_out}")
  endif()
endforeach()

file(READ "${_out}/stdout.log" _log)
if(NOT _log MATCHES "measured_messages=([0-9]+)")
  message(FATAL_ERROR "T039: stdout.log carries no measured_messages line:\n${_log}")
endif()
set(_recorded "${CMAKE_MATCH_1}")
if(NOT _recorded EQUAL _measured)
  message(FATAL_ERROR
    "T039: only ${_recorded} of ${_measured} round trips completed. The client built "
    "against the package but could not sustain the FIX session -- a round trip timed "
    "out and the measured loop broke early.\n${_log}")
endif()

message(STATUS
  "T039: ${_recorded} full FIX round trips over a real loopback socket, from a client "
  "built solely against the installed package")
message(STATUS "fixpp::packaging::real-client: OK")
