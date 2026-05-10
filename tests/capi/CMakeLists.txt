# tests/capi/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(capi_smoke_test capi_smoke_test.cpp)
target_link_libraries(capi_smoke_test PRIVATE
  fixpp_capi
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME capi_smoke COMMAND capi_smoke_test)
