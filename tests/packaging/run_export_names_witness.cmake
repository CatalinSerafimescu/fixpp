# tests/packaging/run_export_names_witness.cmake — 084 T026
#
# Asserts exact set equality between the INSTALLED imported fixpp names and the
# configure-time export manifest derived from the real target graph. Direction B
# is realizable NOW because the comparison base changed: it is no longer "all
# in-tree aliases", which deliberately includes build-only names.

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_export_names_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_pkgdir "${FIXPP_WORK_DIR}/packages")
file(REMOVE_RECURSE "${FIXPP_WORK_DIR}")
file(MAKE_DIRECTORY "${_pkgdir}")

execute_process(
  COMMAND "${CMAKE_CPACK_COMMAND}" --config "${FIXPP_MAIN_BUILD_DIR}/CPackConfig.cmake"
          -B "${_pkgdir}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cpack failed (exit ${_rc}):\n${_out}\n${_err}")
endif()

file(GLOB _artifacts "${_pkgdir}/*.tar.gz" "${_pkgdir}/*.zip")
if(_artifacts STREQUAL "")
  message(FATAL_ERROR "T026: cpack produced no .tar.gz or .zip artifact to inspect")
endif()
list(GET _artifacts 0 _artifact)

set(_extract_dir "${FIXPP_WORK_DIR}/extract")
file(MAKE_DIRECTORY "${_extract_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar xf "${_artifact}"
  WORKING_DIRECTORY "${_extract_dir}"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "T026: failed to extract ${_artifact}")
endif()

file(GLOB_RECURSE _installed_targets "${_extract_dir}/*/fixppTargets*.cmake")
if(_installed_targets STREQUAL "")
  message(FATAL_ERROR "T026: package ships no fixppTargets*.cmake")
endif()

set(_installed_imports "")
foreach(_targets_file IN LISTS _installed_targets)
  file(READ "${_targets_file}" _targets_text)
  string(REGEX MATCHALL
         "add_library\\(fixpp::[A-Za-z0-9_:]+ [A-Z]+ IMPORTED\\)"
         _decls "${_targets_text}")
  foreach(_decl IN LISTS _decls)
    string(REGEX REPLACE
           "^add_library\\((fixpp::[A-Za-z0-9_:]+) [A-Z]+ IMPORTED\\)$"
           "\\1" _name "${_decl}")
    list(APPEND _installed_imports "${_name}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _installed_imports)
list(SORT _installed_imports)
if(_installed_imports STREQUAL "")
  message(FATAL_ERROR "T026: no installed imported fixpp names matched the witness regex")
endif()

set(_manifest "${FIXPP_MAIN_BUILD_DIR}/fixpp-export-names.txt")
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR
    "T026: expected configure-time export-name manifest '${_manifest}' does not exist")
endif()
file(STRINGS "${_manifest}" _expected_imports)
list(FILTER _expected_imports EXCLUDE REGEX "^$")
list(REMOVE_DUPLICATES _expected_imports)
list(SORT _expected_imports)
if(_expected_imports STREQUAL "")
  message(FATAL_ERROR "T026: export-name manifest '${_manifest}' is empty")
endif()

set(_missing_from_install "")
foreach(_expected_name IN LISTS _expected_imports)
  if(NOT _expected_name IN_LIST _installed_imports)
    list(APPEND _missing_from_install "${_expected_name}")
  endif()
endforeach()

set(_unexpected_installed "")
foreach(_installed_name IN LISTS _installed_imports)
  if(NOT _installed_name IN_LIST _expected_imports)
    list(APPEND _unexpected_installed "${_installed_name}")
  endif()
endforeach()

if(_missing_from_install OR _unexpected_installed)
  message(FATAL_ERROR
    "T026/FR-003: installed imported names differ from the configure-time export manifest.\n"
    "Missing from package: ${_missing_from_install}\n"
    "Unexpected in package: ${_unexpected_installed}\n"
    "Expected: ${_expected_imports}\n"
    "Installed: ${_installed_imports}")
endif()

message(STATUS
  "T026: installed imported names exactly match the configure-time export manifest "
  "(${_installed_imports})")
