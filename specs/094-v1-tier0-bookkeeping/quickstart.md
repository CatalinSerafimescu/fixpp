# Quickstart: re-verifying 094

Run from the library root on the feature branch. Each step names the instrument and the result that
counts as a pass. The commands that matter are reproduced in the verify record. The census script
lives in the orchestrator's scratchpad and is reproduced there in full.

## 1. Catalogue census (SC-001)

- Command: `python3 catalogue_census.py spec/feature-catalogue.md`.
- Base `origin/main` reads: OFFICIAL open 94, done 152 parsed (154 actual: C-002 and T-042 carry a
  literal `|` and do not split), dropped 1, postv1 0.
- This branch must read: OFFICIAL open **69**, done **170** parsed (172 actual), dropped 1,
  postv1 **8**.
- The only rows whose Status differs between the two runs are the 25 in data-model §A/§C, plus the new
  NFR-019.
- Known positive first: a copy with one row mutated moves the count by exactly one.

## 2. Witness tests (SC-002)

Each flipped row's Tests cell names its binaries. Run them on a fresh build of this branch:

- `ctest --preset <preset> -R '<entry>' --no-tests=error --output-on-failure` for each ctest entry in
  research R-3 and R-11, on the preset CI runs it on. The mallocnesia gates (NFR-005) run on
  `linux-gcc-release` and `linux-clang-release` (the `if:` of the #448 steps); run both, or record
  which one was skipped and why. The rest run on `linux-clang-debug`.
- Entries: `session_inbound_frame_dispositions`, `session_tc_reject` (has no labels, so `-R` only),
  `session_pure_tests` (incl. the TC-017 `FsmMatrixWitness` and `EnumValidationLogonTest` cells),
  `interop_parity_us3_tests`, `session_inbound_poss_resend`, `session_reset_on_lifecycle`,
  `session_interpret_logon_encrypt_method`, `session_resend_reply_possdup`,
  `session_538_store_lookup_after_reset`, `capi_inbound_frame_dispositions`, `dictionary_pure_tests`,
  the codegen and transport entries.
- CI-evidence rows (NFR-001/003/005/013) cite Tier 1 run `39594667883` and Tier 2 run `39594667975`
  (`push:main` at `9b176090`, both success). Re-read the job and step names, and the `if:` lines, on
  this branch. Re-read branch protection:
  `gh api repos/CatalinSerafimescu/fixpp/branches/main/protection/required_status_checks --jq .contexts`.
- Pass: every named test reports PASSED in the binary's own output, read from the output and not
  inferred from ctest's exit code. A gtest filter for the parameterised store cells needs the
  `Stores/` prefix.
- Fill data-model §H's last column from the assertions the cells ran (FR-015b).
- Snapshot `gh issue view 614 --repo CatalinSerafimescu/fixpp --json state,labels,createdAt` into the
  verify record.

## 3. Licence census (SC-004, SC-005)

- `git ls-files src include | grep -E '\.(c|cc|cpp|h|hpp|ipp|inl)$' | xargs grep -L 'SPDX-License-Identifier: AGPL-3.0-or-later'`
  must print nothing. ⚠️ Run it with the system `grep` (`/usr/bin/grep`): `xargs` cannot run a shell
  function or the `command` builtin, and a pipeline that fails to run prints nothing too. Prove it
  first on `origin/main`, where it must list 53 files (the 37 unmarked plus the 16 Apache-2.0).
- `… | xargs grep -l 'SPDX-License-Identifier: Apache'` must print nothing (on `origin/main`: 16).
- `git diff origin/main -- src include | grep -E '^[+-][^+-]' | sort | uniq -c` must show only the
  two AGPL comment forms added and `// SPDX-License-Identifier: Apache-2.0` removed.
- `git diff --stat origin/main -- ':!spec/' ':!.specify/' ':!brain/' ':!specs/' ':!CHANGELOG.md'`
  lists only the SC-005 files.
- `tools/check_capi_freeze.sh` must print `PASS`.
- `grep -n '^    license' conanfile.py` must read `license = "AGPL-3.0-or-later"`.
- `grep -n 'CPACK_RPM_PACKAGE_LICENSE' cmake/FixppPackaging.cmake` must read `"AGPL-3.0-or-later"`,
  and the packaging lane's RPM must carry `License: AGPL-3.0-or-later` (`rpm -qip`).
- `git log --format=%B origin/main..HEAD -- conanfile.py` shows the comment commit's `Refs #607`, and
  no closing keyword next to #607, #614 or the FR-018 issue anywhere in the branch's messages.

## 4. Cross-surface sweep (SC-003)

- Run research R-12's three `git grep`s. Known positives first, on `origin/main`: (a) must report
  `README.md:15`, (b) must report `brain/components/dictionary.md:75`.
- On the branch, every hit must fall in a "keep", "header", "other concept" or "pattern FP" class of
  R-12, or be an edited line. Record the count per class.
- `grep -niE "grpc|iceoryx|service wrapper" .specify/constitution.md`: every hit is the v4.0 Sync
  Impact Report, a clause that says post-1.0, an earlier Sync Impact Report, or §IV.2 / §XIII.3,
  neither of which states v1.0 scope.

## 5. Line citations and comment claims

- `python3 tools/check_line_citations.py --shift-audit HEAD~1..HEAD` on a scratch commit of the docs
  edits alone, before the PR (the new Sync Impact Report shifts every constitution line; R-6).
- `python3 tools/check_line_citations.py --range origin/main..HEAD` passes.
- `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD` passes. Frozen
  `specs/<id>/` citers are reported, not charged.
- `.claude/scripts/check-comment-claims.py --root . --base origin/main` (parent path) passes.
- `.claude/scripts/check_bl_delta.py` (parent path) sees the L-094-1 delta.
- The parent's `tools/brain-external-sweep.sh` passes; report its execution count.
