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

# T010 — 060-int128-decimal-compare — differential libFuzzer target.
# Decodes two pod_decimals from the input and asserts the production
# compare() agrees bit-for-bit with the FROZEN reference_compare (shared
# with tests/core/decimal_compare_diff_oracle_test.cpp via
# decimal_reference_compare.hpp); mismatch traps (SC-006).
add_executable(fuzz_decimal_compare fuzz_decimal_compare.cpp)
target_link_libraries(fuzz_decimal_compare PRIVATE fixpp_core)
target_compile_options(fuzz_decimal_compare PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_decimal_compare PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_decimal_compare PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_decimal_compare PROPERTIES
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

# 074-orchestra-native-reader — OrchestraLoader fuzzer, sibling to
# fuzz_dict_xml_loader (Seam #8 of `[2c §9]`) per `[const §VII.7]`.
add_executable(fuzz_orchestra_loader fuzz_orchestra_loader.cpp)
target_link_libraries(fuzz_orchestra_loader PRIVATE fixpp_dictionary fixpp_core)
target_compile_options(fuzz_orchestra_loader PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_orchestra_loader PRIVATE -fsanitize=fuzzer,address)
set_target_properties(fuzz_orchestra_loader PROPERTIES
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
# NOTE: fuzz_wire_validator.cpp NOW EXISTS — wired by 041-validation-gate-wiring
# (T026). See the fuzz_wire_validator target below.
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

# 062 T024b — Seam #11 extension — the slice-scoped mid-frame nested-build
# fuzzer. fuzz_wire_nested_slice: feed arbitrary bytes directly as a
# non-enveloped {data, len+1} slice through OffsetTable::nested_group_slices
# (T005/T006 dict-aware nested sub-view builder) — the new 062 input shape
# fuzz_wire_parser.cpp never generates (full-frame only).
add_executable(fuzz_wire_nested_slice fuzz_wire_nested_slice.cpp)
target_link_libraries(fuzz_wire_nested_slice PRIVATE fixpp_wire fixpp_core)
target_compile_options(fuzz_wire_nested_slice PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_wire_nested_slice PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_wire_nested_slice PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_wire_nested_slice PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# T026 — Seam #11 extension — Wire validator fuzzer for 041-validation-gate-wiring
# per `[const §VII.7]`. Ends the feature-004 "US4 PAUSED" deferral.
# fuzz_wire_validator: feed arbitrary bytes through the frame factory and
# Parser<Index>::parse; on a successfully parsed MessageView, run
# dictionary_driven_validator::validate() backed by a real Dictionary
# (as_table_view()). Asserts no crash/OOB/UB, no exception escape, and every
# rejection is a defined wire_* slot (never a raw decimal_* code — T009a remap).
add_executable(fuzz_wire_validator fuzz_wire_validator.cpp)
target_link_libraries(fuzz_wire_validator PRIVATE fixpp_wire fixpp_dictionary fixpp_core)
target_compile_options(fuzz_wire_validator PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_wire_validator PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_wire_validator PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_wire_validator PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# T035 — Seam #12 of `[2d §9]` — VOLUNTARY session cancellation-timing
# fuzzer (Gate-A discretion; NOT a [const §VII.7] obligation — D-11). Fires
# Session::close(graceful|terminal) at an input-selected co_await checkpoint
# interleaved with cancellable_dispatch; asserts no deadlock / double-free /
# PMR leak / silent global-heap dispatch fallback. Links fixpp_session
# (brings fixpp_core + asio).
add_executable(fuzz_session_cancellation fuzz_session_cancellation.cpp)
target_link_libraries(fuzz_session_cancellation PRIVATE fixpp_session fixpp_core)
target_compile_options(fuzz_session_cancellation PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_session_cancellation PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_session_cancellation PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_session_cancellation PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# T030 — Seam #6 of [2g §9] — file_cert_source libFuzzer harness for
# 011-tls-policy per [const §IX.4]. Feeds random PEM/DER bytes via factory
# (never the throwing constructor — fuzzer must not trigger unhandled exceptions).
add_executable(fuzz_file_cert_source fuzz_file_cert_source.cpp)
target_link_libraries(fuzz_file_cert_source PRIVATE fixpp_tls fixpp_core)
target_compile_options(fuzz_file_cert_source PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_file_cert_source PRIVATE -fsanitize=fuzzer,address,undefined)
set_target_properties(fuzz_file_cert_source PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# T051 — Seam #21 — MessageStore libFuzzer harness for 008-message-store per
# SC-011 / FR-033 / [const §VII.7] voluntary / [const §IX.2].
# Drives random interleavings of store/retrieve/reset/next_seqnum against
# both MemoryStore and FileStore. Runs under ASan + UBSan.
# TSan build: build separately with linux-clang-tsan preset + FIXPP_BUILD_FUZZ.
add_executable(fuzz_message_store fuzz_message_store.cpp)
target_link_libraries(fuzz_message_store PRIVATE fixpp_session fixpp_core)
target_compile_options(fuzz_message_store PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_message_store PRIVATE -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_message_store PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_message_store PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ── Phase 3a fuzz harnesses (wired by T029): ─────────────────────────────────

# T024 — [2h §9 seam #11] — read-path libFuzzer harness.
# Drives Framer::feed directly; US4 T041 will add Session-FSM wiring.
add_executable(fuzz_transport_read_path fuzz_transport_read_path.cpp)
target_link_libraries(fuzz_transport_read_path PRIVATE
  fixpp_transport  fixpp_wire  fixpp_core)
target_compile_options(fuzz_transport_read_path PRIVATE
  -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_transport_read_path PRIVATE
  -fsanitize=fuzzer,address,undefined)
set_target_properties(fuzz_transport_read_path PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# T025 — [2h §9 seam #12] — handshake libFuzzer harness.
# Uses a local stub until US4 T041 mock_transport ships.
add_executable(fuzz_transport_handshake fuzz_transport_handshake.cpp)
target_link_libraries(fuzz_transport_handshake PRIVATE
  fixpp_transport  fixpp_core)
target_compile_options(fuzz_transport_handshake PRIVATE
  -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_transport_handshake PRIVATE
  -fsanitize=fuzzer,address,undefined)
set_target_properties(fuzz_transport_handshake PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# T021 — 013-session-reconnect-binding Phase 3 — session recovery + admin parse fuzzer.
# Feeds random bytes into Session::on_inbound_frame() covering Logon+141=Y,
# ResendRequest, SequenceReset-GapFill, Heartbeat+TestReqID, Logout paths.
# Per [const §IX.4] voluntary coverage obligation for the recovery parse surface.
add_executable(fuzz_session_recovery_admin_parse
  fuzz_session_recovery_admin_parse.cpp)
target_link_libraries(fuzz_session_recovery_admin_parse PRIVATE
  fixpp_session
  fixpp_mock_clock
  fixpp_core)
target_compile_options(fuzz_session_recovery_admin_parse PRIVATE
  -fsanitize=fuzzer,address,undefined)
target_link_options(fuzz_session_recovery_admin_parse PRIVATE
  -fsanitize=fuzzer,address,undefined)
target_include_directories(fuzz_session_recovery_admin_parse PRIVATE
  "${CMAKE_SOURCE_DIR}/tests"
)
set_target_properties(fuzz_session_recovery_admin_parse PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
