# tests/transport/CMakeLists.txt
find_package(GTest CONFIG REQUIRED)

add_executable(transport_smoke_test transport_smoke_test.cpp)
target_link_libraries(transport_smoke_test PRIVATE
  fixpp_transport
  GTest::gtest
  GTest::gtest_main
)
add_test(NAME transport_smoke COMMAND transport_smoke_test)
