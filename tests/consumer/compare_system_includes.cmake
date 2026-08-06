# tests/consumer/compare_system_includes.cmake — 087 T008/T009.
#
# Standalone `cmake -P` comparator for the installed system-include interface of
# `fixpp::capi` and `fixpp::service` (contract C-6). It is deliberately its own
# file, not an inline block in run_consumer_witness.cmake — see C-6 for why an
# inline block reproduces 086's "the gate can be removed without anything
# noticing" defect.
#
# `specs/087-system-include-binding/contracts/system-include-interface.md` §3 is
# the single authority for every rule this script implements; comments below
# cite it but do not restate it in full. `data-model.md` E1-E4/I1-I7 is the
# entity/invariant authority for the same rules.
#
# Two modes, selected by -DFIXPP_087_MODE, both driven entirely by -D variables
# (project convention — see run_consumer_witness.cmake:5, "no hard-coded paths,
# everything comes in via -D").
#
#   compare  (contract C-6.1) — one leg's observed-vs-expected comparison:
#     cmake -DFIXPP_087_MODE=compare \
#           -DFIXPP_087_REPLY_DIR=<codemodel-v2 reply dir> \
#           -DFIXPP_087_LEG=<capi|service> \
#           -DFIXPP_087_INSTALL_PREFIX=<staged install prefix> \
#           -DFIXPP_087_EXPECTATION="<path>|<0|1>;<path>|<0|1>;..." \
#           -DFIXPP_087_RESULT_FILE=<file to write> \
#           -P compare_system_includes.cmake
#     Each FIXPP_087_EXPECTATION entry is "<prefix-relative path>|<isSystem: 0
#     or 1>" (E3.entries — a set of (path, isSystem) pairs). This is this
#     script's own encoding, not dictated by the contract; the carrier (T010)
#     is free to build it however it likes as long as the wire format matches.
#
#   leg-set  (contract C-6.4) — the "exactly two distinct known legs" assertion,
#   separately invocable so demonstration #6a's missing-leg / duplicated-leg
#   sub-cases are pure invocation, no tree edit (contract §5, class "invocation"):
#     cmake -DFIXPP_087_MODE=leg-set \
#           -DFIXPP_087_RESULT_FILES="<result-file-1>;<result-file-2>;..." \
#           -P compare_system_includes.cmake
#
# Both modes live in this one file on purpose (C-6.1): deleting it fails the
# carrier's own build command for EITHER mode, adding no second entry to the
# out-of-tree file accounting.

# string(JSON ...) needs CMake >= 3.19; the project floor for this sub-build is
# 3.28 (tests/consumer/CMakeLists.txt:39) and IN_LIST needs CMP0057, which a
# `cmake -P` script (no project()) only gets from cmake_minimum_required itself
# — see tests/packaging/run_package_contents_witness.cmake:20-21 for the same
# note on the same policy.
cmake_minimum_required(VERSION 3.28)

# The two legs, and the probe target whose reply each one reads. ONE table, so
# the known-leg list and the leg->target mapping cannot drift apart: adding a leg
# here without its _FIXPP_087_TARGET_<leg> entry resolves the lookup below to the
# empty string and reds as MISSING_REPLY, rather than silently reading some other
# leg's reply through an else() catch-all.
set(_FIXPP_087_KNOWN_LEGS capi service)
set(_FIXPP_087_TARGET_capi    probe_usage_requirements)
set(_FIXPP_087_TARGET_service probe_service_positive)

# ── Helper: prefix-relative normalisation (C-3, I1) ──────────────────────────
# The File API emits forward slashes on both platforms (data-model.md E1), so
# only string matching is needed here — no path-separator translation.
# An entry outside the prefix is left in canonical absolute form; C-3 says that
# form can never match the closed prefix-relative expectation, so it becomes a
# LEAK by construction rather than by a special case here.
#
# ⚠️ LIMIT OF THE WRONG-PREFIX GUARD BELOW, stated because the guard's error text
# would otherwise read as full coverage. The guard fires when the prefix stripped
# NOTHING. It cannot detect a prefix that strips SOMETHING but the wrong amount —
# specifically a proper ANCESTOR of the real prefix (pass
# <build>/_consumer_witness where the real one is <build>/_consumer_witness/stage
# and every entry normalises to stage/include/... instead of include/...). That
# still degenerates to all-LEAK + all-DROP, and demonstration #3 asserts DROP,
# a subset of it. Not mechanically closable here: "require at least one stage-1
# path match" would false-fire on a genuine total regression, reporting a real
# LEAK+DROP as a usage error. The empty/root prefix — the other member of this
# class — IS closed, at argument validation below.
function(_fixpp_087_normalize_path _abspath _prefix _out_var)
  string(REGEX REPLACE "/+$" "" _p "${_prefix}")
  string(LENGTH "${_p}" _plen)
  string(LENGTH "${_abspath}" _alen)
  if(_alen GREATER _plen)
    string(SUBSTRING "${_abspath}" 0 ${_plen} _head)
    string(SUBSTRING "${_abspath}" ${_plen} 1 _sep)
    # _sep guards a false prefix match at a non-directory boundary, e.g.
    # prefix=/foo must not consume /foobar/include as if /foo were its parent.
    if(_head STREQUAL _p AND _sep STREQUAL "/")
      math(EXPR _restart "${_plen} + 1")
      string(SUBSTRING "${_abspath}" ${_restart} -1 _rel)
      set(${_out_var} "${_rel}" PARENT_SCOPE)
      return()
    endif()
  endif()
  set(${_out_var} "${_abspath}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED FIXPP_087_MODE)
  message(FATAL_ERROR "compare_system_includes.cmake: -DFIXPP_087_MODE=compare|leg-set is required")
endif()

# ════════════════════════════════════════════════════════════════════════════
# Mode: compare  (contract C-6.1)
# ════════════════════════════════════════════════════════════════════════════
if(FIXPP_087_MODE STREQUAL "compare")

  # ── Basic invocation sanity — plain usage errors, not a contract token ─────
  # (mirrors the required--D-var loop convention in run_consumer_witness.cmake)
  foreach(_var FIXPP_087_REPLY_DIR FIXPP_087_INSTALL_PREFIX FIXPP_087_RESULT_FILE)
    if(NOT DEFINED ${_var})
      message(FATAL_ERROR "compare_system_includes.cmake compare: -D${_var}=... is required")
    endif()
  endforeach()
  if(NOT DEFINED FIXPP_087_LEG)
    set(FIXPP_087_LEG "")
  endif()

  # ── C-6.4: argument validation FIRST, before any reply is located ─────────
  # Two of C-2's four LEG_ERROR causes belong here: an unknown leg, and an
  # empty expectation. Both must fire before the reply glob below runs, so
  # they red even against a perfectly correct reply (contract §3, C-6.4).
  if(NOT FIXPP_087_LEG IN_LIST _FIXPP_087_KNOWN_LEGS)
    message(FATAL_ERROR
      "[LEG_ERROR] compare_system_includes.cmake compare: unknown leg "
      "'${FIXPP_087_LEG}' (known: ${_FIXPP_087_KNOWN_LEGS})")
  endif()

  if(NOT DEFINED FIXPP_087_EXPECTATION OR FIXPP_087_EXPECTATION STREQUAL "")
    # data-model.md I3: the expectation is the feature's anti-vacuity property,
    # and it is a property of what actually REACHES compare, not of the tree
    # literal — an empty argument here would otherwise compare emptiness
    # against emptiness and report green having asserted nothing.
    message(FATAL_ERROR
      "[LEG_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "empty -DFIXPP_087_EXPECTATION — refusing an unguarded null-vs-null "
      "comparison (data-model.md I3)")
  endif()

  # Parse the expectation into parallel (path, isSystem) lists. Still
  # argument-validation, still before the reply is located.
  set(_expected_paths "")
  set(_expected_sys "")
  foreach(_entry ${FIXPP_087_EXPECTATION})
    if(NOT _entry MATCHES "^(.*)\\|(0|1)$")
      message(FATAL_ERROR
        "[LEG_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
        "malformed -DFIXPP_087_EXPECTATION entry '${_entry}' — want "
        "<prefix-relative path>|<0|1>")
    endif()
    list(APPEND _expected_paths "${CMAKE_MATCH_1}")
    list(APPEND _expected_sys "${CMAKE_MATCH_2}")
  endforeach()

  # I3 is a property of the expectation that REACHES C-1, and the STREQUAL ""
  # test above only sees the raw string. A value that is non-empty as a string
  # but parses to ZERO entries — ";" is the whole family, and C-6.4's own
  # rationale for this cause is "a quoting error in the carrier's command can
  # drop the argument" — passes that test, iterates zero times here, and then
  # compares an empty expected set against the observation. If the observation
  # is also empty the run reports green having asserted nothing, which is the
  # exact outcome this cause exists to make impossible.
  #
  # Same LEG_ERROR cause as above (an empty expectation), not a fifth one:
  # C-6.4's cause list stays four. This completes its detection rather than
  # extending it.
  if(NOT _expected_paths)
    message(FATAL_ERROR
      "[LEG_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "-DFIXPP_087_EXPECTATION='${FIXPP_087_EXPECTATION}' is non-empty as a "
      "string but parses to ZERO entries — refusing an unguarded null-vs-null "
      "comparison (data-model.md I3)")
  endif()

  # DEFINED is not enough for the prefix: an empty (or "/") value normalises to
  # a zero-length prefix, which matches the leading "/" of EVERY absolute entry,
  # strips it, and so passes the wrong-prefix guard while leaving nothing able to
  # match the expectation — the same degenerate all-LEAK + all-DROP the guard
  # exists to prevent, reached through the one input the guard cannot see.
  # Deliberately a plain usage error, NOT a token: C-6.4's cause list stays four.
  #
  # ⚠️ POSITION IS LOAD-BEARING: this sits BELOW every [LEG_ERROR] check above and
  # ABOVE the reply location below. It must stay pre-location — that is its whole
  # purpose — but it must NOT precede a token guard. §5 row 6a(i) is "compare mode
  # with an unknown leg" and, unlike (iv), does NOT stipulate "every other argument
  # correct", so a demonstrator inducing it may pass a throwaway prefix; run above
  # the unknown-leg check, this guard shadows the [LEG_ERROR] that row exists to
  # record and the row banks NO token at all. Measured: it did exactly that when
  # first written, and the direct-invocation test missed it because that test
  # passed a correct prefix — the one configuration where this guard is silent.
  # The original wrong-prefix guard below follows the same rule, further down still.
  string(REGEX REPLACE "/+$" "" _prefix_probe "${FIXPP_087_INSTALL_PREFIX}")
  if(_prefix_probe STREQUAL "")
    message(FATAL_ERROR
      "compare_system_includes.cmake compare: "
      "-DFIXPP_087_INSTALL_PREFIX is empty or the filesystem root. A zero-length "
      "prefix strips the leading '/' from every observed entry, so the comparison "
      "would degenerate to all-LEAK + all-DROP and void this row's assertion.\n"
      "Pass the prefix the configure actually used — for the shipped witness that "
      "is <build>/_consumer_witness/stage.")
  endif()

  # ── C-5: locate the reply BY GLOB — never a hard-coded hash ────────────────
  # Table lookup, not an if/else: the else() branch used to map EVERY non-capi
  # leg to probe_service_positive, so a third leg added to _FIXPP_087_KNOWN_LEGS
  # would have compared the SERVICE target's reply against the new leg's
  # expectation with nothing to say so. The IN_LIST guard above already
  # guarantees this resolves, so no fallback branch is needed.
  set(_target_name "${_FIXPP_087_TARGET_${FIXPP_087_LEG}}")

  if(NOT EXISTS "${FIXPP_087_REPLY_DIR}")
    message(FATAL_ERROR
      "[MISSING_REPLY] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "reply directory does not exist: ${FIXPP_087_REPLY_DIR}")
  endif()

  file(GLOB _reply_matches "${FIXPP_087_REPLY_DIR}/target-${_target_name}-*.json")
  list(LENGTH _reply_matches _n_matches)
  if(_n_matches EQUAL 0)
    # §2a: the realistic failure. MUST NOT be read as "no includes".
    message(FATAL_ERROR
      "[MISSING_REPLY] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "no target-${_target_name}-*.json under ${FIXPP_087_REPLY_DIR} — the "
      "codemodel-v2 query may not have preceded configure (contract §2a)")
  endif()
  # More than one match is NOT a case C-2 disposes of: its two tokens cover a
  # MISSING artifact (MISSING_REPLY) and a present-but-unparseable one
  # (INPUT_ERROR), and neither covers ambiguity. C-5 says the reply is LOCATED by
  # glob; it does not license silently picking one of several. Unreachable via
  # the carrier — the driver wipes the sub-build and configures it exactly once,
  # so one configure writes one reply per target — but §5's reply-side rows all
  # work by cp -r'ing a real reply directory and mutating the copy by hand, and a
  # copy carrying a second target-<name>-*.json would be compared against the
  # lexicographically-first file with no diagnostic. Demonstration #5(i), which
  # DELETES the per-target reply, would then not emit MISSING_REPLY at all: it
  # would fall through to the surviving sibling and compare that instead.
  #
  # Deliberately a plain usage error, NOT a token: adding a cause to INPUT_ERROR
  # would amend C-2's enumeration and the audited countable sets, and this is a
  # malformed INPUT DIRECTORY rather than malformed input DATA. Same disposition
  # the wrong-prefix guard was given, for the same reason.
  if(_n_matches GREATER 1)
    message(FATAL_ERROR
      "compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "${_n_matches} replies match target-${_target_name}-*.json under "
      "${FIXPP_087_REPLY_DIR} — ambiguous, refusing to pick one.\n"
      "Matches: ${_reply_matches}")
  endif()
  list(GET _reply_matches 0 _reply_file)

  # ── C-2: present-but-unparseable / wrong structure ⇒ INPUT_ERROR ──────────
  file(READ "${_reply_file}" _json)
  string(JSON _root_type ERROR_VARIABLE _root_err TYPE "${_json}")
  if(_root_err)
    message(FATAL_ERROR
      "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "${_reply_file} does not parse as JSON: ${_root_err}")
  endif()
  if(NOT _root_type STREQUAL "OBJECT")
    message(FATAL_ERROR
      "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "${_reply_file} root is not a JSON object (got ${_root_type})")
  endif()

  string(JSON _cg_type ERROR_VARIABLE _cg_err TYPE "${_json}" "compileGroups")
  if(_cg_err OR NOT _cg_type STREQUAL "ARRAY")
    message(FATAL_ERROR
      "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "${_reply_file} has no compileGroups array (${_cg_err})")
  endif()
  string(JSON _cg_len LENGTH "${_json}" "compileGroups")

  # ── Collect E1 entries across ALL compileGroups[], normalised + deduped ───
  # (data-model.md E2: the reply's order is measurement metadata, not asserted
  # — I2 canonicalises to a set. A path seen more than once, e.g. from two
  # compile groups on a multi-language target, keeps only its first isSystem;
  # not observed to disagree in this project's measured replies.)
  set(_observed_paths "")
  set(_observed_sys "")
  set(_any_under_prefix FALSE)
  if(_cg_len GREATER 0)
    math(EXPR _cg_last "${_cg_len} - 1")
    foreach(_gi RANGE 0 ${_cg_last})
      string(JSON _inc_type ERROR_VARIABLE _inc_err TYPE "${_json}" "compileGroups" ${_gi} "includes")
      if(_inc_err)
        continue() # this compile group has no includes[] at all — legal, not an error
      endif()
      if(NOT _inc_type STREQUAL "ARRAY")
        message(FATAL_ERROR
          "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
          "${_reply_file} compileGroups[${_gi}].includes is not an array")
      endif()
      string(JSON _inc_len LENGTH "${_json}" "compileGroups" ${_gi} "includes")
      if(_inc_len GREATER 0)
        math(EXPR _inc_last "${_inc_len} - 1")
        foreach(_ii RANGE 0 ${_inc_last})
          string(JSON _path_type ERROR_VARIABLE _path_type_err TYPE "${_json}" "compileGroups" ${_gi} "includes" ${_ii} "path")
          if(_path_type_err)
            message(FATAL_ERROR
              "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
              "${_reply_file} compileGroups[${_gi}].includes[${_ii}] has no 'path'")
          endif()
          # E1 declares path: string and isSystem: boolean (data-model.md:15-16), so a
          # wrong leaf type is "parses without the expected compileGroups structure"
          # (C-2) — the same TYPE walk applied at the root (:249), compileGroups (:261)
          # and includes (:281) above, one level deeper. No new C-2 cause.
          if(NOT _path_type STREQUAL "STRING")
            message(FATAL_ERROR
              "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
              "${_reply_file} compileGroups[${_gi}].includes[${_ii}].path is not a JSON string (got ${_path_type})")
          endif()
          string(JSON _path GET "${_json}" "compileGroups" ${_gi} "includes" ${_ii} "path")

          # E1: isSystem absent in JSON means false; if present it must be a JSON boolean.
          string(JSON _sys_type ERROR_VARIABLE _sys_type_err TYPE "${_json}" "compileGroups" ${_gi} "includes" ${_ii} "isSystem")
          if(_sys_type_err)
            set(_sys OFF)
          elseif(NOT _sys_type STREQUAL "BOOLEAN")
            message(FATAL_ERROR
              "[INPUT_ERROR] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
              "${_reply_file} compileGroups[${_gi}].includes[${_ii}].isSystem is not a JSON boolean (got ${_sys_type})")
          else()
            string(JSON _sys GET "${_json}" "compileGroups" ${_gi} "includes" ${_ii} "isSystem")
          endif()

          _fixpp_087_normalize_path("${_path}" "${FIXPP_087_INSTALL_PREFIX}" _relpath)
          if(NOT _relpath STREQUAL _path)
            # the prefix was actually stripped, i.e. this entry lies under it
            set(_any_under_prefix TRUE)
          endif()

          list(FIND _observed_paths "${_relpath}" _existing_idx)
          if(_existing_idx EQUAL -1)
            list(APPEND _observed_paths "${_relpath}")
            list(APPEND _observed_sys "${_sys}")
          endif()
        endforeach()
      endif()
    endforeach()
  endif()

  # ── Guard: the prefix must actually match something ────────────────────────
  # NOT a contract token — a plain usage error, same class as the required--D
  # check above, so C-6.4's cause list stays at four and the countable sets in
  # tasks.md are untouched.
  #
  # Why it exists. Passing a prefix the configure did not use does NOT error on
  # its own: under C-3 every observed entry stays in canonical absolute form,
  # nothing path-matches the closed prefix-relative expectation, C-1 stage 1
  # claims no pair, and the comparison degenerates to all-LEAK + all-DROP.
  # MEASURED, not assumed: /tmp/fixpp-stage-087, a nonexistent path and a
  # relative path all produced exactly `LEAK;DROP` against a correct reply.
  #
  # That is a false-green generator for the demonstrations, not for the gate.
  # §5 demonstration #3 asserts `DROP`, which is a SUBSET of the degenerate
  # output — so #3 could be ticked having asserted nothing about the entry it
  # deleted. (#4 escapes only because its wording is "RECLASSIFIED, and that
  # token ALONE"; #3 has no such qualifier.) tasks.md closes this with three
  # prose warnings; this closes it mechanically.
  #
  # It cannot fire on a legitimate comparison: both legs' entries live under the
  # staged prefix, and even demonstration #2's reverted `fixpp::capi` — which
  # gains third-party roots OUTSIDE the prefix — retains `include/capi`, so at
  # least one entry still normalises. A genuinely empty observed set is not
  # caught here by design: that is C-1's DROP, per contract §3's C-2 box.
  if(_observed_paths AND NOT _any_under_prefix)
    message(FATAL_ERROR
      "compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "install-prefix matched NO observed entry (prefix=${FIXPP_087_INSTALL_PREFIX}).\n"
      "Every observed path stayed absolute, so the comparison would degenerate "
      "to all-LEAK + all-DROP and silently void this row's token assertion.\n"
      "Pass the prefix the configure actually used — for the shipped witness "
      "that is <build>/_consumer_witness/stage, never a separate manual "
      "`cmake --install` prefix.\n"
      "Observed: ${_observed_paths}")
  endif()

  # ── C-1: exact set equality, TWO ORDERED STAGES ────────────────────────────
  # Stage 1 — match by path; a matched pair with differing isSystem is
  # RECLASSIFIED. Every matched path (agreeing or not) is removed from stage-2
  # consideration by construction below (it is added to _matched_paths either
  # way).
  set(_reclassified "")
  set(_dropped "")
  set(_matched_paths "")

  list(LENGTH _expected_paths _en)
  if(_en GREATER 0)
    math(EXPR _elast "${_en} - 1")
    foreach(_i RANGE 0 ${_elast})
      list(GET _expected_paths ${_i} _ep)
      list(GET _expected_sys ${_i} _es)
      list(FIND _observed_paths "${_ep}" _oi)
      if(_oi EQUAL -1)
        list(APPEND _dropped "${_ep}")
      else()
        list(APPEND _matched_paths "${_ep}")
        list(GET _observed_sys ${_oi} _os)
        # Both sides are CMake-truthy values (expectation: literal "0"/"1";
        # observed: JSON GET's "ON"/"OFF", or "OFF" default) — normalise via
        # if() rather than STREQUAL so the two spellings compare correctly.
        if(_es)
          set(_es_b TRUE)
        else()
          set(_es_b FALSE)
        endif()
        if(_os)
          set(_os_b TRUE)
        else()
          set(_os_b FALSE)
        endif()
        if(NOT _es_b STREQUAL _os_b)
          list(APPEND _reclassified "${_ep} (expected isSystem=${_es_b}, observed isSystem=${_os_b})")
        endif()
      endif()
    endforeach()
  endif()

  # Stage 2 — residuals. Observed entries never claimed by stage 1 are LEAK.
  set(_leaked "")
  list(LENGTH _observed_paths _on)
  if(_on GREATER 0)
    math(EXPR _olast "${_on} - 1")
    foreach(_i RANGE 0 ${_olast})
      list(GET _observed_paths ${_i} _op)
      if(NOT _op IN_LIST _matched_paths)
        list(APPEND _leaked "${_op}")
      endif()
    endforeach()
  endif()

  # ── Complete token set (C-1 permits more than one at once) ────────────────
  set(_tokens "")
  if(_reclassified)
    list(APPEND _tokens "RECLASSIFIED")
  endif()
  if(_leaked)
    list(APPEND _tokens "LEAK")
  endif()
  if(_dropped)
    list(APPEND _tokens "DROP")
  endif()

  set(_status "OK")
  if(_tokens)
    set(_status "FAIL")
  endif()
  list(JOIN _tokens ";" _tokens_joined)

  # ── C-6.1: write the per-leg result file BEFORE terminating — including on
  # a red comparison. This must happen before the FATAL_ERROR below, because
  # FATAL_ERROR ends the script immediately.
  file(WRITE "${FIXPP_087_RESULT_FILE}"
    "leg=${FIXPP_087_LEG}\nstatus=${_status}\ntokens=${_tokens_joined}\n")

  if(_tokens)
    set(_diag "")
    if(_reclassified)
      list(JOIN _reclassified ", " _r)
      string(APPEND _diag "RECLASSIFIED (path matched, isSystem differs): ${_r}\n")
    endif()
    if(_leaked)
      list(JOIN _leaked ", " _l)
      string(APPEND _diag "LEAK (observed, not expected): ${_l}\n")
    endif()
    if(_dropped)
      list(JOIN _dropped ", " _d)
      string(APPEND _diag "DROP (expected, not observed): ${_d}\n")
    endif()
    message(FATAL_ERROR
      "[${_tokens_joined}] compare_system_includes.cmake compare (${FIXPP_087_LEG}): "
      "system-include comparison failed against ${_reply_file} "
      "(prefix=${FIXPP_087_INSTALL_PREFIX})\n${_diag}")
  endif()

  message(STATUS "compare_system_includes.cmake compare (${FIXPP_087_LEG}): OK — result written to ${FIXPP_087_RESULT_FILE}")

# ════════════════════════════════════════════════════════════════════════════
# Mode: leg-set  (contract C-6.4)
# ════════════════════════════════════════════════════════════════════════════
elseif(FIXPP_087_MODE STREQUAL "leg-set")

  if(NOT DEFINED FIXPP_087_RESULT_FILES OR FIXPP_087_RESULT_FILES STREQUAL "")
    message(FATAL_ERROR
      "[LEG_ERROR] compare_system_includes.cmake leg-set: -DFIXPP_087_RESULT_FILES=... "
      "is required and must name at least one result file (got zero)")
  endif()

  set(_legs_seen "")
  foreach(_rf ${FIXPP_087_RESULT_FILES})
    if(NOT EXISTS "${_rf}")
      message(FATAL_ERROR
        "[LEG_ERROR] compare_system_includes.cmake leg-set: result file does not "
        "exist: ${_rf}")
    endif()
    file(STRINGS "${_rf}" _rf_lines)
    set(_this_leg "")
    foreach(_line ${_rf_lines})
      if(_line MATCHES "^leg=(.*)$")
        set(_this_leg "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(_this_leg STREQUAL "" OR NOT _this_leg IN_LIST _FIXPP_087_KNOWN_LEGS)
      message(FATAL_ERROR
        "[LEG_ERROR] compare_system_includes.cmake leg-set: ${_rf} does not name a "
        "known leg (got '${_this_leg}', known: ${_FIXPP_087_KNOWN_LEGS})")
    endif()
    list(APPEND _legs_seen "${_this_leg}")
  endforeach()

  list(LENGTH FIXPP_087_RESULT_FILES _n_files)
  set(_legs_distinct "${_legs_seen}")
  list(REMOVE_DUPLICATES _legs_distinct)
  list(LENGTH _legs_distinct _n_distinct)

  if(NOT _n_files EQUAL 2 OR NOT _n_distinct EQUAL 2)
    message(FATAL_ERROR
      "[LEG_ERROR] compare_system_includes.cmake leg-set: expected exactly two "
      "distinct legs (capi, service); got ${_n_files} result file(s) naming "
      "legs [${_legs_seen}] (${_n_distinct} distinct)")
  endif()

  message(STATUS "compare_system_includes.cmake leg-set: OK — capi and service both present, this run")

else()
  message(FATAL_ERROR
    "compare_system_includes.cmake: unknown -DFIXPP_087_MODE='${FIXPP_087_MODE}' "
    "(known: compare, leg-set)")
endif()
