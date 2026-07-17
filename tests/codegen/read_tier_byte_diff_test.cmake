# tests/codegen/read_tier_byte_diff_test.cmake — 077-builder-args-dedup T022 [US3]
#
# CTest target: fixpp::dict::read-tier-byte-diff
# Type: cmake -P script test (NOT a GoogleTest C++ TU).
#
# FR-009/SC-005, T006 decision (FULL 4-artifact x 4-version, not narrowed):
# regenerates the legacy read tier (v42/v44/v50sp2/vt11 x Fields/Messages/
# Reify/Validator.hpp, 16 artifacts) into a temp dir via a fresh fixpp-codegen
# invocation, then SHA256-diffs each artifact against the pre-077 T001
# baseline. 077 touches ONLY emit_builders.cpp (+ cmake/Codegen.cmake driver
# wiring) -- the read emitters (emit_messages.cpp / fields / validator /
# reify) are untouched, so all 16 must be byte-identical pre/post. A
# mismatch here is a real read-tier regression (FR-009), not noise.
#
# Baseline hashes below were captured 2026-07-16 (T001) on pre-077 HEAD
# 455737c3 via:
#   fixpp-codegen --xml dictionaries/FIX42.xml   --out <dir> \
#                 --xml dictionaries/FIX44.xml   --out <dir> \
#                 --xml dictionaries/FIX50SP2.xml --out <dir> \
#                 --xml dictionaries/FIXT11.xml  --out <dir>
#   sha256sum <dir>/{v42,v44,v50sp2,vt11}/{Fields,Messages,Reify,Validator}.hpp
#
# SHA256 (not a checked-in content golden) is used deliberately: the read
# tier is ~80MB across the 16 artifacts (v50sp2/Fields.hpp alone is ~40MB),
# and committing that as a second copy of goldened content would bloat the
# repo for no additional discriminating power over a hash. This mirrors the
# existing file(SHA256 ...) content-fingerprint mechanism in
# cmake/Codegen.cmake:105. The 4 Messages.hpp hashes below are corroborated
# (verified byte-identical) against the already-committed
# specs/003-dictionary-codegen/contracts/golden/<ns>_Messages.golden.hpp
# files gated by codegen_determinism_test's GeneratedMatchesGolden -- so this
# script's genuinely new coverage is the 12 ungoldened Fields/Reify/
# Validator artifacts.
#
# Usage (invoked by CTest via add_test in tests/codegen/CMakeLists.txt):
#   cmake -DFIXPP_CODEGEN_BIN=<path> -DFIXPP_DICT_DATA_DIR=<path> \
#         -P read_tier_byte_diff_test.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FIXPP_CODEGEN_BIN OR NOT EXISTS "${FIXPP_CODEGEN_BIN}")
  message(FATAL_ERROR
    "[T022] FIXPP_CODEGEN_BIN not set or does not exist: '${FIXPP_CODEGEN_BIN}'")
endif()
if(NOT DEFINED FIXPP_DICT_DATA_DIR OR NOT EXISTS "${FIXPP_DICT_DATA_DIR}")
  message(FATAL_ERROR
    "[T022] FIXPP_DICT_DATA_DIR not set or does not exist: '${FIXPP_DICT_DATA_DIR}'")
endif()
if(NOT DEFINED FIXPP_READ_TIER_OUT)
  message(FATAL_ERROR "[T022] FIXPP_READ_TIER_OUT (scratch output dir) must be set")
endif()

# ── Pre-077 baseline (T001, HEAD 455737c3) ─────────────────────────────────

set(_expected_v42_Fields.hpp     c8bb64b1be70acdfae7e8efcfeba8c512b19910ed9987eb2a2d62257c48757e5)
set(_expected_v42_Messages.hpp   128b334a7683dffc8757f7560c40b38943472d579e3dc63073a1f75fde7f6a85)
set(_expected_v42_Reify.hpp      b234624461ca1be3452cf8222d3d02b6fccb9c00891ccd8e4333df4ec13f95e8)
set(_expected_v42_Validator.hpp  b5d39106b4cc81eafd32ab65998d109b3bff1ae8198920634869f5765e88f096)
set(_expected_v44_Fields.hpp     e39f240770bc4115c80fc425da35ebb20619502fe633bb3b62dcd44322be5fcb)
set(_expected_v44_Messages.hpp   11e7ebc293d313c91e54f67c0e9ee1a83c936cd3e6818e365a6acd48d235dddb)
set(_expected_v44_Reify.hpp      98eb06542f2dbfb8e424b502a6d077959a34f61e49be4b03f411e73e75825196)
set(_expected_v44_Validator.hpp  ad49a4ec2d2441e6d15181b48fa42ab30d9a2226d41476bfdd9007f5d942e9c0)
set(_expected_v50sp2_Fields.hpp    34079b2d538d3604f865911300eef5d8c76451e3e615b4152576d173bce044a7)
set(_expected_v50sp2_Messages.hpp  9595b4ec461d1cce3737bdf8516e808eb4f97596b7fde5e830afe516e3936f0b)
set(_expected_v50sp2_Reify.hpp     c73e5253cfe5617565df979eeb71d8b5bd5e62a9b2e379b4f8a7f2a350f6cd36)
set(_expected_v50sp2_Validator.hpp f550123aa126489588defef700204532893e6f9679c3ad51353b7433149568be)
set(_expected_vt11_Fields.hpp    18974c06c4608bef154c0c3b08db71c9a2e9290861bfbceb9abb26239d786cd4)
set(_expected_vt11_Messages.hpp  bf38d217945d077fc4a589f873aab7e7d26f719d5d51d23160735c4d6f03fedc)
set(_expected_vt11_Reify.hpp     7532e100be52686a804f31dc66f5614651e356614c190a5e0795255bf9e0fb42)
set(_expected_vt11_Validator.hpp c333ec70d3bdc654f7953686080bc1cac0e43abeab46a1d7386360d75b80f1ed)

# ── Regenerate the read tier into a fresh scratch dir ──────────────────────

file(REMOVE_RECURSE "${FIXPP_READ_TIER_OUT}")
file(MAKE_DIRECTORY "${FIXPP_READ_TIER_OUT}")

execute_process(
  COMMAND "${FIXPP_CODEGEN_BIN}"
    --xml "${FIXPP_DICT_DATA_DIR}/FIX42.xml"    --out "${FIXPP_READ_TIER_OUT}"
    --xml "${FIXPP_DICT_DATA_DIR}/FIX44.xml"    --out "${FIXPP_READ_TIER_OUT}"
    --xml "${FIXPP_DICT_DATA_DIR}/FIX50SP2.xml" --out "${FIXPP_READ_TIER_OUT}"
    --xml "${FIXPP_DICT_DATA_DIR}/FIXT11.xml"   --out "${FIXPP_READ_TIER_OUT}"
  RESULT_VARIABLE _codegen_rc
  OUTPUT_VARIABLE _codegen_out
  ERROR_VARIABLE  _codegen_err
)
if(NOT _codegen_rc EQUAL 0)
  message(FATAL_ERROR
    "[T022] fixpp-codegen run failed (exit ${_codegen_rc}):\n${_codegen_out}\n${_codegen_err}")
endif()

# ── Recursive byte-diff (via SHA256) vs the T001 baseline ─────────────────

set(_PASS_COUNT 0)
set(_FAIL_COUNT 0)
set(_FAIL_MSGS "")

foreach(_ns IN ITEMS v42 v44 v50sp2 vt11)
  foreach(_artifact IN ITEMS Fields.hpp Messages.hpp Reify.hpp Validator.hpp)
    set(_path "${FIXPP_READ_TIER_OUT}/${_ns}/${_artifact}")
    set(_expected "${_expected_${_ns}_${_artifact}}")

    if(NOT EXISTS "${_path}")
      math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
      list(APPEND _FAIL_MSGS "  FAIL: ${_ns}/${_artifact} missing from regenerated read tier: ${_path}")
      continue()
    endif()

    file(SHA256 "${_path}" _actual)
    if(_actual STREQUAL _expected)
      math(EXPR _PASS_COUNT "${_PASS_COUNT} + 1")
      message(STATUS "[T022]   OK  ${_ns}/${_artifact}")
    else()
      math(EXPR _FAIL_COUNT "${_FAIL_COUNT} + 1")
      list(APPEND _FAIL_MSGS
        "  FAIL: ${_ns}/${_artifact} diverged from the pre-077 T001 baseline\n"
        "        expected sha256=${_expected}\n"
        "        actual   sha256=${_actual}\n"
        "        regenerated at: ${_path}")
      message(STATUS "[T022]   DIFF ${_ns}/${_artifact}  expected=${_expected}  actual=${_actual}")
    endif()
  endforeach()
endforeach()

message(STATUS "[T022] ============================================================")
message(STATUS "[T022] Results: ${_PASS_COUNT} passed, ${_FAIL_COUNT} failed (16 artifacts total)")

if(_FAIL_COUNT GREATER 0)
  message(STATUS "[T022] Failures:")
  foreach(_msg IN LISTS _FAIL_MSGS)
    message(STATUS "${_msg}")
  endforeach()
  message(FATAL_ERROR
    "[T022] fixpp::dict::read-tier-byte-diff FAILED (${_FAIL_COUNT} artifact(s) diverged from "
    "the pre-077 baseline -- FR-009/SC-005 violated; this is a real read-tier regression, "
    "not test noise -- see above).")
else()
  message(STATUS "[T022] fixpp::dict::read-tier-byte-diff PASSED "
    "(all 16 legacy read-tier artifacts byte-identical to the pre-077 T001 baseline).")
endif()
