# tests/wire/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(wire_smoke_test wire_smoke_test.cpp)
target_link_libraries(wire_smoke_test PRIVATE
  fixpp_wire
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME wire_smoke COMMAND wire_smoke_test)
