# Quickstart: validating 099

Root: `~/Work/Programming/fixpp-parallel`; build trees on `/mnt/wsl/fixppbuild` (its `build/` symlinks).

## Local

1. Build and run the session, dictionary and wire witnesses (`contracts/witnesses.md` W-1 to W-11, W-15 to W-17, W-19 to W-21) in `linux-clang-debug`, then
   the whole binary unfiltered. Update the tests research R-17 names first, to the literals derived there. A run that disagrees stops for a decision.
2. C ABI and Python: `capi;099` and `bindings/python/tests/test_099_*`. TOML: `tests/config/test_099_*`.
3. Mutants M-1 to M-48, per the mutant table, from a separate worktree on `/mnt/wsl/fixppbuild`, never this tree: each named witness turns RED.
4. Sanitizers: ASan, UBSan, TSan over `session` and `capi`. TSan includes W-15's multi-thread open arm, which reads one validator from several strands.
5. Paired bench, W-22, non-FIXT shape, B41's method (`research/probes/b41_563_564/ph7f_method.md`): `on_inbound_frame_bench`, `validator_bench`, with the new FIXT case reported for information.
6. Live: W-18, the row the owner ruled, through the parent's `phase-9-harness`.
7. Records: W-23, each recipe run on the pre-edit tree first.
8. MSVC: take `.sandbox-lock` first, then build the feature's own targets, the C and Python ones included, with `-k 0` before the first labelled push.

## Expected end state

- `spec/feature-catalogue.md`: S-026 `done` with named witnesses, its row stating what the live cell proved.
- `L-041-2`, `L-033-1`, `L-097-1` in the closed file, each under its condition in research R-16. `coverage-index.md` §5.3.4 re-pointed.
- New L-rows: the overlay trailer tag and the overlay header pair (R-6), `toApp` and replay (R-7), the Message Type Default level (FR-024), and the non-validating FIXT refusal unless the owner takes the breaking form (R-2).
- New B-rows: the QuickFIX C++ departure (ruling 1), the 1137 ordering (FR-010), the repeated MsgType and past-header dispositions and their non-validating form (FR-025, FR-026, R-5), a partial session dictionary refused (R-17).
- `brain/components/session.md`, `dictionary.md`, `config.md`, `python-api.md`, `c-api.md` updated. The `Updated (099-…)` notes of research R-11 applied.
- A validating FIXT.1.1 session, from each of C++, C, Python and TOML, reaches Active and routes `1128`.
