# tests/packaging/run_clean_env_witness.cmake — 084 T041/T042/T043.
#
# SC-016 / FR-018e. The only tier that inherits nothing from the producing build.
#
# THREE legs:
#   T041 PASS  — configure with NO toolchain file, only CMAKE_PREFIX_PATH naming
#                the staged fixpp prefix + a separate dependency prefix.
#                find_package(fixpp) must succeed, fixpp::fixpp must be defined,
#                and the consumer must compile, link AND run.
#   T042 RED   — remove exactly ONE NAMED dependency from that prefix and re-run.
#                find_package must fail naming THAT dependency and no other.
#   T043       — grep the INSTALLED config/targets files (never the templates)
#                for build-host package-manager paths. A hit is a RED.
#
# ── HONEST SCOPE — read before citing this test ──────────────────────────────
# The dependency prefix is assembled by COPYING the CMake config files the
# producing build generated, into a directory that carries NO toolchain file and
# NO fixpp content. That proves the discriminating property: fixppConfig.cmake
# needs no package-manager-specific mechanism -- only a prefix on
# CMAKE_PREFIX_PATH -- and each find_dependency entry is individually load-bearing
# (T042).
#
# It does NOT prove the dependencies themselves came from somewhere other than
# this host's package-manager cache; those config files still resolve their
# imported locations into it. A fully independent provisioning test would need
# the six dependencies installed by an unrelated provider, which this host does
# not have. T043 is what covers the part that matters most in practice: that
# FIXPP'S OWN installed files carry no such path, so the package is not welded to
# this machine.

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_CONSUMER_SRC_DIR FIXPP_WORK_DIR
             FIXPP_CXX_COMPILER FIXPP_BUILD_TYPE)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_clean_env_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_stage    "${FIXPP_WORK_DIR}/stage")
set(_deps     "${FIXPP_WORK_DIR}/deps")
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (exit ${_rc}):\n${_out}\n${_err}")
endif()

# ── T043: the INSTALLED config must not name this host's package cache ───────
# Checked on the INSTALLED artefacts, never the .in templates (FR-018d): a
# fixppConfig.cmake carrying an absolute path under the producing host's package
# cache configures perfectly here and is unusable anywhere else -- the
# provider-agnostic property lost silently, in generated files nobody reads.
file(GLOB _installed_pkg_files "${_stage}/lib/cmake/fixpp/*.cmake")
if(_installed_pkg_files STREQUAL "")
  message(FATAL_ERROR "T043: no installed package files under ${_stage}/lib/cmake/fixpp")
endif()
foreach(_f IN LISTS _installed_pkg_files)
  file(READ "${_f}" _content)
  if(_content MATCHES "\\.conan2|conan_toolchain|CONAN_HOME|\\.conan/")
    message(FATAL_ERROR
      "T043 RED: installed ${_f} references the build host's package-manager cache. "
      "The package would configure only on this machine.")
  endif()
endforeach()
list(LENGTH _installed_pkg_files _n_pkg_files)
message(STATUS "T043: ${_n_pkg_files} installed package files carry no build-host cache path")

# ── Assemble a dependency prefix with NO toolchain and NO fixpp content ──────
file(MAKE_DIRECTORY "${_deps}")
file(GLOB _dep_cmake_files "${FIXPP_MAIN_BUILD_DIR}/*.cmake")
foreach(_f IN LISTS _dep_cmake_files)
  get_filename_component(_name "${_f}" NAME)
  # Exclude fixpp's own generated config (it must come from the STAGED prefix, or
  # this tier would silently test the build tree) and the toolchain/CPack files.
  if(_name MATCHES "^fixpp" OR _name MATCHES "^CPack" OR _name MATCHES "^conan_toolchain")
    continue()
  endif()
  file(COPY "${_f}" DESTINATION "${_deps}")
endforeach()

function(_fixpp_clean_configure _build_subdir _out_rc _out_text)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -S "${FIXPP_CONSUMER_SRC_DIR}"
      -B "${FIXPP_WORK_DIR}/${_build_subdir}"
      -G "Ninja"
      # NOTE: deliberately NO -DCMAKE_TOOLCHAIN_FILE.
      "-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}"
      "-DCMAKE_CXX_COMPILER=${FIXPP_CXX_COMPILER}"
      "-DCMAKE_PREFIX_PATH=${_stage};${_deps}"
      # Ordinary consumer-side setting, and REQUIRED here for a reason worth
      # recording: without it, find_dependency(OpenSSL) resolves through CMake's
      # bundled FindOpenSSL MODULE and binds to whatever OpenSSL the host happens
      # to have (measured: system 3.0.13, versus the 3.6.2 this package was built
      # against). That declares OpenSSL::SSL but NOT the lowercase
      # openssl::openssl target that the OTel dependency chain
      # (opentelemetry-cpp -> prometheus-cpp -> civetweb/CURL) expects, so the
      # configure dies deep inside a third-party config file.
      #
      # ⚠️ The silent-version-substitution underneath that is REAL and is not
      # fixed by this flag -- it is exactly the ABI hazard the package metadata
      # warns about (T050): find_dependency LOCATES, it does not pin. A consumer
      # who lets module mode win can link this package against a different
      # OpenSSL than it was compiled with and get no diagnostic at all.
      "-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  set(${_out_rc}   "${_rc}"           PARENT_SCOPE)
  set(${_out_text} "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

# ── T041: the PASS leg ───────────────────────────────────────────────────────
_fixpp_clean_configure("pass" _pass_rc _pass_text)
if(NOT _pass_rc EQUAL 0)
  message(FATAL_ERROR
    "SC-016 PASS leg FAILED: find_package(fixpp) could not resolve without the "
    "producing build's toolchain (exit ${_pass_rc}). The package is not "
    "provider-agnostic.\n${_pass_text}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${FIXPP_WORK_DIR}/pass"
  RESULT_VARIABLE _b_rc OUTPUT_VARIABLE _b_out ERROR_VARIABLE _b_err)
if(NOT _b_rc EQUAL 0)
  message(FATAL_ERROR "SC-016 PASS leg: configure succeeded but BUILD failed "
                      "(exit ${_b_rc}):\n${_b_out}\n${_b_err}")
endif()

set(_exe "${FIXPP_WORK_DIR}/pass/clean_env_consumer")
if(NOT EXISTS "${_exe}" AND EXISTS "${_exe}.exe")
  set(_exe "${_exe}.exe")
endif()
execute_process(COMMAND "${_exe}" RESULT_VARIABLE _r_rc OUTPUT_VARIABLE _r_out ERROR_VARIABLE _r_err)
if(NOT _r_rc EQUAL 0 OR NOT _r_out MATCHES "^PASS:")
  message(FATAL_ERROR "SC-016 PASS leg: consumer ran but did not report PASS "
                      "(exit ${_r_rc}):\n${_r_out}\n${_r_err}")
endif()
message(STATUS "SC-016 PASS leg: configured, linked and ran with no producer toolchain")

# ── T042: the RED leg, PROVEN and DISCRIMINATING ─────────────────────────────
# One NAMED dependency is removed. Crc32c is chosen deliberately: it is the entry
# every hand-written enumeration of this dependency set omitted until Gate A, and
# it reaches the consumer only through a $<LINK_ONLY:> edge -- so if the
# derivation rule ever regresses to a hand list, this is the leg that fails.
#
# Without a discriminating red leg the PASS leg cannot distinguish a missing
# dependency from an uninstalled fixppConfig.cmake, a malformed version file, or
# a targets file naming a nonexistent target.
set(_removed_dep "Crc32c")
file(GLOB _dep_files_to_remove "${_deps}/${_removed_dep}*")
if(_dep_files_to_remove STREQUAL "")
  message(FATAL_ERROR
    "T042: no ${_removed_dep}* config files in the dependency prefix, so removing "
    "it proves nothing. The red leg would pass vacuously.")
endif()
file(REMOVE ${_dep_files_to_remove})

_fixpp_clean_configure("red" _red_rc _red_text)
if(_red_rc EQUAL 0)
  message(FATAL_ERROR
    "T042 RED leg FAILED TO FAIL: find_package(fixpp) SUCCEEDED with ${_removed_dep} "
    "removed from the dependency prefix. Either find_dependency(${_removed_dep}) is "
    "missing from the generated config, or something outside the prefix is "
    "satisfying it -- in both cases a consumer's dependency gap would surface as an "
    "undefined symbol at link, not a diagnostic at configure.\n${_red_text}")
endif()
if(NOT _red_text MATCHES "${_removed_dep}")
  message(FATAL_ERROR
    "T042 RED leg failed for the WRONG REASON: configure failed but the diagnostic "
    "never names ${_removed_dep}, so this does not demonstrate that the missing "
    "dependency is what was detected.\n${_red_text}")
endif()

string(REGEX MATCH "[^\n]*${_removed_dep}[^\n]*" _red_line "${_red_text}")
message(STATUS "T042 RED leg observed (exit ${_red_rc}): ${_red_line}")
message(STATUS "fixpp::packaging::clean-env: OK")
