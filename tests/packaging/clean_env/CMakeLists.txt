# tests/packaging/clean_env/CMakeLists.txt — 084 T041/T042 (SC-016, FR-018e).
#
# The ONLY witness tier that inherits NOTHING from the producing build.
#
# What makes it different from tests/consumer/: that tier is handed the
# producer's conan_toolchain.cmake, so it sees the producer's exact dependency
# graph and CANNOT fail on the dependency-provisioning gap. This project is
# configured with NO toolchain file at all -- only a CMAKE_PREFIX_PATH naming the
# staged fixpp prefix plus a separate dependency prefix. If fixppConfig.cmake
# needed any package-manager-specific mechanism, this configure would fail.
cmake_minimum_required(VERSION 3.28)
project(fixpp_clean_env_consumer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No hints beyond CMAKE_PREFIX_PATH, which the driver sets on the command line.
find_package(fixpp REQUIRED)

if(NOT TARGET fixpp::fixpp)
  message(FATAL_ERROR
    "SC-016: find_package(fixpp) succeeded but fixpp::fixpp is NOT defined. A "
    "config that resolves without publishing its umbrella target is not a usable "
    "package.")
endif()

add_executable(clean_env_consumer clean_env_consumer.cpp)
target_link_libraries(clean_env_consumer PRIVATE fixpp::fixpp)
