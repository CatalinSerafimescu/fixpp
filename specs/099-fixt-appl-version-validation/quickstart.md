# Quickstart: validating 099

Root: `~/Work/Programming/fixpp-parallel`; build trees on `/mnt/wsl/fixppbuild` (its `build/` symlinks).

## Local

1. Build and run the session, dictionary and wire witnesses (`contracts/witnesses.md` W-1 to W-11, W-15 to W-17, W-19 to W-21, W-25) in `linux-clang-debug`, then
   the whole binary unfiltered. Update the tests research R-17 names first, to the literals derived there. A run that disagrees stops for a decision.
2. C ABI and Python: `capi;099` and `bindings/python/tests/test_099_*`. TOML: `tests/config/test_099_*`.
3. Mutants, per the mutant table, from one reused separate worktree on `/mnt/wsl/fixppbuild`, never this tree, batched by binary: each named cell turns RED on that mutant alone.
4. Sanitizers: ASan, UBSan, TSan over `session` and `capi`. TSan includes W-15's multi-thread open arm, which reads one validator from several strands. C-ABI gates, each with its log: `bash tools/check_capi_freeze.sh` **RED after the header edits**, then green after the manifest refresh; `bash tools/check_capi_reentrancy.sh`; `bash tools/check_capi_occupancy.sh`.
4a. Fuzz, W-24: `cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON`, build, `ctest --preset linux-clang-asan -L fuzz --no-tests=error --output-on-failure`, then, from this root, `build/linux-clang-asan/bin/<target> -max_total_time=600`, ten minutes per target, with an empty scratch directory first on the command line so `git status --porcelain` stays empty, and `tests/fuzz/corpus/<target>/` second, whose `<target>` drops the `fuzz_` prefix. Not through `/speckit-verify`, whose SKILL.md `cd`s to the main checkout: wherever it runs for this feature, `$T` is substituted (tasks.md's Execution rules).
5. Paired bench, W-22, non-FIXT shape, B41's method (`research/probes/b41_563_564/ph7f_method.md`): `on_inbound_frame_bench`, `validator_bench`, with the new FIXT case reported for information.
6. Live: W-18, through a copy of the parent's `phase-9-harness`, in tasks.md's `**Harness layout**`.
7. Records: W-23, each recipe run on the pre-edit tree first.
8. MSVC: take `.sandbox-lock` first, then build the feature's own targets, the C and Python ones included, with `-k 0` before the first labelled push, and run `session_pure_tests` once.

## Expected end state

- `spec/feature-catalogue.md`: S-026 `done` with named witnesses, its row stating what the live cell proved.
- `L-041-2`, `L-033-1`, `L-097-1` in the closed file, each under its condition in research R-16. `coverage-index.md`'s S-026 wording corrected.
- New L-rows: the overlay trailer tag and an overlay header group not composed (R-6, E-3), `toApp` and replay (R-7), what Message Type Default leaves out and its two limits (FR-024), and the non-validating FIXT refusal, with `open()`'s reservation failing on an Engine route, which takes the same branch (R-2, FR-028).
- New B-rows: the QuickFIX C++ departure (ruling 1); the 1137 ordering, with its double-fault and consume limits (FR-010); an application or mismatched frame in an establishment arm (FR-029); the repeated MsgType and past-header dispositions, and their non-validating form, including that a later `35=A` with Logon fields establishes (FR-025, FR-026, R-5, R-8); LogoutSent taking a last `35=5` as the reply; `35=A` once Active (FR-010); a malformed route-only pair, and shape A's `group(627)`, on a non-validating session (FR-011); 370 (R-5); a partial session dictionary refused (R-17); E-5's splits between the accessor and `dict::reify`; a non-FIXT validating session's MsgType split; S's header pair winning a collision (R-20); a header tag past the body (R-6); a malformed NoHops's code against the engines' (E-3); a standard header field on shape A falling to the routed table (R-6); Message Type Default beyond QuickFIX; a 372 L's dictionary closes (E-10); `sizeof(Session)` and `sizeof(MessageView)` growing, a C++ layout change in installed headers, the shape of B-544-3; `FIXT11.xml`'s 1128 enum on an admin frame (`contracts/surface.md`, the spec's Edge Cases); 1406 and 1131 ignored (R-19); `Engine::start()` blocking, the tables it holds and Python's GIL across the walks (R-3, R-11); two 1128s in the header read first-wins (R-5); `dict::reify` ignoring its caller's profile when the byte is set (E-5); a peer's 1128 selecting any registered dictionary (`contracts/surface.md`); `R 5/384 A`'s two endings, and the consume split on 1130 and 372 (E-10, FR-031); a dictionary without MsgTypeGrp refusing on 384 (E-10).
- The BREAKING delta for `fixpp_msg_version`, with its migration line, as `[const §X.7]` requires.
- Issues to file: `interpret_logon` reading the last 35 on a validation-off session; the non-FIXT MsgType split; the `[dictionary]` sub-key lead of R-12.
- `brain/components/session.md`, `dictionary.md`, `config.md`, `python-api.md`, `c-api.md` updated. The `Updated (099-…)` notes of research R-11 applied. `dictionaries/README.md` states FR-032's exception.
- A validating FIXT.1.1 session, from each of C++, C, Python and TOML, reaches Active and routes `1128`.
