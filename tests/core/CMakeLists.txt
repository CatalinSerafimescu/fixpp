# tests/core/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(core_smoke_test core_smoke_test.cpp)
target_link_libraries(core_smoke_test PRIVATE
  fixpp_core
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME core_smoke COMMAND core_smoke_test)
