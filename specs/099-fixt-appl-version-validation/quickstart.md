# Quickstart: validating 099

Root: `~/Work/Programming/fixpp-parallel`; build trees on `/mnt/wsl/fixppbuild` (its `build/` symlinks).

## Local

1. Build and run the session and dictionary witnesses (`contracts/witnesses.md` W-1 to W-11, W-15 to W-17, W-19) in `linux-clang-debug`, then
   the whole binary unfiltered.
2. C ABI and Python: `capi;099` and `bindings/python/tests/test_099_*`. TOML: `tests/config/test_099_*`.
3. Mutants M-1..M-16 from a separate worktree on `/mnt/wsl/fixppbuild`, never this tree: each named witness turns RED.
4. Sanitizers: ASan, UBSan, TSan over `session` and `capi`. TSan includes W-15's concurrent `open()`.
5. Paired bench, non-FIXT shape, B41's method (`research/probes/b41_563_564/ph7f_method.md`): `on_inbound_frame_bench`, `validator_bench`.
6. MSVC: take `.sandbox-lock` first, then build the feature's own targets with `-k 0` before the first labelled push.

## Expected end state

- `spec/feature-catalogue.md`: S-026 `done` with named witnesses.
- `L-041-2`, `L-033-1`, `L-097-1` in the closed file; new L-rows for R-6, R-7 and the Message Type Default level.
- A validating FIXT.1.1 session, from each of C++, C, Python and TOML, reaches Active and routes `1128`.
