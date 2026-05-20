# src/core/test/CMakeLists.txt
# fixpp_mock_clock — TEST-ONLY deterministic Clock impl (D-10 / E3 /
# [2d §4.3]). NOT part of the shipped fixpp_core/fixpp_session libraries:
# mock_clock is a test-infra plugin (public test header
# <fixpp/core/test/mock_clock.hpp>). Built only under FIXPP_BUILD_TESTS
# (guarded in ../CMakeLists.txt). asio-dependent → links asio + fixpp_core
# (core stays the asio-free leaf [arch §2.2]).

add_library(fixpp_mock_clock STATIC
  mock_clock.cpp
)
add_library(fixpp::mock_clock ALIAS fixpp_mock_clock)

target_include_directories(fixpp_mock_clock PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(fixpp_mock_clock PUBLIC
  fixpp_core
  asio::asio
)
