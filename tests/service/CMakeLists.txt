# tests/service/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(service_smoke_test service_smoke_test.cpp)
target_link_libraries(service_smoke_test PRIVATE
  fixpp_service
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME service_smoke COMMAND service_smoke_test)
