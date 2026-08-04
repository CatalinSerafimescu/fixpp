# tests/consumer/run_consumer_witness.cmake — 061-slim T024 (FR-008/SC-004) driver.
#
# Invoked via `cmake -P` as a ctest (see the add_test(... "fixpp::consumer::install-witness")
# registration in the top-level CMakeLists.txt). Mirrors the tests/codegen T039
# cmake -P build-graph-test pattern: no hard-coded paths, everything comes in via -D.
#
# Steps (SC-004 — external consumer TU compiles AND links against the INSTALLED
# package, no build-tree-private path):
#   1. `cmake --install` the main build into a fresh staging prefix.
#   2. Configure tests/consumer/ as a STANDALONE CMake project (own project(),
#      not add_subdirectory()'d) pointed ONLY at the staged include dir.
#   3. Build it.
#   4. Run the resulting executable against the real FIX44.xml and assert it
#      printed PASS and exited 0.
#
# Required -D inputs:
#   FIXPP_MAIN_BUILD_DIR   — the configured main build dir (build/<preset>);
#                            supplies conan_toolchain.cmake + lib/ archives.
#   FIXPP_SOURCE_DIR       — repo root; supplies dictionaries/FIX44.xml (runtime
#                            data, not a header — reading it is not a
#                            build-tree-private *header* path per SC-004).
#   FIXPP_CONSUMER_SRC_DIR — tests/consumer/ (this directory).
#   FIXPP_WITNESS_WORK_DIR — scratch dir for the staged install + consumer
#                            sub-build (created fresh; NOT the source tree).

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_SOURCE_DIR FIXPP_CONSUMER_SRC_DIR FIXPP_WITNESS_WORK_DIR
             FIXPP_CXX_COMPILER FIXPP_C_COMPILER)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_consumer_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_stage    "${FIXPP_WITNESS_WORK_DIR}/stage")
set(_sub_build "${FIXPP_WITNESS_WORK_DIR}/build")

# Build the consumer in the SAME config as the main build so the Conan-resolved
# pugixml (a private link dep of fixpp_dictionary) has an imported location for
# the sub-build's config. A Debug consumer against a Release main build leaves
# pugixml::pugixml empty -> undefined pugixml refs from xml_loader.cpp.o.
# FIXPP_BUILD_TYPE is passed by the top-level add_test; default to Debug if a
# multi-config generator left CMAKE_BUILD_TYPE empty there.
if(NOT DEFINED FIXPP_BUILD_TYPE OR FIXPP_BUILD_TYPE STREQUAL "")
  set(FIXPP_BUILD_TYPE "Debug")
endif()

file(REMOVE_RECURSE "${_stage}" "${_sub_build}")

# ── 1. Stage-install the main build ──────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"
  RESULT_VARIABLE _install_rc
  OUTPUT_VARIABLE _install_out
  ERROR_VARIABLE  _install_err
)
if(NOT _install_rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (exit ${_install_rc}):\n${_install_out}\n${_install_err}")
endif()

# FR-008 sub-assertion: v42/v44/v50sp2 present, _dispatch/vt11 absent.
foreach(_ver v42 v44 v50sp2)
  if(NOT EXISTS "${_stage}/include/fixpp/${_ver}/Messages.hpp")
    message(FATAL_ERROR "Staged install missing fixpp/${_ver}/Messages.hpp — T023 install rule regressed")
  endif()
endforeach()
if(EXISTS "${_stage}/include/fixpp/_dispatch")
  message(FATAL_ERROR "Staged install leaked build-tree-private fixpp/_dispatch/ (must be excluded)")
endif()
if(EXISTS "${_stage}/include/fixpp/vt11")
  message(FATAL_ERROR "Staged install leaked fixpp/vt11/ (must be excluded per FR-008)")
endif()

# ── 2. Configure the standalone consumer project ─────────────────────────────
# Reuses the main build's Conan toolchain (resolves pugixml identically to the
# main build — no separate dependency-resolution mechanism) but points the
# consumer's ONLY include path at the staged prefix (never the build tree or
# source include/ — see tests/consumer/CMakeLists.txt).
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${FIXPP_CONSUMER_SRC_DIR}"
    -B "${_sub_build}"
    -G "Ninja"
    "-DCMAKE_TOOLCHAIN_FILE=${FIXPP_MAIN_BUILD_DIR}/conan_toolchain.cmake"
    "-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}"
    "-DCMAKE_CXX_COMPILER=${FIXPP_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${FIXPP_C_COMPILER}"
    # 086 T053: the sub-build does not go through CMakePresets.json, so it never
    # picks up the _base preset's CMAKE_EXPORT_COMPILE_COMMANDS (:12). Without a
    # compile DB here, clang-tidy cannot be pointed at the new probe TUs at all.
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    "-DFIXPP_STAGE_PREFIX=${_stage}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE  _cfg_err
)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR "consumer configure failed (exit ${_cfg_rc}):\n${_cfg_out}\n${_cfg_err}")
endif()

# ── 3. Build it — BY NAME, so a deleted gate fails closed ────────────────────
#
# 086 / Gate B r1 P1 #3. A bare `cmake --build` builds whatever targets happen to
# exist, so DELETING the positive probes or `consumer_capi_witness` left this
# driver perfectly green — there were simply fewer things to build. The ❌ cells
# had gained a read-back; the ✅ cells and the link-closure witness had no
# equivalent, and "the gate can be removed without anything noticing" is the same
# defect class as "the gate cannot fail".
#
# Naming them makes their absence a hard error: CMake fails with
# "No rule to make target" if any is renamed or removed.
set(_required_targets
  consumer_witness           # the umbrella witness, run at step 4
  consumer_capi_witness      # FR-009 transitive-link closure (built + linked, never run)
  probe_capi_positive        # ✅ all 12 C-ABI headers, C++
  probe_capi_positive_c      # ✅ all 12 C-ABI headers, C
  probe_service_positive     # ✅ fixpp::service reaches the plugin header AND the C ABI
  probe_umbrella             # ✅ the umbrella still reaches everything
  probe_usage_requirements)  # C-3 leg 3 carrier
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_sub_build}" --target ${_required_targets}
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE  _build_err
)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "consumer build failed (exit ${_build_rc}). NOTE: this driver builds the 086 "
    "witness targets BY NAME (${_required_targets}), so a 'No rule to make target' "
    "here means a gate was deleted or renamed, not that the code is broken.\n"
    "${_build_out}\n${_build_err}")
endif()

# ── 3a. 086 FR-006/FR-007 — read back the NEGATIVE-PROBE table ───────────────
#
# Without this block the try_compile probes assert only against THEMSELVES: they
# raise FATAL_ERROR during the consumer configure, so DELETING THE WHOLE PROBE
# BLOCK from tests/consumer/CMakeLists.txt would make the configure succeed and
# this driver report green. A gate that vanishes silently when removed is the
# same defect class as a gate that cannot fail.
#
# The expected labels are named here on purpose. Adding a fourth ❌ cell must
# force an edit to this list — a count-only check would let a new cell be added
# and silently never asserted.
set(_probe_file "${_sub_build}/probe-results.txt")
if(NOT EXISTS "${_probe_file}")
  message(FATAL_ERROR
    "086 FR-006: ${_probe_file} was not written — the negative-probe block is "
    "missing from tests/consumer/CMakeLists.txt, so no ❌ cell of the §1 "
    "reachability matrix is being asserted at all.")
endif()
file(READ "${_probe_file}" _probe_txt)
message(STATUS "086 negative-probe table:\n${_probe_txt}")
foreach(_cell "capi->engine-header" "capi->service-header" "service->engine-header")
  if(NOT _probe_txt MATCHES "(^|\n)${_cell}: reachable=FALSE(\n|$)")
    message(FATAL_ERROR
      "086 FR-006: the ❌ cell '${_cell}' is not recorded as reachable=FALSE in "
      "${_probe_file}. Either the probe was removed, renamed, it resolved a header "
      "it must not reach (reachable=TRUE), or it could not answer at all "
      "(reachable=BROKEN).\nTable was:\n${_probe_txt}")
  endif()
endforeach()
message(STATUS "086 FR-006: OK — all three ❌ cells asserted unreachable")

# ── 3b. 086 FR-009a(ii) / C-3 leg 3 — read back the usage-requirement probe ───
#
# tests/consumer/CMakeLists.txt writes this file with file(GENERATE), which runs
# at GENERATE time and asserts nothing by itself. The compare has to live
# downstream of the sub-build, which is here. Without this block the generated
# file is written and never read, and leg 3 does not exist.
#
# What is being checked: `fixpp::capi` carries `$<LINK_ONLY:fixpp::capi_objects>`,
# and $<LINK_ONLY:> withholds COMPILE_DEFINITIONS, COMPILE_OPTIONS,
# COMPILE_FEATURES and SYSTEM_INCLUDE_DIRECTORIES along with the include path. So
# a target that links only fixpp::capi must end up with an EMPTY effective set for
# all three. Asserting empty — rather than "does not contain FIXPP_LOG_MIN_LEVEL"
# — is deliberate: it is a closed assertion, so a definition nobody predicted
# fails it too. (The withheld set today is at least FIXPP_LOG_MIN_LEVEL, from
# src/log/CMakeLists.txt:27, and ASIO_STANDALONE, carried by asio::asio linked
# unwrapped inside the closure. Membership is decided by this predicate, not by
# that list.) Instrument measured in research.md R10.
set(_usage_file "${_sub_build}/usage-requirements.txt")
if(NOT EXISTS "${_usage_file}")
  message(FATAL_ERROR
    "086 FR-009a(ii): ${_usage_file} was not generated — the usage-requirement probe "
    "is missing from tests/consumer/CMakeLists.txt, so C-3 leg 3 asserts nothing.")
endif()
file(READ "${_usage_file}" _usage_txt)
message(STATUS "086 usage requirements at the C-ABI consumer:\n${_usage_txt}")
foreach(_prop COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES)
  # Both sides are READ, never invented here: tests/consumer/CMakeLists.txt is the
  # named producer of the EXPECTED_ values (FR-009a(ii)), and the OBSERVED_ values
  # come from $<TARGET_PROPERTY:> on a real target inside the configured consumer.
  # Anchored per line — an unanchored match would let the COMPILE_OPTIONS line
  # satisfy the COMPILE_DEFINITIONS lookup through the substring.
  if(NOT _usage_txt MATCHES "(^|\n)EXPECTED_${_prop}=([^\n]*)")
    message(FATAL_ERROR "086 FR-009a(ii): no EXPECTED_${_prop}= line in ${_usage_file}")
  endif()
  set(_expected "${CMAKE_MATCH_2}")
  if(NOT _usage_txt MATCHES "(^|\n)OBSERVED_${_prop}=([^\n]*)")
    message(FATAL_ERROR "086 FR-009a(ii): no OBSERVED_${_prop}= line in ${_usage_file}")
  endif()
  set(_observed "${CMAKE_MATCH_2}")
  if(NOT _observed STREQUAL _expected)
    message(FATAL_ERROR
      "086 FR-009a(ii) FAIL: a consumer linking ONLY fixpp::capi sees "
      "${_prop}=[${_observed}], expected [${_expected}].\n"
      "If something appeared, the include interface was narrowed while this usage "
      "requirement still propagates — \$<LINK_ONLY:> is not in effect on fixpp::capi, "
      "or something outside the C-ABI closure is adding to it. If something "
      "disappeared that the expectation names, the closure lost a requirement it "
      "relies on. Either way the delivered interface is not what "
      "contracts/include-interface.md §2 describes.")
  endif()
endforeach()
message(STATUS "086 FR-009a(ii): OK — no usage requirement propagates through fixpp::capi")

# ── 4. Run it and check the output ────────────────────────────────────────────
# Ninja single-config drops the exe at the sub-build root; the basename carries a
# .exe suffix on Windows (this -P script has no CMAKE_EXECUTABLE_SUFFIX, so probe
# both) and none on UNIX.
set(_exe "${_sub_build}/consumer_witness")
if(NOT EXISTS "${_exe}" AND EXISTS "${_exe}.exe")
  set(_exe "${_exe}.exe")
endif()
if(NOT EXISTS "${_exe}")
  message(FATAL_ERROR "consumer_witness executable not found at ${_exe}")
endif()

# 084 T037 / SC-014 — the API/data pairing must be USABLE, not merely co-located.
# The dictionary is loaded from the INSTALLED PREFIX, never from the source tree:
# FR-018a added the dictionaries/ install rule precisely so a consumer of the
# package can reach this data, and passing ${FIXPP_SOURCE_DIR}/dictionaries/ (as
# this driver did before 084) would prove only that the file exists in the repo —
# which says nothing about the package. Fail loudly rather than falling back.
set(_installed_dict "${_stage}/share/fixpp/dictionaries/FIX44.xml")
if(NOT EXISTS "${_installed_dict}")
  message(FATAL_ERROR
    "SC-014: no FIX44.xml in the installed prefix at ${_installed_dict} — the "
    "FR-018a dictionary install rule regressed. The package would ship "
    "fixpp::dict::load_any with none of its data.")
endif()

execute_process(
  COMMAND "${_exe}" "${_installed_dict}"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _run_out
  ERROR_VARIABLE  _run_err
)
message(STATUS "consumer_witness stdout: ${_run_out}")
if(NOT _run_rc EQUAL 0)
  message(FATAL_ERROR "consumer_witness exited ${_run_rc}:\n${_run_out}\n${_run_err}")
endif()
if(NOT _run_out MATCHES "^PASS:")
  message(FATAL_ERROR "consumer_witness did not print PASS: ${_run_out}")
endif()

message(STATUS "fixpp::consumer::install-witness: OK")
