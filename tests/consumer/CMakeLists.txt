# tests/consumer/CMakeLists.txt — 061-slim FR-008/SC-004 external-consumer
# compile+link witness.
#
# T024. This is a STANDALONE CMake project — it is intentionally NOT
# add_subdirectory()'d from the main build (see run_consumer_witness.cmake,
# which configures/builds it as a separate `cmake -S/-B` invocation). Being a
# separate project() means it does NOT inherit the top-level CMakeLists.txt's
# directory-scoped `include_directories("${CMAKE_SOURCE_DIR}/include")` — the
# only include path this target ever sees is the one explicitly given below
# (FIXPP_STAGE_PREFIX/include, from `cmake --install`), so a missing/broken
# T023 install rule fails this target's configure/build, not silently falls
# back to a build-tree-private path (SC-004: "no build-tree-private path").
#
# There is no fixpp CMake package-config / find_package(fixpp) export
# machinery anywhere in this repository (no install(TARGETS), no
# install(EXPORT), no FixppConfig.cmake.in) — research.md Decision 7 scopes
# 061-slim to the *header* install + an external-TU compile witness only, and
# FR-009 forbids new surface beyond body_builder + the exemplar declarations
# + the header-install rule. Building full package-export machinery is out of
# scope here. This target instead links directly against the already-built
# build-tree static archives (FIXPP_BUILD_LIB_DIR) — those archives are not
# part of the FR-008 gap (only the generated-header install path was
# build-tree-private before T023); the header resolution is the thing this
# witness proves.
cmake_minimum_required(VERSION 3.28)
project(fixpp_consumer_witness CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT FIXPP_STAGE_PREFIX)
  message(FATAL_ERROR "FIXPP_STAGE_PREFIX (staged `cmake --install` prefix) must be set")
endif()
if(NOT FIXPP_BUILD_LIB_DIR)
  message(FATAL_ERROR "FIXPP_BUILD_LIB_DIR (build-tree lib/ dir with the fixpp archives) must be set")
endif()

# pugixml is a private link dependency of fixpp_dictionary (src/dictionary/CMakeLists.txt).
# Resolved via the SAME Conan toolchain file passed by run_consumer_witness.cmake
# (-DCMAKE_TOOLCHAIN_FILE=<build-dir>/conan_toolchain.cmake), so this reuses the
# identical Conan-resolved package the main build already validated — no new
# dependency-resolution mechanism invented.
find_package(pugixml CONFIG REQUIRED)

add_executable(consumer_witness consumer_witness.cpp)

# ONLY include path: the installed prefix. Deliberately does not add
# ${CMAKE_SOURCE_DIR}/../../include or any _codegen/ build-tree path.
target_include_directories(consumer_witness PRIVATE "${FIXPP_STAGE_PREFIX}/include")

set(_fixpp_lib "${FIXPP_BUILD_LIB_DIR}")
target_link_libraries(consumer_witness PRIVATE
  -Wl,--start-group
  "${_fixpp_lib}/libfixpp_dictionary.a"
  "${_fixpp_lib}/libfixpp_wire.a"
  "${_fixpp_lib}/libfixpp_dict_dispatch_bridge.a"
  "${_fixpp_lib}/libfixpp_core.a"
  -Wl,--end-group
  pugixml::pugixml
)
