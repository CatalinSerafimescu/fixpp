# bench/dictionary/CMakeLists.txt
# NFR-002-1 — Google Benchmark harness for fixpp::dict::XmlLoader.
# Built only when FIXPP_BUILD_BENCH=ON (gated by the parent CMakeLists).

find_package(benchmark CONFIG REQUIRED)

set(FIXPP_DICT_DATA_DIR "${CMAKE_SOURCE_DIR}/dictionaries")

add_executable(xml_loader_bench xml_loader_bench.cpp)
target_link_libraries(xml_loader_bench PRIVATE
  fixpp_dictionary
  benchmark::benchmark
  benchmark::benchmark_main
)
target_compile_definitions(xml_loader_bench PRIVATE
  "FIXPP_DICT_DATA_DIR=\"${FIXPP_DICT_DATA_DIR}\""
)
set_target_properties(xml_loader_bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/dictionary"
)
