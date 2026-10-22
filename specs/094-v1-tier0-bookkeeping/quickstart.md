# Quickstart: re-verifying 094

Run from the library root on the feature branch. Each step names the instrument and the result that
counts as a pass. The commands that matter are reproduced in the verify record. The census script
lives in the orchestrator's scratchpad and is reproduced there in full.

## 1. Catalogue census (SC-001)

- Command: `python3 catalogue_census.py spec/feature-catalogue.md`.
- Base `origin/main` reads: OFFICIAL open 94, done 152, dropped 1.
- This branch must read: OFFICIAL open **69**, done **170**, dropped 1, postv1 **8**.
- The only rows whose Status differs between the two runs are the 25 in data-model §A/§C, plus the new
  NFR-019.
- Known positive first: a copy with one row mutated moves the count by exactly one.

## 2. Witness tests (SC-002)

Each flipped row's Tests cell names its binaries. Run them on a fresh build of this branch:

- `ctest --preset <preset> -R '<entry>' --no-tests=error --output-on-failure` for each ctest entry in
  research R-3, on the preset CI runs it on. The mallocnesia gates (NFR-005) run on
  `linux-clang-release` (#478); the rest run on `linux-clang-debug`.
- CI-evidence rows (NFR-001/003/005/013) cite Tier 1 run `39594667883` and Tier 2 run `39594667975`
  (`push:main` at `9b176090`, both success). Re-read the job and step names on this branch. For example: `session_inbound_frame_dispositions`, `session_tc_reject` (has no labels,
  so `-R` only), `session_pure_tests`, `interop_parity_us3_tests`, `session_inbound_poss_resend`,
  `session_reset_on_lifecycle`, `session_interpret_logon_encrypt_method`,
  `session_resend_reply_possdup`, `session_538_store_lookup_after_reset`,
  `capi_inbound_frame_dispositions`, `dictionary_pure_tests`, the codegen and transport entries.
- Pass: every named test reports PASSED in the binary's own output, read from the output and not
  inferred from ctest's exit code. A gtest filter for the parameterised store cells needs the
  `Stores/` prefix.

## 3. Licence census (SC-004, SC-005)

- `git ls-files src include | grep -E '\.(c|cc|cpp|h|hpp|ipp|inl)$' | xargs grep -L 'SPDX-License-Identifier: AGPL-3.0-or-later'`
  must print nothing.
- `… | xargs grep -l 'SPDX-License-Identifier: Apache'` must print nothing.
- `git diff origin/main -- src include | grep -E '^[+-][^+-]' | sort | uniq -c` must show only the
  two AGPL comment forms added and `// SPDX-License-Identifier: Apache-2.0` removed.
- `tools/check_capi_freeze.sh` must print `PASS`.
- `grep -n '^    license' conanfile.py` must read `license = "AGPL-3.0-or-later"`.

## 4. Constitution sweep (SC-003)

- `grep -niE "grpc|iceoryx|service wrapper" .specify/constitution.md`: every hit is either the v3.1 Sync
  Impact Report, a clause that says post-1.0, or §IV.2 / §XIII.3. Neither of those states v1.0 scope.

## 5. Line citations and comment claims

- `python3 tools/check_line_citations.py --range origin/main..HEAD` passes.
- `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD` passes. Frozen
  `specs/<id>/` citers are reported, not charged.
- `.claude/scripts/check-comment-claims.py --root . --base origin/main` (parent path) passes.
- The parent's `tools/brain-external-sweep.sh` passes; report its execution count.
