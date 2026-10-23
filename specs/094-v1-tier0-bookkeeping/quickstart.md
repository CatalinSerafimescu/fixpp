# Quickstart: re-verifying 094

Run from the library root on the feature branch. Each step names the instrument and the result that
counts as a pass. The commands that matter are reproduced in the verify record. The census script
lives in the orchestrator's scratchpad; its full text is committed into the verify record, so a
reader runs it from there.

Scripts that live outside the library resolve through the workspace root:
`W=$(git rev-parse --show-superproject-working-tree)`, run from the library root, prints it (the
library is a submodule of the workspace repository). In a `fixpp-parallel` worktree, set `W` to the
absolute workspace root instead.

## 1. Catalogue census (SC-001)

- Command: `python3 catalogue_census.py spec/feature-catalogue.md`.
- Base `origin/main` reads: OFFICIAL open 94, done 152 parsed (154 actual: C-002 and T-042 carry a
  literal `|` and do not split), dropped 1, postv1 0.
- This branch must read: OFFICIAL open **69**, done **170** parsed (172 actual), dropped 1,
  postv1 **8**.
- The only rows whose Status differs between the two runs are the 25 in data-model §A/§C, plus the new
  NFR-019.
- Known positive first (T002, research R-4): a mutant copy with NFR-001 set to `done` and W-015 set to
  the drop form reads open 92 and postv1 1.

## 2. Witness tests (SC-002)

Each flipped row's Tests cell names its binaries. Run them on a fresh build of this branch:

- `ctest --preset <preset> -R '^<entry>$' --no-tests=error --output-on-failure` for each ctest entry
  in research R-15's map (test file → target → ctest NAME), re-derived with its recipe first, on the
  preset CI runs it on. The mallocnesia gates (NFR-005) run on `linux-gcc-release` and
  `linux-clang-release` (the `if:` of the #448 steps); run both, or record which one was skipped and
  why. The rest run on `linux-clang-debug`.
- Entries (R-15):
  - wire and store: `wire_pure_tests`, `store_memory_zero_allocator_calls`, `store_memory_capacity`;
  - dictionary: `dictionary_pmr_allocation_test`, `dictionary_orchestra_tests`,
    `dictionary_pure_tests`, `delimiter_census`, `dictionary_dict_enum_census_test`,
    `dictionary_load_any_tests`;
  - codegen: `codegen_pure_tests`, `codegen_077_v42_vt11_completeness_c4_test`,
    `codegen_vlatest_tests`, `fixpp::dict::read-tier-byte-diff` (a `cmake -P` script, not gtest),
    `codegen_determinism_test`;
  - transport: `transport_smoke`, `transport_asio_plain_transport`, `transport_listener_acceptor`,
    `transport_cancellation_propagation`;
  - C ABI: `capi_dictionary_load`, `capi_inbound_frame_dispositions`;
  - session: `session_inbound_frame_dispositions`, `session_tc_reject` (has no labels),
    `session_pure_tests` (incl. the TC-017 `EnumValidationLogonTest` cells and FR-022's two cells),
    `session_resend_reply_possdup`, `session_538_store_lookup_after_reset`,
    `session_inbound_poss_resend`, `session_reset_on_lifecycle`,
    `session_next_expected_msgseqnum` (filter `Reset.InitiatorResetLogon_Advertises1`),
    `session_interpret_logon_encrypt_method`;
  - interop: `interop_parity_us3_tests`;
  - `-L mallocnesia` (NFR-005), with the `alloc_guard_*_positive_control_mallocnesia` controls.
- CI-evidence rows (NFR-001/003/005/013) cite Tier 1 run `39594667883` and Tier 2 run `39594667975`
  (`push:main` at `9b176090`, both success). Re-read the job and step names, and the `if:` lines, on
  this branch. Re-read branch protection:
  `gh api repos/CatalinSerafimescu/fixpp/branches/main/protection/required_status_checks --jq .contexts`.
- Pass: every named test reports PASSED in the binary's own output, read from the output and not
  inferred from ctest's exit code. A gtest filter for the parameterised store cells needs the
  `Stores/` prefix.
- FR-022's two cells: each passes on the branch, and each fails in a scratch copy with the
  `if (invalid_encrypt_method_seen)` return deleted from `interpret_logon`
  (`src/session/admin_messages.cpp`). Record both runs.
- FR-022b's census cell (`DictionaryCensus.Fix42AllMessagesAndFieldsLoaded`, in `dictionary_pure_tests`):
  it passes on the branch, and it fails under each of spec FR-022b's two loader mutants (one field, one
  message), each in its own scratch copy. Record the runs.
- FR-022c's census cell (`CodegenAccessorResolutionCensus.EveryTypedAccessorResolvesTagAndTypeAtCompileTime`,
  in `codegen_pure_tests`): it passes on the branch, and it fails under each of spec FR-022c's
  emitter mutants, each run after the scratch's generated `Messages.hpp` is shown to hold the mutant
  bytes, and under its test-side mutant, at the assertion FR-022c names for each. Record the runs.
- FR-022d's assertion (`InboundPossResendTest.DeliverInSeq_97Y`): it passes on the branch, and it
  fails in a scratch copy whose `make_poss_resend_frame` writes `97=N`. Record both runs.
- Fill data-model §H's last column from the assertions the cells ran (FR-015b), and run its recipe
  rows (TC-014, TC-017) after their seeded positives.
- NFR-013's backend grep (data-model §C) prints nothing over the build files; over every tracked file
  it prints at least one hit (the known positive), and no hit is a build file.
- `docs/prebuild.py`: run research R-5's `_status_header` one-liner on the branch's catalogue. The three
  `dropped(post-v1.0, …` strings appear with counts 2, 5 and 1 (on the base: none).
- Snapshot `gh issue view 614 --repo CatalinSerafimescu/fixpp --json state,labels,createdAt` into the
  verify record, and the same for the FR-018, FR-020, FR-021 and FR-023 issues.

## 3. Licence census (SC-004, SC-005)

- `git ls-files src include | grep -E '\.(c|cc|cpp|h|hpp|ipp|inl)$' | xargs grep -L 'SPDX-License-Identifier: AGPL-3.0-or-later'`
  must print nothing. ⚠️ Run it with the system `grep` (`/usr/bin/grep`): `xargs` cannot run a shell
  function or the `command` builtin, and a pipeline that fails to run prints nothing too. Prove it
  first on `origin/main`, where it must list 53 files (the 37 unmarked plus the 16 Apache-2.0).
- `… | xargs grep -l 'SPDX-License-Identifier: Apache'` must print nothing (on `origin/main`: 16).
- `git diff origin/main -- src include | grep -E '^[+-][^+-]' | sort | uniq -c` must show only the
  two AGPL comment forms added and `// SPDX-License-Identifier: Apache-2.0` removed.
- That filter cannot see an added or removed empty line (`+` or `-` alone) or a line starting with
  `+`/`-`, so pair it with `git diff --numstat origin/main -- src include`: it must list exactly the 53
  files, each `1 0` (the 37 inserts) or `1 1` (the 16 replacements). Over the frozen C-ABI headers
  (`include/fix/c_api.h`, `include/fix/c_api/*.h`) this is the comment-only check FR-014 names, beside
  research R-7's symbol diff. The numstat lists the 10 of the 12 frozen C-ABI headers that change
  (one licence line each); the other 2 already carry it and are unchanged.
- `git diff --stat origin/main -- ':!spec/' ':!.specify/' ':!brain/' ':!specs/' ':!CHANGELOG.md'`
  lists only files SC-005 names; any other path, under `tests/` or elsewhere, is a finding.
- `tools/check_capi_freeze.sh` must print `PASS`.
- `grep -n '^    license' conanfile.py` must read `license = "AGPL-3.0-or-later"`.
- `grep -n 'CPACK_RPM_PACKAGE_LICENSE' cmake/FixppPackaging.cmake` must read `"AGPL-3.0-or-later"`,
  and the packaging lane's RPM must carry `License: AGPL-3.0-or-later` (`rpm -qip`).
- `git log --format=%B origin/main..HEAD -- conanfile.py` shows the comment commit's `Refs #607`, and
  no closing keyword next to #607, #612, #614 or the FR-018, FR-020, FR-021 and FR-023 issues anywhere in the
  branch's messages.

## 4. Cross-surface sweep (SC-003)

- Mechanical half of the constitution check: for each data-model §E.3–§E.7 after-text,
  `grep -cF '<after-text>' .specify/constitution.md` prints 1; for each before-text it replaced,
  `grep -cF` prints 0. Skip the before-text check for §E.3a, whose after-text extends its before-text,
  and for §E.7, which adds a bullet. Known positive: the same checks on `origin/main` print 0 and 1.
- Run research R-12's four `git grep` commands, and (c)'s syntax-independent licence sweep beside it. Known positives first, on `origin/main`: (a) must report
  `README.md`'s tagline and, through its widened terms, `.specify/constitution.md` §I.2; (b) must report
  `brain/components/dictionary.md`'s D-007 table row.
- On the branch, every hit must fall in a "keep", "header", "other concept" or "pattern FP" class of
  R-12, or be an edited line. Record the count per class.
- `grep -niE "grpc|iceoryx|service wrapper|service[- ]mode|out-of-process" .specify/constitution.md`:
  every hit is the v4.0 Sync Impact Report, the Status line (its v4.0 text names the post-1.0 scope), a
  clause that says post-1.0, an earlier Sync Impact Report, or §I.2 / §IV.2 / §XIII.3, none of which
  states v1.0 scope.

## 5. Line citations and comment claims

- `python3 tools/check_line_citations.py --shift-audit HEAD~1..HEAD` on a scratch commit of the docs
  edits alone, before the PR (the new Sync Impact Report shifts every constitution line; R-6).
- `python3 tools/check_line_citations.py --range origin/main..HEAD` passes.
- `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD` passes. Frozen
  `specs/<id>/` citers are reported, not charged.
- `"$W/.claude/scripts/check-comment-claims.py" --root "$PWD" --base origin/main` passes.
- `"$W/.claude/scripts/check_bl_delta.py"` sees the L-094-1 delta.
- `"$W/research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh"` passes; report its execution
  count.
