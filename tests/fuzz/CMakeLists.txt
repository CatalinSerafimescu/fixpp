# tests/fuzz/CMakeLists.txt — libFuzzer harnesses.
# Built only when FIXPP_BUILD_FUZZ=ON.
# Clang-only: uses -fsanitize=fuzzer,address which GCC does not support.

# Seam #7 of 001-core-decimal — decimal parse fuzzer.
add_executable(fuzz_decimal_parse fuzz_decimal_parse.cpp)
target_link_libraries(fuzz_decimal_parse PRIVATE fixpp_core)
target_compile_options(fuzz_decimal_parse PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_decimal_parse PRIVATE -fsanitize=fuzzer,address)
set_target_properties(fuzz_decimal_parse PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Seam #8 of `[2c §9]` — XmlLoader fuzzer for 002-dictionary-xml-loader per
# `[const §VII.7]`. Added in Gate A round 1 per Opus root cause #3.
# fixpp_dictionary link target lands when /implement wires up `src/dictionary/`;
# at /plan time the harness source ships as part of the contract.
add_executable(fuzz_dict_xml_loader fuzz_dict_xml_loader.cpp)
target_link_libraries(fuzz_dict_xml_loader PRIVATE fixpp_dictionary fixpp_core)
target_compile_options(fuzz_dict_xml_loader PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_dict_xml_loader PRIVATE -fsanitize=fuzzer,address)
set_target_properties(fuzz_dict_xml_loader PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# T050 — Seam #11 — Wire framer fuzzer for 004-wire-codec per `[const §VII.7]`.
# fuzz_wire_framer: feed arbitrary bytes to Framer::feed; assert bounded carry,
# no OOB/UB, all rejections are defined wire_* errors.
add_executable(fuzz_wire_framer fuzz_wire_framer.cpp)
target_link_libraries(fuzz_wire_framer PRIVATE fixpp_wire fixpp_core)
target_compile_options(fuzz_wire_framer PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_wire_framer PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_wire_framer PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_wire_framer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# T050 — Seam #11 — Wire parser + OffsetTable fuzzer for 004-wire-codec.
# fuzz_wire_parser: feed arbitrary bytes through the test factory to
# Parser<Index>::parse; assert no OOB/UB, bounded OffsetTable, defined errors.
# NOTE: fuzz_wire_validator.cpp is intentionally ABSENT (US4 PAUSED).
add_executable(fuzz_wire_parser fuzz_wire_parser.cpp)
target_link_libraries(fuzz_wire_parser PRIVATE fixpp_wire fixpp_core)
target_compile_options(fuzz_wire_parser PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_wire_parser PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_wire_parser PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_wire_parser PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
