# tests/tap/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(tap_smoke_test tap_smoke_test.cpp)
target_link_libraries(tap_smoke_test PRIVATE
  fixpp_tap
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME tap_smoke COMMAND tap_smoke_test)
