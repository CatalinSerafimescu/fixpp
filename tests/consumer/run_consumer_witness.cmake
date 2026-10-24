# tests/consumer/run_consumer_witness.cmake — 061-slim T024 (FR-008/SC-004) driver.
#
# Invoked via `cmake -P` as a ctest (see the add_test(... "fixpp::consumer::install-witness")
# registration in the top-level CMakeLists.txt). Mirrors the tests/codegen T039
# cmake -P build-graph-test pattern: no hard-coded paths, everything comes in via -D.
#
# Steps (SC-004 — external consumer TU compiles AND links against the INSTALLED
# package, no build-tree-private path):
#   1. `cmake --install` the main build into a fresh staging prefix.
#   2. Configure tests/consumer/ as a STANDALONE CMake project (own project(),
#      not add_subdirectory()'d) pointed ONLY at the staged include dir.
#   3. Build it.
#   4. Run the resulting executable against the real FIX44.xml and assert it
#      printed PASS and exited 0.
#
# Required -D inputs:
#   FIXPP_MAIN_BUILD_DIR   — the configured main build dir (build/<preset>);
#                            supplies conan_toolchain.cmake + lib/ archives.
#   FIXPP_SOURCE_DIR       — repo root; supplies dictionaries/FIX44.xml (runtime
#                            data, not a header — reading it is not a
#                            build-tree-private *header* path per SC-004).
#   FIXPP_CONSUMER_SRC_DIR — tests/consumer/ (this directory).
#   FIXPP_WITNESS_WORK_DIR — scratch dir for the staged install + consumer
#                            sub-build (created fresh; NOT the source tree).
#   FIXPP_CODEGEN_FIX_LATEST — whether vlatest is generated (its typed pair and
#                            its headers exist only then).
#
# #613 (`.specify/613-builders-export.md` §4, §4.1) inputs, deliberately NOT in the
# required-variable loop below: guard 0 refuses an undefined value and an empty one
# alike, and naming them there would make the loop, not guard 0, the line that fires.
#   FIXPP_TYPED_TIER_SETTING   — the raw FIXPP_INSTALL_TYPED_TIER cache value.
#   FIXPP_TYPED_TIER_EFFECTIVE — FIXPP_INSTALL_TYPED_TIER_EFFECTIVE (ON|OFF).
#   FIXPP_GENERATOR_IS_MULTI_CONFIG — the generator's GENERATOR_IS_MULTI_CONFIG (0|1).
#   FIXPP_PACKAGING_ENABLED, FIXPP_IS_MSVC, FIXPP_TYPED_DEBUG_INFO,
#   FIXPP_TYPED_DEBUG_INFO_SET — the typed PDB expectation's inputs (D-9).
#
# Environment: FIXPP_WITNESS_KEEP_STAGE_LIB set to a true value skips the post-PASS
# removal of stage/lib/ (D-10), for the hand procedures that relink or rebuild the
# consumer from the kept stage.

foreach(_var FIXPP_MAIN_BUILD_DIR FIXPP_SOURCE_DIR FIXPP_CONSUMER_SRC_DIR FIXPP_WITNESS_WORK_DIR
             FIXPP_CXX_COMPILER FIXPP_C_COMPILER FIXPP_CODEGEN_FIX_LATEST)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "run_consumer_witness.cmake: -D${_var}=... is required")
  endif()
endforeach()

set(_stage    "${FIXPP_WITNESS_WORK_DIR}/stage")
set(_sub_build "${FIXPP_WITNESS_WORK_DIR}/build")

# Build the consumer in the SAME config as the main build so the Conan-resolved
# pugixml (a private link dep of fixpp_dictionary) has an imported location for
# the sub-build's config. A Debug consumer against a Release main build leaves
# pugixml::pugixml empty -> undefined pugixml refs from xml_loader.cpp.o.
# FIXPP_BUILD_TYPE is passed by the top-level add_test; default to Debug if a
# multi-config generator left CMAKE_BUILD_TYPE empty there.
if(NOT DEFINED FIXPP_BUILD_TYPE OR FIXPP_BUILD_TYPE STREQUAL "")
  set(FIXPP_BUILD_TYPE "Debug")
endif()

# ── 0. #613 — is the typed tier expected? Decided BEFORE the multi-GB copy ─────
# PRESENT/ABSENT is the EFFECTIVE value the build passed, but it is never trusted
# alone: guard 0 checks every input's domain, guard 1 re-derives the default from
# FIXPP_BUILD_TYPE and the generator kind (or normalises an explicit setting) and
# requires agreement. So a
# defaulting bug cannot turn both sides green, and an ordering slip in the root
# CMakeLists (a value read before it is set reaches here EMPTY) is refused.
include("${FIXPP_SOURCE_DIR}/cmake/FixppTypedTierRule.cmake")
fixpp_typed_tier_check_domain("${FIXPP_TYPED_TIER_SETTING}" "${FIXPP_TYPED_TIER_EFFECTIVE}"
  "${FIXPP_GENERATOR_IS_MULTI_CONFIG}"
  "Empty means the add_test registration read FIXPP_INSTALL_TYPED_TIER before the root CMakeLists.txt declared it."
  "Empty means the add_test registration read FIXPP_INSTALL_TYPED_TIER_EFFECTIVE before the root CMakeLists.txt resolved it."
  "Empty means the add_test registration did not pass the generator's GENERATOR_IS_MULTI_CONFIG.")
fixpp_typed_tier_rederive("${FIXPP_TYPED_TIER_SETTING}" "${FIXPP_BUILD_TYPE}" "${FIXPP_GENERATOR_IS_MULTI_CONFIG}"
  "the default for FIXPP_BUILD_TYPE=${FIXPP_BUILD_TYPE}" _typed_rederived _typed_rederived_from)
if(NOT _typed_rederived STREQUAL FIXPP_TYPED_TIER_EFFECTIVE)
  message(FATAL_ERROR
    "#613 guard 1: FIXPP_INSTALL_TYPED_TIER=${FIXPP_TYPED_TIER_SETTING} resolved "
    "${FIXPP_TYPED_TIER_EFFECTIVE}, but ${_typed_rederived_from} is ${_typed_rederived}. "
    "The root CMakeLists.txt's resolution disagrees with the rule.")
endif()
if(FIXPP_TYPED_TIER_EFFECTIVE STREQUAL "ON")
  set(_typed_outcome PRESENT)
else()
  set(_typed_outcome ABSENT)
endif()
message(STATUS "#613 typed tier: expected ${_typed_outcome} (setting=${FIXPP_TYPED_TIER_SETTING}, "
               "effective=${FIXPP_TYPED_TIER_EFFECTIVE}, FIXPP_BUILD_TYPE=${FIXPP_BUILD_TYPE})")

# The expected version list, never the directories found in the stage.
set(_typed_versions v42 v44 v50sp2)
if(FIXPP_CODEGEN_FIX_LATEST)
  list(APPEND _typed_versions vlatest)
endif()

file(REMOVE_RECURSE "${_stage}" "${_sub_build}")

# #613 §4.4 disk instrument: free space of the work directory before and after the
# install. It reports; it gates nothing.
file(MAKE_DIRECTORY "${FIXPP_WITNESS_WORK_DIR}")
find_program(_fixpp_df NAMES df)
function(_fixpp_witness_free _out)
  set(_free "unavailable")
  if(_fixpp_df)
    execute_process(COMMAND "${_fixpp_df}" -B1 --output=avail "${FIXPP_WITNESS_WORK_DIR}"
                    RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_QUIET)
    if(_rc EQUAL 0 AND _o MATCHES "([0-9]+)[ \t\r\n]*$")
      set(_free "${CMAKE_MATCH_1}")
    endif()
  endif()
  set(${_out} "${_free}" PARENT_SCOPE)
endfunction()
_fixpp_witness_free(_free_before)

# ── 1. Stage-install the main build ──────────────────────────────────────────
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${FIXPP_MAIN_BUILD_DIR}" --prefix "${_stage}"
  RESULT_VARIABLE _install_rc
  OUTPUT_VARIABLE _install_out
  ERROR_VARIABLE  _install_err
)
if(NOT _install_rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (exit ${_install_rc}):\n${_install_out}\n${_install_err}")
endif()

_fixpp_witness_free(_free_after)

# FR-008 sub-assertion: v42/v44/v50sp2 present, _dispatch/vt11 absent.
foreach(_ver v42 v44 v50sp2)
  if(NOT EXISTS "${_stage}/include/fixpp/${_ver}/Messages.hpp")
    message(FATAL_ERROR "Staged install missing fixpp/${_ver}/Messages.hpp — T023 install rule regressed")
  endif()
endforeach()
if(EXISTS "${_stage}/include/fixpp/_dispatch")
  message(FATAL_ERROR "Staged install leaked build-tree-private fixpp/_dispatch/ (must be excluded)")
endif()
if(EXISTS "${_stage}/include/fixpp/vt11")
  message(FATAL_ERROR "Staged install leaked fixpp/vt11/ (must be excluded per FR-008)")
endif()

# ── 1b. #613 §4.1 stage check, both branches — BEFORE any consumer configure ───
# Each expected version's immediate children must be EXACTLY the set
# fixpp_typed_tier_expected_children() names (cmake/FixppTypedTierRule.cmake).
fixpp_typed_tier_expected_children(${FIXPP_TYPED_TIER_EFFECTIVE} _typed_children_expected)
foreach(_ver IN LISTS _typed_versions)
  set(_vdir "${_stage}/include/fixpp/${_ver}")
  if(NOT IS_DIRECTORY "${_vdir}")
    message(FATAL_ERROR "#613: the staged install has no include/fixpp/${_ver}/ (an expected version)")
  endif()
  file(GLOB _children LIST_DIRECTORIES true RELATIVE "${_vdir}" "${_vdir}/*")
  list(SORT _children)
  if(NOT "${_children}" STREQUAL "${_typed_children_expected}")
    message(FATAL_ERROR
      "#613: include/fixpp/${_ver}/'s children are not the ${_typed_outcome} set.\n"
      "  expected: ${_typed_children_expected}\n"
      "  staged:   ${_children}")
  endif()
endforeach()
if(NOT FIXPP_CODEGEN_FIX_LATEST AND EXISTS "${_stage}/include/fixpp/vlatest")
  message(FATAL_ERROR
    "#613: FIXPP_CODEGEN_FIX_LATEST is OFF but the staged install has include/fixpp/vlatest/")
endif()

file(GLOB _staged_lib_files LIST_DIRECTORIES false RELATIVE "${_stage}" "${_stage}/lib/*")
if(NOT FIXPP_TYPED_TIER_EFFECTIVE)
  # ABSENT: no typed file in lib/ (archives AND .pdb, by name), and no typed imported
  # target in the staged export.
  set(_typed_lib_hits "")
  foreach(_f IN LISTS _staged_lib_files)
    if(_f MATCHES "fixpp_(builders|validators)_")
      list(APPEND _typed_lib_hits "${_f}")
    endif()
  endforeach()
  if(NOT _typed_lib_hits STREQUAL "")
    message(FATAL_ERROR
      "#613: the typed tier is ABSENT here, but the staged lib/ carries: ${_typed_lib_hits}")
  endif()
  file(GLOB _staged_targets "${_stage}/lib/cmake/fixpp/fixppTargets*.cmake")
  foreach(_t IN LISTS _staged_targets)
    file(READ "${_t}" _t_text)
    if(_t_text MATCHES "fixpp::(builders|validators)::[A-Za-z0-9_]+")
      message(FATAL_ERROR
        "#613: the typed tier is ABSENT here, but ${_t} names ${CMAKE_MATCH_0}")
    endif()
  endforeach()
else()
  # PRESENT: the typed PDB expectation (D-9). Required iff MSVC, packaging enabled
  # (the PDB plumbing lives in cmake/FixppPackaging.cmake) and a PDB debug-info
  # format; forbidden under Embedded (/Z7); otherwise not checked.
  if(NOT "${FIXPP_TYPED_DEBUG_INFO}" MATCHES "^(|ProgramDatabase|EditAndContinue|Embedded)$")
    message(FATAL_ERROR
      "#613: FIXPP_TYPED_DEBUG_INFO='${FIXPP_TYPED_DEBUG_INFO}' is not empty, ProgramDatabase, "
      "EditAndContinue or Embedded")
  endif()
  string(TOUPPER "${FIXPP_BUILD_TYPE}" _typed_build_type_uc)
  if(FIXPP_TYPED_DEBUG_INFO_SET)
    set(_typed_dbg "${FIXPP_TYPED_DEBUG_INFO}")
  elseif(_typed_build_type_uc MATCHES "^(DEBUG|RELWITHDEBINFO)$")
    # CMake's documented CMP0141 default: $<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>.
    set(_typed_dbg "ProgramDatabase")
  else()
    set(_typed_dbg "")
  endif()
  set(_typed_pdbs "")
  set(_typed_libs "")
  foreach(_f IN LISTS _staged_lib_files)
    if(_f MATCHES "^lib/fixpp_(builders|validators)_[A-Za-z0-9]+\\.pdb$")
      list(APPEND _typed_pdbs "${_f}")
    elseif(_f MATCHES "^lib/fixpp_(builders|validators)_[A-Za-z0-9]+\\.lib$")
      list(APPEND _typed_libs "${_f}")
    endif()
  endforeach()
  if(FIXPP_IS_MSVC AND FIXPP_PACKAGING_ENABLED AND _typed_dbg MATCHES "^(ProgramDatabase|EditAndContinue)$")
    set(_typed_pdb_arm "required")
    set(_typed_missing_pdbs "")
    foreach(_l IN LISTS _typed_libs)
      string(REGEX REPLACE "\\.lib$" ".pdb" _p "${_l}")
      # list(FIND), not IN_LIST: this -P script sets no policy version (CMP0057).
      list(FIND _typed_pdbs "${_p}" _p_idx)
      if(_p_idx EQUAL -1)
        list(APPEND _typed_missing_pdbs "${_l}")
      endif()
    endforeach()
    if(_typed_libs STREQUAL "" OR NOT _typed_missing_pdbs STREQUAL "")
      message(FATAL_ERROR
        "#613 (D-9): resolved debug-information format ${_typed_dbg}, so every staged typed .lib "
        "needs its .pdb beside it. Staged typed .lib files: [${_typed_libs}]; without a .pdb: "
        "[${_typed_missing_pdbs}]")
    endif()
  elseif(_typed_dbg STREQUAL "Embedded")
    set(_typed_pdb_arm "forbidden")
    if(NOT _typed_pdbs STREQUAL "")
      message(FATAL_ERROR
        "#613 (D-9): resolved debug-information format Embedded (/Z7) writes no compiler PDB, "
        "but the stage carries: ${_typed_pdbs}")
    endif()
  else()
    set(_typed_pdb_arm "not checked")
  endif()
  message(STATUS "#613 typed PDB expectation: resolved format '${_typed_dbg}' "
                 "(set=${FIXPP_TYPED_DEBUG_INFO_SET}, MSVC=${FIXPP_IS_MSVC}, "
                 "packaging=${FIXPP_PACKAGING_ENABLED}), arm: ${_typed_pdb_arm}")
endif()

# #613 §4.4: the stage's size, summed per file so it works on every platform. A zero
# sum means the glob saw nothing, which is a broken instrument, not a small stage.
file(GLOB_RECURSE _stage_files LIST_DIRECTORIES false "${_stage}/*")
set(_stage_bytes 0)
foreach(_f IN LISTS _stage_files)
  file(SIZE "${_f}" _sz)
  math(EXPR _stage_bytes "${_stage_bytes} + ${_sz}")
endforeach()
if(_stage_bytes EQUAL 0)
  message(FATAL_ERROR "#613 disk instrument: the stage sums to 0 bytes; the glob saw nothing")
endif()
message(STATUS "#613 consumer stage: ${_stage_bytes} bytes; work-dir free: "
               "${_free_before} before the install, ${_free_after} after")

# ── 1a. 087 T006 — request the codemodel-v2 File API reply ───────────────────
# contract §2a: a reply exists only if .cmake/api/v1/query/codemodel-v2 was
# present when CMake configured the sub-build below. tests/consumer/CMakeLists.txt
# cannot write this file itself — it executes *during* the configure it would be
# requesting a reply for, so the driver (here) writes it first. _sub_build does not
# exist yet at this point (the configure below creates it), so the query directory
# is created explicitly.
file(MAKE_DIRECTORY "${_sub_build}/.cmake/api/v1/query")
file(TOUCH "${_sub_build}/.cmake/api/v1/query/codemodel-v2")

# ── 2. Configure the standalone consumer project ─────────────────────────────
# Reuses the main build's Conan toolchain (resolves pugixml identically to the
# main build — no separate dependency-resolution mechanism) but points the
# consumer's ONLY include path at the staged prefix (never the build tree or
# source include/ — see tests/consumer/CMakeLists.txt).
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${FIXPP_CONSUMER_SRC_DIR}"
    -B "${_sub_build}"
    -G "Ninja"
    "-DCMAKE_TOOLCHAIN_FILE=${FIXPP_MAIN_BUILD_DIR}/conan_toolchain.cmake"
    "-DCMAKE_BUILD_TYPE=${FIXPP_BUILD_TYPE}"
    "-DCMAKE_CXX_COMPILER=${FIXPP_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${FIXPP_C_COMPILER}"
    # 086 T053: the sub-build does not go through CMakePresets.json, so it never
    # picks up the _base preset's CMAKE_EXPORT_COMPILE_COMMANDS. Without a
    # compile DB here, clang-tidy cannot be pointed at the new probe TUs at all.
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    "-DFIXPP_STAGE_PREFIX=${_stage}"
    "-DFIXPP_TYPED_EXPECTED=${FIXPP_TYPED_TIER_EFFECTIVE}"
    "-DFIXPP_TYPED_VERSIONS=${_typed_versions}"
    "-DFIXPP_CODEGEN_FIX_LATEST=${FIXPP_CODEGEN_FIX_LATEST}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE  _cfg_err
)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR "consumer configure failed (exit ${_cfg_rc}):\n${_cfg_out}\n${_cfg_err}")
endif()

# ── 2a. fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.4) — the recycler legs ──
#
# Each leg keeps the collected COMPILE_DEFINITIONS entries of its carrier that match
# ^ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE(=|$), removes identical duplicates, and requires the
# kept set to EQUAL exactly ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>. So it fails when the
# definition is missing (empty set), has another value (a different entry), or arrives with
# two values (two entries). N is read from the guard header, the value's one source.
#
# It runs HERE, before the build, on purpose: file(GENERATE) writes both carriers'
# definition sets at the end of the configure, so the legs can be judged without depending
# on whether anything in the build below would stop first.
include("${FIXPP_SOURCE_DIR}/cmake/FixppAsioRecycler.cmake")
fixpp_read_asio_recycler_cache_size(
  "${FIXPP_SOURCE_DIR}/include/fixpp/core/detail/asio_recycler_config.hpp" _recycler_n)
set(_recycler_expected "ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=${_recycler_n}")
set(_recycler_file "${_sub_build}/asio-recycler-definitions.txt")
if(NOT EXISTS "${_recycler_file}")
  message(FATAL_ERROR
    "fixpp#544: ${_recycler_file} was not generated — the recycler legs' carriers are missing "
    "from tests/consumer/CMakeLists.txt, so the legs assert nothing.")
endif()
file(READ "${_recycler_file}" _recycler_txt)
message(STATUS "fixpp#544 recycler definitions at the C++ consumer:\n${_recycler_txt}")
set(_recycler_failed "")
foreach(_leg UMBRELLA CORE)
  if(NOT _recycler_txt MATCHES "(^|\n)OBSERVED_${_leg}=([^\n]*)")
    message(FATAL_ERROR "fixpp#544: no OBSERVED_${_leg}= line in ${_recycler_file}")
  endif()
  string(REPLACE "|" ";" _defs "${CMAKE_MATCH_2}")
  set(_kept "")
  foreach(_d IN LISTS _defs)
    if(_d MATCHES "^ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE(=|$)")
      list(APPEND _kept "${_d}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _kept)
  if(NOT _kept STREQUAL _recycler_expected)
    message(STATUS
      "fixpp#544 FAIL: the ${_leg} leg's ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE set is [${_kept}], "
      "expected exactly [${_recycler_expected}].")
    list(APPEND _recycler_failed "${_leg}")
  endif()
endforeach()
if(_recycler_failed)
  message(FATAL_ERROR
    "fixpp#544 recycler legs failed: ${_recycler_failed}. An empty set means the PUBLIC "
    "definition no longer reaches that carrier (src/core/CMakeLists.txt attaches it to "
    "fixpp_core); another value means the attachment and the guard header disagree.")
endif()
message(STATUS "fixpp#544 recycler legs: OK — both carriers see exactly ${_recycler_expected}")

# ── 2b. fixpp#544 (finding F-5) — the installed guard refuses a TU without the definition ──
#
# tests/consumer/probe_recycler_guard.cpp includes an installed fixpp header that reaches
# asio. Here it is compiled with the staged include directory and asio's (read from the
# inputs tests/consumer/CMakeLists.txt writes), and no fixpp target, by the in-tree driver
# tests/sync/run_asio_recycler_negative_compile.cmake. That driver requires: the definition
# omitted fails on the guard's token; a wrong value fails on it; the exported value
# compiles. The twin, the same TU through fixpp::fixpp, is probe_recycler_guard_twin in the
# build below. Skipped on an MSVC front end, as the driver's in-tree registration is
# (tests/sync/CMakeLists.txt, under `if(NOT WIN32)`).
set(_guard_inputs "${_sub_build}/asio-recycler-guard-inputs.txt")
if(NOT EXISTS "${_guard_inputs}")
  message(FATAL_ERROR
    "fixpp#544: ${_guard_inputs} was not generated — the installed guard's negative arm "
    "has no inputs, so it asserts nothing.")
endif()
file(STRINGS "${_guard_inputs}" _guard_lines)
foreach(_key CXX_ID CXX_FRONTEND CXX_FLAGS ASIO_INCLUDE_DIRS ASIO_DEFINES TU)
  set(_guard_${_key} "")
  set(_guard_${_key}_seen FALSE)
  foreach(_l IN LISTS _guard_lines)
    if(_l MATCHES "^${_key}=(.*)$")
      set(_guard_${_key} "${CMAKE_MATCH_1}")
      set(_guard_${_key}_seen TRUE)
    endif()
  endforeach()
  if(NOT _guard_${_key}_seen)
    message(FATAL_ERROR "fixpp#544: no ${_key}= line in ${_guard_inputs}")
  endif()
endforeach()
if(_guard_CXX_ID STREQUAL "MSVC" OR _guard_CXX_FRONTEND STREQUAL "MSVC")
  message(STATUS "fixpp#544 installed guard negative arm: not run on an MSVC front end")
else()
  if(_guard_ASIO_INCLUDE_DIRS STREQUAL "")
    message(FATAL_ERROR "fixpp#544: the consumer's asio::asio has no include directory")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXPP_CXX=${FIXPP_CXX_COMPILER}"
      "-DFIXPP_CXX_ID=${_guard_CXX_ID}"
      "-DFIXPP_CXX_FLAGS=${_guard_CXX_FLAGS}"
      "-DFIXPP_INCLUDE_DIRS=${_stage}/include|${_guard_ASIO_INCLUDE_DIRS}"
      "-DFIXPP_DEFINES=${_guard_ASIO_DEFINES}"
      "-DFIXPP_TU=${_guard_TU}"
      "-DFIXPP_CACHE_SIZE=${_recycler_n}"
      -P "${FIXPP_SOURCE_DIR}/tests/sync/run_asio_recycler_negative_compile.cmake"
    RESULT_VARIABLE _guard_rc
    OUTPUT_VARIABLE _guard_out
    ERROR_VARIABLE  _guard_err)
  message(STATUS "fixpp#544 installed guard arms:\n${_guard_out}${_guard_err}")
  if(NOT _guard_rc EQUAL 0)
    message(FATAL_ERROR
      "fixpp#544: the installed guard's negative arm failed (exit ${_guard_rc}): a staged "
      "asio-reaching header compiled without ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE, or failed "
      "for a reason other than the guard (output above).")
  endif()
endif()

# ── 3. Build it — BY NAME, so a deleted gate fails closed ────────────────────
#
# 086 / Gate B r1 P1 #3. A bare `cmake --build` builds whatever targets happen to
# exist, so DELETING the positive probes or `consumer_capi_witness` left this
# driver perfectly green — there were simply fewer things to build. The ❌ cells
# had gained a read-back; the ✅ cells and the link-closure witness had no
# equivalent, and "the gate can be removed without anything noticing" is the same
# defect class as "the gate cannot fail".
#
# Naming them makes their absence a hard error if any is renamed or removed. The
# wording is GENERATOR-SPECIFIC: Ninja (what this project uses) says
# "ninja: error: unknown target '<name>'"; the Makefile generators say
# "No rule to make target". MEASURED, not assumed — deleting probe_service_negative
# produced the Ninja form. Match on both if you ever grep for it.
set(_required_targets
  consumer_witness           # the umbrella witness, run at step 4
  consumer_capi_witness      # FR-009 transitive-link closure (built + linked, never run)
  probe_capi_positive        # ✅ all 12 C-ABI headers, C++
  probe_capi_positive_c      # ✅ all 12 C-ABI headers, C
  probe_service_positive     # ✅ fixpp::service reaches the plugin header AND the C ABI
  probe_umbrella             # ✅ the umbrella still reaches everything
  probe_core                 # fixpp#544 recycler leg carrier (links only fixpp::core)
  probe_recycler_guard_twin  # fixpp#544 F-5 twin: an asio-reaching header through fixpp::fixpp
  probe_usage_requirements   # C-3 leg 3 carrier
  # ❌ cells. Since Gate B r3 these are ordinary targets asserted to COMPILE
  # (`__has_include` + a unique-token `#error`), so BUILDING them IS the
  # assertion — and naming them here is the only thing that stops one being
  # deleted silently, now that there is no configure-time probe table to read
  # back.
  probe_capi_negative        # ❌ fixpp::capi must not reach <fixpp/wire/parser.hpp>
  probe_capi_negative_service # ❌ fixpp::capi must not reach the service header
  probe_service_negative     # ❌ fixpp::service must not reach an engine header
  probe_system_include_contract) # 087 T010 — installed system-include interface (C-6.3)
# #613 §4.1: both branches add required targets, so neither can pass by registering
# nothing.
if(FIXPP_TYPED_TIER_EFFECTIVE)
  list(APPEND _required_targets
    consumer_typed_witness     # C-1: build_/validate_ per version, linked by name, RUN at step 4
    probe_typed_inline_small   # C-2a: whole-TU header-only mode, v42 + v44
    probe_typed_inline_large)  # C-2b: per-message header-only ListStatus, v50sp2 (+ vlatest)
else()
  list(APPEND _required_targets
    probe_typed_absent)        # C-3: no slim header resolves through fixpp::fixpp
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${_sub_build}" --target ${_required_targets}
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE  _build_err
)
# ⚠️ The token names in the message below are spelled WITHOUT brackets, and that
# is load-bearing rather than stylistic. The comparator emits its real token
# BRACKETED, as `[<TOKEN>] compare_system_includes.cmake compare (<leg>): ...`,
# and contract §5's demonstrations record that token as their evidence. If this
# help text enumerated them bracketed, a bare grep for the bracketed form would
# match THIS TEXT on every red, so a demonstration could be ticked having matched
# the enumeration rather than the emission — §5 row #8 asserts LEAK *and* DROP and
# would confirm both vacuously. Measured: the first draft of this message did
# exactly that, and all six token names appeared in a run whose only real token
# was the capi leg's LEG_ERROR.
#
# Evidence greps MUST anchor on the emission, e.g.
#   grep -oE '\[[A-Z_;]+\] compare_system_includes'
# rather than on a bare bracketed token name.
#
# ⚠️ THE ';' IN THAT CHARACTER CLASS IS REQUIRED, and its absence was a real
# false-negative until 2026-08-06. A leg that emits MORE THAN ONE token spells
# them semicolon-joined inside the brackets, in the `[<TOKEN;TOKEN>] ...` shape —
# so the narrower '\[[A-Z_]+\]' matches NOTHING on contract §5 row #8, the one row
# whose entire assertion is LEAK *and* DROP from a single mutation (FR-004, the
# direction of the mismatch). Measured: 0 hits with the old pattern against a
# genuine row-#8 red, 1 with this one.
#
# (The two-token example is written with the same `<...>` placeholder convention
# as the single-token one above, and for the same reason: spelling a REAL pair
# here would make this very comment match the widened grep. It did, in the first
# draft of this note — caught by re-running the property check against this file.)
#
# That failure mode is worse than it looks. A demonstrator who trusts the
# documented grep reads "no token emitted" on the row that emitted two, and the
# obvious repair — relaxing to a bare '\[DROP\]' — is exactly the vacuity the
# unbracketed rule above exists to prevent, because it matches this help text.
# Widening the class is the repair that keeps both properties: verified 0 matches
# against this file, and correct single-token matches on rows #1/#2.
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "consumer build failed (exit ${_build_rc}). This driver builds the 086/087 "
    "witness targets BY NAME (${_required_targets}). Three dispositions are "
    "possible, and they are NOT the same finding (contract C-6.3, and §3's "
    "pre-comparison split):\n"
    "  * an \"unknown target\" error (Ninja) or \"No rule to make target\" "
    "(Makefiles) means a gate was deleted or renamed;\n"
    "  * a non-zero exit FROM probe_system_include_contract carrying LEAK, DROP "
    "or RECLASSIFIED means the comparison RAN and FAILED — a genuine interface "
    "violation;\n"
    "  * a non-zero exit carrying MISSING_REPLY, INPUT_ERROR or LEG_ERROR means "
    "the leg terminated BEFORE comparing anything — the reply was absent, the "
    "reply was unparseable, or the comparator was driven wrong. These are NOT "
    "interface findings, and C-2 keeps them apart on purpose: \"the reply was "
    "corrupt\" and \"the carrier was driven wrong\" are different defects with "
    "different owners.\n"
    "(Token names are spelled unbracketed here on purpose; see the comment above "
    "this message in run_consumer_witness.cmake.) The token and its first "
    "diagnostic line are in the build output printed directly below.\n"
    "${_build_out}\n${_build_err}")
endif()

# ── 3a. 086 FR-006/FR-007 — the ❌ cells are asserted BY THE BUILD ABOVE ──────
#
# There is no probe table to read back any more. Until Gate B r3 the ❌ cells were
# configure-time `try_compile` calls that wrote `probe-results.txt`, and this
# block re-read it so deleting the probe block could not pass unnoticed. MSVC
# Debug then proved `try_compile` unusable here: it exports the imported-target
# closure into CMake's scratch project, which never defines Conan's
# `CONAN_LIB::…_DEBUG` targets, so the probes failed for a reason unrelated to
# include reachability on every Debug run.
#
# They are now ordinary OBJECT-library targets that must COMPILE, listed BY NAME
# in `_required_targets` above. That gives both properties the read-back gave:
#   * a probe that stops compiling (the header became reachable) fails the build;
#   * a probe that is DELETED or renamed fails the build — "ninja: error: unknown
#     target '<name>'" under Ninja — because the driver names it.
# The build failure above prints the compiler output verbatim, so an isolation
# breach is identifiable by its `FIXPP_086_FORBIDDEN_HEADER_REACHABLE` token
# while any other failure reads as a broken probe.


# ── 3b. 086 FR-009a(ii) / C-3 leg 3 — read back the usage-requirement probe ───
#
# tests/consumer/CMakeLists.txt writes this file with file(GENERATE), which runs
# at GENERATE time and asserts nothing by itself. The compare has to live
# downstream of the sub-build, which is here. Without this block the generated
# file is written and never read, and leg 3 does not exist.
#
# What is being checked: `fixpp::capi` carries `$<LINK_ONLY:fixpp::capi_objects>`,
# and $<LINK_ONLY:> withholds COMPILE_DEFINITIONS, COMPILE_OPTIONS,
# COMPILE_FEATURES and SYSTEM_INCLUDE_DIRECTORIES along with the include path. So
# a target that links only fixpp::capi must end up with an EMPTY effective set for
# all three.
#
# ⚠️ FOUR withheld, "all three" asserted — the seam is NOT a typo, and since 087
# (#234, 2026-08-05) it is no longer unexplained. This instrument (file(GENERATE)
# + the compare below) binds THREE of the four BY CONSTRUCTION: each has a
# documented collected consumer property to read via $<TARGET_PROPERTY:>.
# SYSTEM_INCLUDE_DIRECTORIES has none — comparing an absent property would read
# empty against empty, an assertion that cannot fail — so it is deliberately not
# among the three here.
#
# The FOURTH is bound by 087, through a different instrument: the CMake File API
# `codemodel-v2` compile groups, read at the installed consumer by
# tests/consumer/compare_system_includes.cmake and carried by the target
# `probe_system_include_contract` — which is listed in THIS FILE's own
# _required_targets above, so it cannot be deleted silently.
#
# Leg 3's own scope is UNCHANGED: it still asserts exactly the three properties
# named below, for exactly the reason given. Asserting empty — rather than "does not contain FIXPP_LOG_MIN_LEVEL"
# — is deliberate: it is a closed assertion, so a definition nobody predicted
# fails it too. (The withheld set today is at least FIXPP_LOG_MIN_LEVEL, from
# src/log/CMakeLists.txt's fixpp_log FIXPP_LOG_MIN_LEVEL definition, and ASIO_STANDALONE, carried by asio::asio linked
# unwrapped inside the closure. Membership is decided by this predicate, not by
# that list.) Instrument measured in research.md R10.
set(_usage_file "${_sub_build}/usage-requirements.txt")
if(NOT EXISTS "${_usage_file}")
  message(FATAL_ERROR
    "086 FR-009a(ii): ${_usage_file} was not generated — the usage-requirement probe "
    "is missing from tests/consumer/CMakeLists.txt, so C-3 leg 3 asserts nothing.")
endif()
file(READ "${_usage_file}" _usage_txt)
message(STATUS "086 usage requirements at the C-ABI consumer:\n${_usage_txt}")
foreach(_prop COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES)
  # Both sides are READ, never invented here: tests/consumer/CMakeLists.txt is the
  # named producer of the EXPECTED_ values (FR-009a(ii)), and the OBSERVED_ values
  # come from $<TARGET_PROPERTY:> on a real target inside the configured consumer.
  # Anchored per line — an unanchored match would let the COMPILE_OPTIONS line
  # satisfy the COMPILE_DEFINITIONS lookup through the substring.
  if(NOT _usage_txt MATCHES "(^|\n)EXPECTED_${_prop}=([^\n]*)")
    message(FATAL_ERROR "086 FR-009a(ii): no EXPECTED_${_prop}= line in ${_usage_file}")
  endif()
  set(_expected "${CMAKE_MATCH_2}")
  if(NOT _usage_txt MATCHES "(^|\n)OBSERVED_${_prop}=([^\n]*)")
    message(FATAL_ERROR "086 FR-009a(ii): no OBSERVED_${_prop}= line in ${_usage_file}")
  endif()
  set(_observed "${CMAKE_MATCH_2}")
  if(NOT _observed STREQUAL _expected)
    message(FATAL_ERROR
      "086 FR-009a(ii) FAIL: a consumer linking ONLY fixpp::capi sees "
      "${_prop}=[${_observed}], expected [${_expected}].\n"
      "If something appeared, the include interface was narrowed while this usage "
      "requirement still propagates — \$<LINK_ONLY:> is not in effect on fixpp::capi, "
      "or something outside the C-ABI closure is adding to it. If something "
      "disappeared that the expectation names, the closure lost a requirement it "
      "relies on. Either way the delivered interface is not what "
      "contracts/include-interface.md §2 describes.")
  endif()
endforeach()
message(STATUS "086 FR-009a(ii): OK — no usage requirement propagates through fixpp::capi")

# ── 4. Run it and check the output ────────────────────────────────────────────
# Run one built witness executable and require exit 0 and a PASS line.
#   name        the executable's basename in the sub-build
#   pass_regex  what its stdout must match (each caller keeps its own)
#   prefix      the caller's message prefix
#   ARGN        its command-line arguments
# Ninja single-config drops the exe at the sub-build root; the basename carries a
# .exe suffix on Windows (this -P script has no CMAKE_EXECUTABLE_SUFFIX, so probe
# both) and none on UNIX.
function(_fixpp_run_witness_exe name pass_regex prefix)
  set(_exe "${_sub_build}/${name}")
  if(NOT EXISTS "${_exe}" AND EXISTS "${_exe}.exe")
    set(_exe "${_exe}.exe")
  endif()
  if(NOT EXISTS "${_exe}")
    message(FATAL_ERROR "${prefix}${name} executable not found at ${_exe}")
  endif()
  execute_process(
    COMMAND "${_exe}" ${ARGN}
    RESULT_VARIABLE _run_rc
    OUTPUT_VARIABLE _run_out
    ERROR_VARIABLE  _run_err
  )
  message(STATUS "${name} stdout: ${_run_out}")
  if(NOT _run_rc EQUAL 0)
    message(FATAL_ERROR "${prefix}${name} exited ${_run_rc}:\n${_run_out}\n${_run_err}")
  endif()
  if(NOT _run_out MATCHES "${pass_regex}")
    message(FATAL_ERROR "${prefix}${name} did not print PASS: ${_run_out}")
  endif()
endfunction()

# 084 T037 / SC-014 — the API/data pairing must be USABLE, not merely co-located.
# The dictionary is loaded from the INSTALLED PREFIX, never from the source tree:
# FR-018a added the dictionaries/ install rule precisely so a consumer of the
# package can reach this data, and passing ${FIXPP_SOURCE_DIR}/dictionaries/ (as
# this driver did before 084) would prove only that the file exists in the repo —
# which says nothing about the package. Fail loudly rather than falling back.
set(_installed_dict "${_stage}/share/fixpp/dictionaries/FIX44.xml")
if(NOT EXISTS "${_installed_dict}")
  message(FATAL_ERROR
    "SC-014: no FIX44.xml in the installed prefix at ${_installed_dict} — the "
    "FR-018a dictionary install rule regressed. The package would ship "
    "fixpp::dict::load_any with none of its data.")
endif()

_fixpp_run_witness_exe(consumer_witness "^PASS:" "" "${_installed_dict}")

# ── 4a. #613 C-1 — RUN the typed witness (PRESENT only) ─────────────────────
if(FIXPP_TYPED_TIER_EFFECTIVE)
  _fixpp_run_witness_exe(consumer_typed_witness "(^|\n)PASS:" "#613: ")
endif()

# ── 5. #613 D-10 — after a PASS, remove only stage/lib/ ──────────────────────
# On a PRESENT lane stage/lib/ holds a full copy of the tree's typed archives, which
# lib/ in the build tree already holds once. stage/include and the sub-build (with the
# compile database 086's `clang-tidy -p` step reads) stay. A FATAL above leaves the
# whole stage in place for diagnosis. After the removal the sub-build can neither
# rebuild nor reconfigure, so the hand procedures that relink from the stage set
# FIXPP_WITNESS_KEEP_STAGE_LIB first.
if("$ENV{FIXPP_WITNESS_KEEP_STAGE_LIB}")
  message(STATUS "#613 D-10: FIXPP_WITNESS_KEEP_STAGE_LIB is set; keeping ${_stage}/lib")
else()
  file(REMOVE_RECURSE "${_stage}/lib")
  message(STATUS "#613 D-10: removed ${_stage}/lib after the PASS (stage/include and the sub-build kept)")
endif()

message(STATUS "fixpp::consumer::install-witness: OK")
