# cmake/FixppCopyArtifacts.cmake — 084 T052 (FR-020, FR-021, I20/I21).
#
# Copies finished package artifacts out of the build tree into the durable
# artifact directory. Run via `cmake -P` from the fixpp-package target.
#
# Why a separate script rather than a `cmake -E copy` in the target: this must
# FAIL LOUDLY when there is nothing to copy. A glob that matches nothing copies
# nothing and exits 0, which would leave the artifact directory holding only the
# PREVIOUS configuration's packages while reporting success -- and T062 asserts a
# count and a name set over exactly that directory, so a silent no-op there is
# the silent-omission failure SC-003 exists to catch.
#
# It also refuses to copy _CPack_Packages/ staging content: that is the staging
# tree CPack builds packages FROM, it lives inside the build tree by design so
# tree deletion cleans it up (I20/I21), and copying it out would both defeat that
# and pollute the count.

foreach(_var FIXPP_PACKAGE_DIR FIXPP_ARTIFACT_DIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "FixppCopyArtifacts.cmake: -D${_var}=... is required")
  endif()
endforeach()

if(NOT EXISTS "${FIXPP_PACKAGE_DIR}")
  message(FATAL_ERROR
    "FR-021: no package directory at ${FIXPP_PACKAGE_DIR} — cpack did not run, or "
    "ran into a different directory. Nothing was copied.")
endif()

# Only finished artifacts. Extensions are the format dimension of the FR-017 name.
file(GLOB _artifacts
  "${FIXPP_PACKAGE_DIR}/*.deb"
  "${FIXPP_PACKAGE_DIR}/*.rpm"
  "${FIXPP_PACKAGE_DIR}/*.tar.gz"
  "${FIXPP_PACKAGE_DIR}/*.zip")

if(_artifacts STREQUAL "")
  message(FATAL_ERROR
    "FR-021: cpack produced NO artifacts in ${FIXPP_PACKAGE_DIR}. Refusing to exit "
    "successfully: the artifact directory would silently keep only earlier "
    "configurations' packages while this one reported success.")
endif()

file(MAKE_DIRECTORY "${FIXPP_ARTIFACT_DIR}")

foreach(_a IN LISTS _artifacts)
  get_filename_component(_name "${_a}" NAME)
  # FR-017/I10: names are unique across the whole matrix, so a collision here means
  # either the naming lost a dimension or a stale artifact of the SAME name is
  # present. Overwriting is correct (this build is newer and its provenance stamp
  # is inside it), but it must be visible rather than silent.
  if(EXISTS "${FIXPP_ARTIFACT_DIR}/${_name}")
    message(STATUS "FR-021: replacing existing artifact ${_name}")
  endif()
  file(COPY "${_a}" DESTINATION "${FIXPP_ARTIFACT_DIR}")
  message(STATUS "FR-021: ${_name} -> ${FIXPP_ARTIFACT_DIR}")
endforeach()

list(LENGTH _artifacts _n)
message(STATUS "FR-021: copied ${_n} artifact(s) to ${FIXPP_ARTIFACT_DIR}")
