# tests/otel/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(otel_smoke_test otel_smoke_test.cpp)
target_link_libraries(otel_smoke_test PRIVATE
  fixpp_otel
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME otel_smoke COMMAND otel_smoke_test)
