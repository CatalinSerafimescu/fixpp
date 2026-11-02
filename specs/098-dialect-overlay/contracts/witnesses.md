# Contract: 098 witnesses and their RED arms

Every witness names its file, what it asserts, and how it is shown RED **for its stated reason**. Every
CTest entry a 098 witness runs in carries label `098`. A bucket that already has labels gains it with
`set_property(TEST … APPEND PROPERTY LABELS 098)` after that entry's last LABELS setter, so its existing
labels survive. The standalone census also carries `dictionary` and `census`; the standalone live C-ABI
arm also carries `capi`. One exemption: W-11's replay entry, `fuzz_replay_dict_overlay`, is registered
by `tests/fuzz/CMakeLists.txt`'s corpus glob with label `fuzz` and exists only in a
`-DFIXPP_BUILD_FUZZ=ON` tree; it is selected by `ctest -L fuzz` in the ASan tree, not by `-L 098`.

**Test-binary disposition (`[const §VII.8]`).**

| Witness | Bucket | Reason |
|---|---|---|
| W-0 | `dictionary_pure_tests` | Self-test of test-local declared data, with no isolation hazard, like `T020_DeferredSetExactCoverage`. |
| W-1 | `dictionary_pure_tests` | Pure, single-threaded dictionary read/parse/compare tests. |
| W-2 | `dictionary_pure_tests` | Pure, single-threaded vendored-base comparisons. Its census is the comparator's key source for two dictionaries built in-process, not a completeness gate over a population: no W-2 row asserts that a closed list of shipped files or tags equals a fixed set, which is what makes W-7 isolation-sensitive. |
| W-3 | `dictionary_pure_tests` | Pure, single-threaded refusal tests. Its budget recipe row reads every vendored base, but it asserts that one constant equals the recipe's result over them, not that a closed list of shipped files or tags equals a fixed set, which is what makes W-7 isolation-sensitive (the `dictionary_dict_enum_census_test` block's comment: "an exact-set completeness gate is isolation-sensitive"). |
| W-4 | `dictionary_pure_tests` | Pure, single-threaded dispatch tests. |
| W-5 | `session_pure_tests` | Pure session test; this bucket supplies `src` and `FIXPP_DICT_DATA_DIR`. |
| W-6 | `session_pure_tests` | Pure session send tests with no isolation hazard. |
| W-7 | both halves: standalone `dictionary_098_header_census_test`, labels `dictionary;098;census`, with `src/session` on its include directories | Both halves are exact-set censuses over the shipped dictionaries, standalone as the module's other census gates are (the `dictionary_dict_enum_census_test` block's comment: "an exact-set completeness gate is isolation-sensitive"). The session half reads the production fixed set from `src/session/send_header_tags.hpp`, a self-contained header, so the executable includes it without linking `fixpp_session`. |
| W-8 | `config_045_tests` (the new `test_098_dialect_overlay.cpp` and the T020 cells of `test_load_deferred_surface.cpp`); `config_044_tests` (the rewritten T018 cell of `test_load_negative_battery.cpp` and T028 cell of `test_load_selectors.cpp`) | Pure configuration parsing and resolution tests. AS-1's cell runs `Session::open()` on a local `asio::io_context` driven by `run()` on the test's own thread: no worker thread, no global allocation hook, so it stays in the bucket. |
| W-9 | pure rows: `capi_pure_tests`; status-mapping rows: `dictionary_pure_tests`; live arm: standalone `capi_098_dict_overlay_live_test`, labels `capi;098`, with an explicit `TIMEOUT`; splice allocation-failure rows: standalone `dictionary_overlay_pugi_oom_test`, labels `098;dictionary` | The pure rows start no engine, which is the bucket's admission rule (no `fixpp_engine_start()`). The status-mapping rows are pure, single-threaded calls of a dictionary-library function. The live arm starts two C-ABI engines with their worker threads over loopback, so `[const §VII.8]` keeps it standalone, as `capi_dict066_group_membership_red_test` and `capi_inbound_frame_dispositions_test` are. The splice rows install pugixml's process-global allocation hook from the executable's own `main`, before any pugixml object exists, so `[const §VII.8]` keeps them standalone, as `dictionary_table_view_pair_oom_test` is. |
| W-10 | in-tree Python suite (`bindings/python/tests/`); the import-surface row in the wheel suite | Not a gtest executable. The new test has no wheel twin: its overlay fixtures are not in the wheel. |
| W-11 | `fuzz_dict_overlay` and its replay | Fuzz target outside §VII.8, built only under `FIXPP_BUILD_FUZZ`. |
| W-12 | verify measurement | Not a test executable. |
| W-13 | record-check scripts | Not a gtest executable. |

**RED arms.** A witness that calls a new symbol does not compile on the unfixed tree, and a compile
failure is RED for the wrong reason. Such a witness is shown RED by a **named mutant** applied to the
fixed tree, rebuilt, and run unfiltered (`--gtest_filter` narrows the report, never the binary). A row
whose rebuild failed proves nothing and is gated on build rc 0. Where a witness compiles on the unfixed
tree **and its assertion fails there**, its RED is the unfixed tree itself (marked **U**). An arm that
compiles on the unfixed tree and passes there is not a RED arm: it is a **negative control** (it must
hold before and after 098) or a **characterization pin** (it records today's behaviour), and it is never
in the Phase 1 list. A row that compiles only on the fixed tree and whose refusal is the base loader's
own, which no 098 mutant removes, is also a characterization pin, not a RED arm (W-3's unknown-reference,
unknown-field-type and group-delimiter-collision rows). The verify record keeps a table: witness | mutant or U | build rc | test rc | the
failing assertion's message, plus every other test the unfiltered run reports failing (collateral is
listed, not assumed).

**Where mutants run (`[const §XVI.6]`).** 098 is built in `~/Work/Programming/fixpp-parallel`, so
mutants run in a **third** worktree, created detached at the implementation head and removed at
close-out; never in the PR's tree, never in the main checkout:

```
H=$(git -C ~/Work/Programming/fixpp-parallel rev-parse HEAD)
df -h /mnt/wsl/fixppbuild                                   # headroom for a Debug tree first
git -C ~/Work/Programming/fixpp-parallel worktree add --detach /mnt/wsl/fixppbuild/fixpp-098-mutants "$H"
export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache
cd /mnt/wsl/fixppbuild/fixpp-098-mutants
conan install . -pr conan/profiles/linux-clang-debug --build=missing -of build/linux-clang-debug
cmake --preset linux-clang-debug -DFIXPP_BUILD_PYTHON=ON     # bindings for W-10's M-11 / M-19 runs
J=${J:-8}                                                   # never empty: a bare -j is unbounded
cmake --build --preset linux-clang-debug -j"$J"
conan install . -pr conan/profiles/linux-clang-asan --build=missing -of build/linux-clang-asan
cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON        # M-7's overflow report and W-11's rows
cmake --build --preset linux-clang-asan --target dictionary_pure_tests -j"$J"
# per mutant: apply, rebuild (rc must be 0), run the named binary unfiltered under `timeout`, record,
# then `git checkout -- . && git clean -fd` (no -x: build trees are ignored) and a clean `git status --porcelain`
```

**Bounded runs.** Each unfiltered mutant run is wrapped in `timeout <T>`, where T is ten times that
binary's unmutated baseline run time and at least 60 s. T is recorded with the row. Exit code 124 is
recorded as "loops (timed out)". For a row whose stated RED allows a loop, an overflow or a crash (M-7,
M-13), a timeout or a sanitizer report counts as RED, but only together with the assertion the row names
failing or not being reached. A hang in an entry with no `TIMEOUT` is therefore always a recorded,
bounded outcome.

`J` is `8` for every 098 build (owner, 2026-11-01: "yes, you can run skip asking me before each build, and you can use -j8 for all of them"; tasks T002). `J` is set before the first build (`J=${J:-8}`
above), since an empty `J` gives a bare `-j`, which is unbounded.

## Arm table (plan Phase 1 and quickstart §1 are derived from it)

One row per arm. "Unfixed outcome" is what the arm's own assertion does on the unfixed tree. Plan
Phase 1 and quickstart §1 list exactly the rows whose unfixed outcome is **fails**; no other row is run
as a RED arm before production code.

| Arm | Compiles on unfixed tree? | Assertion | Unfixed outcome | RED source, or role |
|---|---|---|---|---|
| W-0 seeded rows | no (test-only, but written in plan Phase 2 against Phase 2's code; the header-store row joins in Phase 5) | reported set equals the seed's declared expected set | — | the seeded rows themselves; M-0 |
| W-0 identical pair | no (as above) | zero differences | — | control |
| W-0 invariant seeds (each invariant check has its own seed; the `header_bits_` one joins in Phase 5) | no (as above) | each invariant check, fed hand-built store values that break its invariant only, reports that invariant | — | each seed itself; outside the expected-set union rule |
| W-1 rows (including the `tolerant` policy rows) | no | zero differences vs the complete reference; no counter-origin refusal; step counter = prediction | — | qf: M-1, M-2, M-3; orch: M-1o, M-2o, M-3; both: M-17, M-24 at each loader (the `tolerant` rows refuse); each row's in-test seeded mismatch |
| W-2 rows, side against side and against the census | no | zero differences vs the patched base; no counter-origin refusal; step counter = prediction | — | M-3; M-17; a zero- or two-match hunk fails the cell; each row's in-test seeded mismatch; W-0's census row |
| W-2 empty-overlay rows (every base) | no | zero differences vs `load(base)`; no refusal; step counter = prediction | — | M-17; each row's seeded mismatch: `load(base)` vs the in-test base copy with one enum value removed (recipe below) gives at least one difference naming its store (a zero- or two-match anchor, or a copy that does not load, fails the cell) |
| W-2 wrap probe (FIX50SP2) | yes | records `component_fields` of the first component past the wrap point against its raw members | either | probe, recorded in the verify record (research R-10); not a RED arm |
| W-3 rows | no | the named exception, its message naming the key, cap, table or counter origin as applicable | — | M-4, M-5, M-6, M-7, M-12 (by the side-table fit on the doubling rows, by the counter on the empty-chain rows), M-13, M-14, M-16, M-17, M-18, M-22, M-23 (per row and format, below) |
| W-3 merged-in-place reference-cap row and its at-cap control | no | cap + 1: `overlay_limit_error` naming the reference cap; the control at the cap loads | — | M-14, M-25 |
| W-3 unknown-reference, unknown-field-type and group-delimiter-collision rows | no | the base loader's own exception type for its format | — | characterization pin: the refusal is the loader's own, after the splice; no 098 mutant removes it |
| W-4 rows (including the `tolerant` policy row) | no | dispatch matches the direct loader | — | M-8; M-24 at `load_any_with_overlay` (the `tolerant` row refuses on the `load_any` side) |
| W-5 overlay session | no (`load_any_with_overlay`) | agrees with the edited session on every frame; each accepted frame's scenario read holds | — | M-8 |
| W-5 edited session | no (it shares `test_098_dialect_overlay_inbound.cpp`, which calls `load_any_with_overlay`) | accepts or refuses each frame as the edited dictionary dictates; each accepted frame's scenario read holds | — | characterization pin of a complete edited file, observed after the fix only |
| W-5 base session | yes (its own file, `test_098_dialect_overlay_inbound_base.cpp`) | the outcome W-5 states for each frame | passes | negative control: run at the baseline (tasks T004a) and in every later suite run |
| W-6 edited dictionary, scalar 5020 | yes | 5020 before the first body field | **fails** (5020 sent in the body) | **U**; M-9 after the fix |
| W-6 edited dictionary, custom counted pair in the header | yes | Length and Data both before the first body field, adjacent | **fails** (pair sent in the body) | **U**; M-9 after the fix |
| W-6 replay of the 5020 message | yes | 5020, 43 and 122 all inside the header | **fails** (5020 in the body) | **U** for the 5020 half; the 43/122 half is a pin |
| W-6 overlay dictionary | no | as the edited-dictionary arm | — | M-9 |
| W-6 existing header-order witnesses | yes | unchanged bytes | passes | negative control |
| W-7 `is_header_tag` vs the raw walk | no | equal per vendored dictionary | — | M-9 |
| W-7 union with the production fixed set (in the census executable) | no (`send_header_tags.hpp` is new) | union equals the fixed set | — | characterization pin of research R-7; its instrument is proven by the seeded FIX44 copy, which MUST make it fail |
| W-8 AS-1 (no overlay: dictionary filled, `open()` succeeds) | yes | `config.dictionary` set, session opens | **fails** (null; `invalid_session_config`) | **U** |
| W-8 AS-2, shared pointer, AS-5 | yes | merged dictionary per session as stated | **fails** (the key is refused as deferred) | **U**; M-10 for the stated reason |
| W-8 AS-3, AS-4, value checks, two origins, no `[dictionary]` | yes | the stated reason class at the stated key path | **fails** (`recognized_not_yet_supported_step2` instead) | **U** |
| W-8 engine dictionaries (on the AS-2 bundle, exactly one entry, the no-overlay session's pointer) | yes | `engine.dictionaries` as stated | **fails** (the AS-2 file reports the deferred key, so no bundle is returned) | **U** |
| W-8 location (each diagnostic's `location`, asserted together with its stated reason class) | yes | the value's position in its raw table, with the stated reason class | **fails** (the reason class is `recognized_not_yet_supported_step2`; the location alone would pass, because the deferred-key diagnostic is located at the same value) | **U** |
| W-8 failure message (a load failure's `message` carries the loader's `what()`) | yes | the loader's text | **fails** (the message is the deferred-key text "dialect_overlay is recognized but not yet supported") | **U** |
| W-8 rewritten cells (`T018_SessionDialectOverlayFileMissing`, `T028_DialectOverlayFileMissing`, `T020_RootDialectOverlayRefused`) | yes | the new outcomes (W-8 below) | **fails** (the old outcomes) | **U** |
| W-8 `T020_DeferredSetExactCoverage` over the remaining keys | yes | every remaining key deferred | passes (the remaining keys are deferred on the unfixed tree too) | negative control: it guards the remaining keys before and after |
| W-8 deferred keys at session scopes (a remaining deferred key in `[default]` and in `[[session]]`) | yes | `recognized_not_yet_supported_step2` at `default.<key>` and `session[0].<key>` | passes | negative control: the scope-aware change touches `dialect_overlay` only |
| W-8 `T020_MissingRequired_Dictionary` | yes | unchanged | passes | negative control (N-3) |
| W-9 cap rows (the thunk's entry-cap and DOCTYPE rows; the mapping function fed `overlay_limit_error`) | no | 201 (and NULL from the thunk) | — | M-11 |
| W-9 bad-alloc rows (the mapping function fed `xml_oom_error` and a bare `std::bad_alloc`) | no | 202 | — | M-15 |
| W-9 code-200 rows (the thunk's conflict row; the mapping function fed `xml_parse_error`, `overlay_conflict_error`, a `std::runtime_error` and a non-`std::exception`) | no | 200 (and NULL from the thunk) | — | M-20 |
| W-9 NULL-argument rows | no | `FIXPP_ERR_NULL_HANDLE` | — | control |
| W-9 status-mapping rows (`dictionary_pure_tests`) | no (the shared status-mapping function is new) | `status_out_of_memory` → `xml_oom_error`; another failed status → the format's parse error | — | M-21 |
| W-9 pure handle row (the handle is accepted by `fixpp_session_config_set_dictionary` and `fixpp_session_open` on an unstarted engine) | no | both calls return `FIXPP_ERR_OK` | — | control: a base-only handle passes it too; the live arm is the discriminating row |
| W-9 version pin (`version_test.cpp`) | yes | the version is 1.13 | **fails** (MINOR is 12) | **U** |
| W-9 symbol golden (`fixpp_capi_symbols.txt` gains the symbol) | yes | the library's exported set equals the golden | **fails**: exactly one `MISSING exported symbol` line, naming `fixpp_dict_load_from_xml_with_overlay`, and no `UNEXPECTED exported symbol` line (`abi_symbol_golden_test.cpp` prints one line per difference); any other difference is RED for the wrong reason (stop rule) | **U** |
| W-9 live arm (raw `fixpp_session_send` payload) | no | the counted 5011 value read intact through `fixpp_msg_get_bytes` | — | **M-19** |
| W-9 splice allocation failure, persistent and transient sweeps (`dictionary_overlay_pugi_oom_test`) | no (the merge classes are new) | over every remainder of the base's page, and in the transient sweep for every failing page request of the splice: a splice that met a failed allocation throws `std::bad_alloc`; a splice that returns serializes byte-equal to the splice without failure; in the persistent sweep, both outcomes occur | — | MO-1 to MO-8 |
| W-9 splice positive control | no | a clean overlay load calls the hook; with every page failing, an overlay load throws exactly `xml_oom_error` | — | control: the hook reaches the library's pugixml |
| W-9 splice composition (`load_with_overlay_from_string`, the n-th allocation failing) | no | exactly `xml_oom_error` for every n over a clean call; one past it, equal to the edited reference | — | control: a parse failure satisfies it too |
| W-10 import surface | yes | the exact set includes `dict_load_from_xml_with_overlay` | **fails**: the exact-set assertion, with that name as its only difference | **U** |
| W-10 inbound read | no (the function is absent) | `msg_get_string(5011)` equals the full counted value | — | **M-19** |
| W-10 cap row | no | `ValidatorError` with `.code == 201` | — | M-11 |
| W-11 fuzz | no | only `dict::` errors escape (Orchestra by derived type); abort on a counter-origin refusal; counter = prediction on every load | — | M-7 (cycle seeds), M-12 (empty-chain seeds) and M-17 against the harness |

## Mutant index (closed)

This is every mutant the bundle names, once each. A mutant named elsewhere and missing here is a defect
in this table. Check it by collecting the IDs named everywhere except this table, and diffing them with
the table's ID column. The diff must print nothing. Removing one row from a scratch copy of this file
makes it print that ID, and that seeded run is how the check is proven able to report.

```
B=specs/098-dialect-overlay W=$B/contracts/witnesses.md N=$(mktemp) I=$(mktemp)
{ cat $B/tasks.md $B/research.md $B/plan.md $B/quickstart.md
  awk '/^## Mutant index/{s=1;next} /^## /{s=0} !s' $W; } | grep -o -E '\bM-[0-9]+o?\b' | sort -u -V > $N
awk '/^## Mutant index/{s=1;next} /^## /{s=0} s && /^\| M-/{print $2}' $W | sort -u -V > $I
diff $N $I
```

"Per format" means one row per format where the witness rows exist.

| ID | Change | Format / tree | Witness rows (binary) | Stated RED | How the mutated site reaches the assertion |
|---|---|---|---|---|---|
| M-0 | the comparison of the context `required_members` entry removed, the entry still listed | test-only / Debug | W-0 context `required_members` seed (`dictionary_pure_tests`) | that row's reported set misses its expected entry | the comparator is the code under test |
| M-1 | XML splice prepends a new group member | XML / Debug | W-1 qf row | a difference naming the store | splice → spliced document → `build_handle_from_doc` → comparator |
| M-1o | Orchestra splice prepends a new group member | Orchestra / Debug | W-1 orch row | as M-1 | as M-1 |
| M-2 | XML splice skips enum-value additions | XML / Debug | W-1 qf row | a difference naming the enum store | as M-1 |
| M-2o | Orchestra splice skips `fixr:code` additions | Orchestra / Debug | W-1 orch row | as M-2 | as M-1 |
| M-3 | header/trailer additions dropped | per format / Debug | W-1 rows (the trailer entry in the reported set); W-2 FIX44, FIX50SP2, FIXT11, Orchestra | a difference naming the store | as M-1 |
| M-4 | step 7's conflict checks removed | per format / Debug | W-3 step-7 conflict rows | each loads | guard routine → no refusal |
| M-5 | byte cap removed | per format / Debug | W-3 byte-cap rows | they load | step 1 |
| M-6 | `parse_doctype` dropped | per format / Debug | W-3 DOCTYPE row | it loads | step 2 → step 3 sees no node |
| M-7 | cycle check removed | per format / ASan | W-3 cycle row; W-11 cycle seeds | the cycle-error assertion fails (loop, overflow or another error; bounded by `timeout`) | step 6 → the build recurses on the cycle |
| M-8 | `load_any_with_overlay` ignores the overlay | both / Debug | W-4 match rows (`dictionary_pure_tests`); W-5 overlay arm (`session_pure_tests`) | difference; the overlay session disagrees with the edited one | `load_any_with_overlay` → session's `config.dictionary` |
| M-9 | `as_table_view` skips the header store | both / Debug | every W-6 arm (`session_pure_tests`); W-7 dictionary half (`dictionary_098_header_census_test`) | the custom header tags are sent in the body; `is_header_tag` false | `load` and `load_from_string` fill the handle's store at finalize (W-6's edited arms load that way); `as_table_view` copies it into `header_bits_`; `open()` builds `inbound_tv_` from `as_table_view()`; the classification reads `inbound_tv_` |
| M-10 | the resolver assigns the base to every session | — / Debug | W-8 AS-2, AS-5 (`config_045_tests`) | the overlay session's pointer equals the base's | resolver → `bundle.sessions[i].config.dictionary` |
| M-11 | the limit branch maps to 200 | — / Debug (Python ON) | W-9 cap rows (`capi_pure_tests`); W-10 cap row (pytest) | 200, `.code` 200 | thunk catch → mapping function → return code → SWIG out-typemap |
| M-12 | step 6's budget comparison removed (the side-table fit stays) | per format / Debug; ASan for W-11 | W-3 bomb rows; W-11 empty-chain seeds | doubling rows: side-table origin, not budget; empty-chain rows: counter origin | step 6 → arming at base total + budget → counter (research R-5) |
| M-13 | guard steps 6 and 7 swapped (the step-6 refusal block and the step-7 call change places; step 6's walk always finishes its memo, research R-5) | per format / Debug | W-3 guard-order row | the redeclare conflict, or a crash or timeout | guard routine order |
| M-14 | step 5's entry and reference caps removed | per format / Debug | W-3 entry and reference cap rows | they load | step 5 |
| M-15 | `std::exception` caught before `std::bad_alloc` | — / Debug | W-9 bad-alloc rows | 200, not 202 | mapping function |
| M-16 | step 8a replaced by the previous draft's adjacency rule | per format / Debug | W-3 Length+Data refuse rows and Orchestra `lengthId` rows; load rows stay GREEN | refuse rows load | step 8a |
| M-17 | one extra `expand_field_list` call in `finalize` | per loader / Debug; ASan for W-11 | every W-1 and W-2 row; W-3 header-expansion row; W-11 valid seed | counter-origin refusal | the build's counter, armed at the prediction |
| M-18 | the side-table fit check removed | per format / Debug | W-3 side-table rows | they load | step 6 |
| M-19 | the thunk calls `load_any(base)` and drops the overlay | — / Debug (Python ON) | W-9 live arm (`capi_098_dict_overlay_live_test`); W-10 inbound row (pytest) | 5011 reads `AB` | handle → `fixpp_session_config_set_dictionary` → `Session::open()` builds `inbound_tv_` → the `Parser` pairs through `session_hooks(inbound_tv_)`; the Python function is the same C function through SWIG |
| M-20 | the mapping function's `std::exception` and `...` branches return `FIXPP_ERR_CAPI_CONFIG_INVALID` | — / Debug | W-9 code-200 rows | 10, not 200 | mapping function |
| M-21 | step 2's status mapping with its branches swapped | both / Debug | W-9 status-mapping rows (`dictionary_pure_tests`) | the wrong type | the shared mapping function |
| M-22 | step 4's depth walk removed | per format / Debug | W-3 depth-33 row | it loads, or its refusal no longer names the depth cap | step 4 |
| M-23 | step 4a's root checks removed | per format / Debug | W-3 version-mismatch, other-format, root-child-outside-the-list, repeated-root-container and step-4a-against-step-5 order rows | the type or the named key is wrong | step 4a |
| M-24 | `policy` not forwarded to the build | per loader and at `load_any_with_overlay` / Debug | W-1 `tolerant` rows (the overlay side refuses); W-4 `tolerant` row (the `load_any` side refuses) | `xml_parse_error` / `orchestra_parse_error` where a load was expected | entry point → `build_handle_from_doc` with `fail_closed` → the group that declares no first member is refused |
| M-25 | step 5's reference count skips the reference elements the splice merges in place (appended ones still count) | per format / Debug | W-3 merged-in-place reference-cap row | it loads | step 5 |
| MO-1 | `copy_element_if` appends with `append_child(name)` and checks only the handle | both / Debug | W-9 transient sweep, the copy fixtures (`dictionary_overlay_pugi_oom_test`) | a divergent success: a nameless element | `copy_element_if` → the base document → the oracle's serialization |
| MO-2 | `copy_element_if` appends each attribute with `append_attribute(name)` and checks the handle and `set_value` | both / Debug | W-9 persistent sweep, the QuickFIX copy fixtures; W-9 transient sweep, the copy fixtures (`dictionary_overlay_pugi_oom_test`) | a divergent success: a nameless attribute | as MO-1 |
| MO-3 | the QuickFIX promotion in its pre-helper form: `append_attribute("required")`, then the handle and `set_value` checked | QuickFIX / Debug | W-9 persistent and transient sweeps, `QfPromotion` (`dictionary_overlay_pugi_oom_test`) | a divergent success: a nameless attribute holding `Y` | `xml_overlay_merge::impl::splice`'s promotion → the base document → the oracle |
| MO-3o | the Orchestra promotion in its pre-helper form | Orchestra / Debug | W-9 transient sweep, `OrchPromotion` (`dictionary_overlay_pugi_oom_test`) | as MO-3, holding `required` | as MO-3, Orchestra |
| MO-4 | `append_element` ignores `set_name`'s result | both / Debug | W-9 transient sweep, the copy fixtures (`dictionary_overlay_pugi_oom_test`) | a divergent success: a nameless element | as MO-1 |
| MO-5 | `append_named_attribute` ignores `set_name`'s and `set_value`'s results | both / Debug | W-9 persistent and transient sweeps, every fixture but `QfPromotionOverAnEmptyAttribute` (`dictionary_overlay_pugi_oom_test`) | a splice that returned after a failed allocation, with a divergent document | the copy and both promotions |
| MO-6 | the QuickFIX promotion swallows `std::bad_alloc` | QuickFIX / Debug | W-9 persistent and transient sweeps, `QfPromotion` and `QfPromotionOverAnEmptyAttribute` (`dictionary_overlay_pugi_oom_test`) | a splice that returned after a failed allocation, `required` not `Y` | as MO-3 |
| MO-7 | the Orchestra promotion swallows `std::bad_alloc` | Orchestra / Debug | W-9 persistent and transient sweeps, `OrchPromotion` (`dictionary_overlay_pugi_oom_test`) | as MO-6, `presence` not `required` | as MO-3o |
| MO-8 | `set_attribute` ignores `set_value`'s result on an attribute the base already has | both / Debug | W-9 persistent and transient sweeps, `QfPromotionOverAnEmptyAttribute` (`dictionary_overlay_pugi_oom_test`) | as MO-6, `required` left empty | as MO-3 |

## Comparator self-test (runs first; a zero is trusted only after it)

**W-0** `tests/dictionary/dictionary_equivalence_test.cpp`. The comparator declares its compared stores
as data (research R-10 §3). Each seeded pair of valid small dictionaries declares its expected reported
set. W-0 asserts that the comparator reports exactly that set. The union of all expected sets equals
the seedable part of the declared list; adding the reason-only entries that a version-matched overlay
cannot change yields the full list. Separable entries have separating seeds. Every coupled or derived entry has
a stated reason naming its invariant, the entry it cannot differ independently from, and the query
that observes it; this covers the group and enum bit caches, both pair-map directions and the
`Dictionary`/`table_view` twins. A context-store seed leaves the bare and exact stores equal (for
example the context `required_members` seed has equal bare `group_required_members`). Also seeded, for
the key sources: an unused extra component on one side; a
context record that differs only where the bare value would mask it; and a census mismatch (the census
of a document that differs from the loaded one in one component member). A pair of identical loads MUST
report none. Where a coupled entry's reason is an invariant between two stores the comparator reads, the
comparator asserts that invariant on each side (research R-10 §3). The invariants are:
- the reverse pair map is the inverse of the forward map;
- every key that `group_first_`, `group_members_`, `group_required_members_` or `group_ctx_` holds has its
  `group_bits_` bit set (a clear bit means every one of those stores misses, the source's own statement of
  the filter; `add_group_required_member` sets the bit too, and the 3-argument `group_required_members`
  pre-filters on it, so that store is in the check: implement, T013, 2026-11-01);
- every key that `enums_` holds has its `enum_bits_` bit set;
- from plan Phase 5 (tasks T055): every tag of the declared header store has its `header_bits_` bit set,
  and no other tag has.

Each check is a function of the store values it reads, not of a `table_view`. No loader or
`table_view_builder` call can build a dictionary that breaks one: `set_length_pair_data_tag` keeps the
pair directions inverse; every population path of the group stores calls `set_group_bit`, and both of
`enums_`'s (`add_enum`, `set_multi_value`) call `set_enum_bit`; `as_table_view` fills `header_bits_` from
the header store. So a dictionary seed cannot reach a check. **Each invariant check has its own seed**,
and none is a pair of valid dictionaries: it feeds that check hand-built store values that break its
invariant only (for example a reverse pair map missing one inverse entry, or a `group_bits_` value with
one held key's bit cleared), and the check MUST report that invariant. The seeds sit outside the
expected-set union rule above. **RED**: the seeded rows are the RED arms; mutant **M-0** (the comparison of one declared
entry, the context `required_members` one, removed while the entry stays listed) turns that entry's row
RED.

## Loading and merging

**W-1 Equivalence, every addition kind (small bases).** `tests/dictionary/overlay_equivalence_test.cpp`,
fixtures `tests/dictionary/fixtures/098/{qf,orch}/{base,overlay,edited}.xml` and the `tolerant` triple
beside them (hand-written). For each
format: `load_with_overlay_from_string(base, overlay)` vs `load_from_string(edited)` → zero differences.
The overlays cover every FR-003/FR-004 kind: new field (one of a non-string type), a new field with enum
values (a new Orchestra code set for Orchestra), enum value on an existing field (and on an
existing Orchestra code set), new component, new message (with a required field, a new group, a new
component), field added to an existing message, a group and a component reference added to an existing
message (the new group's first child is its delimiter), a field, a group and a component reference
added to an existing component, a group nested in an existing group, a component reference added to an
existing group, member added to an existing group
(inline and inside a component), field and group added to the header, field added to the trailer (XML
`<trailer>`; Orchestra component 1025), Required promotion on a message, component and group child,
custom Length+Data pair, an identical redeclaration (a base field with its number, name and type; an
existing enum value; for Orchestra an existing code), which is a no-op, an enum value on a base field with no values (N-2: the domain closes;
`enum_valid` refuses a value not listed), a new container the base lacks (R-3), a member appended to a
group whose existing members emit nothing (an empty component), which becomes the delimiter as in the
edited file (spec Edge Cases), and a new DATA after a base LENGTH whose base pair forms earlier in
`detect_length_pairs`'s visit order (loads, R-3). The list is closed against FR-003 and FR-004 clause by
clause, and T016 and T039 copy it. One type per new field is enough: the type string is parsed by the
loader's own code after the splice, so the splice has no per-type path.
**Each complete reference is mechanically an append-only edit.** Before the references' own commit (tasks
T017a, T039a),
`git diff --no-index --unified=1 base.xml edited.xml` is recorded in the evidence file, per format and
for the `tolerant` triple. Every hunk adds lines only, and each added run ends immediately before a
closing-tag line of its container. Two other changes are allowed, each one line rewritten in place and
nothing else on it changing (implement, T016/T017, 2026-11-01: a Required promotion and an addition to a
self-closing container cannot be written as an append):
- a self-closing base element opened to hold appended children: `<x …/>` becomes `<x …>` plus an added
  `</x>`;
- a Required promotion: one existing reference line (`<field>`, `<component>` or `<group>`) whose only
  change is its `required` attribute set to `Y`, from `N` or from absent, every other byte of the line
  kept. The Orchestra form is a `fixr:fieldRef` / `fixr:componentRef` / `fixr:groupRef` line whose only
  change is `presence` set to `required`, from `optional` or from absent (implement, T039, 2026-11-01). The
  vendored Orchestra patch carries no promotion, so the patch helper's promotion shape stays QuickFIX-XML
  only; extend it, with a positive row, before any Orchestra patch rewrites a line.

Orchestra forms of kinds worded for QuickFIX XML (implement, T039, 2026-11-01): N-2 is a code added to a
base code set that lists none (a field's `type` change is refused); "a new DATA after a base LENGTH whose
pair forms earlier" is a new `data` field with no `lengthId` referenced after a paired base Length
(Orchestra pairs by `lengthId` only); "a member added to an existing group, inline and inside a component"
is a group referenced from a message and one referenced from a component (Orchestra has no inline group);
and "a new container the base lacks" sits in the Orchestra `tolerant` triple, since every Orchestra
container is required by the loader or by another kind in the main triple.

A hunk of any other shape is fixed before the commit.
A `tolerant` row per format: a small base holding one group the loader leaves unregistered under
`unresolved_group_policy::tolerant` and refuses under `fail_closed`, its overlay and its edited file;
`load_with_overlay_from_string(base, overlay, mr, tolerant)` vs `load_from_string(edited, mr, tolerant)`
→ zero differences, and with the default policy both sides refuse (the policy reaches the build).
Every row loads through research R-5's read seam and asserts no counter-origin refusal and that the
step counter equals step 6's prediction. Each row also has an in-test seeded mismatch: the reference
with one non-no-op addition removed (never an identical redeclaration, which is a no-op) MUST give at
least one difference naming its store (SC-001's seeded
mismatch). The test names, in advance, the addition it removes and the store or stores it expects; the
reported set must include each (research R-10). **RED**: **M-1** (the XML splice prepends a new group member instead of appending) and
**M-2** (the XML splice skips enum-value additions), and for the Orchestra splice **M-1o** (it
prepends a new group member) and **M-2o** (it skips `fixr:code` additions); each turns its format's row
RED with the comparator naming the store. **M-3** (header/trailer additions dropped, per format) turns
each format's row RED, the comparator naming the store; the header kind and the trailer kind each show the
drop: the reported difference set MUST include the trailer addition's entry, so the trailer half is
shown separately from the header half. **M-17** (one extra `expand_field_list` call in
`finalize`) turns every row RED through the distinct counter origin. **M-24** (the `policy` argument
is not forwarded where `load_with_overlay` / `load_with_overlay_from_string` hand it to the build, both
overloads covered, so the build uses the default; once per loader) turns that format's `tolerant` row
RED: the overlay side refuses.

**W-2 Equivalence on the vendored bases.** Same file. FIX44, FIX50SP2 and the Orchestra file, each with an
overlay under `tests/dictionary/fixtures/098/real/` and a hand-written insertion patch (research R-10:
hunks with a multi-line context block that must match exactly once, insertion before the block's last
line), applied by a line-based test helper. `load_with_overlay(base, overlay)` vs
`load_from_string(patched base)` → zero differences, side against side and, for `component_fields` /
`group_fields`, against the raw census under R-10's wrap condition. Kind coverage per FR-017; the FIX44
overlay also extends more than 64 existing messages (N-7: accepted, since the entry caps count new
definitions). The FIX44 overlay is also W-5's, W-9's and W-10's, so, among its other additions, it
declares each constant `tests/session/test_098_inbound_frames.hpp` names, with exactly that header's values: 5001 on 35=D, 5002 in Parties, MsgType
`U1` with its required field, and `U1` as a value of MsgType(35) (FIX44.xml lists a closed set of values on
field 35, so without that value a `U1` frame is refused at the enum check before its required fields are read;
a QuickFIX user adding a message adds its value to field 35 in the same way), the pair 5010/5011 on the MsgType that header names, enum value `Z` on
OrdType(40), and an Optional→Required promotion of a field that stays Optional on a second FIX44 MsgType
(AS-5). Every Required addition the overlay makes on a frame's MsgType is carried by that frame, except
the one the frame's own scenario omits (one way to hold it: the additions W-5's frames meet are
Optional, except each scenario's own). That covers the promoted field, U1's required field and any other
Required addition; a Required addition to the header or the trailer is on every MsgType, so every frame
carries it. So each frame is valid on the edited dictionary but for its one scenario defect. A FIXT11 row, beyond FR-017's three vendored bases and backing FR-019's FIXT.1.1 scope:
a header-field overlay of `FIXT11.xml` and its insertion patch, under the same rules. Header-only is
enough for FR-019's FIXT.1.1 claim. FIXT11.xml holds the transport header and the session messages, and
every other kind goes through the same splice code on the other bases. The FIXT-specific part, the
transport header, is what the row covers.
Each row also asserts no counter-origin refusal and step counter = prediction (research
R-5), and has an in-test seeded mismatch: the patch with one hunk dropped MUST give at least one
difference naming its store, the hunk and the expected store named in the test in advance. **Empty overlay** (spec Edge Cases): for each base in `reused_tag_census.hpp`'s
`kRuntimeDicts` plus the Orchestra file, `load_with_overlay(base, an empty overlay of the base's format
and version)` vs `load(base)` → zero differences, no refusal, step counter = prediction. Each
empty-overlay row has its own seeded mismatch, since it has no patch: `load(base)` vs an in-test copy of
the base with one enum value removed MUST give at least one difference naming its store. The
recipe is one for every base (implement, T018, 2026-11-01: the earlier recipe, an unreferenced field
definition removed, has no such field on FIX40, FIX41, FIX43, FIX44 and FIXT11, measured): the first field
definition in document order that lists two or more values (`<value>`; Orchestra, the first code set with two
or more `fixr:code`), located by its opening line (QuickFIX XML, the `<field number='N'` line; Orchestra, the
`fixr:codeSet` line), which MUST match exactly once; the copy drops that definition's last value element
(a possibly multi-line `fixr:code` element on Orchestra). Removing a value never makes a base fail to load.
The expected stores include `Dictionary::enum_values` (`EnumValueRef::value`) and `table_view::enums_`
(`enum_domain::codes`). A base with no such definition fails the cell; there is no fallback. A zero- or
two-match anchor, or a copy that does not load, fails the cell before any comparison. That seeded mismatch proves only that the
comparator can report a difference at that base's scale. That the row's overlay side went through the
overlay path rests on the seam's counter equalling the prediction and on M-17. Anchors and hunks match
lines with one trailing CR stripped (research R-10, line endings). The helper's positive row (a hunk that
applies once inserts exactly its lines before the block's last line, against a hand-written expected
text) runs beside its zero- and two-match rows. The FIX50SP2 wrap probe
(research R-10) runs here and is recorded, not asserted; where a base side table does not fit, the
overlay's appended definitions past the wrap point are compared side against side only, and the verify
record says so.
**RED**: (a) a deliberately non-applying patch (a context block that matches zero places, and one that
matches two) MUST fail the cell before any comparison; (b) **M-3** (header/trailer additions are
dropped) turns the FIX44, FIX50SP2, FIXT11 and Orchestra rows RED; (c) each row's seeded mismatch, the
empty-overlay rows' base copy with one enum value removed included;
(d) **M-17** turns every row, the empty-overlay rows included, RED through the counter origin.

**W-3 Refusals.** `tests/dictionary/overlay_refusal_test.cpp`, table-driven. The table is the
product of the refusal kinds below × {QuickFIX XML, Orchestra}. Every cell is a row, or N/A with the
reason written in the table (for example "`<fields>` adjacency: Orchestra has none"); the verify record
lists the N/A cells. Each row asserts the exception's exact type for its format (an Orchestra schema
error is `orchestra_parse_error`, caught by the derived type, not by `xml_parse_error`) and that its
message names the key, cap or table as applicable. M-17's counter refusal instead names the distinct
counter origin.
- **Exact type** means equal dynamic type: the row catches `std::exception const&` and asserts
  `typeid(e) == typeid(<expected>)`. This holds for every row, the characterization pins included. A
  catch-based assertion is not used, because both overlay errors are `xml_parse_error`s and would pass a
  pin row expecting `xml_parse_error`.
- **Each step-7 row's message names its own check**: the rule and the element (for example "required
  relaxed" and the field and MsgType). A row refused by a different step-7 check therefore fails its
  message assertion.
- **The kinds below are closed.** Each R-4 bullet maps to one kind, and T019 asserts that the table
  holds a row or an N/A for every cell of the product.
- **"The loader itself refuses repeated" is established by running.** A test row loads a base carrying
  the repeated key with the base loader alone and asserts the loader's own error. It is not established
  by reading the code.
- **R-4 conflicts:** identifying attribute changed (type; name; msgtype name; Orchestra message `id`,
  code-set `type`); field name already used by another number; an Orchestra code set with a new
  `id` and an existing name (N-6; XML N/A, no code sets; R-3 matches a code set by name, so a changed
  code-set `id` is this input and this row covers it, implement, T041, 2026-11-01); required relaxed; redescribed enum value or
  code; `required="y"`, `required="false"` and `presence="require"` (A-013); duplicate within the
  overlay; two root containers of one name; version mismatch; overlay of the other format; a root child
  outside data-model §A.1's list, such as `fixr:datatypes` (N-10); an Orchestra group whose
  `fixr:numInGroup` `id` differs, and a new group with none or two (N-9; XML N/A, the NumInGroup is the
  group's `name`).
- **One tag twice at one level (research R-3):** a child redeclared directly that the parent reaches
  through a component, including through a two-level component chain (N-2); a new component reference
  that reaches a tag the parent already declares (N-7); for XML, a header addition whose tag a message
  body already has.
- **Repeated base key (G098-R2-005):** one row per match-key kind in research R-3's two tables that the
  base loader admits repeated (a repeated MsgType, a repeated `<field>` name, a repeated direct child;
  Orchestra a repeated `msgType` or direct child). A kind the loader itself refuses repeated is N/A
  with that reason (for example a duplicate Orchestra field, component or group `id`, or code-set name);
  the overlay names the key, and the row asserts both base occurrences in the message. A control: the
  same base with an empty overlay loads.
- **Length+Data relation (research R-3, step 8a).** One row per cell; "load" rows assert the load
  succeeds and the relation is unchanged. The relation is read through the dictionary in both
  directions: `Dictionary::length_pair_data_tag` for every Length tag, and both `table_view` pair
  directions for every base tag, compared between `load(base)` and the merged load. The factored
  detection function is not the instrument, because it is what is under test. "Earlier" and "later" are `detect_length_pairs`'s visit order
  (`<fields>`, header, trailer, messages, components, groups):

  | Site | new Data → unpaired base Length | new Data → base Length paired earlier | new Data → base Length paired later | new Length → base Data |
  |---|---|---|---|---|
  | `<fields>` | refuse | N/A: a Length `<fields>` pairs is followed by its Data, so it is never last | refuse (base pair lost) | N/A: a redeclared base Data is a match, not an append |
  | strict container (component, group) | refuse | load | refuse | refuse |
  | non-strict container (message, header, trailer), base group between | refuse | load | refuse | refuse |
  | non-strict container, overlay component between | refuse | load | refuse | refuse |
  | Orchestra `lengthId` | refuse | refuse, as `overlay_conflict_error` (not the loader's `orchestra_parse_error`); Orchestra has no visit order | (same cell) | N/A: a base Data's `lengthId` is fixed by R-3's equal-`lengthId` rule |

- **R-5 guards:** bytes at cap + 1 through both the path and the string form, depth 33, each entry cap +
  1 counting new definitions (new messages = cap + 1, N-7), the reference cap + 1 and a DOCTYPE, each
  `overlay_limit_error` naming its cap (or DOCTYPE); a cycle that closes through a base component (XML
  `xml_parse_error`; Orchestra `orchestra_parse_error`).
  - **Merged-in-place references count (research R-5 step 5).** One more reference-cap row per format,
    whose count reaches cap + 1 only if the reference elements the splice merges in place count: (cap −
    k) reference elements the splice appends, plus (k + 1) that it merges in place, each a Required
    promotion or an identical redeclaration of a reference the base already has at that place (a
    `<field>` / `<component>` / `<group>` reference element, Orchestra `fixr:fieldRef` / `componentRef` /
    `groupRef`; a `<fields>` definition is not a reference). It MUST fail with `overlay_limit_error`
    naming the reference cap. Its control is the same mix at exactly the cap ((cap − k) appended plus k
    merged in place), which loads. Both overlays stay inside every other cap, the expansion budget and the
    side-table fit, so the reference cap is the only limit the cap + 1 row can meet.
  - **At-cap controls.** Each cap + 1 row has a control at the cap that loads: exactly 4 MiB through both
    the path and the string form, depth 32, and each entry cap and the reference cap at its value.
  - **Budget recipe row.** Through the read seam, the row computes step 6's total over every vendored
    base with an empty overlay and applies research R-5's recipe to the largest. It asserts that the
    budget constant equals the result.
  - **Longest-chain row.** On a small base: the deepest chain the caps and the budget jointly admit. Its
    shape is chosen in-test through the seam's prediction: new components linked one to the next, each
    link through one new group, with the depth cap's nesting spent in one component. The field cap bounds
    the NumInGroup fields such a chain needs. It MUST load with counter = prediction. It runs under ASan
    in tier 1's `linux-clang-asan` lane, whose matrix job runs `ctest --preset <preset> -LE packaging`
    (research R-5, recursion depth).
- **The loader's own refusals (characterization pins, arm table):** an unknown reference (a leaf in
  step 6; the loader refuses it after the splice), an overlay field with an unknown type string, and a
  group-delimiter collision the merge creates (spec Edge Cases); each asserts the type the base loader
  throws for the equivalent edited file (`xml_parse_error` or `orchestra_parse_error`;
  `group_delimiter_collision_error`).
- **Expansion bomb (A-001):** the doubling rows' base is a small base whose side tables fit. Each row
  asserts that fit first, through the seam, so that a base change cannot silently move M-12's RED.
  XML, the doubling overlay
  `C_n = { component C_{n-1}; group NoG_n { field F_n; component C_{n-1} } }` over the 63 levels the caps
  allow, rooted at the base's `Instrument`; Orchestra, `C_n = { componentRef C_{n-1}; groupRef G_n }` with
  `G_n = { numInGroup; fieldRef F_n; componentRef C_{n-1} }`. Both referenced by no message. A third row
  per format: two empty-bodied component chains, each level referencing both chains' previous level,
  which emit nothing (research R-5). Each MUST fail with `overlay_limit_error` whose origin, read
  through research R-5's seam, is the pre-walk budget, with the budget's message. That origin shows the
  pre-walk refused before the build started (SC-003). No wall-clock bound is asserted: timing is W-12's
  controlled measurement, not a correctness bound.
- **Header expansion (G098-R2-003; XML only, Orchestra N/A: its header is component 1024, charged
  standalone):** a base with an empty `<messages>`, whose overlay puts nested groups under `<header>`
  that each reference one large component. It MUST load, with no counter-origin refusal, step counter = prediction and the
  declared header store holding the component's tags (the visited-set walk, research R-7). The
  header-store assertion joins in plan phase 5, when the store exists.
- **Side-table fit (N-3):** on a small base whose tables fit, an overlay that grows an early-declared
  component until a later base component's start passes 65535; an overlay component whose own entry
  count passes 65535; for groups, a large group with a low NumInGroup tag that pushes base groups past
  65535. Each MUST fail with `overlay_limit_error` naming the table. A control: the same shape kept just
  inside the fit loads.
- **Guard order (N-1):** an overlay that closes a cycle through a base component **and** redeclares a
  child that the parent reaches through that component. It MUST fail with the cycle error of its format,
  not with the redeclare conflict and not by recursion. Why this row shows FR-010's order: the cycle check
  (step 6) runs before the conflict checks (step 7), so an overlay that fails both must report the cycle.
  Two more order rows:
  - an overlay with a version mismatch and new messages = cap + 1 MUST fail with the version-mismatch
    conflict (step 4a), not the entry cap (step 5);
  - an overlay that fails both the budget and the side-table fit MUST fail with the budget origin
    (research R-5 step 6: "an overlay that fails both is reported as over the budget").

FR-008 has no row: it holds by construction (spec FR-008), and no mutant short of global state can make
two fresh base loads differ. **RED**, each mutant run once per format where its rows exist: **M-4**
(step 7's conflict checks removed: the step-7 conflict rows load; it does not touch step 4a),
**M-23** (step 4a's root checks removed: the version-mismatch, other-format, root-child-outside-the-list
and repeated-root-container rows' assertion (type and named key) fails; each loads, fails with another
type, or no longer names the key), **M-5** (byte cap removed), **M-22** (step 4's depth walk removed:
the depth-33 row's
assertion fails; it loads, or its refusal no longer names the depth cap), **M-6** (`parse_doctype`
dropped: the DOCTYPE row loads), **M-7** (cycle check removed: the cycle row no longer fails with the
cycle error; it loops, overflows, or fails differently, for example with a counter-origin refusal where
the walk's prediction stays finite; the RED is the cycle row's cycle-error assertion, the run uses
`linux-clang-asan` so that an overflow is reported, and the verify record states how the run ended),
**M-12** (the step 6 budget comparison removed; step 6's side-table fit check stays). The RED differs
per bomb row, and the verify record's message column confirms which check fired on each:
  - a **doubling** row: on a base whose side tables fit, the chain's emitted entries pass 65535, so
    step 6's side-table fit refuses it before the build; the row goes RED with "side-table origin,
    expected budget origin" and the table's message instead of the budget's;
  - an **empty-chain** row: it emits nothing, so it fits and the build is reached; the counter, armed at
    the smaller of the prediction and the base's total plus the budget (research R-5), refuses once the
    build passes the base's total plus the budget; the row goes RED with "counter origin, expected
    budget origin";
  - both end in bounded time, in the Debug tree.

**M-13** (guard steps 6 and 7 swapped: the guard-order row reports the redeclare
conflict, or crashes, instead of the cycle error), **M-14** (step 5's entry and reference caps removed:
those rows load), **M-16** (step 8a replaced by the previous draft's adjacency rule: the interposed,
paired-later and new-Length refuse rows and the Orchestra rows go RED, and the load rows stay GREEN,
since that rule never refused a paired base Length), **M-17**
(one extra `expand_field_list` call in `finalize`: the header-expansion row is refused with the counter
origin),
**M-18** (the side-table fit check removed: those rows load), **M-25** (step 5's reference count skips
the reference elements merged in place: the merged-in-place cap + 1 row loads, while the appended-only
reference-cap row stays RED).

**W-4 Load-any dispatch.** Same file as W-1. `load_any_with_overlay` on each format matches the loader
called directly; a mixed-format pair is refused. A `tolerant` row: W-1's `tolerant` triple through
`load_any_with_overlay(base, overlay, mr, unresolved_group_policy::tolerant)` matches the direct loader
called with `tolerant`. **RED**: **M-8** (`load_any_with_overlay` ignores the overlay) turns the match
rows RED. **M-24** at `load_any_with_overlay` (the `policy` argument is not forwarded to the loader,
which builds with the default) turns the `tolerant` row RED: the `load_any` side refuses while the
direct loader called with `tolerant` loads.

## Wire behaviour

**W-5 Inbound (US1 AS-1..AS-6, SC-002).** `tests/session/test_098_dialect_overlay_inbound.cpp` (joins
`session_pure_tests`) holds the overlay and edited sessions.

The base-session arm is in its own file, `tests/session/test_098_dialect_overlay_inbound_base.cpp`
(joins `session_pure_tests`). It uses only the existing API, so it compiles on the unfixed tree.
- The frames both files send come from a shared header, `tests/session/test_098_inbound_frames.hpp`. It
  names as constants the scenario tags (5001, 5002, U1's required field, 5010/5011, OrdType's `Z`), the
  MsgTypes each frame uses and the promoted field with its two MsgTypes, and W-2's FIX44 overlay declares
  each of them with exactly those values (W-2).
- It is written and run at the baseline (tasks T004a), which is the "before" that FR-018's negative
  control needs, and it runs in every later suite run, the "after".
- Its expected outcome for each frame is stated here, before any run:
  - AS-1, AS-2, AS-3, AS-4 and AS-6 are refused. AS-2 is refused too, not read as a field after the
    group: `dictionary_driven_validator::validate`'s field walk (its step 1) is flat, so it meets 5002,
    which the base does not declare for that MsgType (spec FR-018). T004a confirms from source that the
    gate's parse does not fail the frame first;
  - AS-5's two frames are accepted, because the promoted field is Optional in the base;
  - the enum-closure frame is accepted.
- Each refused frame's refusal is derived from source before the cell is written: the message type that
  answers it and the fields it carries. For a 35=3 Reject that is 373 and 371, derived from the
  validator's rule for the frame and `include/fixpp/wire/reject_reason_map.hpp`'s mapping. An unknown
  MsgType (AS-3) may instead take the session's unsupported-MsgType path or a BusinessMessageReject
  (35=j). The derivation is recorded in the evidence file, and the values are written into the test as
  literals.
- A run that disagrees with the derivation stops for a decision. The expected values are never
  re-recorded from the run.

Three sessions with `validate_inbound_messages = true` in all: FIX44 base alone (the file above), and in
the overlay file FIX44 base + the W-2 overlay loaded through `dict::load_any_with_overlay` (so M-8
reaches it) and FIX44 patched (the edited reference). One frame per scenario: custom field on 35=D; custom member inside Parties; private
MsgType `U1` with and without its required field; custom Length+Data value containing SOH;
Optional→Required promotion missing; the promoted field missing on another MsgType that carries it
Optional (accepted on all three sessions: AS-5's "other MsgTypes are unaffected"); OrdType `Z`. The enum-closure frame (N-2) uses its own small
overlay fixture and its own pair of sessions (overlay and edited), so that closing a field does not
reach the other frames: that overlay lists a first value on Text(58), which FIX44.xml declares with no
`<value>` (re-check with a grep before relying on it), and a frame carrying a Text value it does not
list is refused there and accepted on the base session. Assert: overlay session and edited session agree on every frame (accept,
or the same refusal message and fields); the base session's outcome is the one stated above. On every
accepted frame, the overlay and edited sessions also assert, from the application callback, the read
the scenario names: AS-1, the value of 5001 by tag; AS-2, 5002 read inside the Parties entry through the
group view (the group read is the assertion, not a top-level read); AS-3, `U1` reaches the callback;
AS-4, the 5011 value equals the full counted bytes, SOH included; AS-6, 40 reads `Z`. Accept/reject
alone does not show these: the validator's `table_view` and the layout the callback reads are two
consumers, and a defect in the second would leave both sessions agreeing (FR-018). **RED**:
**M-8** for the overlay arm. The base-session arm is a **negative control**, observed before (T004a) and
after. The edited-session arm is a **characterization pin** of a complete edited file, observed after the
fix only, because it shares the overlay arm's file (arm table).

**W-6 Send (US1 AS-7, FR-022, SC-008).** `tests/session/test_098_header_send.cpp` in
`session_pure_tests`. A session whose
dictionary declares header tag 5020 (a complete edited FIX44, loaded with the existing `load_from_string`,
so the witness compiles on the unfixed tree) sends a message with 5020 after a body field; the frame
carries 5020 before the first body field. A second edited-dictionary arm declares a custom pair
5030 (LENGTH) / 5031 (DATA), both non-standard, in the header and sends it after a body field: both
tags land in the header, adjacent, the Data value following its Length's class (A-008; the first
header-class pair that goes through `session_hooks(inbound_tv_)` rather than the standard table). The
pair enters as a raw application payload passed to `Session::send` (bytes written in the test), as
`bench/session/session_send_bench.cpp` sends, because `body_builder::field_data` refuses a custom Data
tag (spec Edge Cases). A third arm uses base + overlay (new API). A fourth arm replays the 5020 message through ResendRequest
and asserts 5020, 43 and 122 all sit inside the header (`kReplayHeaderTags` unchanged, research R-7).
Its 43/122 assertions are non-fatal (`EXPECT_*`) and do not depend on the 5020 assertion. On the unfixed
tree the run therefore reports both the 5020 failure (U) and the pin's result, and the RED record lists
both.
**Byte identity:** the existing header-order witnesses (`tests/session/test_resend_answer_field_order.cpp`
`SendPayloadTagTest.*`, `ResendAnswer*`; `tests/session/test_091_data_send.cpp` `HeaderPairSend091.*`,
`DataSend091.*`) pass unchanged. **Allocation (`[const §XV.1]`):** no test cell (a counting resource
alone cannot report a non-zero here without a known positive); the verify record carries the
by-construction argument, re-derived from the diff (the new branch calls only `noexcept` lookups over
storage filled at load), and the paired bench (W-12). **RED**: **U** for both edited-dictionary arms and the replay arm's 5020
half (the unfixed tree sends the custom tags in the body); **M-9** (`as_table_view` skips the header
store) for every arm.

**W-7 Header store per vendored dictionary.** Two halves, both in `tests/dictionary/header_tags_test.cpp`,
the standalone `dictionary_098_header_census_test` (`src/session` on its include directories). Both
enumerate the vendored QuickFIX XML files by `tests/dictionary/reused_tag_census.hpp`'s `kRuntimeDicts`,
plus the vendored Orchestra file through `FIXPP_ORCHESTRA_DATA_DIR`, as the
`dictionary_dict_enum_census_test` block (075) of `tests/dictionary/CMakeLists.txt` does.
- Dictionary half: for each result, `is_header_tag` answers true exactly for the tags of an independent
  raw-XML header walk in the test (QuickFIX `<header>` expansion; Orchestra component 1024), and false
  for 5020. **RED**: **M-9**.
- Session half: the same raw walk's tags, unioned with the production fixed set read from
  `src/session/send_header_tags.hpp` (research R-7), equal the fixed set. It is a characterization pin of
  R-7's measurement, not a RED arm. Its instrument is proven by a seeded FIX44 copy with 5020 in
  `<header>`, which MUST make the union assertion fail.

## Configuration

**W-8 TOML (US3 AS-1..AS-5, SC-004).** `tests/config/test_098_dialect_overlay.cpp` (joins
`config_045_tests`) and fixtures under `tests/config/fixtures/098/`. A session without an overlay gets
`engine.dictionaries.front()` and opens with no host code (AS-1, SC-004); a session with one gets the
merged dictionary (pointer differs from the base, a custom tag is valid); two sessions naming one path
share one pointer; `[default]` inheritance and override (AS-5); a missing file and a conflicting overlay
each report at the key path where the value was written (AS-3); root scope is refused (AS-4). Value and
origin rows (A-005, N-3): a non-string value (`malformed_value`); an empty string (`empty_required`);
`[default]` and `session[1]` naming one failing file give two diagnostics, one at each key path; a
`[[session]]` with `dialect_overlay` and no `[dictionary]` gives "dialect_overlay requires [dictionary]"
at that key path, and does not crash; that row asserts `missing_required` at `dictionary` too (research R-9).
More rows:
- **Engine dictionaries.** On the AS-2 bundle, `engine.dictionaries` holds exactly one entry. That entry
  is the pointer of the session without an overlay, and each overlay session's pointer differs from it.
  This shows FR-015's "keeps holding the base" and FR-021's unchanged registry input.
- **Location.** Each diagnostic's `location` is the value's position in the raw table where it was
  written (research R-9). The cell asserts it together with that diagnostic's stated reason class, so it
  is a U arm: on the unfixed tree the deferred-key diagnostic sits at the same location, with
  `recognized_not_yet_supported_step2`.
- **Message.** A load failure's `message` carries the loader's `what()` (research R-9). On the unfixed
  tree it is the deferred-key text.
- **Deferred keys at session scopes** (negative control). A remaining deferred key (`tracer`) in
  `[default]` and in a `[[session]]` gives `recognized_not_yet_supported_step2` at `default.tracer` and
  `session[0].tracer`. The scope-aware change touches `dialect_overlay` only.
- **No Orchestra TOML row.** The resolver step is format-agnostic: it passes paths to
  `load_any_with_overlay`, and W-4 witnesses that function's format dispatch.

`LoadNegativeBattery.T020_MissingRequired_Dictionary` stays green unchanged. Three existing cells state the superseded outcome in their names; each is renamed (suite
name and T0xx prefix kept, 097's rule) and rewritten to its new outcome, old → new names recorded in
the evidence file:
- `LoadNegativeBattery.T018_Step2DialectOverlay` → `T018_SessionDialectOverlayFileMissing`
  (`test_load_negative_battery.cpp`, `config_044_tests`) and `LoadSelectors.T028_DialectOverlayDeferred`
  → `T028_DialectOverlayFileMissing` (`test_load_selectors.cpp`, `config_044_tests`). Both load
  `tests/config/fixtures/neg_step2_dialect_overlay.toml`, whose `[dictionary]` names the fixture
  directory's `FIX44.xml` and whose `[[session]]` names `dialect_overlay = "my_overlay"`, a file that does
  not exist there. New outcome of both: the load fails with `invalid_or_contradictory_selector` at
  `session[0].dialect_overlay` (AS-3's missing file), and no `recognized_not_yet_supported_step2`. The
  two cells then assert one outcome; the overlap is accepted and recorded, since each guards its own
  suite's battery.
- `LoadDeferredSurface.T020_DeferredDialectOverlay` → `T020_RootDialectOverlayRefused`
  (`test_load_deferred_surface.cpp`, `config_045_tests`). It loads `neg_deferred_toplevel.toml`, which sets
  `dialect_overlay` at root. New outcome: AS-4's root refusal, `invalid_or_contradictory_selector` at key
  path `dialect_overlay` with "dialect_overlay is a session key; set it in [default] or [[session]]",
  and no `recognized_not_yet_supported_step2` or `unknown_key` for it.
- `T020_DeferredSetExactCoverage` keeps every other deferred key. Its literal `kAllDeferredKeys` list
  loses `dialect_overlay` and nothing else, re-derived by reading `kDeferred` (research R-9). It passes on
  the unfixed tree too, so it is a negative control, not a U arm.

Comments that state the superseded outcome are rewritten as rules, not results: the header comments of
`tests/config/fixtures/neg_step2_dialect_overlay.toml` and `tests/config/fixtures/neg_deferred_toplevel.toml`,
and every comment in `tests/config/test_load_negative_battery.cpp`, `tests/config/test_load_selectors.cpp`
and `tests/config/test_load_deferred_surface.cpp` that calls `dialect_overlay` deferred or step-2 or
counts the deferred keys (found with `git grep -n -e dialect_overlay -e kDeferred -- tests/config`). A
comment that counts the deferred keys loses the count and states the rule (the fixture carries every key
of `kDeferred`).
**RED**: **U** for every row except the negative controls `T020_MissingRequired_Dictionary`,
`T020_DeferredSetExactCoverage` and the session-scope deferred-key row (arm table: on the unfixed tree
`config.dictionary` is null, so AS-1's `open()` fails with `invalid_session_config`, and every
`dialect_overlay` row gets `recognized_not_yet_supported_step2` instead of its stated outcome, so the
engine-dictionaries row gets no bundle);
**M-10** (the resolver assigns the base to every session) for AS-2/AS-5, the reason those rows state.

## C ABI and Python

**W-9 C ABI.** Two executables.
- **Pure rows**, `tests/capi/dict_overlay_capi_test.cpp` (joins `capi_pure_tests` in plan phase 6; it
  starts no engine): cap → 201 and NULL; a DOCTYPE overlay → 201 and NULL (spec US2 AS-3); conflict →
  200 and NULL; NULL arguments →
  `FIXPP_ERR_NULL_HANDLE`; a control that the returned handle is accepted by
  `fixpp_session_config_set_dictionary` and by `fixpp_session_open` on an engine that is never started
  (a base-only handle passes it too). The cap row uses an entry-cap overlay (new messages = cap + 1),
  not a byte-cap file. The bucket gains a compile definition, `FIXPP_098_FIXTURE_DIR`
  (= `tests/dictionary/fixtures/098`), because none of its data-directory definitions reaches those
  fixtures. The exception-to-code mapping function (research R-6, declared in
  `src/capi/capi_internal.hpp`) is fed each type directly: `overlay_limit_error` → 201; `xml_oom_error`
  and a bare `std::bad_alloc` → 202; `xml_parse_error`, `overlay_conflict_error` and a
  `std::runtime_error` → 200; a non-`std::exception` → 200. In the `dictionary_pure_tests`, research R-5
  step 2's status mapping, the one function both parses of an overlay call use (N-10), is fed
  `status_out_of_memory` (→ `xml_oom_error`) and one other failed status (→ the format's parse error);
  that both parses call it is structural. A real parse that runs out of memory is the splice
  executable's (below).
  `tests/capi/version_test.cpp` pins 1.13; `tests/abi/golden/fixpp_capi_symbols.txt` gains the symbol;
  `tools/capi_freeze.sha256` refreshed; `tools/check_capi_reentrancy.sh` passes.
- **Live arm**, `tests/capi/dict_overlay_capi_live_test.cpp`, the standalone
  `capi_098_dict_overlay_live_test` (labels `capi;098`; compile definitions `FIXPP_DICT_DATA_DIR` and
  `FIXPP_098_FIXTURE_DIR`; a `TIMEOUT` sized by the module's recipe for live tests, the slowest lane's
  time with headroom, as the comment on `capi_inbound_frame_dispositions` states it). Built on
  `capi_dict066_loopback_support.hpp`, as `dict066_group_membership_red_test.cpp` is: two C-ABI engines
  over plaintext loopback. Each session's dictionary is a handle that
  `fixpp_dict_load_from_xml_with_overlay(FIX44.xml, the W-2 FIX44 overlay)` returns, set with
  `fixpp_session_config_set_dictionary`, not the support header's base-dictionary seam. The sender
  passes a raw application payload to `fixpp_session_send` (a byte pointer and a length), as
  `dict066_group_membership_red_test.cpp` does: bytes written in the test that lead with
  `35=<application MsgType><SOH>`, carry no session framing tag, and carry the overlay's custom pair
  5010 (LENGTH, the byte count of the 5011 value) / 5011 (DATA). The payload is not built with
  `fixpp_msg_set_data`: under M-19 the sender's handle is base-only too, so that call returns
  `FIXPP_ERR_TYPE_MISMATCH` for 5011 (`pair_hooks(h).length_tag_for_data` returns 0,
  `src/capi/message_write.cpp`) and the arm would fail at the sender, not with the value mismatch.
  `fixpp_session_send` does no C-ABI-level check of the pair (`src/capi/session.cpp`), and on a
  base-only dictionary `Session::build_send_frame_` scans the payload as well-formed fields, none in its
  `kRefusedTags` (`src/session/session.cpp`), so the raw bytes reach the wire unchanged under M-19.
  The Data value holds an SOH followed by text shaped as a field, for example `AB<SOH>58=CD`, with 58 not
  otherwise in the frame. The registered callback reads 5011 with `fixpp_msg_get_bytes`, the reader
  091's `length_data_send_recv_test.cpp` uses for an inbound Data value (only its reader is taken from
  091, not its builder); the test asserts that it equals
  the full counted value.
  - *Why this observable.* A C-ABI session never validates inbound
    (`SessionConfig::validate_inbound_messages` defaults to `false`, and nothing in `src/capi/` sets it),
    and `fixpp_msg_get_string` / `fixpp_msg_get_bytes` read any top-level tag whatever the dictionary
    declares (both return `view->get(tag)`, `src/capi/message_read.cpp`). So a custom
    scalar tag reads the same on a base-only handle. Pairing follows the session's dictionary
    (`Session::parse_and_dispatch_` parses with a `Parser` built over `inbound_tv_`, fixpp#426), so on a
    base-only handle 5011 reads `AB`. The field-shaped tail keeps that frame well formed, so the RED is a
    value mismatch, not a garbled frame whose callback never fires.

**RED**: **M-11** (the limit branch maps to 200) turns the cap rows RED; **M-15** (`std::exception`
caught before `std::bad_alloc`) turns the bad-alloc rows RED; **U** for the version pin and the symbol
golden, which compile on the unfixed tree and fail there; **M-19** (`fixpp_dict_load_from_xml_with_overlay`
calls `load_any(base)` and drops the overlay) turns the live arm RED with the value mismatch, and the W-10
inbound row too; **M-20** (the mapping function's `std::exception` and `...` branches return
`FIXPP_ERR_CAPI_CONFIG_INVALID`, as `fixpp_dict_load_from_xml` does) turns the code-200 rows RED: the
thunk's conflict row and the mapping function's `xml_parse_error`, `overlay_conflict_error`,
`std::runtime_error` and non-`std::exception` rows; **M-21** (research R-5 step 2's status mapping with
its two branches swapped: `status_out_of_memory` → the format's parse error, any other failed status →
`xml_oom_error`) turns both status-mapping rows RED. The pure handle row and the NULL-argument rows are
controls, not RED arms.

**W-9 splice allocation failure.** `tests/dictionary/overlay_pugi_oom_test.cpp`, the standalone
`dictionary_overlay_pugi_oom_test` (labels `098;dictionary`; compile definitions `FIXPP_DICT_DATA_DIR`
and `FIXPP_098_FIXTURE_DIR`; its own `main`). pugixml reports a failed allocation by a null handle or
`false`, never by throwing, and its named mutators (`append_child(name)`, `append_attribute(name)`)
discard `set_name`'s result, so a name that does not fit leaves a live, nameless node or attribute that
both loaders skip. The splice therefore adds to the base only through `append_element`,
`append_named_attribute` and `set_attribute` (`src/dictionary/overlay_merge.cpp`), each of which checks
every step and throws `std::bad_alloc`. The executable installs `pugi::set_memory_management_functions`
once, before any pugixml object exists, and never removes it; the hook forwards to `std::malloc` and
`std::free`, and a mode flag makes it return null. pugixml calls the hook only for a new page, so each
splice cell first consumes the base document's current page, through a scratch node outside the root,
down to an exact remainder, swept in 8-byte steps from 0 past the splice's whole size. Fixtures, inline:
QuickFIX, a promotion with `required` absent in the base, the same over an empty `required`, one
appended reference whose last allocation is `required="Y"`, and a root container the base lacks;
Orchestra, a `presence="required"` promotion, a copied `fixr:field` with its reference, and a root
container the base lacks.
- **Persistent sweep** (`OverlayPugiOomSpliceUnderPersistentFailure`): every new page fails from the
  splice on. A run whose splice met a failed allocation throws `std::bad_alloc`; a run that returns
  leaves the base's root serialized byte-equal to the splice without failure; across the sweep both
  outcomes occur (the straddle check).
- **Transient sweep** (`OverlayPugiOomSpliceUnderTransientFailure`): exactly the n-th new page of the
  splice fails and later ones succeed, for every n up to one past the splice's own count, from every
  remainder. Same oracle; a nameless node or attribute then stays in the tree instead of being masked
  by a later failure.
- **Positive control**: a clean overlay load calls the hook, and with every page failing an overlay
  load throws exactly `xml_oom_error`. Without both, every other row would read green.
- **Composition** (`OverlayPugiOomPublicEntryComposition`): `XmlLoader` and `OrchestraLoader`'s
  `load_with_overlay_from_string` over the W-1 triples, with the n-th allocation failing for every n
  over a clean call, each throw exactly `xml_oom_error`; one past the count, the load equals the edited
  reference. A failure in either parse satisfies it too, so it is a control for the composition, not
  a witness of the splice checks.

**RED**: MO-1 to MO-8 (mutant index), each turning the sweep rows it names RED with a divergent
success or a splice that returned after a failed allocation. The positive control and the composition
rows are controls.

**W-10 Python.** `bindings/python/tests/test_098_dialect_overlay.py`, in-tree only (run with
`PYTHONPATH=<build>/lib`, as `test_093_inbound_frame_dispositions.py` is). It has no wheel twin: the
overlay fixtures are not in the wheel. The wheel README's enforced allowlist names wheel files, and
`ci/assert-wheel-test-parity.py` derives its twins from the wheel side, so an in-tree file without a twin
needs no row (as `test_error_coverage.py` and `test_gil_release_canary.py` have none). Rows:
- **Inbound read:** `fixpp.dict_load_from_xml_with_overlay(FIX44.xml, the W-2 FIX44 overlay)`, set on an
  initiator session config. A raw TCP peer written in the test (the `test_093` pattern: it accepts the
  connection, reads the Logon and answers it) writes W-9's live-arm frame: the 5010/5011 pair, with the
  SOH and the field-shaped tail in the Data value. The callback registered with
  `session_register_callback` copies `msg_get_string(5011)` out, and the test asserts that it equals the
  full counted value. `msg_get_string` reads the same field as the C arm's `fixpp_msg_get_bytes` (both
  return `view->get(tag)`), and the value is ASCII, so the binding's UTF-8 decode keeps it. Python wraps no group reader (`fixpp.i` re-declares only the in-scope read
  functions of `message.h`), so this Length+Data value is the dictionary-only observable both surfaces
  can read. It is the same frame as W-9's live arm, so US4 AS-3 ("matches the C ABI") is witnessed on the
  inbound behaviour, not only on reachability.
- **Cap row:** an entry-cap overlay raises `ValidatorError` with `.code == 201`. The class alone is not
  the assertion: `ValidatorError` covers every code from 200 to 299, so it stays green when the limit
  branch maps to 200.
- **Import surface:** `bindings/python/tests/wheel/test_import_surface.py`'s exact set includes
  `dict_load_from_xml_with_overlay` (a suite-native row, already on the allowlist).

**A skip is not a pass.** The test file never calls `pytest.skip` or `importorskip`: a missing extension,
fixture or bind fails the row. Every W-10 run is recorded with `-rs`, and a skipped count other than zero
is a failed run, in the RED record and at the final head alike.

**RED**: **U** for the import-surface row. The recorded failure MUST be the exact-set assertion whose only
difference is the missing `dict_load_from_xml_with_overlay`; an ImportError, a `fixpp` imported from
outside the build's `lib/`, or any other difference is RED for the wrong reason (stop rule). **M-19** for
the inbound read (the value reads `AB`); **M-11** for the cap row (`.code` is 200).

## Fuzz, performance, records

**W-11 Fuzz (`[const §VII.7]`).** `tests/fuzz/fuzz_dict_overlay.cpp` with a seed corpus
(`tests/fuzz/corpus/dict_overlay/`): the W-1 overlays, the W-3 refusal overlays, a DOCTYPE overlay.
Every exception escaping the harness must be a `dict::` error type; the Orchestra half catches
`orchestra_parse_error` by its derived type, as `tests/fuzz/fuzz_orchestra_loader.cpp` does (research
R-11). The harness aborts on a counter-origin refusal even though its public type is
`overlay_limit_error`; on every input that loads, it also aborts if the step counter differs from step
6's prediction. The seeds include the expansion bombs and the empty-component chain of both formats and an
Orchestra cycle. CI replays the seeds (`linux-clang-asan`, the entry `fuzz_replay_dict_overlay`, label
`fuzz`, preamble exemption); the ten-minute campaign is tasks T095a, with research R-11's flags and
pass condition. The accepted exception set is research R-11's. **RED**: **M-7** re-run against the harness with each format's cycle seed, **M-12** with
each format's empty-chain seed (the harness aborts on the counter origin; under M-12 a doubling seed is
refused by step 6's side-table fit, an accepted `dict::` error, so it never reaches the counter), and
**M-17** with a valid overlay (the
harness aborts on the counter origin).

**W-12 Performance (SC-005, `[const §VIII.2]`, `[const §VIII.3]`).** Measured in the verify record, not
a ctest. `/speckit-verify`'s bench step is not this measurement. It compares a run against a stored
baseline, which `[const §VIII.2]` says does not gate, and it is N/A without one. So tasks T094, T094a and T095 run
everything below.
- **Load-time ratio (SC-005).** `load_with_overlay(FIX50SP2, the named at-cap fixture overlay)` against
  `load(FIX50SP2)`, both in the 098 head's Release build.
  - Both are path forms, so the file read is inside the timed region.
  - One discarded warm-up run of each is taken first, so the page cache is warm.
  - At least five runs each are made, interleaved A-B-A-B.
  - The ratio of medians must be ≤ 1.5 (SC-005's statistic). The minima are recorded too.
  - The record holds the machine, the commit and the fixture's expansion count (research R-5 step 6).
  - The fixture holds exactly the field, component and message caps' counts of new definitions and
    extends no existing definition. Its references and enum values are as many as those definitions
    need, below their caps, and their counts are recorded. Its expansion is within the budget, since it
    loads.
- **Characterisations, recorded, not bounded:**
  - `load_with_overlay(FIX44, W-2's FIX44 overlay)` against `load(FIX44)`. That overlay adds a header
    field, so step 7 builds a level set for every message (research R-5 step 7).
  - An at-budget overlay on FIX50SP2: an empty-component chain as deep as the budget admits, its depth
    found through the seam's prediction. This answers how long an admitted overlay can take (US2).
- **Send path (`[const §VIII.2]`).** `bench/session/session_send_bench.cpp`, paired base-vs-candidate
  on one machine.
  - The base is the merge-base with `origin/main` and the candidate is the head, both Release. The base
    tree's root path has the head root's length, and its `strings` and `.text` checks run before any
    paired run (tasks Execution rules, Builds): a source-root literal of another length shifts code
    layout. The `.text` check compares a linked target, never an object file: an object's `.text` keeps
    its relocations unresolved, so a root-length difference cannot change it.
  - Runs are A-B-A-B, compared min per tree.
  - Pass: a slowdown of no more than +5%. A larger slowdown needs `[const §VIII.2]`'s recorded
    measurement, a rationale and approval by someone other than the author. CI's provisional band is not
    this condition.
  - The bench sends a 35=D payload whose body fields are outside the fixed header set, on a FIX44
    session, so every such field takes the new lookup after a fixed-set miss. Confirm this against the
    source at T095.
  - The hit case (a dictionary header tag beyond the fixed set) costs the same as the miss: both read one
    `header_bits_` word after the same bounds check. Only the branch outcome differs, as for a fixed-set
    hit.
  - CI does not time this path in 098. The owner ruled on 2026-11-01 ("yes, add it to CI time'd list")
    that T094b add `bin/session_send_bench` as a `paired` row to `bench/ci-suite.txt`. On 2026-11-02 the
    row was withdrawn pending a re-ruling: open fixpp#592 says the bench runs at one of two levels for a
    whole process, set by the file's bytes and the invocation, and "must not get a `paired` row in this
    state". The question the owner answered did not cite #592. A paired row is irreversible
    (`[const §VIII.2]` 2a). Before 098 there was no such row, and the paired `engine_send_bench` refuses
    its payload before `build_send_frame_`'s partition (that bench's header comment), so CI never timed
    this branch. The local paired run above is the evidence for the ≤ +5% condition. Because of the two
    levels, T095 measures it by B41's method (PARENT `research/probes/b41_563_564/ph7f_method.md`):
    instance copies, string-variant cells and the level recorded per start. A difference below the gap
    between the levels is reported as not resolved.
  - `[const §VIII.3]` ("a benchmark in the same PR") is met by this existing bench, run paired and
    reported in the verify record and the PR body.
- **Plain load (`[const §VIII.2]`).** 098 changes code `XmlLoader::load` runs: the factored pair
  detection, the unarmed counter test and the header-store walk.
  - `bench/dictionary/xml_loader_bench` is a `paired` row in `bench/ci-suite.txt`, so CI's paired gate
    runs it at its provisional threshold.
  - T095 also runs it paired locally under the send path's protocol, with a pass of ≤ +5%.
  - `OrchestraLoader::load` runs the same three changes and has no bench. A seam-free scratch driver
    (tasks T094a), which calls only `OrchestraLoader::load`, an API both trees have, is built against the
    merge-base's and the head's Release `fixpp_dictionary` and times `load(OrchestraFIXLatest.xml)`.
    T095 runs it under the send path's protocol: A-B-A-B, min per tree, pass ≤ +5%; a larger slowdown
    needs `[const §VIII.2]`'s recorded measurement, a rationale and approval by someone other than the
    author.
- **Footprint.** `bench/dictionary/table_view_footprint_bench` is run at base and head. The record holds
  `sizeof(table_view)` and the `header_bits_` heap words per copy for FIX44 and FIX50SP2, as its
  `group_bits_` note records that store. Recorded, not gated (a `no` row).

**W-13 Records.** The parent repo's `python3 .claude/scripts/check_bl_delta.py --self-test` (its known
positive), then `python3 .claude/scripts/check_bl_delta.py gate --feature 098-dialect-overlay --root <tree>
--base origin/main` (data-model §D; the default `--root` is the main checkout, the wrong tree),
the brain freshness checks (`.github/workflows/brain-freshness.yml`'s steps, run locally) and the
parent's `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh`, the parent's
`.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`, the line-citation gate over
the bundle, the records inventory recipe (data-model §G) with its seeded positive, and a positive-presence
check for each "Updated (098-dialect-overlay)" note the inventory's dispositions call for (seeded
negative: the check fails on a copy with one note removed).
