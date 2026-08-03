# tests/packaging/run_export_names_witness.cmake — 084 T026
#
# Asserts the realizable direction only: every INSTALLED imported fixpp name
# must match an in-tree fixpp:: alias. The reverse direction is deliberately not
# asserted here because several aliases are intentionally build-only or
# non-exported.

cmake_minimum_required(VERSION 3.28)

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_WORK_DIR FIXPP_SOURCE_DIR)
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
         "add_library\\(fixpp::[A-Za-z0-9_:]+ (STATIC|INTERFACE) IMPORTED\\)"
         _decls "${_targets_text}")
  foreach(_decl IN LISTS _decls)
    string(REGEX REPLACE
           "^add_library\\((fixpp::[A-Za-z0-9_:]+) (STATIC|INTERFACE) IMPORTED\\)$"
           "\\1" _name "${_decl}")
    list(APPEND _installed_imports "${_name}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _installed_imports)
list(SORT _installed_imports)
if(_installed_imports STREQUAL "")
  message(FATAL_ERROR "T026: no installed imported fixpp names matched the witness regex")
endif()

set(_alias_files "${FIXPP_SOURCE_DIR}/CMakeLists.txt")
file(GLOB_RECURSE _tree_files
  LIST_DIRECTORIES FALSE
  "${FIXPP_SOURCE_DIR}/src/*"
  "${FIXPP_SOURCE_DIR}/cmake/*")
foreach(_tree_file IN LISTS _tree_files)
  get_filename_component(_tree_name "${_tree_file}" NAME)
  if(_tree_name STREQUAL "CMakeLists.txt" OR _tree_file MATCHES "\\.cmake$")
    list(APPEND _alias_files "${_tree_file}")
  endif()
endforeach()

set(_alias_census "")
foreach(_cmake_file IN LISTS _alias_files)
  if(NOT EXISTS "${_cmake_file}")
    continue()
  endif()
  file(READ "${_cmake_file}" _cmake_text)
  string(REGEX REPLACE "#[^\n]*" "" _cmake_text "${_cmake_text}")
  string(REGEX MATCHALL
         "add_library\\(fixpp::[A-Za-z0-9_:]+ ALIAS [A-Za-z0-9_:$<>.-]+\\)"
         _alias_decls "${_cmake_text}")
  foreach(_decl IN LISTS _alias_decls)
    string(REGEX REPLACE
           "^add_library\\((fixpp::[A-Za-z0-9_:]+) ALIAS [A-Za-z0-9_:$<>.-]+\\)$"
           "\\1" _alias "${_decl}")
    list(APPEND _alias_census "${_alias}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES _alias_census)
list(SORT _alias_census)
if(_alias_census STREQUAL "")
  message(FATAL_ERROR "T026: no in-tree fixpp:: aliases matched the witness regex")
endif()

set(_missing_aliases "")
foreach(_installed_name IN LISTS _installed_imports)
  if(NOT _installed_name IN_LIST _alias_census)
    list(APPEND _missing_aliases "${_installed_name}")
  endif()
endforeach()

if(_missing_aliases)
  message(FATAL_ERROR
    "T026/FR-003: installed imported names missing in-tree aliases: ${_missing_aliases}\n"
    "Installed names: ${_installed_imports}\n"
    "Alias census: ${_alias_census}")
endif()

message(STATUS
  "T026: every installed imported name has a matching in-tree alias "
  "(${_installed_imports})")
