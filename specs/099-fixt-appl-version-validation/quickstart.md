# Quickstart: validating 099

Root: `~/Work/Programming/fixpp-parallel`; build trees on `/mnt/wsl/fixppbuild` (its `build/` symlinks).

## Local

1. Build and run the session, dictionary and wire witnesses (`contracts/witnesses.md` W-1 to W-11, W-15 to W-17, W-19 to W-21) in `linux-clang-debug`, then
   the whole binary unfiltered. Update the tests research R-17 names first, to the literals derived there. A run that disagrees stops for a decision.
2. C ABI and Python: `capi;099` and `bindings/python/tests/test_099_*`. TOML: `tests/config/test_099_*`.
3. Mutants, per the mutant table, from one reused separate worktree on `/mnt/wsl/fixppbuild`, never this tree, batched by binary: each named cell turns RED on that mutant alone.
4. Sanitizers: ASan, UBSan, TSan over `session` and `capi`. TSan includes W-15's multi-thread open arm, which reads one validator from several strands. C-ABI gates, each with its log: `bash tools/check_capi_freeze.sh` **RED after the header edits**, then green after the manifest refresh; `bash tools/check_capi_reentrancy.sh`; `bash tools/check_capi_occupancy.sh`.
4a. Fuzz, W-24: `cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON`, build, `ctest --preset linux-clang-asan -L fuzz --no-tests=error --output-on-failure`, then `/speckit-verify --fuzz-duration=600`, ten minutes per target, with an empty scratch directory first on the command line so `git status --porcelain` stays empty.
5. Paired bench, W-22, non-FIXT shape, B41's method (`research/probes/b41_563_564/ph7f_method.md`): `on_inbound_frame_bench`, `validator_bench`, with the new FIXT case reported for information.
6. Live: W-18, the row the owner ruled, through the parent's `phase-9-harness`.
7. Records: W-23, each recipe run on the pre-edit tree first.
8. MSVC: take `.sandbox-lock` first, then build the feature's own targets, the C and Python ones included, with `-k 0` before the first labelled push.

## Expected end state

- `spec/feature-catalogue.md`: S-026 `done` with named witnesses, its row stating what the live cell proved.
- `L-041-2`, `L-033-1`, `L-097-1` in the closed file, each under its condition in research R-16. `coverage-index.md` §5.3.4 re-pointed.
- New L-rows: the overlay trailer tag and the overlay header pair (R-6, each naming what it narrows), `toApp` and replay (R-7), the Message Type Default level (FR-024), and the non-validating FIXT refusal unless the owner takes the breaking form (R-2).
- New B-rows: the QuickFIX C++ departure (ruling 1); the 1137 ordering, with its double-fault and consume limits (FR-010); an application or mismatched frame in an establishment arm (FR-029); the repeated MsgType and past-header dispositions, and their non-validating form, including that a later `35=A` with Logon fields establishes (FR-025, FR-026, R-5, R-8); LogoutSent taking a last `35=5` as the reply; `35=A` once Active (FR-010); a malformed route-only pair on a non-validating session (FR-011); 370 (R-5); a partial session dictionary refused (R-17); E-5's three splits between the accessor and `dict::reify`; a non-FIXT validating session's MsgType split.
- Issues to file: `interpret_logon` reading the last 35 on a validation-off session; the non-FIXT MsgType split; the `[dictionary]` sub-key lead of R-12.
- `brain/components/session.md`, `dictionary.md`, `config.md`, `python-api.md`, `c-api.md` updated. The `Updated (099-…)` notes of research R-11 applied.
- A validating FIXT.1.1 session, from each of C++, C, Python and TOML, reaches Active and routes `1128`.
